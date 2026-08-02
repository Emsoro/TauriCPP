#include "tauricpp/dialog.hpp"
#include <shobjidl.h>
#include <commdlg.h>
#include <thread>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")

namespace tauricpp {

// 辅助：UTF-8 -> wstring
static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring result(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, result.data(), len);
    return result;
}

// 辅助：wstring -> UTF-8
static std::string WideToUtf8(const std::wstring& str) {
    if (str.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, result.data(), len, nullptr, nullptr);
    return result;
}

// 辅助：构建COMDLG_FILTERSPEC数组
// 注意：COMDLG_FILTERSPEC 只保存指针，必须保证 name/pattern 字符串生命周期
// 覆盖 Show() 调用，因此这里把 wstring 与 specs 一起返回（持有所有权）。
struct FilterSpecs {
    std::vector<std::wstring> names;
    std::vector<std::wstring> patterns;
    std::vector<COMDLG_FILTERSPEC> specs;
};

static FilterSpecs BuildFilters(const std::vector<Dialog::FileFilter>& filters) {
    FilterSpecs out;
    out.names.reserve(filters.size());
    out.patterns.reserve(filters.size());
    out.specs.reserve(filters.size());

    for (const auto& f : filters) {
        out.names.push_back(Utf8ToWide(f.name));
        out.patterns.push_back(Utf8ToWide(f.pattern));
    }
    for (size_t i = 0; i < filters.size(); ++i) {
        out.specs.push_back({ out.names[i].c_str(), out.patterns[i].c_str() });
    }
    return out;
}

// 在独立 STA 线程中运行模态对话框。Common Item Dialog 必须在拥有消息泵的
// 线程（或自身内部泵消息的 STA 线程）上调用 Show，否则在 WebView2 回调线程
// 等上下文中会卡死/无法交互。此处统一把对话框放到新 STA 线程执行。
template <typename F>
static auto RunOnStaThread(F&& fn) -> decltype(fn()) {
    using ResultType = decltype(fn());
    ResultType result{};
    std::thread t([&result, &fn]() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        bool needUninit = SUCCEEDED(hr);
        result = fn();
        if (needUninit) CoUninitialize();
    });
    t.join();
    return result;
}

