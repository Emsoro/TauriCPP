#include "tauricpp/window.hpp"
#include "tauricpp/bridge.hpp"
#include "tauricpp/virtual_fs.hpp"

#include <WebView2.h>
#include <wrl.h>
#include <shlwapi.h>
#include <shellscalingapi.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shcore.lib")   // 用于 GetDpiForMonitor / SetProcessDpiAwarenessContext
// 手动定义坐标提取宏（避免包含windowsx.h带来的宏冲突）
#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

using namespace Microsoft::WRL;

// ============================================================================
// DPI 辅助：Per-Monitor V2 感知（运行时兜底，即便 manifest 缺失也能正常渲染）
// ============================================================================
static void EnsurePerMonitorDpiAwareV2() {
    // 优先使用 Win10+ 的 PerMonitorV2：系统自动缩放非客户区 + 子窗口 DPI 通知
    typedef BOOL(WINAPI* PSetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
    auto setContext = reinterpret_cast<PSetProcessDpiAwarenessContext>(
        ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (setContext) {
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = -4
        if (setContext(reinterpret_cast<DPI_AWARENESS_CONTEXT>(-4))) {
            return;
        }
    }
    // Win8.1 兜底：PROCESS_PER_MONITOR_DPI_AWARE = 2
    typedef HRESULT(WINAPI* PSetProcessDpiAwareness)(PROCESS_DPI_AWARENESS);
    auto setAwareness = reinterpret_cast<PSetProcessDpiAwareness>(
        ::GetProcAddress(::GetModuleHandleW(L"shcore.dll"), "SetProcessDpiAwareness"));
    if (setAwareness) {
        setAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    }
}

namespace tauricpp {

// ============================================================================
// EventTokens::Impl - 持有WebView2事件注册令牌
// ============================================================================
struct Window::EventTokens::Impl {
    EventRegistrationToken webResourceRequestedToken = {};
    EventRegistrationToken webMessageReceivedToken = {};
    EventRegistrationToken navigationCompletedToken = {};
    EventRegistrationToken sourceChangedToken = {};
    EventRegistrationToken focusChangedToken = {};
};

Window::EventTokens::EventTokens() : impl(std::make_unique<Impl>()) {}
Window::EventTokens::~EventTokens() = default;

// ============================================================================
// 构造/析构
// ============================================================================
Window::Window(const Config& config) : config_(config) {}

Window::~Window() {
    shutting_down_ = true;

    // 注销WebView2事件处理器
    if (webview_) {
        ComPtr<ICoreWebView2_2> webview2;
        if (SUCCEEDED(webview_->QueryInterface(IID_PPV_ARGS(&webview2)))) {
            webview2->remove_WebResourceRequested(event_tokens_.impl->webResourceRequestedToken);
            webview2->remove_WebMessageReceived(event_tokens_.impl->webMessageReceivedToken);
        }
        webview_->remove_NavigationCompleted(event_tokens_.impl->navigationCompletedToken);
    }

    if (controller_) {
        controller_->Release();
        controller_ = nullptr;
    }
    if (webview_) {
        webview_->Release();
        webview_ = nullptr;
    }
    if (env_) {
        env_->Release();
        env_ = nullptr;
    }
}

// ============================================================================
// 辅助函数 - 正确的UTF-8 <-> Wide转换
// ============================================================================
static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring result(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, result.data(), len);
    return result;
}

static std::string WideToUtf8(const std::wstring& str) {
    if (str.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, result.data(), len, nullptr, nullptr);
    return result;
}

// ============================================================================
// 创建Win32原生窗口
// ============================================================================
bool Window::CreateNativeWindow() {
    EnsurePerMonitorDpiAwareV2();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    // 使用配置的背景色作为窗口类背景，消除白色闪烁
    wc.hbrBackground = CreateSolidBrush(config_.bg_color);
    wc.lpszClassName = L"TauriCPPWindowClass";

    static bool registered = false;
    if (!registered) {
        RegisterClassExW(&wc);
        registered = true;
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (config_.frameless) {
        style = WS_OVERLAPPEDWINDOW & ~WS_CAPTION;
        if (!config_.resizable) {
            style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        }
    } else {
        if (!config_.resizable) {
            style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        }
    }
    // 根据窗口目标显示位置的 DPI 将"设计尺寸"换算为实际物理像素尺寸
    // 设计尺寸约定：以 96 DPI (100%) 为基准（即 CSS 逻辑像素）
    UINT targetDpi = 96;
    {
        // 创建一个隐藏临时窗口以获得准确的 WMDPICHANGED 前的屏幕DPI；
        // 更简单：直接用主显示器的 DPI，窗口创建后系统会再次发送 WM_DPICHANGED。
        typedef UINT(WINAPI* PGetDpiForSystem)();
        auto getDpiForSystem = reinterpret_cast<PGetDpiForSystem>(
            ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "GetDpiForSystem"));
        if (getDpiForSystem) {
            targetDpi = getDpiForSystem();
        }
    }
    const int baseDpi = 96;
    int scaledWidth = MulDiv(config_.width, targetDpi, baseDpi);
    int scaledHeight = MulDiv(config_.height, targetDpi, baseDpi);

    RECT rect = { 0, 0, scaledWidth, scaledHeight };
    // 注意：AdjustWindowRect 非 DPI 感知，使用 DPI 感知版本（Win10 1607+），
    // 若不可用则退回 AdjustWindowRect，DPI 差异由 WM_DPICHANGED 补偿。
    typedef BOOL(WINAPI* PAdjustWindowRectExForDpi)(LPRECT, DWORD, BOOL, DWORD, UINT);
    auto adjustForDpi = reinterpret_cast<PAdjustWindowRectExForDpi>(
        ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
    DWORD exStyle = config_.always_on_top ? WS_EX_TOPMOST : 0;
    if (adjustForDpi) {
        adjustForDpi(&rect, style, FALSE, exStyle, targetDpi);
    } else {
        AdjustWindowRectEx(&rect, style, FALSE, exStyle);
    }

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (config_.center) {
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        x = (screenW - (rect.right - rect.left)) / 2;
        y = (screenH - (rect.bottom - rect.top)) / 2;
    }

    // 关键：创建时不显示窗口（不传WS_VISIBLE），等WebView2导航完成后再显示
    hwnd_ = CreateWindowExW(
        exStyle,
        wc.lpszClassName,
        Utf8ToWide(config_.title).c_str(),
        style, x, y,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, wc.hInstance, this
    );

    return hwnd_ != nullptr;
}

// ============================================================================
// 窗口过程
// ============================================================================
LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    switch (msg) {
    case WM_SIZE: {
        if (self && self->controller_) {
            RECT bounds;
            GetClientRect(hwnd, &bounds);
            bounds = GetAdjustedClientBounds(self->config_, hwnd, bounds);
            self->controller_->put_Bounds(bounds);
        }
        if (self && self->on_resize_ && wParam != SIZE_MINIMIZED) {
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            self->on_resize_(clientRect.right, clientRect.bottom);
        }
        if (self) {
            if (wParam == SIZE_MINIMIZED && self->on_minimize_) {
                self->on_minimize_();
            }
            if (wParam == SIZE_MAXIMIZED && self->on_maximize_) {
                self->on_maximize_();
            }
        }
        return 0;
    }

    case WM_DPICHANGED: {
        // DPI 变化（拖到不同缩放比例的屏幕）：重新调整窗口尺寸 + WebView2 bounds
        // 确保 WebView2 始终以物理像素渲染，避免位图拉伸导致模糊
        if (self) {
            UINT newDpi = LOWORD(wParam);
            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            if (suggested) {
                SetWindowPos(hwnd, nullptr,
                    suggested->left, suggested->top,
                    suggested->right - suggested->left,
                    suggested->bottom - suggested->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
            if (self->controller_) {
                RECT bounds;
                GetClientRect(hwnd, &bounds);
                bounds = GetAdjustedClientBounds(self->config_, hwnd, bounds);
                self->controller_->put_Bounds(bounds);
            }
            (void)newDpi;
        }
        return 0;
    }

    case WM_ACTIVATE:
        if (self && self->on_focus_ && LOWORD(wParam) != WA_INACTIVE) {
            self->on_focus_();
        }
        return 0;

    case WM_CLOSE:
        if (self && self->on_close_) {
            if (!self->on_close_()) {
                // 用户回调拒绝关闭
                return 0;
            }
        }
        break;

    case WM_DESTROY:
        if (self) {
            self->shutting_down_ = true;
        }
        PostQuitMessage(0);
        return 0;

    // 处理异步invoke响应
    case WM_TAURICPP_INVOKE_RESPONSE:
        if (self) self->FlushInvokeResponses();
        return 0;

    // 处理线程安全Emit队列
    case WM_TAURICPP_EMIT:
        if (self) self->FlushEmitQueue();
        return 0;
    case WM_NCCALCSIZE: 
        if (self && self->config_.frameless && wParam) {
            return 0;
        }
        break;
    case WM_NCPAINT: 
        if (self && self->config_.frameless) {
            return 0; 
        }
        break;
    case WM_NCACTIVATE:
        if (self && self->config_.frameless) {
            return TRUE; 
        }
        break;
    case WM_NCHITTEST:
        if (self && self->config_.frameless) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            RECT rc;
            GetClientRect(hwnd, &rc);
            const int border = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);

            if (self->config_.resizable) {
                if (pt.x < border && pt.y < border) return HTTOPLEFT;
                if (pt.x >= rc.right - border && pt.y < border) return HTTOPRIGHT;
                if (pt.x < border && pt.y >= rc.bottom - border) return HTBOTTOMLEFT;
                if (pt.x >= rc.right - border && pt.y >= rc.bottom - border) return HTBOTTOMRIGHT;
                if (pt.x < border) return HTLEFT;
                if (pt.x >= rc.right - border) return HTRIGHT;
                if (pt.y < border) return HTTOP;
                if (pt.y >= rc.bottom - border) return HTBOTTOM;
            }
            if (pt.y < 40) return HTCAPTION;

            return HTCLIENT;
        }
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
// 设置WebView2默认背景色 - 消除白屏核心方案
// ============================================================================
void Window::SetWebViewBackgroundColor() {
    if (!controller_) return;

    // 使用 ICoreWebView2Controller2 设置默认背景色
    // 这样 WebView2 在页面加载前就显示指定颜色而非白色
    ComPtr<ICoreWebView2Controller2> controller2;
    if (SUCCEEDED(controller_->QueryInterface(IID_PPV_ARGS(&controller2)))) {
        COREWEBVIEW2_COLOR bgColor;
        bgColor.A = 255;
        bgColor.R = GetRValue(config_.bg_color);
        bgColor.G = GetGValue(config_.bg_color);
        bgColor.B = GetBValue(config_.bg_color);
        controller2->put_DefaultBackgroundColor(bgColor);
    }
}

// ============================================================================
// 初始化WebView2
// ============================================================================
bool Window::InitWebView() {
    // 使用exe名称作为稳定的基础路径，确保IndexedDB等持久数据跨启动保留
    WCHAR tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    WCHAR exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    WCHAR* exeName = wcsrchr(exePath, L'\\');
    exeName = exeName ? exeName + 1 : exePath;
    std::wstring userDataFolder = std::wstring(tempDir) + L"tauricpp_" + exeName;
    CreateDirectoryW(userDataFolder.c_str(), nullptr);
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userDataFolder.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result)) return result;

                env_ = env;
                env_->AddRef();

                env->CreateCoreWebView2Controller(hwnd_,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(result)) return result;

                            controller_ = controller;
                            controller_->AddRef();

                            controller_->get_CoreWebView2(&webview_);
                            if (webview_) {
                                webview_->AddRef();
                            }
                            ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(webview_->get_Settings(&settings))) {
                                settings->put_AreDevToolsEnabled(config_.devtools);  // 控制 DevTools
                            }
                            // 设置WebView填满窗口
                            RECT bounds;
                            GetClientRect(hwnd_, &bounds);
                            bounds = GetAdjustedClientBounds(config_, hwnd_, bounds);
                            controller_->put_Bounds(bounds);

                            // ★ 关键：设置WebView2默认背景色，消除白屏闪烁
                            SetWebViewBackgroundColor();

                            // 设置虚拟主机名映射（将tauricpp://映射到虚拟文件系统）
                            SetupResourceInterception();

                            // 设置通信桥接
                            SetupBridge();

                            // 导航到起始URL
                            webview_->Navigate(Utf8ToWide(config_.start_url).c_str());

                            // ★ 导航完成后刷新WebView布局
                            webview_->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                                        // 导航完成后重新设置bounds确保WebView正确布局
                                        if (controller_) {
                                            RECT bounds;
                                            GetClientRect(hwnd_, &bounds);
                                            bounds = GetAdjustedClientBounds(config_, hwnd_, bounds);
                                            controller_->put_Bounds(bounds);
                                        }
                                        return S_OK;
                                    }
                                ).Get(),
                                &event_tokens_.impl->navigationCompletedToken
                            );

                            webview_ready_ = true;
                            // ★ 设置Bridge的UI线程信息
                            Bridge::Instance().SetUiHwnd(hwnd_, GetCurrentThreadId());

                            return S_OK;
                        }
                    ).Get()
                );
                return S_OK;
            }
        ).Get()
    );

    return SUCCEEDED(hr);
}

