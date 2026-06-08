#include "tauricpp/app.hpp"
#include "tauricpp/embedded_dll.hpp"
#include "tauricpp/dialog.hpp"
#include "tauricpp/clipboard.hpp"
#include <Windows.h>
#include <thread>
#include <chrono>
#include <ctime>
#include <string>
#include <atomic>

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
    config.window_config.devtools = true;  // 启用DevTools，F12切换

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

    // dialog.open 命令 - 打开文件选择对话框
    bridge.RegisterCommand("dialog.open", [](const nlohmann::json& args) -> nlohmann::json {
        tauricpp::Dialog::OpenOptions opts;
        opts.title = args.value("title", "Open File");
        if (args.contains("filters") && args["filters"].is_array()) {
            opts.filters.clear();
            for (const auto& f : args["filters"]) {
                opts.filters.push_back({f.value("name", "Files"), f.value("pattern", "*.*")});
            }
        }
        auto files = tauricpp::Dialog::OpenFile(nullptr, opts);
        nlohmann::json result;
        result["files"] = files;
        result["cancelled"] = files.empty();
        return result;
    });

    // dialog.save 命令 - 保存文件对话框
    bridge.RegisterCommand("dialog.save", [](const nlohmann::json& args) -> nlohmann::json {
        tauricpp::Dialog::SaveOptions opts;
        opts.title = args.value("title", "Save File");
        opts.default_filename = args.value("default_filename", "");
        auto path = tauricpp::Dialog::SaveFile(nullptr, opts);
        nlohmann::json result;
        result["path"] = path.value_or("");
        result["cancelled"] = !path.has_value();
        return result;
    });

    // clipboard.read 命令
    bridge.RegisterCommand("clipboard.read", [](const nlohmann::json&) -> nlohmann::json {
        auto text = tauricpp::Clipboard::ReadText();
        return {{"text", text.value_or("")}, {"has_text", text.has_value()}};
    });

    // clipboard.write 命令
    bridge.RegisterCommand("clipboard.write", [](const nlohmann::json& args) -> nlohmann::json {
        std::string text = args.value("text", "");
        bool ok = tauricpp::Clipboard::WriteText(text);
        return {{"success", ok}};
    });

    // window manipulation commands
    bridge.RegisterCommand("window.minimize", [&app](const nlohmann::json&) -> nlohmann::json {
        app.GetWindow().Minimize();
        return {{"success", true}};
    });

    bridge.RegisterCommand("window.maximize", [&app](const nlohmann::json&) -> nlohmann::json {
        app.GetWindow().Maximize();
        return {{"success", true}};
    });

    bridge.RegisterCommand("window.restore", [&app](const nlohmann::json&) -> nlohmann::json {
        app.GetWindow().Restore();
        return {{"success", true}};
    });

    bridge.RegisterCommand("window.set_title", [&app](const nlohmann::json& args) -> nlohmann::json {
        app.GetWindow().SetTitle(args.value("title", ""));
        return {{"success", true}};
    });

    // 设置回调：窗口创建后启动后台事件推送线程
    // ★ 修复：使用原子bool控制线程退出，避免use-after-free
    static std::atomic<bool> g_running{true};
    app.OnSetup([](tauricpp::App& app) {
        // 设置窗口关闭回调
        app.GetWindow().OnClose([]() -> bool {
            g_running = false;  // 通知后台线程退出
            return true;        // 允许关闭
        });

        // 启动一个后台线程，定期向前端推送事件
        std::thread([&app]() {
            int counter = 0;
            while (g_running) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                if (!g_running) break;  // 再次检查，避免退出后发送事件
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
