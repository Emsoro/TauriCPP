# TauriCPP

A lightweight C++ desktop application framework powered by WebView2, inspired by [Tauri](https://tauri.app). Build modern desktop apps with web frontends and C++ backends — single-file deployment, zero source code leakage.

## Features

- **Single EXE Deployment** — WebView2Loader.dll and all frontend assets are embedded into the executable. No external files needed.
- **Source Code Protection** — Frontend resources (HTML/CSS/JS) are compiled into the exe as Windows resources and served from memory via VirtualFS. No temporary files are generated.
- **Bidirectional Communication** — JS-to-C++ command invocation (Promise-based) and C++-to-JS event emission.
- **Dev/Prod Dual Mode** — Automatically loads from filesystem in development, from embedded resources in production. No code changes required.
- **Minimal Dependencies** — Only WebView2 SDK and nlohmann/json. No Chromium bundled, no .NET runtime, no Qt.
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
│   └── embedded_dll.hpp        # Embedded DLL loader (delay-load hook)
├── src/
│   ├── app.cpp
│   ├── bridge.cpp              # Contains injected JS bridge code
│   ├── window.cpp              # WebView2 initialization & resource interception
│   ├── virtual_fs.cpp
│   └── embedded_dll.cpp        # delay-load hook (__pfnDliNotifyHook2)
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
#include "tauricpp/embedded_dll.hpp"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // Load embedded WebView2Loader.dll from exe resources
    tauricpp::EmbeddedDll::Load("1", "EMBEDDED_DLL", "WebView2Loader.dll");

    // Configure application
    tauricpp::App::Config config;
    config.window_config.title = "My App";
    config.window_config.width = 1200;
    config.window_config.height = 800;
    config.window_config.center = true;

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

// C++ -> JS: Listen for events
__tauricpp__.listen('timer', function(data) {
    console.log('Timer tick:', data);
});

// Remove listener
__tauricpp__.removeListener('timer', callback);
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
│  ┌──────────────┐  ┌─────────────────┐│         │
│  │ VirtualFS    │  │ EmbeddedDll     ││         │
│  │              │  │                 ││         │
│  │ RegisterFile │  │ Load from res   ││         │
│  │ FindFile     │  │ delay-load hook ││         │
│  └──────┬───────┘  └─────────────────┘│         │
│         │                              │         │
│  ┌──────▼──────────────────────────────▼───────┐ │
│  │         Windows Resource Section            │ │
│  │  ┌─────────────┐  ┌──────────────────────┐  │ │
│  │  │ WebView2    │  │ Frontend Assets       │  │ │
│  │  │ Loader.dll  │  │ (HTML/CSS/JS/...)     │  │ │
│  │  └─────────────┘  └──────────────────────┘  │ │
│  └─────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────┘
```

### How It Works

1. **Build Time**: `pack_resources.py` scans the frontend directory, generates a `.rc` resource script with numeric IDs, and compiles it into the exe alongside `WebView2Loader.dll`.

2. **Runtime - DLL Loading**: `EmbeddedDll::Load()` extracts `WebView2Loader.dll` from exe resources to a temp file, calls `LoadLibraryW()`, then immediately deletes the temp file. The `__pfnDliNotifyHook2` delay-load hook intercepts the MSVC delay-load mechanism, returning the already-loaded module handle.

3. **Runtime - Resource Serving**: `SetVirtualHostNameToFolderMapping` maps `tauricpp.app` to a directory. `WebResourceRequested` intercepts all requests to `https://tauricpp.app/*` and serves content from `VirtualFS` (in-memory), with proper MIME types, CORS, and cache headers.

4. **Runtime - Communication**: `AddScriptToExecuteOnDocumentCreated` injects the bridge JS before any page script runs. `postMessage` / `WebMessageReceived` handles JS→C++, while `ExecuteScript` handles C++→JS.

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
config.window_config.start_url = "https://tauricpp.app/index.html";

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

// Emit event to frontend (JS listens via __tauricpp__.listen('event', cb))
bridge.Emit("event_name", nlohmann::json{{"key", "value"}});
```

### Window

```cpp
auto& window = app.GetWindow();
window.ExecuteJs("console.log('Hello from C++')");
HWND hwnd = window.GetHwnd();
```

### VirtualFS

```cpp
auto& vfs = app.GetFS();

// Register a file in the virtual filesystem
vfs.RegisterFile("/path/to/file.html", "<h1>Hello</h1>", "text/html");

// Find a file
tauricpp::VirtualFS::VFile file;
if (vfs.FindFile("/path/to/file.html", file)) {
    // file.data: std::vector<uint8_t>
    // file.mime_type: std::string
}
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

## License

MIT