// ============================================================================
// 设置WebResourceRequested拦截 - 核心防泄露机制
// ============================================================================
void Window::SetupResourceInterception() {
    if (!webview_) return;

    // 方案：SetVirtualHostNameToFolderMapping 映射到实际前端目录
    // 这样 WebView2 识别 tauricpp.app 为本地域名，不会发起真实网络请求
    // WebResourceRequested 拦截请求，优先从 VirtualFS 内存提供内容
    // 如果 VirtualFS 中没有（开发阶段未加载），则回退到文件系统映射

    ComPtr<ICoreWebView2_3> webview3;
    if (SUCCEEDED(webview_->QueryInterface(IID_PPV_ARGS(&webview3)))) {
        // 映射到 exe 旁边的 sample/frontend 目录（作为兜底）
        // 生产环境中 VirtualFS 从 exe 资源加载，此目录可以不存在
        WCHAR exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        // 获取 exe 所在目录
        WCHAR* lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash) *lastSlash = L'\0';

        // 始终映射虚拟主机名
        std::wstring frontendDir;
        std::vector<std::wstring> candidates = {
            std::wstring(exePath) + L"\\..\\..\\sample\\frontend",
            std::wstring(exePath) + L"\\frontend",
        };

        for (const auto& cand : candidates) {
            WCHAR absPath[MAX_PATH];
            if (GetFullPathNameW(cand.c_str(), MAX_PATH, absPath, nullptr)) {
                if (GetFileAttributesW(absPath) != INVALID_FILE_ATTRIBUTES) {
                    frontendDir = absPath;
                    break;
                }
            }
        }

        // 没找到前端目录时，将VirtualFS内容写入临时目录
        if (frontendDir.empty()) {
            WCHAR tempDir[MAX_PATH];
            GetTempPathW(MAX_PATH, tempDir);
            std::wstring vfsDir = std::wstring(tempDir) + L"tauricpp_vfs_" + std::to_wstring(GetCurrentProcessId());
            CreateDirectoryW(vfsDir.c_str(), nullptr);
            frontendDir = vfsDir;

            // 将VirtualFS中的所有文件写入临时目录
            auto paths = VirtualFS::Instance().GetAllPaths();
            for (const auto& vpath : paths) {
                VirtualFS::VFile file;
                if (VirtualFS::Instance().FindFile(vpath, file)) {
                    // 使用正确的UTF-8转Wide转换
                    std::wstring wPath = Utf8ToWide(vpath);
                    // 去掉开头的 /
                    if (!wPath.empty() && wPath[0] == L'/') wPath = wPath.substr(1);
                    // 将 / 替换为 \，确保 Windows 路径正确
                    for (auto& ch : wPath) {
                        if (ch == L'/') ch = L'\\';
                    }
                    std::wstring fullPath = vfsDir + L"\\" + wPath;

                    // 创建子目录
                    std::wstring dirPart = fullPath.substr(0, fullPath.rfind(L'\\'));
                    for (size_t pos = vfsDir.size(); pos < dirPart.size(); ) {
                        pos = dirPart.find(L'\\', pos);
                        if (pos == std::wstring::npos) pos = dirPart.size();
                        std::wstring subDir = dirPart.substr(0, pos);
                        CreateDirectoryW(subDir.c_str(), nullptr);
                        pos++;
                    }

                    // 写入文件
                    HANDLE hFile = CreateFileW(fullPath.c_str(), GENERIC_WRITE, 0,
                        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (hFile != INVALID_HANDLE_VALUE) {
                        DWORD written = 0;
                        WriteFile(hFile, file.data.data(), (DWORD)file.data.size(), &written, nullptr);
                        CloseHandle(hFile);
                    }
                }
            }
        }

        webview3->SetVirtualHostNameToFolderMapping(
            L"tauricpp.app",
            frontendDir.c_str(),
            COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW
        );
    }

    // 拦截所有 tauricpp.app 请求，从内存VirtualFS提供内容
    ComPtr<ICoreWebView2_2> webview2;
    if (SUCCEEDED(webview_->QueryInterface(IID_PPV_ARGS(&webview2)))) {
        webview2->AddWebResourceRequestedFilter(L"https://tauricpp.app/*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);

        webview2->add_WebResourceRequested(
            Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                [this](ICoreWebView2* sender, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                    ComPtr<ICoreWebView2WebResourceRequest> request;
                    args->get_Request(&request);

                    LPWSTR uriW = nullptr;
                    request->get_Uri(&uriW);
                    std::string uri = WideToUtf8(uriW);
                    CoTaskMemFree(uriW);

                    // 解析虚拟路径：https://tauricpp.app/path -> /path
                    const std::string scheme = "https://tauricpp.app";
                    std::string vpath;
                    if (uri.find(scheme) == 0) {
                        vpath = uri.substr(scheme.size());
                        auto qpos = vpath.find('?');
                        if (qpos != std::string::npos) vpath = vpath.substr(0, qpos);
                        auto hpos = vpath.find('#');
                        if (hpos != std::string::npos) vpath = vpath.substr(0, hpos);
                        if (vpath.empty() || vpath[0] != '/') vpath = "/" + vpath;
                    } else {
                        return S_OK;
                    }

                    // 从虚拟文件系统查找
                    VirtualFS::VFile file;
                    if (!VirtualFS::Instance().FindFile(vpath, file)) {
                        // SPA回退：如果找不到文件且不是资源文件，返回index.html
                        // 这使得前端路由（如 /settings, /about）可以正常工作
                        bool isStaticAsset = vpath.rfind('.') != std::string::npos;
                        // 排除常见静态资源扩展名
                        static const char* assetExts[] = {
                            ".js", ".css", ".png", ".jpg", ".jpeg", ".gif",
                            ".svg", ".ico", ".woff", ".woff2", ".ttf", ".otf",
                            ".wasm", ".json", ".xml", ".txt", ".webp", ".map", nullptr
                        };
                        bool isAsset = false;
                        for (auto ext = assetExts; *ext; ++ext) {
                            if (vpath.size() >= strlen(*ext) &&
                                _stricmp(vpath.c_str() + vpath.size() - strlen(*ext), *ext) == 0) {
                                isAsset = true;
                                break;
                            }
                        }

                        if (!isAsset && VirtualFS::Instance().FindFile("/index.html", file)) {
                            // SPA fallback - 返回index.html
                        } else {
                            // VirtualFS 中没有，让 WebView2 从文件系统映射获取（兜底）
                            return S_OK;
                        }
                    }

                    // 从内存提供内容，覆盖文件系统映射
                    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, file.data.size());
                    if (!hGlobal) return E_OUTOFMEMORY;
                    void* pData = GlobalLock(hGlobal);
                    memcpy(pData, file.data.data(), file.data.size());
                    GlobalUnlock(hGlobal);

                    ComPtr<IStream> stream;
                    CreateStreamOnHGlobal(hGlobal, TRUE, &stream);

                    std::wstring headers = L"Content-Type: " + Utf8ToWide(file.mime_type) + L"\r\n"
                                         + L"Access-Control-Allow-Origin: *\r\n"
                                         + L"Cache-Control: no-cache";

                    ComPtr<ICoreWebView2WebResourceResponse> response;
                    if (env_) {
                        env_->CreateWebResourceResponse(
                            stream.Get(), 200, L"OK", headers.c_str(), &response
                        );
                        args->put_Response(response.Get());
                    }

                    return S_OK;
                }
            ).Get(),
            &event_tokens_.impl->webResourceRequestedToken
        );
    }
}

