#include "tauricpp/app.hpp"
#include "tauricpp/embedded_dll.hpp"
#include <Windows.h>
#include <thread>
#include <chrono>
#include <ctime>
#include <string>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // 从exe资源段加载嵌入的WebView2Loader.dll
    // 这样不需要在exe旁边放置DLL文件，实现真正的单文件部署
    tauricpp::EmbeddedDll::Load("WEBVIEW2_LOADER", "EMBEDDED_DLL", "WebView2Loader.dll");

    // 配置应用
    tauricpp::App::Config config;
    config.window_config.title = "TauriCPP Demo";
    config.window_config.width = 900;
    config.window_config.height = 750;
    config.window_config.center = true;
    config.window_config.start_url = "https://tauricpp.app/index.html";

    tauricpp::App app(config);

    // 注册前端可调用的命令
    auto& bridge = app.GetBridge();

    // greet 命令 - 问候
    bridge.RegisterCommand("greet", [](const nlohmann::json& args) -> nlohmann::json {
        std::string name = args.value("name", "World");
        return {{"message", "Hello, " + name + "! Welcome to TauriCPP!"}};
    });

    // add 命令 - 加法
    bridge.RegisterCommand("add", [](const nlohmann::json& args) -> nlohmann::json {
        int a = args.value("a", 0);
        int b = args.value("b", 0);
        return {{"result", a + b}, {"expression", std::to_string(a) + " + " + std::to_string(b) + " = " + std::to_string(a + b)}};
    });

    // echo 命令 - 回显
    bridge.RegisterCommand("echo", [](const nlohmann::json& args) -> nlohmann::json {
        std::string message = args.value("message", "");
        return {{"echo", message}, {"length", message.size()}, {"timestamp", std::time(nullptr)}};
    });

    // get_system_info 命令 - 获取系统信息
    bridge.RegisterCommand("get_system_info", [](const nlohmann::json& args) -> nlohmann::json {
        SYSTEM_INFO si;
        GetSystemInfo(&si);

        MEMORYSTATUSEX ms;
        ms.dwLength = sizeof(ms);
        GlobalMemoryStatusEx(&ms);

        return {
            {"cpu_cores", si.dwNumberOfProcessors},
            {"total_memory_mb", ms.ullTotalPhys / (1024 * 1024)},
            {"available_memory_mb", ms.ullAvailPhys / (1024 * 1024)},
            {"memory_load_percent", ms.dwMemoryLoad},
            {"architecture", si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x64" : "x86"},
            {"framework", "TauriCPP"},
            {"backend", "C++ + WebView2"},
            {"loading_mode", "VirtualFS Memory Stream (No Temp Files)"}
        };
    });

    // 设置回调：窗口创建后启动后台事件推送线程
    app.OnSetup([](tauricpp::App& app) {
        // 启动一个后台线程，定期向前端推送事件
        std::thread([&app]() {
            int counter = 0;
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                counter++;
                app.GetBridge().Emit("timer", {
                    {"count", counter},
                    {"message", "Backend heartbeat #" + std::to_string(counter)}
                });
            }
        }).detach();
    });

    return app.Run();
}
