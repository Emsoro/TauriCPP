#pragma once
#include <string>
#include <optional>
#include <Windows.h>

namespace tauricpp {

/// 剪贴板API - 读写系统剪贴板
class Clipboard {
public:
    /// 读取剪贴板文本
    /// @return 剪贴板中的文本，如果剪贴板为空或不包含文本则返回空
    static std::optional<std::string> ReadText(HWND owner = nullptr);

    /// 写入文本到剪贴板
    /// @return 是否成功
    static bool WriteText(const std::string& text, HWND owner = nullptr);

    /// 清空剪贴板
    /// @return 是否成功
    static bool Clear(HWND owner = nullptr);
};

} // namespace tauricpp
