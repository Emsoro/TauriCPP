#pragma once
#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <nlohmann/json.hpp>

namespace tauricpp {

/// 前后端通信桥接
/// JS -> C++: window.__tauricpp__.invoke(cmd, args) -> 返回Promise
/// C++ -> JS: bridge.Emit(event, data) -> 前端通过 window.__tauricpp__.listen(event, callback) 接收
class Bridge {
public:
    using InvokeHandler = std::function<nlohmann::json(const nlohmann::json& args)>;
    using EventHandler = std::function<void(const nlohmann::json& data)>;

    static Bridge& Instance();

    /// 注册一个可被前端调用的命令
    void RegisterCommand(const std::string& cmd, InvokeHandler handler);

    /// 处理来自前端的调用请求（由WebView2的WebMessageReceived触发）
    std::string HandleInvoke(const std::string& cmd, const std::string& args_json);

    /// 向前端发送事件
    void Emit(const std::string& event, const nlohmann::json& data);

    /// 设置执行JS的回调（由Window设置，用于Emit）
    using ExecuteJsCallback = std::function<void(const std::string& js)>;
    void SetExecuteJsCallback(ExecuteJsCallback cb);

    /// 获取注入到前端的桥接JS代码
    static std::string GetBridgeJs();

private:
    Bridge() = default;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, InvokeHandler> commands_;
    ExecuteJsCallback execute_js_;
};

} // namespace tauricpp