// ============================================================================
// 设置通信桥接
// ============================================================================
void Window::SetupBridge() {
    if (!webview_) return;

    // 设置Bridge的JS执行回调
    Bridge::Instance().SetExecuteJsCallback([this](const std::string& js) {
        ExecuteJs(js);
    });

    // 监听来自前端的消息
    ComPtr<ICoreWebView2_2> webview2;
    if (SUCCEEDED(webview_->QueryInterface(IID_PPV_ARGS(&webview2)))) {
        webview2->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [this](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                    LPWSTR msgW = nullptr;
                    args->get_WebMessageAsJson(&msgW);
                    std::string msgJson = WideToUtf8(msgW);
                    CoTaskMemFree(msgW);

                    try {
                        auto parsed = nlohmann::json::parse(msgJson);
                        nlohmann::json msg;
                        if (parsed.is_string()) {
                            msg = nlohmann::json::parse(parsed.get<std::string>());
                        } else {
                            msg = parsed;
                        }
                        if (msg.contains("__tauricpp_start_drag")) {
                            ReleaseCapture();
                            SendMessage(hwnd_, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
                            return S_OK;
                        }
                        if (msg.contains("__tauricpp_invoke") && msg["__tauricpp_invoke"].get<bool>()) {
                            int id = msg["id"].get<int>();
                            std::string cmd = msg["cmd"].get<std::string>();
                            std::string argsStr = msg["args"].dump();

                            std::thread([this, id, cmd, argsStr]() {
                                std::string resultJson = Bridge::Instance().HandleInvoke(cmd, argsStr);
                                {
                                    std::lock_guard<std::mutex> lk(response_mtx_);
                                    pending_responses_.push({id, std::move(resultJson)});
                                }
                                PostMessage(hwnd_, WM_TAURICPP_INVOKE_RESPONSE, 0, 0);
                            }).detach();

                            return S_OK; // 不在此处回复
                        }
                    } catch (const std::exception&) {}

                    return S_OK;
                }
            ).Get(),
            &event_tokens_.impl->webMessageReceivedToken
        );
    }

    // 在文档创建时注入桥接JS（在页面脚本执行之前）
    ComPtr<ICoreWebView2_5> webview5;
    if (SUCCEEDED(webview_->QueryInterface(IID_PPV_ARGS(&webview5)))) {
        std::string bridgeJs = Bridge::GetBridgeJs();
        webview5->AddScriptToExecuteOnDocumentCreated(
            Utf8ToWide(bridgeJs).c_str(), nullptr
        );
    } else {
        // 回退：导航完成后注入桥接JS
        webview_->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                    std::string bridgeJs = Bridge::GetBridgeJs();
                    sender->ExecuteScript(Utf8ToWide(bridgeJs).c_str(), nullptr);
                    return S_OK;
                }
            ).Get(),
            &event_tokens_.impl->sourceChangedToken  // 复用sourceChangedToken作为回退令牌
        );
    }
}

