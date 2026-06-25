# TauriCPP

A lightweight C++ desktop application framework powered by WebView2, inspired by [Tauri](https://tauri.app). Build modern desktop apps with web frontends and C++ backends — single-file deployment, zero source code leakage.

## Features

- **Single EXE Deployment** — WebView2Loader is statically linked, and all frontend assets are embedded into the executable. No external DLLs or files needed.
- **Source Code Protection** — Frontend resources (HTML/CSS/JS) are compiled into the exe as Windows resources and served from memory via VirtualFS. No temporary files are generated.
- **Bidirectional Communication** — JS-to-C++ command invocation (Promise-based) and C++-to-JS event emission.
- **Window Manipulation API** — SetTitle, SetSize, SetPosition, Minimize, Maximize, Restore, SetAlwaysOnTop, SetResizable, SetIcon, and more.
- **Window Lifecycle Callbacks** — OnClose (with veto capability), OnResize, OnMinimize, OnMaximize, OnFocus.
- **File Dialog** — Open/Save file dialogs and folder picker via `tauricpp::Dialog`.
- **Clipboard API** — Read/write system clipboard via `tauricpp::Clipboard`.
- **DevTools Toggle** — Press F12 to open/close DevTools when `config.devtools = true`.
- **SPA Fallback** — Non-asset routes automatically serve `/index.html` for single-page applications.
- **Per-Monitor DPI Awareness (Per-Monitor V2)** — Manifest-declared Per-Monitor V2 DPI awareness, combined with runtime `SetProcessDpiAwarenessContext` fallback and `WM_DPICHANGED` handling. The window and WebView2 are always rendered at physical pixel resolution — no bitmap-stretch blur on 125%/150%/200% scaled displays.
- **Dark Launch** — Window background color matches frontend theme, eliminating white flash on startup.
- **Dev/Prod Dual Mode** — Automatically loads from filesystem in development, from embedded resources in production. No code changes required.
- **Minimal Dependencies** — Only WebView2 SDK and nlohmann/json. No Chromium bundled, no .NET runtime, no Qt.
- **Persistent Frontend Storage** — IndexedDB, LocalStorage, and SessionStorage work reliably across app launches. WebView2 user data is stored in a stable directory (`<exe_dir>/webview2_data/`) instead of per-PID temp folders, ensuring frontend-side persistence survives restarts.
- **C++17** — Modern C++ with clean API design.

## Quick Start

### Prerequisites

- Visual Studio 2019+ (with C++ Desktop Development workload)
- [vcpkg](https://vcpkg.io) (classic mode, installed at `C:\vcpkg`)
- Python 3.7+ (for resource packing script)
- PowerShell 5.1+

### Setup Dependencies (First Time)

```powershell
.\build.ps1 -SetupDeps
```

This will:
1. Download `nlohmann/json.hpp` to `third_party/`
2. Install `webview2` via vcpkg (`C:\vcpkg install webview2:x64-windows`)

### Build

```powershell
.\build.ps1              # Build (Release)
.\build.ps1 -Clean       # Clean build directory and rebuild
```

Output: `build\sample.exe`

### Run

```powershell
.\build\sample.exe
```

## Project Structure

```
TauriCPP/
├── CMakeLists.txt              # CMake build configuration
├── build.ps1                   # PowerShell build script (Ninja + MSVC)
├── vcpkg.json                  # vcpkg dependency manifest
├── include/tauricpp/
│   ├── app.hpp                 # Application entry point
│   ├── bridge.hpp              # Frontend-backend communication bridge
│   ├── window.hpp              # Win32 + WebView2 window
│   ├── virtual_fs.hpp          # In-memory virtual filesystem
│   ├── embedded_dll.hpp        # Embedded DLL loader (general purpose)
│   ├── dialog.hpp              # File dialog API (open/save/pick folder)
│   └── clipboard.hpp           # Clipboard API (read/write text)
├── src/
│   ├── app.cpp
│   ├── bridge.cpp              # Contains injected JS bridge code
│   ├── window.cpp              # WebView2 initialization & resource interception
│   ├── virtual_fs.cpp
│   ├── embedded_dll.cpp        # Embedded DLL loader implementation
│   ├── dialog.cpp              # IFileDialog implementation
│   └── clipboard.cpp           # Win32 clipboard implementation
├── sample/
│   ├── src/main.cpp            # Example application
│   └── frontend/
│       ├── index.html
│       ├── css/style.css
│       └── js/app.js
├── tools/
│   └── pack_resources.py       # Frontend -> .rc resource packing tool
└── third_party/
    └── nlohmann/json.hpp       # JSON library (single header)
```

## Usage

### 1. Create Your App

```cpp
#include "tauricpp/app.hpp"
#include "tauricpp/dialog.hpp"
#include "tauricpp/clipboard.hpp"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // WebView2Loader is statically linked — no DLL loading needed

    // Configure application
    tauricpp::App::Config config;
    config.window_config.title = "My App";
    config.window_config.width = 1200;
    config.window_config.height = 800;
    config.window_config.center = true;
    config.window_config.devtools = true;  // Enable F12 DevTools toggle
    config.window_config.bg_color = RGB(15, 12, 41);  // Dark background (no white flash)

    tauricpp::App app(config);

    // Register commands callable from frontend
    app.GetBridge().RegisterCommand("greet", [](const nlohmann::json& args) {
        std::string name = args.value("name", "World");
        return nlohmann::json{{"message", "Hello, " + name + "!"}};
    });

    // Emit events to frontend
    app.GetBridge().Emit("timer", nlohmann::json{{"tick", 1}});

    return app.Run();
}
```

### 2. Frontend JavaScript API

```javascript
// JS -> C++: Invoke a command (returns Promise)
const result = await __tauricpp__.invoke('greet', { name: 'World' });
// result: { "message": "Hello, World!" }

// C++ -> JS: Listen for events (returns unsubscribe function)
const unlisten = __tauricpp__.listen('timer', function(data) {
    console.log('Timer tick:', data);
});

// Remove listener
__tauricpp__.removeListener('timer', callback);

// Or use returned unsubscribe function
unlisten();
```

### 3. Build Frontend into EXE

Frontend files in `sample/frontend/` are automatically packed into the exe during build via `tools/pack_resources.py`. At runtime, `VirtualFS` loads them from the exe resource section and serves them through `WebResourceRequested` interception — no temporary files are written to disk.

## Architecture

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
│  ┌──────────────┐                              │
│  │ VirtualFS    │                              │
│  │              │                              │
│  │ RegisterFile │                              │
│  │ FindFile     │                              │
│  └──────┬───────┘                              │
│         │                                      │
│  ┌──────▼──────────────────────────────────────┐ │
│  │         Windows Resource Section            │ │
│  │  ┌──────────────────────────────────────┐   │ │
│  │  │ Frontend Assets (HTML/CSS/JS/...)     │   │ │
│  │  └──────────────────────────────────────┘   │ │
│  └─────────────────────────────────────────────┘ │
│                                                  │
│  ┌──────────┐  ┌───────────┐                     │
│  │ Dialog   │  │ Clipboard │                     │
│  │ OpenFile │  │ ReadText  │                     │
│  │ SaveFile │  │ WriteText │                     │
│  └──────────┘  └───────────┘                     │
└─────────────────────────────────────────────────┘
```

### How It Works

1. **Build Time**: `pack_resources.py` scans the frontend directory, generates a `.rc` resource script with numeric IDs, and compiles it into the exe. WebView2Loader is statically linked via `WebView2LoaderStatic.lib`.

2. **Runtime - Resource Serving**: `SetVirtualHostNameToFolderMapping` maps `tauricpp.app` to a directory. `WebResourceRequested` intercepts all requests to `https://tauricpp.app/*` and serves content from `VirtualFS` (in-memory), with proper MIME types, CORS, and cache headers. SPA fallback serves `/index.html` for non-asset routes.

4. **Runtime - Communication**: `AddScriptToExecuteOnDocumentCreated` injects the bridge JS before any page script runs. `postMessage` / `WebMessageReceived` handles JS→C++, while `ExecuteScript` handles C++→JS.

5. **Runtime - Dark Launch**: The window background color (`Config::bg_color`) and WebView2 default background color are both set to match the frontend theme, eliminating the white flash during startup.

## Comparison

| | **TauriCPP** | **Electron** | **Tauri** | **WPF** | **Core UI** |
|---|---|---|---|---|---|
| **Language** | C++ | JS/TS | Rust | C#/.NET | C/JS(QuickJS) |
| **Rendering** | WebView2 (Edge) | Chromium | WebView2/WebKitGTK | DirectX (.NET) | Direct2D/D3D11 |
| **Frontend** | Any web framework | Any web framework | Any web framework | XAML | .uix (Vue SFC-like) |
| **Binary Size** | ~1-10 MB | 100+ MB | 3-10 MB | Requires .NET runtime | ~3 MB DLL |
| **Memory Usage** | 50-80 MB | 150+ MB | 30-80 MB | 80+ MB | < 30 MB |
| **Startup** | < 500ms | 1-3s | < 500ms | 0.5-1s | < 200ms |
| **Source Protection** | Yes (embedded in exe, served from memory) | No (asar is archive, not encryption) | Limited | No (IL can be decompiled) | Yes (compiled binary) |
| **Single File Deploy** | Yes | No | Yes (bundled) | No | Yes (single DLL) |
| **Cross-Platform** | Windows only | Windows/macOS/Linux | Windows/macOS/Linux | Windows only | Windows only |
| **Runtime Dependency** | WebView2 (pre-installed on Win10/11) | Bundled Chromium | WebView2/WebKitGTK | .NET Runtime | None |
| **Backend Ecosystem** | C++ native libraries | npm ecosystem | Rust crates ecosystem | NuGet ecosystem | C ABI, any language |

### TauriCPP vs Electron

Electron bundles the entire Chromium engine, resulting in 100+ MB binaries and 150+ MB memory usage. TauriCPP uses the system's WebView2 (pre-installed on Windows 10/11), keeping the binary under 10 MB. Frontend source code is embedded in the exe and served from memory — no asar archives that can be extracted.

### TauriCPP vs Tauri

Tauri is the most similar project, but it requires Rust. TauriCPP uses C++, giving direct access to the Windows API and existing C++ codebases without FFI overhead. Both use WebView2 on Windows and achieve similar binary sizes. Tauri offers cross-platform support (macOS/Linux via WebKitGTK), while TauriCPP is Windows-only but with a simpler build toolchain.

### TauriCPP vs WPF

WPF requires the .NET runtime and uses XAML for UI definition. TauriCPP uses standard web technologies (HTML/CSS/JS), leveraging the massive web ecosystem and allowing teams to reuse frontend skills. WPF applications can be decompiled from IL, while TauriCPP embeds frontend resources in the native binary.

### TauriCPP vs Core UI

Core UI is a native rendering framework using Direct2D with a custom `.uix` component format and QuickJS engine — extremely lightweight (3 MB, < 30 MB memory). However, it uses a custom UI framework with limited CSS support and a small widget set. TauriCPP uses the full web platform (any CSS, any JS framework, full DOM), giving access to the entire npm ecosystem. The tradeoff is higher memory usage (WebView2 overhead) but vastly greater UI flexibility and developer familiarity.

## API Reference

### App

```cpp
tauricpp::App::Config config;
config.window_config.title = "My App";
config.window_config.width = 1024;
config.window_config.height = 768;
config.window_config.center = true;
config.window_config.resizable = true;
config.window_config.always_on_top = false;
config.window_config.devtools = true;
config.window_config.bg_color = RGB(15, 12, 41);  // Dark background, no white flash
config.window_config.start_url = "https://tauricpp.app/index.html";
config.dev_mode = false;

tauricpp::App app(config);
app.OnSetup([](tauricpp::App& app) { /* called before message loop */ });
int exitCode = app.Run();
```

### Bridge

```cpp
auto& bridge = app.GetBridge();

// Register command (JS callable via __tauricpp__.invoke('cmd', args))
bridge.RegisterCommand("cmd_name", [](const nlohmann::json& args) -> nlohmann::json {
    return {{"result", "ok"}};
});

// Type-safe command registration (Args/Result must be JSON-serializable)
bridge.RegisterCommand<MyArgs, MyResult>("typed_cmd", [](const MyArgs& args) -> MyResult {
    return MyResult{...};
});

// Unregister a command
bridge.UnregisterCommand("cmd_name");

// Emit event to frontend (JS listens via __tauricpp__.listen('event', cb))
bridge.Emit("event_name", nlohmann::json{{"key", "value"}});
```

### Window

```cpp
auto& window = app.GetWindow();

// Window manipulation
window.SetTitle("New Title");
window.SetSize(800, 600);
window.SetPosition(100, 100);
window.SetAlwaysOnTop(true);
window.SetResizable(false);
window.SetIcon(LoadIcon(nullptr, IDI_APPLICATION));
window.Minimize();
window.Maximize();
window.Restore();

// Query state
bool min = window.IsMinimized();
bool max = window.IsMaximized();
bool focus = window.IsFocused();

// Lifecycle callbacks
window.OnClose([]() -> bool { return true; });  // return false to veto close
window.OnResize([](int w, int h) { /* handle resize */ });
window.OnMinimize([]() { /* handle minimize */ });
window.OnMaximize([]() { /* handle maximize */ });
window.OnFocus([]() { /* handle focus */ });

// DevTools
window.ToggleDevTools();

// Execute JS
window.ExecuteJs("console.log('Hello from C++')");
HWND hwnd = window.GetHwnd();
```

### Dialog

```cpp
// Open file dialog
tauricpp::Dialog::OpenOptions opts;
opts.title = "Open File";
opts.filters = {{"Text Files", "*.txt"}, {"All Files", "*.*"}};
opts.multi_select = true;
auto files = tauricpp::Dialog::OpenFile(hwnd, opts);

// Save file dialog
tauricpp::Dialog::SaveOptions saveOpts;
saveOpts.title = "Save File";
saveOpts.default_filename = "untitled.txt";
auto path = tauricpp::Dialog::SaveFile(hwnd, saveOpts);

// Folder picker
auto folder = tauricpp::Dialog::PickFolder(hwnd, "Select Folder");

// Message boxes
tauricpp::Dialog::ShowInfo(hwnd, "Info", "Operation completed");
bool confirmed = tauricpp::Dialog::AskConfirm(hwnd, "Confirm", "Are you sure?");
```

### Clipboard

```cpp
// Read clipboard
auto text = tauricpp::Clipboard::ReadText();
if (text) { std::string value = *text; }

// Write clipboard
tauricpp::Clipboard::WriteText("Hello, Clipboard!");

// Clear clipboard
tauricpp::Clipboard::Clear();
```

### VirtualFS

```cpp
auto& vfs = app.GetFS();

// Register a file in the virtual filesystem
vfs.RegisterFile("/path/to/file.html", "<h1>Hello</h1>", "text/html");

// Register with move semantics (avoids copy for large files)
std::vector<uint8_t> data = LoadLargeFile();
vfs.RegisterFile("/path/to/large.bin", std::move(data), "application/octet-stream");

// Find a file (pointer version, no copy)
const tauricpp::VirtualFS::VFile* file = vfs.FindFile("/path/to/file.html");
if (file) { /* file->data, file->mime_type */ }

// Find a file (copy version)
tauricpp::VirtualFS::VFile fileCopy;
if (vfs.FindFile("/path/to/file.html", fileCopy)) { /* ... */ }
```

## Build Options

```powershell
.\build.ps1                    # Build (Release, Ninja + MSVC)
.\build.ps1 -Clean             # Clean and rebuild
.\build.ps1 -SetupDeps         # Install dependencies (first time only)
```

## Requirements

- Windows 10/11 (WebView2 runtime, pre-installed on most systems)
- Visual Studio 2019+ with C++ Desktop Development workload
- CMake 3.20+
- Ninja (comes with VS C++ workload)
- vcpkg (classic mode)
- Python 3.7+

## Changelog

### v0.2.2

- **New**: Persistent frontend storage — IndexedDB, LocalStorage, and SessionStorage now survive app restarts
- **Fix**: WebView2 user data folder changed from `%TEMP%\tauricpp_webview2_<PID>` to `<exe_dir>/webview2_data/`. Previously the per-process-ID temp folder was recreated on every launch, causing all browser-side storage (IndexedDB, cache, etc.) to be lost

### v0.2.1

- **Fix**: Rendering on high-DPI (125% / 150% / 200%) displays was blurry compared to the Edge browser. The application now declares Per-Monitor V2 DPI awareness via the embedded manifest (fallback `SetProcessDpiAwarenessContext` at runtime), scales window geometry using physical pixels via `MulDiv` + DPI-aware `AdjustWindowRectExForDpi`, and handles `WM_DPICHANGED` to relayout `WebView2::put_Bounds` when the window is dragged across monitors with different scaling.

### v0.2.0

- **New**: WebView2Loader statically linked — no more DLL embedding, delay-load hooks, or runtime DLL extraction
- **New**: Window manipulation API (SetTitle, SetSize, SetPosition, Minimize, Maximize, Restore, SetAlwaysOnTop, SetResizable, SetIcon)
- **New**: Window lifecycle callbacks (OnClose with veto, OnResize, OnMinimize, OnMaximize, OnFocus)
- **New**: File Dialog API (`tauricpp::Dialog` — OpenFile, SaveFile, PickFolder, ShowInfo, AskConfirm)
- **New**: Clipboard API (`tauricpp::Clipboard` — ReadText, WriteText, Clear)
- **New**: DevTools toggle (F12 shortcut, `ToggleDevTools()`, `Config::devtools`)
- **New**: SPA fallback (non-asset routes automatically serve `/index.html`)
- **New**: Type-safe command registration (`RegisterCommand<ArgsT, ResultT>`)
- **New**: `UnregisterCommand` for dynamic command management
- **New**: `listen()` returns unsubscribe function in frontend API
- **New**: Dark launch — window background color matches frontend theme, eliminating white flash
- **New**: Config options `always_on_top`, `devtools`, `bg_color`, `dev_mode`
- **Fix**: Bridge deadlock — handler now called outside mutex lock
- **Fix**: Bridge race condition — `execute_js_` copied to local before use
- **Fix**: XSS vulnerability in `Emit` — event names now JSON-encoded instead of string-concatenated
- **Fix**: Use-after-free in detached threads — `atomic<bool>` shutdown signal + `OnClose` callback
- **Fix**: Non-ASCII path handling — all `wstring(string.begin(), string.end())` replaced with proper `MultiByteToWideChar`
- **Fix**: `FindFile` unnecessary copy — added pointer version and move-semantics `RegisterFile`
- **Fix**: Bridge JS error swallowing — `catch(ex) {}` replaced with `console.error`
- **Fix**: CMake resource tracking — frontend file changes now trigger repack
- **Fix**: Delete copy/move constructors on non-copyable classes

## License

MIT