#include "tauricpp/dialog.hpp"
#include <shobjidl.h>
#include <commdlg.h>

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
static std::vector<COMDLG_FILTERSPEC> BuildFilters(const std::vector<Dialog::FileFilter>& filters) {
    std::vector<COMDLG_FILTERSPEC> specs;
    specs.reserve(filters.size());

    // 需要保持wstring生命周期
    std::vector<std::wstring> names;
    std::vector<std::wstring> patterns;
    names.reserve(filters.size());
    patterns.reserve(filters.size());

    for (const auto& f : filters) {
        names.push_back(Utf8ToWide(f.name));
        patterns.push_back(Utf8ToWide(f.pattern));
    }

    for (size_t i = 0; i < filters.size(); ++i) {
        specs.push_back({ names[i].c_str(), patterns[i].c_str() });
    }

    return specs;
}

std::vector<std::string> Dialog::OpenFile(HWND parent, const OpenOptions& options) {
    std::vector<std::string> result;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool needUninitialize = SUCCEEDED(hr);

    IFileOpenDialog* pfd = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&pfd));
    if (FAILED(hr)) {
        if (needUninitialize) CoUninitialize();
        return result;
    }

    // 设置选项
    FILEOPENDIALOGOPTIONS fos;
    pfd->GetOptions(&fos);
    fos |= FOS_FILEMUSTEXIST;
    if (options.multi_select) {
        fos |= FOS_ALLOWMULTISELECT;
    }
    pfd->SetOptions(fos);

    // 设置标题
    if (!options.title.empty()) {
        pfd->SetTitle(Utf8ToWide(options.title).c_str());
    }

    // 设置默认路径
    if (!options.default_path.empty()) {
        IShellItem* psiFolder = nullptr;
        SHCreateItemFromParsingName(Utf8ToWide(options.default_path).c_str(), nullptr,
                                     IID_PPV_ARGS(&psiFolder));
        if (psiFolder) {
            pfd->SetDefaultFolder(psiFolder);
            psiFolder->Release();
        }
    }

    // 设置过滤器
    auto specs = BuildFilters(options.filters);
    if (!specs.empty()) {
        pfd->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
    }

    // 显示对话框
    hr = pfd->Show(parent);
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
    if (needUninitialize) CoUninitialize();

    return result;
}

std::optional<std::string> Dialog::SaveFile(HWND parent, const SaveOptions& options) {
    std::optional<std::string> result;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool needUninitialize = SUCCEEDED(hr);

    IFileSaveDialog* pfd = nullptr;
    hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&pfd));
    if (FAILED(hr)) {
        if (needUninitialize) CoUninitialize();
        return result;
    }

    // 设置选项
    FILEOPENDIALOGOPTIONS fos;
    pfd->GetOptions(&fos);
    if (options.overwrite_prompt) {
        fos |= FOS_OVERWRITEPROMPT;
    }
    pfd->SetOptions(fos);

    // 设置标题
    if (!options.title.empty()) {
        pfd->SetTitle(Utf8ToWide(options.title).c_str());
    }

    // 设置默认文件名
    if (!options.default_filename.empty()) {
        pfd->SetFileName(Utf8ToWide(options.default_filename).c_str());
    }

    // 设置默认路径
    if (!options.default_path.empty()) {
        IShellItem* psiFolder = nullptr;
        SHCreateItemFromParsingName(Utf8ToWide(options.default_path).c_str(), nullptr,
                                     IID_PPV_ARGS(&psiFolder));
        if (psiFolder) {
            pfd->SetDefaultFolder(psiFolder);
            psiFolder->Release();
        }
    }

    // 设置过滤器
    auto specs = BuildFilters(options.filters);
    if (!specs.empty()) {
        pfd->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
    }

    // 显示对话框
    hr = pfd->Show(parent);
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
    if (needUninitialize) CoUninitialize();

    return result;
}

std::optional<std::string> Dialog::PickFolder(HWND parent, const std::string& title) {
    std::optional<std::string> result;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool needUninitialize = SUCCEEDED(hr);

    IFileOpenDialog* pfd = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&pfd));
    if (FAILED(hr)) {
        if (needUninitialize) CoUninitialize();
        return result;
    }

    FILEOPENDIALOGOPTIONS fos;
    pfd->GetOptions(&fos);
    fos |= FOS_PICKFOLDERS | FOS_FILEMUSTEXIST;
    pfd->SetOptions(fos);

    if (!title.empty()) {
        pfd->SetTitle(Utf8ToWide(title).c_str());
    }

    hr = pfd->Show(parent);
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
    if (needUninitialize) CoUninitialize();

    return result;
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
