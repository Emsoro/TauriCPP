#include "tauricpp/virtual_fs.hpp"
#include <algorithm>
#include <Windows.h>

namespace tauricpp {

VirtualFS& VirtualFS::Instance() {
    static VirtualFS instance;
    return instance;
}

void VirtualFS::RegisterFile(const std::string& path, const std::vector<uint8_t>& data, const std::string& mime_type) {
    std::lock_guard<std::mutex> lock(mutex_);
    VFile file;
    file.data = data;
    file.mime_type = mime_type.empty() ? InferMimeType(path) : mime_type;
    files_[path] = std::move(file);
}

void VirtualFS::RegisterFile(const std::string& path, const std::string& content, const std::string& mime_type) {
    std::vector<uint8_t> data(content.begin(), content.end());
    RegisterFile(path, std::move(data), mime_type);
}

void VirtualFS::RegisterFile(const std::string& path, std::vector<uint8_t>&& data, const std::string& mime_type) {
    std::lock_guard<std::mutex> lock(mutex_);
    VFile file;
    file.data = std::move(data);
    file.mime_type = mime_type.empty() ? InferMimeType(path) : mime_type;
    files_[path] = std::move(file);
}

const VirtualFS::VFile* VirtualFS::FindFile(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(path);
    if (it != files_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool VirtualFS::FindFile(const std::string& path, VFile& out) const {
    const VFile* file = FindFile(path);
    if (file) {
        out = *file;
        return true;
    }
    return false;
}

// LoadFromResources 由构建时生成的 resource_map.cpp 实现
// 不在此文件中定义，避免链接冲突

void VirtualFS::LoadFromDirectory(const std::string& dir_path) {
    // ★ 修复：使用正确的UTF-8到Wide转换，而非简单的char遍历
    std::wstring wDirPath;
    int wLen = MultiByteToWideChar(CP_UTF8, 0, dir_path.c_str(), -1, nullptr, 0);
    if (wLen > 0) {
        wDirPath.resize(wLen - 1);
        MultiByteToWideChar(CP_UTF8, 0, dir_path.c_str(), -1, wDirPath.data(), wLen);
    }

    std::wstring searchPattern = wDirPath + L"\\*";

    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        std::wstring fileName = findData.cFileName;
        if (fileName == L"." || fileName == L"..") continue;

        std::wstring fullPath = wDirPath + L"\\" + fileName;

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // 递归加载子目录
            // ★ 修复：使用正确的Wide到UTF-8转换
            int mbLen = WideCharToMultiByte(CP_UTF8, 0, fullPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string subDir;
            if (mbLen > 0) {
                subDir.resize(mbLen - 1);
                WideCharToMultiByte(CP_UTF8, 0, fullPath.c_str(), -1, subDir.data(), mbLen, nullptr, nullptr);
            }
            LoadFromDirectory(subDir);
        } else {
            // 读取文件到内存
            HANDLE hFile = CreateFileW(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile == INVALID_HANDLE_VALUE) continue;

            LARGE_INTEGER fileSize;
            if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart == 0) {
                CloseHandle(hFile);
                continue;
            }

            // ★ 修复：使用LARGE_INTEGER支持大于4GB的文件（虽然不太可能）
            std::vector<uint8_t> buffer(static_cast<size_t>(fileSize.QuadPart));
            DWORD bytesRead = 0;
            ReadFile(hFile, buffer.data(), static_cast<DWORD>(fileSize.QuadPart), &bytesRead, nullptr);
            CloseHandle(hFile);

            if (bytesRead > 0) {
                buffer.resize(bytesRead);
                // 计算相对路径作为虚拟路径
                std::wstring relPathW = fullPath.substr(wDirPath.size());
                if (!relPathW.empty() && relPathW[0] == L'\\') {
                    relPathW = relPathW.substr(1);
                }
                // 反斜杠转正斜杠
                for (auto& ch : relPathW) {
                    if (ch == L'\\') ch = L'/';
                }
                // ★ 修复：使用正确的Wide到UTF-8转换
                int mbLen2 = WideCharToMultiByte(CP_UTF8, 0, relPathW.c_str(), -1, nullptr, 0, nullptr, nullptr);
                std::string relPath;
                if (mbLen2 > 0) {
                    relPath.resize(mbLen2 - 1);
                    WideCharToMultiByte(CP_UTF8, 0, relPathW.c_str(), -1, relPath.data(), mbLen2, nullptr, nullptr);
                }
                // 使用右值引用版本，避免拷贝
                RegisterFile("/" + relPath, std::move(buffer), "");
            }
        }
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);
}

void VirtualFS::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    files_.clear();
}

std::vector<std::string> VirtualFS::GetAllPaths() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> paths;
    paths.reserve(files_.size());
    for (const auto& [path, _] : files_) {
        paths.push_back(path);
    }
    return paths;
}

std::string VirtualFS::InferMimeType(const std::string& path) {
    auto dotPos = path.rfind('.');
    if (dotPos == std::string::npos) return "application/octet-stream";

    std::string ext = path.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    static const std::unordered_map<std::string, std::string> mimeMap = {
        {"html", "text/html"}, {"htm", "text/html"},
        {"css", "text/css"},
        {"js", "application/javascript"}, {"mjs", "application/javascript"},
        {"json", "application/json"},
        {"png", "image/png"},
        {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},
        {"svg", "image/svg+xml"},
        {"ico", "image/x-icon"},
        {"woff", "font/woff"}, {"woff2", "font/woff2"},
        {"ttf", "font/ttf"}, {"otf", "font/otf"},
        {"wasm", "application/wasm"},
        {"xml", "application/xml"},
        {"txt", "text/plain"},
        {"webp", "image/webp"},
        {"map", "application/json"},           // source map
        {"webmanifest", "application/manifest+json"},  // PWA manifest
        {"mp3", "audio/mpeg"}, {"mp4", "video/mp4"},
        {"webm", "video/webm"},
        {"pdf", "application/pdf"},
        {"zip", "application/zip"},
    };

    auto it = mimeMap.find(ext);
    return it != mimeMap.end() ? it->second : "application/octet-stream";
}

} // namespace tauricpp
