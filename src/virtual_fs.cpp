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
    RegisterFile(path, data, mime_type);
}

bool VirtualFS::FindFile(const std::string& path, VFile& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(path);
    if (it != files_.end()) {
        out = it->second;
        return true;
    }
    return false;
}

// LoadFromResources 由构建时生成的 resource_map.cpp 实现
// 不在此文件中定义，避免链接冲突

void VirtualFS::LoadFromDirectory(const std::string& dir_path) {
    std::wstring wDirPath(dir_path.begin(), dir_path.end());
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
            std::string subDir(fullPath.begin(), fullPath.end());
            LoadFromDirectory(subDir);
        } else {
            // 读取文件到内存
            HANDLE hFile = CreateFileW(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile == INVALID_HANDLE_VALUE) continue;

            DWORD fileSize = GetFileSize(hFile, nullptr);
            if (fileSize == INVALID_FILE_SIZE || fileSize == 0) {
                CloseHandle(hFile);
                continue;
            }

            std::vector<uint8_t> buffer(fileSize);
            DWORD bytesRead = 0;
            ReadFile(hFile, buffer.data(), fileSize, &bytesRead, nullptr);
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
                std::string relPath(relPathW.begin(), relPathW.end());
                RegisterFile("/" + relPath, buffer, "");
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
    };

    auto it = mimeMap.find(ext);
    return it != mimeMap.end() ? it->second : "application/octet-stream";
}

} // namespace tauricpp
