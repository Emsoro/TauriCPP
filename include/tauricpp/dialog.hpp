#pragma once
#include <string>
#include <vector>
#include <optional>
#include <Windows.h>

namespace tauricpp {

/// 文件对话框API - 封装Windows IFileDialog
class Dialog {
public:
    struct FileFilter {
        std::string name;     ///< 显示名称，如 "Text Files"
        std::string pattern;  ///< 扩展名模式，如 "*.txt"
    };

    struct OpenOptions {
        std::string title = "Open File";
        std::string default_path;
        std::vector<FileFilter> filters = {{"All Files", "*.*"}};
        bool multi_select = false;
    };

    struct SaveOptions {
        std::string title = "Save File";
        std::string default_path;
        std::string default_filename;
        std::vector<FileFilter> filters = {{"All Files", "*.*"}};
        bool overwrite_prompt = true;
    };

    /// 打开文件选择对话框（阻塞）
    /// @return 选中的文件路径列表，取消返回空
    static std::vector<std::string> OpenFile(HWND parent, const OpenOptions& options = {});

    /// 保存文件对话框（阻塞）
    /// @return 选中的保存路径，取消返回空
    static std::optional<std::string> SaveFile(HWND parent, const SaveOptions& options = {});

    /// 选择文件夹对话框（阻塞）
    /// @return 选中的文件夹路径，取消返回空
    static std::optional<std::string> PickFolder(HWND parent, const std::string& title = "Select Folder");

    /// 显示消息框
    static void ShowInfo(HWND parent, const std::string& title, const std::string& message);
    static bool AskConfirm(HWND parent, const std::string& title, const std::string& message);
};

} // namespace tauricpp
