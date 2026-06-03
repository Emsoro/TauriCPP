#include "tauricpp/embedded_dll.hpp"
#include <Windows.h>
#include <delayimp.h>
#include <vector>

namespace tauricpp {

HMODULE EmbeddedDll::loaded_module_ = nullptr;
std::wstring EmbeddedDll::temp_file_path_;

} // namespace tauricpp

// ============================================================================
// delay-load 钩子
// 当 MSVC delay-load 机制尝试加载 WebView2Loader.dll 时，
// 检查 EmbeddedDll 是否已经从 exe 资源加载了该 DLL
// 如果已加载，直接返回已加载的模块句柄，无需再次 LoadLibrary
// ============================================================================
extern "C" {

static FARPROC WINAPI tauricpp_delayLoadHook(
    unsigned dliNotify,
    PDelayLoadInfo pdli
) {
    switch (dliNotify) {
    case dliNotePreLoadLibrary:
        if (tauricpp::EmbeddedDll::loaded_module_) {
            return (FARPROC)tauricpp::EmbeddedDll::loaded_module_;
        }
        break;
    default:
        break;
    }
    return nullptr;
}

// 设置全局 delay-load 钩子
const PfnDliHook __pfnDliNotifyHook2 = tauricpp_delayLoadHook;

} // extern "C"

namespace tauricpp {

HMODULE EmbeddedDll::Load(const std::string& resourceId,
                           const std::string& resourceType,
                           const std::string& dllName) {
    if (loaded_module_) return loaded_module_;

    HMODULE hExe = GetModuleHandle(nullptr);

    // 查找资源（使用数字ID 1，类型 EMBEDDED_DLL）
    HRSRC hrsrc = FindResourceA(hExe, MAKEINTRESOURCEA(1), resourceType.c_str());
    if (!hrsrc) return nullptr;

    HGLOBAL hGlobal = LoadResource(hExe, hrsrc);
    if (!hGlobal) return nullptr;

    DWORD size = SizeofResource(hExe, hrsrc);
    void* data = LockResource(hGlobal);
    if (!data || size == 0) return nullptr;

    // 构造临时文件路径：使用原始 DLL 名称
    // delay-load 机制通过名称查找 DLL，所以必须用原始名称
    WCHAR tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    std::wstring wDllName(dllName.begin(), dllName.end());
    temp_file_path_ = std::wstring(tempDir) + wDllName;

    // 写入临时文件
    HANDLE hFile = CreateFileW(
        temp_file_path_.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) return nullptr;

    DWORD written = 0;
    WriteFile(hFile, data, size, &written, nullptr);
    FlushFileBuffers(hFile);
    CloseHandle(hFile);

    if (written != size) return nullptr;

    // LoadLibrary 加载DLL
    loaded_module_ = LoadLibraryW(temp_file_path_.c_str());

    // 加载成功后删除临时文件（DLL已在内存中，文件可删除）
    if (loaded_module_) {
        DeleteFileW(temp_file_path_.c_str());
    }

    return loaded_module_;
}

void EmbeddedDll::Cleanup() {
    if (loaded_module_) {
        FreeLibrary(loaded_module_);
        loaded_module_ = nullptr;
    }
    temp_file_path_.clear();
}

} // namespace tauricpp
