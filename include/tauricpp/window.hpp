#pragma once
#include <string>
#include <functional>
#include <memory>
#include <Windows.h>

// WebView2 前向声明
struct ICoreWebView2;
struct ICoreWebView2Controller;
struct ICoreWebView2Environment;
struct ICoreWebView2WebResourceRequest;
struct ICoreWebView2WebResourceResponse;

namespace tauricpp {

class Bridge;

/// 窗口类 - 封装Win32窗口 + WebView2
/// 核心功能：
/// 1. 创建Win32原生窗口
/// 2. 初始化WebView2环境
/// 3. 拦截WebResourceRequested，从VirtualFS提供内容
/// 4. 桥接Bridge实现双向通信
class Window {
public:
    struct Config {
        std::string title = "TauriCPP App";
        int width = 1024;
        int height = 768;
        bool center = true;
        bool resizable = true;
        std::string start_url = "https://tauricpp.app/index.html";
    };

    explicit Window(const Config& config = {});
    ~Window();

    /// 显示窗口并进入消息循环（阻塞）
    int Run();

    /// 关闭窗口
    void Close();

    /// 执行JavaScript代码
    void ExecuteJs(const std::string& js);

    /// 获取窗口句柄
    HWND GetHwnd() const { return hwnd_; }

private:
    /// 创建Win32窗口
    bool CreateNativeWindow();

    /// 初始化WebView2
    bool InitWebView();

    /// 设置WebResourceRequested拦截
    void SetupResourceInterception();

    /// 设置通信桥接
    void SetupBridge();

    /// Win32窗口过程
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    Config config_;
    HWND hwnd_ = nullptr;
    ICoreWebView2* webview_ = nullptr;
    ICoreWebView2Controller* controller_ = nullptr;
    ICoreWebView2Environment* env_ = nullptr;
    bool webview_ready_ = false;

    // WebView2 事件令牌，用于注销
    struct EventTokens {
        EventTokens();
        ~EventTokens();
        struct Impl;
        std::unique_ptr<Impl> impl;
    } event_tokens_;
};

} // namespace tauricpp
