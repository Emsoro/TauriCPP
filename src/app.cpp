#include "tauricpp/app.hpp"
#include <Windows.h>

namespace tauricpp {

App::App(const Config& config)
    : config_(config)
    , bridge_(Bridge::Instance())
    , vfs_(VirtualFS::Instance())
{
    // 优先从exe资源段加载前端资源（生产模式）
    vfs_.LoadFromResources(GetModuleHandle(nullptr));

    // 如果资源段没有资源，从文件系统加载（开发模式）
    if (vfs_.GetAllPaths().empty()) {
        WCHAR exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        WCHAR* lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash) *lastSlash = L'\0';

        std::vector<std::wstring> candidates = {
            std::wstring(exePath) + L"\\..\\..\\sample\\frontend",
            std::wstring(exePath) + L"\\frontend",
        };

        for (const auto& cand : candidates) {
            WCHAR absPath[MAX_PATH];
            if (GetFullPathNameW(cand.c_str(), MAX_PATH, absPath, nullptr)) {
                if (GetFileAttributesW(absPath) != INVALID_FILE_ATTRIBUTES) {
                    // ★ 修复：使用WideCharToMultiByte正确转换路径，避免非ASCII路径问题
                    int mbLen = WideCharToMultiByte(CP_UTF8, 0, absPath, -1, nullptr, 0, nullptr, nullptr);
                    std::string dirPath;
                    if (mbLen > 0) {
                        dirPath.resize(mbLen - 1);
                        WideCharToMultiByte(CP_UTF8, 0, absPath, -1, dirPath.data(), mbLen, nullptr, nullptr);
                    }
                    vfs_.LoadFromDirectory(dirPath);
                    break;
                }
            }
        }
    }
}

App::~App() = default;

void App::OnSetup(SetupCallback cb) {
    setup_cb_ = std::move(cb);
}

int App::Run() {
    window_ = std::make_unique<Window>(config_.window_config);

    if (setup_cb_) {
        setup_cb_(*this);
    }

    return window_->Run();
}

} // namespace tauricpp