// ============================================================================
// 执行JS
// ============================================================================
void Window::ExecuteJs(const std::string& js) {
    if (!webview_ || !webview_ready_ || shutting_down_) return;
    webview_->ExecuteScript(Utf8ToWide(js).c_str(), nullptr);
}
RECT Window::GetAdjustedClientBounds(const Config& cfg, HWND hwnd, const RECT& clientRect) {
    if (!cfg.frameless || !cfg.resizable || IsZoomed(hwnd) || IsIconic(hwnd)) {
        return clientRect;
    }
    const int border = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
    RECT adjusted = clientRect;
    adjusted.left   += border;
    adjusted.top    += border;
    adjusted.right  -= border;
    adjusted.bottom -= border;
    return adjusted;
}
void Window::FlushInvokeResponses() {
    std::vector<std::pair<int64_t, std::string>> batch;
    {
        std::lock_guard<std::mutex> lk(response_mtx_);
        while (!pending_responses_.empty()) {
            batch.push_back(std::move(pending_responses_.front()));
            pending_responses_.pop();
        }
    }
    for (auto& [id, resultJson] : batch) {
        nlohmann::json response;
        response["__tauricpp_result"] = true;
        response["id"] = id;
        try {
            response["result"] = nlohmann::json::parse(resultJson);
        } catch (...) {
            response["result"] = resultJson;
        }
        std::string responseStr = response.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        webview_->PostWebMessageAsJson(Utf8ToWide(responseStr).c_str());
    }
}
void Window::FlushEmitQueue() {
    auto events = Bridge::Instance().ConsumePendingEvents();
    for (auto& [event, payloadJson] : events) {
        nlohmann::json callArgs;
        callArgs["event"] = event;
        callArgs["data"] = nlohmann::json::parse(payloadJson);
        std::string js = "__tauricpp_internal_emit(" 
            + callArgs.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) 
            + ");";
        ExecuteJs(js);
    }
}
// ============================================================================
// 窗口操作API
// ============================================================================
void Window::SetTitle(const std::string& title) {
    if (hwnd_) {
        SetWindowTextW(hwnd_, Utf8ToWide(title).c_str());
    }
}

