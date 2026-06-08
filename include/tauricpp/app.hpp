#pragma once
#include "window.hpp"
#include "bridge.hpp"
#include "virtual_fs.hpp"
#include <memory>
#include <functional>

namespace tauricpp {

/// 应用入口类 - 管理应用生命周期
class App {
public:
    using SetupCallback = std::function<void(App& app)>;

    struct Config {
        Window::Config window_config;
        bool dev_mode = false;  ///< 开发模式标志，可用于条件逻辑
    };

    explicit App(const Config& config = {});
    ~App();

    // 禁止拷贝和移动
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    /// 设置初始化回调（在窗口创建前调用，用于注册命令等）
    void OnSetup(SetupCallback cb);

    /// 运行应用（阻塞）
    int Run();

    /// 获取Bridge引用
    Bridge& GetBridge() { return bridge_; }

    /// 获取VirtualFS引用
    VirtualFS& GetFS() { return vfs_; }

    /// 获取Window引用
    Window& GetWindow() { return *window_; }

private:
    Config config_;
    Bridge& bridge_;
    VirtualFS& vfs_;
    std::unique_ptr<Window> window_;
    SetupCallback setup_cb_;
};

} // namespace tauricpp
