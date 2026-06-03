cmake_minimum_required(VERSION 3.15)
project(TauriCPP VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
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
    NAMES WebView2Loader.dll
    PATH_SUFFIXES lib
    PATHS ${WEBVIEW2_SEARCH_PATHS}
)

find_file(WEBVIEW2_DLL
    NAMES WebView2Loader.dll
    PATH_SUFFIXES bin
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
message(STATUS "WebView2 dll: ${WEBVIEW2_DLL}")

# ============================================================================
# TauriCPP 静态库
# ============================================================================
add_library(tauricpp STATIC
    src/app.cpp
    src/bridge.cpp
    src/embedded_dll.cpp
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
    ${WEBVIEW2_LIBRARY}
)

# ============================================================================
# 资源打包：将前端文件 + WebView2Loader.dll 生成.rc并编译嵌入exe
# 使用数字资源ID，确保 rc.exe 和 llvm-rc 兼容
# ============================================================================
set(FRONTEND_DIR "${CMAKE_SOURCE_DIR}/sample/frontend")
set(GEN_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GEN_DIR}")
set(RESOURCES_RC "${GEN_DIR}/resources.rc")
set(RESOURCE_MAP_CPP "${GEN_DIR}/resource_map.cpp")

find_package(Python3 COMPONENTS Interpreter)

if(Python3_Interpreter_FOUND)
    add_custom_command(
        OUTPUT "${RESOURCES_RC}" "${RESOURCE_MAP_CPP}"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/pack_resources.py"
                "${FRONTEND_DIR}" -o "${RESOURCES_RC}" -t "TAURI_RES"
        DEPENDS "${CMAKE_SOURCE_DIR}/tools/pack_resources.py"
        COMMENT "Packing frontend resources into .rc file"
        VERBATIM
    )
else()
    # 没有Python时手动生成
    file(WRITE "${RESOURCES_RC}" "// Auto-generated resources\n")
    file(WRITE "${RESOURCE_MAP_CPP}" "// Auto-generated\n")
endif()

# WebView2Loader.dll 嵌入资源（使用数字ID 1）
set(DLL_RC "${GEN_DIR}/dll_resource.rc")
if(WEBVIEW2_DLL)
    file(WRITE "${DLL_RC}"
        "// Auto-generated: WebView2Loader.dll embedded as resource\n"
        "1 EMBEDDED_DLL \"${WEBVIEW2_DLL}\"\n"
    )
else()
    file(WRITE "${DLL_RC}" "// No WebView2Loader.dll found\n")
endif()

# ============================================================================
# Sample 应用
# ============================================================================
add_executable(sample WIN32
    sample/src/main.cpp
    "${RESOURCE_MAP_CPP}"
    "${RESOURCES_RC}"
    "${DLL_RC}"
)

target_include_directories(sample PRIVATE "${GEN_DIR}")
target_link_libraries(sample PRIVATE tauricpp delayimp)

# 关键：delay-load WebView2Loader.dll
# 这样 Windows 不会在程序启动时加载 DLL，而是在首次调用时才加载
# EmbeddedDll::Load 在 WinMain 中先执行，从 exe 资源释放 DLL 到内存
set_target_properties(sample PROPERTIES
    LINK_FLAGS "/DELAYLOAD:WebView2Loader.dll"
)

add_dependencies(sample pack_resources)

add_custom_target(pack_resources
    DEPENDS "${RESOURCES_RC}" "${RESOURCE_MAP_CPP}"
    COMMENT "Pack frontend resources"
)
