#pragma once
#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <queue>
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
    using CloseCallback = std::function<bool()>;  ///< 返回true允许关闭，false阻止关闭
    using ResizeCallback = std::function<void(int w, int h)>;
    using MinimizeCallback = std::function<void()>;
    using MaximizeCallback = std::function<void()>;
    using FocusCallback = std::function<void()>;

    struct Config {
        std::string title = "TauriCPP App";
        int width = 1024;
        int height = 768;
        bool center = true;
        bool resizable = true;
        bool always_on_top = false;
        bool devtools = false;          ///< 是否启用DevTools（F12切换）
        bool frameless = false;              // 无边框模式
        std::string start_url = "https://tauricpp.app/index.html";
        COLORREF bg_color = RGB(15, 12, 41);  ///< 默认背景色（匹配前端渐变起点），消除白屏
    };

    explicit Window(const Config& config = {});
    ~Window();

    // 禁止拷贝和移动
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    /// 显示窗口并进入消息循环（阻塞）
    int Run();

    /// 关闭窗口
    void Close();

    /// 执行JavaScript代码
    void ExecuteJs(const std::string& js);

    /// 获取窗口句柄
    HWND GetHwnd() const { return hwnd_; }

    // ---- 窗口操作API ----
    void SetTitle(const std::string& title);
    void SetSize(int width, int height);
    void SetPosition(int x, int y);
    void SetAlwaysOnTop(bool on_top);
    void SetResizable(bool resizable);
    void SetIcon(HICON icon);
    void Minimize();
    void Maximize();
    void Restore();
    bool IsMinimized() const;
    bool IsMaximized() const;
    bool IsFocused() const;

    // ---- 窗口生命周期回调 ----
    void OnClose(CloseCallback cb) { on_close_ = std::move(cb); }
    void OnResize(ResizeCallback cb) { on_resize_ = std::move(cb); }
    void OnMinimize(MinimizeCallback cb) { on_minimize_ = std::move(cb); }
    void OnMaximize(MaximizeCallback cb) { on_maximize_ = std::move(cb); }
    void OnFocus(FocusCallback cb) { on_focus_ = std::move(cb); }


private:
    /// 创建Win32窗口
    bool CreateNativeWindow();

    /// 初始化WebView2
    bool InitWebView();

    /// 设置WebResourceRequested拦截
    void SetupResourceInterception();

    /// 设置通信桥接
    void SetupBridge();

    /// 设置WebView2默认背景色（消除白屏闪烁）
    void SetWebViewBackgroundColor();

    /// Win32窗口过程
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    static RECT GetAdjustedClientBounds(const Config& cfg, HWND hwnd, const RECT& clientRect);
    // --- 异步invoke相关 ---
    void FlushInvokeResponses();
    void FlushEmitQueue();
    Config config_;
    HWND hwnd_ = nullptr;
    ICoreWebView2* webview_ = nullptr;
    ICoreWebView2Controller* controller_ = nullptr;
    ICoreWebView2Environment* env_ = nullptr;
    bool webview_ready_ = false;
    std::atomic<bool> shutting_down_{false};

    // 窗口生命周期回调
    CloseCallback on_close_;
    ResizeCallback on_resize_;
    MinimizeCallback on_minimize_;
    MaximizeCallback on_maximize_;
    FocusCallback on_focus_;

    // WebView2 事件令牌，用于注销
    struct EventTokens {
        EventTokens();
        ~EventTokens();
        struct Impl;
        std::unique_ptr<Impl> impl;
    } event_tokens_;
    std::mutex response_mtx_;
    std::queue<std::pair<int64_t, std::string>> pending_responses_;
};

} // namespace tauricpp