namespace {
    std::vector<std::string> OpenFileImpl(const Dialog::OpenOptions& options) {
        std::vector<std::string> result;
        IFileOpenDialog* pfd = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&pfd));
        if (FAILED(hr)) return result;

        FILEOPENDIALOGOPTIONS fos;
        pfd->GetOptions(&fos);
        fos |= FOS_FILEMUSTEXIST;
        if (options.multi_select) fos |= FOS_ALLOWMULTISELECT;
        pfd->SetOptions(fos);

        if (!options.title.empty()) pfd->SetTitle(Utf8ToWide(options.title).c_str());

        if (!options.default_path.empty()) {
            IShellItem* psiFolder = nullptr;
            SHCreateItemFromParsingName(Utf8ToWide(options.default_path).c_str(), nullptr,
                                         IID_PPV_ARGS(&psiFolder));
            if (psiFolder) { pfd->SetDefaultFolder(psiFolder); psiFolder->Release(); }
        }

        auto specs = BuildFilters(options.filters);
        if (!specs.specs.empty()) {
            pfd->SetFileTypes(static_cast<UINT>(specs.specs.size()), specs.specs.data());
            pfd->SetFileTypeIndex(1);
        }

        hr = pfd->Show(nullptr);
        if (SUCCEEDED(hr)) {
            IShellItemArray* pItems = nullptr;
            hr = pfd->GetResults(&pItems);
            if (SUCCEEDED(hr) && pItems) {
                DWORD count = 0;
                pItems->GetCount(&count);
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem* pItem = nullptr;
                    if (SUCCEEDED(pItems->GetItemAt(i, &pItem)) && pItem) {
                        LPWSTR pPath = nullptr;
                        if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pPath))) {
                            result.push_back(WideToUtf8(pPath));
                            CoTaskMemFree(pPath);
                        }
                        pItem->Release();
                    }
                }
                pItems->Release();
            }
        }
        pfd->Release();
        return result;
    }

    std::optional<std::string> SaveFileImpl(const Dialog::SaveOptions& options) {
        std::optional<std::string> result;
        IFileSaveDialog* pfd = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&pfd));
        if (FAILED(hr)) return result;

        FILEOPENDIALOGOPTIONS fos;
        pfd->GetOptions(&fos);
        if (options.overwrite_prompt) fos |= FOS_OVERWRITEPROMPT;
        pfd->SetOptions(fos);

        if (!options.title.empty()) pfd->SetTitle(Utf8ToWide(options.title).c_str());
        if (!options.default_filename.empty())
            pfd->SetFileName(Utf8ToWide(options.default_filename).c_str());
        if (!options.default_path.empty()) {
            IShellItem* psiFolder = nullptr;
            SHCreateItemFromParsingName(Utf8ToWide(options.default_path).c_str(), nullptr,
                                         IID_PPV_ARGS(&psiFolder));
            if (psiFolder) { pfd->SetDefaultFolder(psiFolder); psiFolder->Release(); }
        }

        auto specs = BuildFilters(options.filters);
        if (!specs.specs.empty()) {
            pfd->SetFileTypes(static_cast<UINT>(specs.specs.size()), specs.specs.data());
            pfd->SetFileTypeIndex(1);
        }

        hr = pfd->Show(nullptr);
        if (SUCCEEDED(hr)) {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pfd->GetResult(&pItem)) && pItem) {
                LPWSTR pPath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pPath))) {
                    result = WideToUtf8(pPath);
                    CoTaskMemFree(pPath);
                }
                pItem->Release();
            }
        }
        pfd->Release();
        return result;
    }

    std::optional<std::string> PickFolderImpl(const Dialog::PickFolderOptions& options) {
        std::optional<std::string> result;
        IFileOpenDialog* pfd = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&pfd));
        if (FAILED(hr)) return result;

        FILEOPENDIALOGOPTIONS fos;
        pfd->GetOptions(&fos);
        fos |= FOS_PICKFOLDERS | FOS_FILEMUSTEXIST;
        pfd->SetOptions(fos);

        if (!options.title.empty()) pfd->SetTitle(Utf8ToWide(options.title).c_str());
        if (!options.default_path.empty()) {
            IShellItem* psiFolder = nullptr;
            SHCreateItemFromParsingName(Utf8ToWide(options.default_path).c_str(), nullptr,
                                         IID_PPV_ARGS(&psiFolder));
            if (psiFolder) { pfd->SetDefaultFolder(psiFolder); psiFolder->Release(); }
        }

        hr = pfd->Show(nullptr);
        if (SUCCEEDED(hr)) {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pfd->GetResult(&pItem)) && pItem) {
                LPWSTR pPath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pPath))) {
                    result = WideToUtf8(pPath);
                    CoTaskMemFree(pPath);
                }
                pItem->Release();
            }
        }
        pfd->Release();
        return result;
    }
}

std::vector<std::string> Dialog::OpenFile(HWND parent, const OpenOptions& options) {
    (void)parent;
    return RunOnStaThread([&]() { return OpenFileImpl(options); });
}

std::optional<std::string> Dialog::SaveFile(HWND parent, const SaveOptions& options) {
    (void)parent;
    return RunOnStaThread([&]() { return SaveFileImpl(options); });
}

std::optional<std::string> Dialog::PickFolder(HWND parent, const std::string& title) {
    (void)parent;
    return PickFolder(parent, PickFolderOptions{title, ""});
}

std::optional<std::string> Dialog::PickFolder(HWND parent, const PickFolderOptions& options) {
    (void)parent;
    return RunOnStaThread([&]() { return PickFolderImpl(options); });
}

void Dialog::ShowInfo(HWND parent, const std::string& title, const std::string& message) {
    MessageBoxW(parent, Utf8ToWide(message).c_str(), Utf8ToWide(title).c_str(), MB_OK | MB_ICONINFORMATION);
}

bool Dialog::AskConfirm(HWND parent, const std::string& title, const std::string& message) {
    int result = MessageBoxW(parent, Utf8ToWide(message).c_str(), Utf8ToWide(title).c_str(),
                             MB_YESNO | MB_ICONQUESTION);
    return result == IDYES;
}

} // namespace tauricpp
