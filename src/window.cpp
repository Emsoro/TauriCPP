#include "tauricpp/window.hpp"
#include "tauricpp/bridge.hpp"
#include "tauricpp/virtual_fs.hpp"

#include <WebView2.h>
#include <wrl.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

using namespace Microsoft::WRL;

namespace tauricpp {

// ============================================================================
// EventTokens::Impl - 持有WebView2事件注册令牌
// ============================================================================
struct Window::EventTokens::Impl {
    EventRegistrationToken webResourceRequestedToken = {};
    EventRegistrationToken webMessageReceivedToken = {};
    EventRegistrationToken navigationCompletedToken = {};
    EventRegistrationToken sourceChangedToken = {};
};

Window::EventTokens::EventTokens() : impl(std::make_unique<Impl>()) {}
Window::EventTokens::~EventTokens() = default;

// ============================================================================
// 构造/析构
// ============================================================================
Window::Window(const Config& config) : config_(config) {}
Window::~Window() {
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
// 辅助函数
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
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"TauriCPPWindowClass";

    static bool registered = false;
    if (!registered) {
        RegisterClassExW(&wc);
        registered = true;
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!config_.resizable) {
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }

    RECT rect = { 0, 0, config_.width, config_.height };
    AdjustWindowRect(&rect, style, FALSE);

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (config_.center) {
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        x = (screenW - (rect.right - rect.left)) / 2;
        y = (screenH - (rect.bottom - rect.top)) / 2;
    }

    hwnd_ = CreateWindowExW(
        0, wc.lpszClassName,
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
    case WM_SIZE:
        if (self && self->controller_) {
            RECT bounds;
            GetClientRect(hwnd, &bounds);
            self->controller_->put_Bounds(bounds);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
// 初始化WebView2
// ============================================================================
bool Window::InitWebView() {
    WCHAR tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    std::wstring userDataFolder = std::wstring(tempDir) + L"tauricpp_webview2_" + std::to_wstring(GetCurrentProcessId());
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

                            // 设置WebView填满窗口
                            RECT bounds;
                            GetClientRect(hwnd_, &bounds);
                            controller_->put_Bounds(bounds);

                            // 设置虚拟主机名映射（将tauricpp://映射到虚拟文件系统）
                            SetupResourceInterception();

                            // 设置通信桥接
                            SetupBridge();

                            // 导航到起始URL
                            webview_->Navigate(Utf8ToWide(config_.start_url).c_str());

                            webview_ready_ = true;

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
        // 优先使用实际前端目录（开发模式），否则将VirtualFS内容写入临时目录
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
                    // 构建完整文件路径
                    std::wstring wPath(vpath.begin(), vpath.end());
                    // 去掉开头的 /
                    if (!wPath.empty() && wPath[0] == L'/') wPath = wPath.substr(1);
                    // 将 / 替换为 \，确保 Windows 路径正确
                    for (auto& ch : wPath) {
                        if (ch == L'/') ch = L'\\';
                    }
                    std::wstring fullPath = vfsDir + L"\\" + wPath;

                    // 创建子目录
                    std::wstring dirPart = fullPath.substr(0, fullPath.rfind(L'\\'));
                    // 递归创建目录
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
    // 当 VirtualFS 有文件时，覆盖文件系统映射的响应
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
                        // VirtualFS 中没有，让 WebView2 从文件系统映射获取（兜底）
                        return S_OK;
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
                [](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                    // get_WebMessageAsJson: 如果前端 postMessage(string)，返回 JSON编码的字符串如 "\"...\""
                    // 需要先解析外层JSON，如果是字符串则再解析内层
                    LPWSTR msgW = nullptr;
                    args->get_WebMessageAsJson(&msgW);
                    std::string msgJson = WideToUtf8(msgW);
                    CoTaskMemFree(msgW);

                    try {
                        auto parsed = nlohmann::json::parse(msgJson);
                        // postMessage(JSON.stringify(obj)) 导致双重编码
                        // get_WebMessageAsJson 返回的是 "\"{...}\""，parse后是字符串
                        nlohmann::json msg;
                        if (parsed.is_string()) {
                            msg = nlohmann::json::parse(parsed.get<std::string>());
                        } else {
                            msg = parsed;
                        }

                        if (msg.contains("__tauricpp_invoke") && msg["__tauricpp_invoke"].get<bool>()) {
                            int id = msg["id"].get<int>();
                            std::string cmd = msg["cmd"].get<std::string>();
                            std::string argsStr = msg["args"].dump();

                            std::string resultJson = Bridge::Instance().HandleInvoke(cmd, argsStr);

                            // 将结果发回前端
                            nlohmann::json response;
                            response["__tauricpp_result"] = true;
                            response["id"] = id;
                            try {
                                response["result"] = nlohmann::json::parse(resultJson);
                            } catch (...) {
                                response["result"] = resultJson;
                            }

                            std::string responseStr = response.dump();
                            sender->PostWebMessageAsJson(Utf8ToWide(responseStr).c_str());
                        }
                    } catch (const std::exception&) {}

                    return S_OK;
                }
            ).Get(),
            &event_tokens_.impl->webMessageReceivedToken
        );
    }

    // 在文档创建时注入桥接JS（在页面脚本执行之前）
    // 使用 AddScriptToExecuteOnDocumentCreated 确保 __tauricpp__ 在页面脚本运行前就可用
    ComPtr<ICoreWebView2_5> webview5;
    if (SUCCEEDED(webview_->QueryInterface(IID_PPV_ARGS(&webview5)))) {
        std::string bridgeJs = Bridge::GetBridgeJs();
        webview5->AddScriptToExecuteOnDocumentCreated(
            Utf8ToWide(bridgeJs).c_str(), nullptr
        );
    } else {
        // 回退：导航完成后注入
        webview_->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                    std::string bridgeJs = Bridge::GetBridgeJs();
                    sender->ExecuteScript(Utf8ToWide(bridgeJs).c_str(), nullptr);
                    return S_OK;
                }
            ).Get(),
            &event_tokens_.impl->navigationCompletedToken
        );
    }
}

// ============================================================================
// 执行JS
// ============================================================================
void Window::ExecuteJs(const std::string& js) {
    if (!webview_ || !webview_ready_) return;
    webview_->ExecuteScript(Utf8ToWide(js).c_str(), nullptr);
}

// ============================================================================
// 运行
// ============================================================================
int Window::Run() {
    if (!CreateNativeWindow()) return -1;

    ShowWindow(hwnd_, SW_SHOW);
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
