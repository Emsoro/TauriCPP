#pragma once
#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <vector>
#include <queue>
#include <Windows.h>
#include <nlohmann/json.hpp>

namespace tauricpp {

// 自定义消息 ID，用于 Bridge 与 Window 之间的线程间通信
inline constexpr UINT WM_TAURICPP_INVOKE_RESPONSE = WM_APP + 300;
inline constexpr UINT WM_TAURICPP_EMIT            = WM_APP + 301;

/// 前后端通信桥接
/// JS -> C++: window.__tauricpp__.invoke(cmd, args) -> 返回Promise
/// C++ -> JS: bridge.Emit(event, data) -> 前端通过 window.__tauricpp__.listen(event, callback) 接收
class Bridge {
public:
    using InvokeHandler = std::function<nlohmann::json(const nlohmann::json& args)>;
    using EventHandler = std::function<void(const nlohmann::json& data)>;

    static Bridge& Instance();

    // 禁止拷贝和移动
    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;

    /// 注册一个可被前端调用的命令
    void RegisterCommand(const std::string& cmd, InvokeHandler handler);

    /// 注销一个命令
    void UnregisterCommand(const std::string& cmd);

    /// 类型安全的命令注册
    /// 用法: bridge.RegisterCommand<Args, Result>("cmd", [](const Args& args) -> Result { ... });
    /// Args和Result必须是可被nlohmann/json序列化/反序列化的类型
    template<typename ArgsT, typename ResultT>
    void RegisterCommand(const std::string& cmd, std::function<ResultT(const ArgsT&)> handler) {
        RegisterCommand(cmd, [handler = std::move(handler)](const nlohmann::json& args) -> nlohmann::json {
            ArgsT typed_args = args.get<ArgsT>();
            ResultT result = handler(typed_args);
            return nlohmann::json(result);
        });
    }

    /// 简化版：无参数命令注册
    template<typename ResultT>
    void RegisterCommand(const std::string& cmd, std::function<ResultT()> handler) {
        RegisterCommand(cmd, [handler = std::move(handler)](const nlohmann::json&) -> nlohmann::json {
            ResultT result = handler();
            return nlohmann::json(result);
        });
    }

    /// 处理来自前端的调用请求（由WebView2的WebMessageReceived触发）
    std::string HandleInvoke(const std::string& cmd, const std::string& args_json);

    /// 向前端发送事件
    void Emit(const std::string& event, const nlohmann::json& data);

    /// 设置执行JS的回调（由Window设置，用于Emit）
    using ExecuteJsCallback = std::function<void(const std::string& js)>;
    void SetExecuteJsCallback(ExecuteJsCallback cb);

    /// 获取注入到前端的桥接JS代码
    static std::string GetBridgeJs();

    void SetUiHwnd(HWND hwnd, DWORD tid);
    std::vector<std::pair<std::string, std::string>> ConsumePendingEvents();
private:
    Bridge() = default;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, InvokeHandler> commands_;
    ExecuteJsCallback execute_js_;
    HWND ui_hwnd_ = nullptr;
    DWORD ui_thread_id_ = 0;
    std::mutex pending_mtx_;
    std::vector<std::pair<std::string, std::string>> pending_events_;
};

} // namespace tauricpp
