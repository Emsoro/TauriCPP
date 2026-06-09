cmake_minimum_required(VERSION 3.15)
project(TauriCPP VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
    # 生成 Per-Monitor V2 DPI 感知 manifest 片段，MSVC 会将其与默认 manifest 合并
    # 避免系统对窗口做位图拉伸缩放，消除 WebView2 在高DPI屏幕下的模糊问题
    set(DPI_MANIFEST_XML "<?xml version='1.0' encoding='UTF-8' standalone='yes'?>
<assembly xmlns='urn:schemas-microsoft-com:asm.v1' manifestVersion='1.0'>
  <trustInfo xmlns=\"urn:schemas-microsoft-com:asm.v3\">
    <security>
      <requestedPrivileges>
        <requestedExecutionLevel level='asInvoker' uiAccess='false' />
      </requestedPrivileges>
    </security>
  </trustInfo>
  <application>
    <windowsSettings>
      <dpiAware xmlns='http://schemas.microsoft.com/SMI/2005/WindowsSettings'>true/pm</dpiAware>
      <dpiAwareness xmlns='http://schemas.microsoft.com/SMI/2016/WindowsSettings'>PerMonitorV2</dpiAwareness>
    </windowsSettings>
  </application>
  <compatibility xmlns='urn:schemas-microsoft-com:compatibility.v1'>
    <application>
      <!-- Windows 10/11 -->
      <supportedOS Id='{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}'/>
      <!-- Windows 8.1 -->
      <supportedOS Id='{1f676c76-80e1-4239-95bb-83d0f6d0da78}'/>
      <!-- Windows 8 -->
      <supportedOS Id='{4a2f28e3-53b9-4441-ba9c-d69d4a4a6e38}'/>
      <!-- Windows 7 -->
      <supportedOS Id='{35138b9a-5d96-4fbd-8e2d-a2440225f93a}'/>
    </application>
  </compatibility>
</assembly>")
    file(WRITE "${CMAKE_BINARY_DIR}/generated/dpi_awareness.manifest" "${DPI_MANIFEST_XML}")
endif()

# ============================================================================
# 依赖管理
# ============================================================================

set(NLOHMANN_JSON_DIR "${CMAKE_SOURCE_DIR}/third_party/nlohmann")
if(NOT EXISTS "${NLOHMANN_JSON_DIR}/json.hpp")
    message(STATUS "Downloading nlohmann/json.hpp...")
    file(DOWNLOAD
        "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp"
        "${NLOHMANN_JSON_DIR}/json.hpp"
        SHOW_PROGRESS
    )
endif()

set(WEBVIEW2_SEARCH_PATHS
    "${CMAKE_SOURCE_DIR}/vcpkg/installed/x64-windows"
    "$ENV{VCPKG_ROOT}/installed/x64-windows"
    "C:/vcpkg/installed/x64-windows"
    "${CMAKE_SOURCE_DIR}/third_party/WebView2"
)

find_path(WEBVIEW2_INCLUDE_DIR
    NAMES WebView2.h
    PATH_SUFFIXES include
    PATHS ${WEBVIEW2_SEARCH_PATHS}
)

find_library(WEBVIEW2_LIBRARY
    NAMES WebView2LoaderStatic WebView2Loader.dll.lib
    PATH_SUFFIXES lib
    PATHS ${WEBVIEW2_SEARCH_PATHS}
)


if(NOT WEBVIEW2_INCLUDE_DIR)
    message(FATAL_ERROR
        "WebView2 SDK not found!\n"
        "Install via: vcpkg install webview2 --classic\n"
        "Or download from: https://www.nuget.org/packages/Microsoft.Web.WebView2"
    )
endif()

message(STATUS "WebView2 include: ${WEBVIEW2_INCLUDE_DIR}")
message(STATUS "WebView2 library: ${WEBVIEW2_LIBRARY}")

# ============================================================================
# TauriCPP 静态库
# ============================================================================
add_library(tauricpp STATIC
    src/app.cpp
    src/bridge.cpp
    src/clipboard.cpp
    src/dialog.cpp
    src/virtual_fs.cpp
    src/window.cpp
)

target_include_directories(tauricpp PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${NLOHMANN_JSON_DIR}/..
    ${WEBVIEW2_INCLUDE_DIR}
)

target_link_libraries(tauricpp PUBLIC
    shlwapi
    shell32
    comdlg32
    ole32
    ${WEBVIEW2_LIBRARY}
)

# ============================================================================
# 资源打包：将前端文件生成.rc并编译嵌入exe
# 使用数字资源ID，确保 rc.exe 和 llvm-rc 兼容
# ============================================================================
set(FRONTEND_DIR "${CMAKE_SOURCE_DIR}/sample/frontend")
set(GEN_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GEN_DIR}")
set(RESOURCES_RC "${GEN_DIR}/resources.rc")
set(RESOURCE_MAP_CPP "${GEN_DIR}/resource_map.cpp")

find_package(Python3 COMPONENTS Interpreter)

if(Python3_Interpreter_FOUND)
    # ★ 修复：收集前端文件列表作为依赖，文件变化时自动重新打包
    file(GLOB_RECURSE FRONTEND_FILES "${FRONTEND_DIR}/*")
    add_custom_command(
        OUTPUT "${RESOURCES_RC}" "${RESOURCE_MAP_CPP}"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/pack_resources.py"
                "${FRONTEND_DIR}" -o "${RESOURCES_RC}" -t "TAURI_RES"
        DEPENDS "${CMAKE_SOURCE_DIR}/tools/pack_resources.py" ${FRONTEND_FILES}
        COMMENT "Packing frontend resources into .rc file"
        VERBATIM
    )
else()
    # 没有Python时手动生成
    file(WRITE "${RESOURCES_RC}" "// Auto-generated resources\n")
    file(WRITE "${RESOURCE_MAP_CPP}" "// Auto-generated\n")
endif()


# ============================================================================
# Sample 应用
# ============================================================================
set(SAMPLE_ADDITIONAL_SOURCES "")
if(MSVC)
    list(APPEND SAMPLE_ADDITIONAL_SOURCES "${CMAKE_BINARY_DIR}/generated/dpi_awareness.manifest")
endif()

add_executable(sample WIN32
    sample/src/main.cpp
    "${RESOURCE_MAP_CPP}"
    "${RESOURCES_RC}"
    ${SAMPLE_ADDITIONAL_SOURCES}
)

target_include_directories(sample PRIVATE "${GEN_DIR}")
target_link_libraries(sample PRIVATE tauricpp)

add_dependencies(sample pack_resources)

add_custom_target(pack_resources
    DEPENDS "${RESOURCES_RC}" "${RESOURCE_MAP_CPP}"
    COMMENT "Pack frontend resources"
)
