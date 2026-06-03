#pragma once
#include <string>
#include <Windows.h>

namespace tauricpp {

/// 嵌入式DLL加载器
/// 将DLL作为资源嵌入exe，运行时释放到临时文件并加载
/// 使用 FILE_FLAG_DELETE_ON_CLOSE 确保进程退出后自动删除，不留痕迹
class EmbeddedDll {
public:
    /// 从exe资源段加载DLL
    /// @param resourceId 资源ID (如 "WEBVIEW2_LOADER")
    /// @param resourceType 资源类型 (如 "DLL")
    /// @param dllName 临时文件名 (如 "WebView2Loader.dll")
    /// @return 加载后的模块句柄，失败返回 nullptr
    static HMODULE Load(const std::string& resourceId,
                        const std::string& resourceType,
                        const std::string& dllName);

    /// 卸载已加载的DLL并清理
    static void Cleanup();

    /// 已加载的模块句柄（public，供 delay-load 钩子访问）
    static HMODULE loaded_module_;

private:
    static std::wstring temp_file_path_;
};

} // namespace tauricpp
