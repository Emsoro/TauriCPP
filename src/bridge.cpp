#include "tauricpp/bridge.hpp"
#include <sstream>

namespace tauricpp {

Bridge& Bridge::Instance() {
    static Bridge instance;
    return instance;
}

void Bridge::RegisterCommand(const std::string& cmd, InvokeHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    commands_[cmd] = std::move(handler);
}

std::string Bridge::HandleInvoke(const std::string& cmd, const std::string& args_json) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = commands_.find(cmd);
    if (it == commands_.end()) {
        nlohmann::json result;
        result["error"] = "Unknown command: " + cmd;
        return result.dump();
    }

    try {
        nlohmann::json args = nlohmann::json::parse(args_json);
        nlohmann::json result = it->second(args);
        return result.dump();
    } catch (const std::exception& e) {
        nlohmann::json result;
        result["error"] = std::string("Exception: ") + e.what();
        return result.dump();
    }
}

void Bridge::Emit(const std::string& event, const nlohmann::json& data) {
    std::string js = "__tauricpp_internal_onEvent('" + event + "', " + data.dump() + ");";
    if (execute_js_) {
        execute_js_(js);
    }
}

void Bridge::SetExecuteJsCallback(ExecuteJsCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    execute_js_ = std::move(cb);
}

std::string Bridge::GetBridgeJs() {
    return R"js(
(function() {
    const __tauricpp__ = {
        _listeners: {},
        _invokeId: 0,
        _invokeCallbacks: {},

        invoke: function(cmd, args) {
            return new Promise((resolve, reject) => {
                const id = ++this._invokeId;
                this._invokeCallbacks[id] = { resolve, reject };
                window.chrome.webview.postMessage(JSON.stringify({
                    __tauricpp_invoke: true,
                    id: id,
                    cmd: cmd,
                    args: args || {}
                }));
            });
        },

        listen: function(event, callback) {
            if (!this._listeners[event]) {
                this._listeners[event] = [];
            }
            this._listeners[event].push(callback);
        },

        removeListener: function(event, callback) {
            if (this._listeners[event]) {
                this._listeners[event] = this._listeners[event].filter(cb => cb !== callback);
            }
        }
    };

    window.__tauricpp_internal_onEvent = function(event, data) {
        if (__tauricpp__._listeners[event]) {
            __tauricpp__._listeners[event].forEach(cb => cb(data));
        }
    };

    window.chrome.webview.addEventListener('message', function(e) {
        try {
            var msg = (typeof e.data === 'string') ? JSON.parse(e.data) : e.data;
            if (msg.__tauricpp_result && msg.id) {
                var cb = __tauricpp__._invokeCallbacks[msg.id];
                if (cb) {
                    if (msg.error) {
                        cb.reject(new Error(msg.error));
                    } else {
                        cb.resolve(msg.result);
                    }
                    delete __tauricpp__._invokeCallbacks[msg.id];
                }
            }
        } catch(ex) {}
    });

    window.__tauricpp__ = __tauricpp__;
})();
)js";
}

} // namespace tauricpp
