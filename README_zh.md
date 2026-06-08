# TauriCPP

基于 WebView2 的轻量级 C++ 桌面应用框架，灵感来自 [Tauri](https://tauri.app)。使用 Web 前端 + C++ 后端构建现代桌面应用——单文件部署，源码不泄露。

## 特性

- **单 EXE 部署** — WebView2Loader.dll 和所有前端资源均嵌入可执行文件，无需任何外部文件。
- **源码保护** — 前端资源（HTML/CSS/JS）编译时作为 Windows 资源嵌入 exe，运行时通过 VirtualFS 从内存提供，不生成临时文件。
- **双向通信** — JS 调用 C++ 命令（Promise 风格），C++ 向 JS 推送事件。
- **窗口操作 API** — SetTitle、SetSize、SetPosition、Minimize、Maximize、Restore、SetAlwaysOnTop、SetResizable、SetIcon 等。
- **窗口生命周期回调** — OnClose（支持否决关闭）、OnResize、OnMinimize、OnMaximize、OnFocus。
- **文件对话框** — 通过 `tauricpp::Dialog` 实现打开/保存文件对话框和文件夹选择。
- **剪贴板 API** — 通过 `tauricpp::Clipboard` 读写系统剪贴板。
- **DevTools 切换** — 设置 `config.devtools = true` 后按 F12 打开/关闭开发者工具。
- **SPA 路由回退** — 非静态资源路径自动返回 `/index.html`，支持前端路由。
- **深色启动** — 窗口背景色匹配前端主题，启动时无白屏闪烁。
- **开发/生产双模式** — 开发时自动从文件系统加载，生产时从嵌入资源加载，无需修改代码。
- **极简依赖** — 仅需 WebView2 SDK 和 nlohmann/json，不捆绑 Chromium，不依赖 .NET 运行时，不依赖 Qt。
- **C++17** — 现代 C++，API 简洁清晰。

## 快速开始

### 环境要求

- Visual Studio 2019+（需安装 C++ 桌面开发工作负载）
- [vcpkg](https://vcpkg.io)（classic 模式，安装在 `C:\vcpkg`）
- Python 3.7+（用于资源打包脚本）
- PowerShell 5.1+

### 安装依赖（首次使用）

```powershell
.\build.ps1 -SetupDeps
```

此命令将：
1. 下载 `nlohmann/json.hpp` 到 `third_party/` 目录
2. 通过 vcpkg 安装 `webview2`（`vcpkg install webview2:x64-windows`）

### 构建

```powershell
.\build.ps1              # 构建（Release 配置）
.\build.ps1 -Clean       # 清理构建目录并重新构建
```

输出：`build\sample.exe`

### 运行

```powershell
.\build\sample.exe
```

## 项目结构

```
TauriCPP/
├── CMakeLists.txt              # CMake 构建配置
├── build.ps1                   # PowerShell 构建脚本（Ninja + MSVC）
├── vcpkg.json                  # vcpkg 依赖清单
├── include/tauricpp/
│   ├── app.hpp                 # 应用入口
│   ├── bridge.hpp              # 前后端通信桥
│   ├── window.hpp              # Win32 + WebView2 窗口
│   ├── virtual_fs.hpp          # 内存虚拟文件系统
│   ├── embedded_dll.hpp        # 嵌入式 DLL 加载器（delay-load 钩子）
│   ├── dialog.hpp              # 文件对话框 API（打开/保存/选择文件夹）
│   └── clipboard.hpp           # 剪贴板 API（读写文本）
├── src/
│   ├── app.cpp
│   ├── bridge.cpp              # 含注入前端的 JS 桥接代码
│   ├── window.cpp              # WebView2 初始化与资源拦截
│   ├── virtual_fs.cpp
│   ├── embedded_dll.cpp        # delay-load 钩子（__pfnDliNotifyHook2）
│   ├── dialog.cpp              # IFileDialog 实现
│   └── clipboard.cpp           # Win32 剪贴板实现
├── sample/
│   ├── src/main.cpp            # 示例应用
│   └── frontend/
│       ├── index.html
│       ├── css/style.css
│       └── js/app.js
├── tools/
│   └── pack_resources.py       # 前端文件 -> .rc 资源打包工具
└── third_party/
    └── nlohmann/json.hpp       # JSON 库（单头文件）
```

## 使用方法

### 1. 创建应用

```cpp
#include "tauricpp/app.hpp"
#include "tauricpp/embedded_dll.hpp"
#include "tauricpp/dialog.hpp"
#include "tauricpp/clipboard.hpp"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // 从 exe 资源段加载嵌入的 WebView2Loader.dll
    tauricpp::EmbeddedDll::Load("1", "EMBEDDED_DLL", "WebView2Loader.dll");

    // 配置应用
    tauricpp::App::Config config;
    config.window_config.title = "我的应用";
    config.window_config.width = 1200;
    config.window_config.height = 800;
    config.window_config.center = true;
    config.window_config.devtools = true;  // 启用 F12 DevTools 切换
    config.window_config.bg_color = RGB(15, 12, 41);  // 深色背景，消除白屏

    tauricpp::App app(config);

    // 注册前端可调用的命令
    app.GetBridge().RegisterCommand("greet", [](const nlohmann::json& args) {
        std::string name = args.value("name", "World");
        return nlohmann::json{{"message", "你好, " + name + "!"}};
    });

    // 向前端推送事件
    app.GetBridge().Emit("timer", nlohmann::json{{"tick", 1}});

    return app.Run();
}
```

### 2. 前端 JavaScript API

```javascript
// JS -> C++：调用命令（返回 Promise）
const result = await __tauricpp__.invoke('greet', { name: 'World' });
// result: { "message": "你好, World!" }

// C++ -> JS：监听事件（返回取消监听函数）
const unlisten = __tauricpp__.listen('timer', function(data) {
    console.log('定时器:', data);
});

// 移除监听
__tauricpp__.removeListener('timer', callback);

// 或使用返回的取消函数
unlisten();
```

### 3. 将前端打包进 EXE

`sample/frontend/` 目录中的前端文件在构建时由 `tools/pack_resources.py` 自动打包为 Windows 资源脚本并编译进 exe。运行时，`VirtualFS` 从 exe 资源段加载文件，通过 `WebResourceRequested` 拦截请求从内存提供内容——不写入任何临时文件到磁盘。

## 架构

```
┌─────────────────────────────────────────────────┐
│                   sample.exe                     │
│                                                  │
│  ┌──────────┐  ┌───────────┐  ┌──────────────┐ │
│  │ App      │  │ Bridge    │  │ Window       │ │
│  │          │──│           │──│              │ │
│  │ Config   │  │ Register  │  │ Win32 +      │ │
│  │ Run()    │  │ Emit()    │  │ WebView2     │ │
│  └──────────┘  └───────────┘  └──────┬───────┘ │
│                                       │         │
│  ┌──────────────┐  ┌─────────────────┐│         │
│  │ VirtualFS    │  │ EmbeddedDll     ││         │
│  │              │  │                 ││         │
│  │ RegisterFile │  │ 从资源段加载     ││         │
│  │ FindFile     │  │ delay-load 钩子 ││         │
│  └──────┬───────┘  └─────────────────┘│         │
│         │                              │         │
│  ┌──────▼──────────────────────────────▼───────┐ │
│  │         Windows 资源段                       │ │
│  │  ┌─────────────┐  ┌──────────────────────┐  │ │
│  │  │ WebView2    │  │ 前端资源              │  │ │
│  │  │ Loader.dll  │  │ (HTML/CSS/JS/...)     │  │ │
│  │  └─────────────┘  └──────────────────────┘  │ │
│  └─────────────────────────────────────────────┘ │
│                                                  │
│  ┌──────────┐  ┌───────────┐                     │
│  │ Dialog   │  │ Clipboard │                     │
│  │ OpenFile │  │ ReadText  │                     │
│  │ SaveFile │  │ WriteText │                     │
│  └──────────┘  └───────────┘                     │
└─────────────────────────────────────────────────┘
```

### 运行机制

1. **构建时**：`pack_resources.py` 扫描前端目录，生成带数字 ID 的 `.rc` 资源脚本，与 `WebView2Loader.dll` 一起编译进 exe。

2. **运行时 - DLL 加载**：`EmbeddedDll::Load()` 从 exe 资源段提取 `WebView2Loader.dll` 写入临时文件，调用 `LoadLibraryW()` 加载后立即删除临时文件。`__pfnDliNotifyHook2` delay-load 钩子拦截 MSVC 的延迟加载机制，直接返回已加载的模块句柄。

3. **运行时 - 资源提供**：`SetVirtualHostNameToFolderMapping` 将 `tauricpp.app` 映射为虚拟主机名。`WebResourceRequested` 拦截所有 `https://tauricpp.app/*` 请求，从 `VirtualFS`（内存中）提供内容，自动设置 MIME 类型、CORS 和缓存头。SPA 回退为非资源路径自动返回 `/index.html`。

4. **运行时 - 通信**：`AddScriptToExecuteOnDocumentCreated` 在页面脚本执行前注入桥接 JS。`postMessage` / `WebMessageReceived` 处理 JS→C++ 通信，`ExecuteScript` 处理 C++→JS 通信。

5. **运行时 - 深色启动**：窗口背景色（`Config::bg_color`）和 WebView2 默认背景色均设置为匹配前端主题，消除启动时的白屏闪烁。

## 对比

| | **TauriCPP** | **Electron** | **Tauri** | **WPF** | **Core UI** |
|---|---|---|---|---|---|
| **后端语言** | C++ | JS/TS | Rust | C#/.NET | C/JS(QuickJS) |
| **渲染引擎** | WebView2 (Edge) | Chromium | WebView2/WebKitGTK | DirectX (.NET) | Direct2D/D3D11 |
| **前端技术** | 任意 Web 框架 | 任意 Web 框架 | 任意 Web 框架 | XAML | .uix（类 Vue SFC） |
| **二进制体积** | ~1-10 MB | 100+ MB | 3-10 MB | 需 .NET 运行时 | ~3 MB DLL |
| **内存占用** | 50-80 MB | 150+ MB | 30-80 MB | 80+ MB | < 30 MB |
| **启动速度** | < 500ms | 1-3s | < 500ms | 0.5-1s | < 200ms |
| **源码保护** | 是（嵌入 exe，内存提供） | 否（asar 可解包） | 有限 | 否（IL 可反编译） | 是（编译为二进制） |
| **单文件部署** | 是 | 否 | 是（打包后） | 否 | 是（单个 DLL） |
| **跨平台** | 仅 Windows | Win/macOS/Linux | Win/macOS/Linux | 仅 Windows | 仅 Windows |
| **运行时依赖** | WebView2（Win10/11 预装） | 捆绑 Chromium | WebView2/WebKitGTK | .NET 运行时 | 无 |
| **后端生态** | C++ 原生库 | npm 生态 | Rust crates 生态 | NuGet 生态 | C ABI，任意语言 |

### TauriCPP vs Electron

Electron 捆绑整个 Chromium 引擎，二进制体积 100+ MB，内存占用 150+ MB。TauriCPP 使用系统自带的 WebView2（Windows 10/11 已预装），二进制体积控制在 10 MB 以内。前端源码嵌入 exe 并从内存提供——不像 asar 归档可以被解包提取。

### TauriCPP vs Tauri

Tauri 是最相似的框架，但需要 Rust 语言。TauriCPP 使用 C++，可直接调用 Windows API 和现有 C++ 代码库，无需 FFI 开销。两者在 Windows 上都使用 WebView2，二进制体积相近。Tauri 支持跨平台（macOS/Linux 通过 WebKitGTK），TauriCPP 仅支持 Windows，但构建工具链更简单。

### TauriCPP vs WPF

WPF 需要 .NET 运行时，使用 XAML 定义 UI。TauriCPP 使用标准 Web 技术（HTML/CSS/JS），可利用庞大的 Web 生态，前端团队技能可直接复用。WPF 应用的 IL 可被反编译，TauriCPP 将前端资源嵌入原生二进制，更难提取。

### TauriCPP vs Core UI

Core UI 是一个原生渲染框架，使用 Direct2D 渲染和自定义 `.uix` 组件格式 + QuickJS 引擎——极其轻量（3 MB，< 30 MB 内存）。但它使用自定义 UI 框架，CSS 支持有限，控件集较小。TauriCPP 使用完整的 Web 平台（任意 CSS、任意 JS 框架、完整 DOM），可访问整个 npm 生态。代价是内存占用较高（WebView2 开销），但 UI 灵活性和开发者熟悉度大幅提升。

## API 参考

### App

```cpp
tauricpp::App::Config config;
config.window_config.title = "我的应用";
config.window_config.width = 1024;
config.window_config.height = 768;
config.window_config.center = true;
config.window_config.resizable = true;
config.window_config.always_on_top = false;
config.window_config.devtools = true;
config.window_config.bg_color = RGB(15, 12, 41);  // 深色背景，消除白屏
config.window_config.start_url = "https://tauricpp.app/index.html";
config.dev_mode = false;

tauricpp::App app(config);
app.OnSetup([](tauricpp::App& app) { /* 消息循环前调用 */ });
int exitCode = app.Run();
```

### Bridge

```cpp
auto& bridge = app.GetBridge();

// 注册命令（前端通过 __tauricpp__.invoke('cmd', args) 调用）
bridge.RegisterCommand("cmd_name", [](const nlohmann::json& args) -> nlohmann::json {
    return {{"result", "ok"}};
});

// 类型安全注册（Args/Result 需可被 JSON 序列化）
bridge.RegisterCommand<MyArgs, MyResult>("typed_cmd", [](const MyArgs& args) -> MyResult {
    return MyResult{...};
});

// 注销命令
bridge.UnregisterCommand("cmd_name");

// 向前端推送事件（前端通过 __tauricpp__.listen('event', cb) 监听）
bridge.Emit("event_name", nlohmann::json{{"key", "value"}});
```

### Window

```cpp
auto& window = app.GetWindow();

// 窗口操作
window.SetTitle("新标题");
window.SetSize(800, 600);
window.SetPosition(100, 100);
window.SetAlwaysOnTop(true);
window.SetResizable(false);
window.SetIcon(LoadIcon(nullptr, IDI_APPLICATION));
window.Minimize();
window.Maximize();
window.Restore();

// 查询状态
bool min = window.IsMinimized();
bool max = window.IsMaximized();
bool focus = window.IsFocused();

// 生命周期回调
window.OnClose([]() -> bool { return true; });  // 返回 false 可否决关闭
window.OnResize([](int w, int h) { /* 处理窗口大小变化 */ });
window.OnMinimize([]() { /* 处理最小化 */ });
window.OnMaximize([]() { /* 处理最大化 */ });
window.OnFocus([]() { /* 处理获得焦点 */ });

// DevTools
window.ToggleDevTools();

// 执行 JS
window.ExecuteJs("console.log('Hello from C++')");
HWND hwnd = window.GetHwnd();
```

### Dialog

```cpp
// 打开文件对话框
tauricpp::Dialog::OpenOptions opts;
opts.title = "打开文件";
opts.filters = {{"文本文件", "*.txt"}, {"所有文件", "*.*"}};
opts.multi_select = true;
auto files = tauricpp::Dialog::OpenFile(hwnd, opts);

// 保存文件对话框
tauricpp::Dialog::SaveOptions saveOpts;
saveOpts.title = "保存文件";
saveOpts.default_filename = "未命名.txt";
auto path = tauricpp::Dialog::SaveFile(hwnd, saveOpts);

// 选择文件夹
auto folder = tauricpp::Dialog::PickFolder(hwnd, "选择文件夹");

// 消息框
tauricpp::Dialog::ShowInfo(hwnd, "提示", "操作完成");
bool confirmed = tauricpp::Dialog::AskConfirm(hwnd, "确认", "确定要继续吗？");
```

### Clipboard

```cpp
// 读取剪贴板
auto text = tauricpp::Clipboard::ReadText();
if (text) { std::string value = *text; }

// 写入剪贴板
tauricpp::Clipboard::WriteText("你好，剪贴板！");

// 清空剪贴板
tauricpp::Clipboard::Clear();
```

### VirtualFS

```cpp
auto& vfs = app.GetFS();

// 注册文件到虚拟文件系统
vfs.RegisterFile("/path/to/file.html", "<h1>Hello</h1>", "text/html");

// 使用移动语义注册（避免大文件拷贝）
std::vector<uint8_t> data = LoadLargeFile();
vfs.RegisterFile("/path/to/large.bin", std::move(data), "application/octet-stream");

// 查找文件（指针版本，不拷贝）
const tauricpp::VirtualFS::VFile* file = vfs.FindFile("/path/to/file.html");
if (file) { /* file->data, file->mime_type */ }

// 查找文件（拷贝版本）
tauricpp::VirtualFS::VFile fileCopy;
if (vfs.FindFile("/path/to/file.html", fileCopy)) { /* ... */ }
```

## 构建选项

```powershell
.\build.ps1                    # 构建（Release，Ninja + MSVC）
.\build.ps1 -Clean             # 清理并重新构建
.\build.ps1 -SetupDeps         # 安装依赖（仅首次使用）
```

## 系统要求

- Windows 10/11（WebView2 运行时，大多数系统已预装）
- Visual Studio 2019+（需安装 C++ 桌面开发工作负载）
- CMake 3.20+
- Ninja（随 VS C++ 工作负载安装）
- vcpkg（classic 模式）
- Python 3.7+

## 更新日志

### v0.2.0

- **新增**：窗口操作 API（SetTitle、SetSize、SetPosition、Minimize、Maximize、Restore、SetAlwaysOnTop、SetResizable、SetIcon）
- **新增**：窗口生命周期回调（OnClose 支持否决、OnResize、OnMinimize、OnMaximize、OnFocus）
- **新增**：文件对话框 API（`tauricpp::Dialog` — OpenFile、SaveFile、PickFolder、ShowInfo、AskConfirm）
- **新增**：剪贴板 API（`tauricpp::Clipboard` — ReadText、WriteText、Clear）
- **新增**：DevTools 切换（F12 快捷键、`ToggleDevTools()`、`Config::devtools`）
- **新增**：SPA 路由回退（非静态资源路径自动返回 `/index.html`）
- **新增**：类型安全命令注册（`RegisterCommand<ArgsT, ResultT>`）
- **新增**：`UnregisterCommand` 动态注销命令
- **新增**：`listen()` 返回取消监听函数
- **新增**：深色启动——窗口背景色匹配前端主题，消除白屏闪烁
- **新增**：配置选项 `always_on_top`、`devtools`、`bg_color`、`dev_mode`
- **修复**：Bridge 死锁——handler 在锁外调用
- **修复**：Bridge 竞态条件——`execute_js_` 拷贝到局部变量再使用
- **修复**：Emit XSS 漏洞——事件名使用 JSON 编码而非字符串拼接
- **修复**：detached 线程 use-after-free——使用 `atomic<bool>` 关闭信号 + `OnClose` 回调
- **修复**：非 ASCII 路径处理——所有 `wstring(string.begin(), string.end())` 替换为 `MultiByteToWideChar`
- **修复**：`FindFile` 不必要拷贝——新增指针版本和移动语义 `RegisterFile`
- **修复**：Bridge JS 错误吞没——`catch(ex) {}` 替换为 `console.error`
- **修复**：CMake 资源跟踪——前端文件变更时触发重新打包
- **修复**：不可拷贝类删除拷贝/移动构造函数

## 许可证

MIT