void Window::SetSize(int width, int height) {
    if (!hwnd_) return;
    DWORD style = GetWindowLongW(hwnd_, GWL_STYLE);
    RECT rect = { 0, 0, width, height };
    AdjustWindowRect(&rect, style, FALSE);
    SetWindowPos(hwnd_, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER);
}

void Window::SetPosition(int x, int y) {
    if (hwnd_) {
        SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
}

void Window::SetAlwaysOnTop(bool on_top) {
    if (hwnd_) {
        HWND zIndex = on_top ? HWND_TOPMOST : HWND_NOTOPMOST;
        SetWindowPos(hwnd_, zIndex, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
}

void Window::SetResizable(bool resizable) {
    if (!hwnd_) return;
    DWORD style = GetWindowLongW(hwnd_, GWL_STYLE);
    if (resizable) {
        style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    } else {
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }
    SetWindowLongW(hwnd_, GWL_STYLE, style);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
}

void Window::SetIcon(HICON icon) {
    if (hwnd_ && icon) {
        SendMessageW(hwnd_, WM_SETICON, ICON_BIG, (LPARAM)icon);
        SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, (LPARAM)icon);
    }
}

void Window::Minimize() {
    if (hwnd_) ShowWindow(hwnd_, SW_MINIMIZE);
}

void Window::Maximize() {
    if (hwnd_) ShowWindow(hwnd_, SW_MAXIMIZE);
}

void Window::Restore() {
    if (hwnd_) ShowWindow(hwnd_, SW_RESTORE);
}

bool Window::IsMinimized() const {
    return hwnd_ ? IsIconic(hwnd_) : false;
}

bool Window::IsMaximized() const {
    return hwnd_ ? IsZoomed(hwnd_) : false;
}

bool Window::IsFocused() const {
    return hwnd_ ? (GetForegroundWindow() == hwnd_) : false;
}

// ============================================================================
// 运行
// ============================================================================
int Window::Run() {
    if (!CreateNativeWindow()) return -1;

    // 立即显示窗口（背景色已通过SetWebViewBackgroundColor和窗口类画刷设置为深色）
    // 这样用户立刻看到窗口，而不是等WebView2初始化
    ShowWindow(hwnd_, SW_SHOW);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    UpdateWindow(hwnd_);

    if (!InitWebView()) return -1;

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}

void Window::Close() {
    if (hwnd_) {
        PostMessage(hwnd_, WM_CLOSE, 0, 0);
    }
}

} // namespace tauricpp
