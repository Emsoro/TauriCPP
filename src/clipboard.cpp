#include "tauricpp/clipboard.hpp"

namespace tauricpp {

std::optional<std::string> Clipboard::ReadText(HWND owner) {
    if (!OpenClipboard(owner)) return std::nullopt;

    std::optional<std::string> result;

    // 优先读取Unicode文本
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        auto wstr = static_cast<const wchar_t*>(GlobalLock(hData));
        if (wstr) {
            int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                std::string text(len - 1, 0);
                WideCharToMultiByte(CP_UTF8, 0, wstr, -1, text.data(), len, nullptr, nullptr);
                result = std::move(text);
            }
            GlobalUnlock(hData);
        }
    } else {
        // 回退到ANSI文本
        hData = GetClipboardData(CF_TEXT);
        if (hData) {
            auto str = static_cast<const char*>(GlobalLock(hData));
            if (str) {
                result = std::string(str);
                GlobalUnlock(hData);
            }
        }
    }

    CloseClipboard();
    return result;
}

bool Clipboard::WriteText(const std::string& text, HWND owner) {
    if (!OpenClipboard(owner)) return false;

    EmptyClipboard();

    // 转换为宽字符串
    int wLen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (wLen <= 0) {
        CloseClipboard();
        return false;
    }

    size_t wBufSize = static_cast<size_t>(wLen) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wBufSize);
    if (!hMem) {
        CloseClipboard();
        return false;
    }

    auto wBuf = static_cast<wchar_t*>(GlobalLock(hMem));
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wBuf, wLen);
    GlobalUnlock(hMem);

    SetClipboardData(CF_UNICODETEXT, hMem);
    CloseClipboard();
    return true;
}

bool Clipboard::Clear(HWND owner) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    CloseClipboard();
    return true;
}

} // namespace tauricpp
