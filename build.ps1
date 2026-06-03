# TauriCPP 构建脚本
# 使用方法:
#   .\build.ps1              - 构建
#   .\build.ps1 -Clean       - 清理构建
#   .\build.ps1 -SetupDeps   - 首次使用：安装依赖

param(
    [switch]$SetupDeps,
    [switch]$Clean,
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$RootDir = $PSScriptRoot

# ============================================================================
# 安装依赖
# ============================================================================
if ($SetupDeps) {
    $JsonDir = Join-Path $RootDir "third_party\nlohmann"
    $JsonFile = Join-Path $JsonDir "json.hpp"
    if (-not (Test-Path $JsonFile)) {
        Write-Host "Downloading nlohmann/json.hpp..." -ForegroundColor Cyan
        New-Item -ItemType Directory -Path $JsonDir -Force | Out-Null
        Invoke-WebRequest -Uri "https://ghfast.top/https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp" -OutFile $JsonFile
        Write-Host "  Done" -ForegroundColor Green
    }

    $PwshPath = "C:\PowerShell\pwsh.exe"
    if (-not (Test-Path $PwshPath)) {
        $PwshPath = (Get-Command "pwsh" -ErrorAction SilentlyContinue).Source
    }

    $VcpkgExe = $null
    if (Test-Path "C:\vcpkg\vcpkg.exe") { $VcpkgExe = "C:\vcpkg\vcpkg.exe" }

    if ($VcpkgExe) {
        Write-Host "Installing webview2 via vcpkg..." -ForegroundColor Cyan
        if ($PwshPath) {
            & $PwshPath -Command "& '$VcpkgExe' install webview2 --triplet=x64-windows --classic"
        } else {
            & $VcpkgExe install webview2 --triplet=x64-windows --classic
        }
    }

    Write-Host "Dependencies installed!" -ForegroundColor Green
    return
}

# ============================================================================
# 清理
# ============================================================================
if ($Clean) {
    $BuildDir = Join-Path $RootDir "build"
    if (Test-Path $BuildDir) {
        Remove-Item $BuildDir -Recurse -Force
        Write-Host "Cleaned build directory" -ForegroundColor Green
    }
    return
}

# ============================================================================
# 使用 VS Developer Shell + Ninja 构建
# ============================================================================
$BuildDir = Join-Path $RootDir "build"

Write-Host "=== Configuring CMake (Ninja + MSVC) ===" -ForegroundColor Cyan

# 在 VS Developer Shell 中运行 cmake 和 ninja
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vsWhere -latest -property installationPath 2>$null
if (-not $vsPath) { $vsPath = "C:\Program Files\Microsoft Visual Studio\2022\Enterprise" }

# 使用 vcvarsall.bat 设置环境后运行 cmake + ninja
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"

# 创建临时构建脚本
$buildScript = Join-Path $BuildDir "build_cmd.bat"
if (-not (Test-Path $BuildDir)) { New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null }

$configureNeeded = -not (Test-Path (Join-Path $BuildDir "build.ninja"))

@"
@echo off
call "$vcvars" >nul 2>&1
if %ERRORLEVEL% neq 0 exit /b 1
"@ | Out-File -FilePath $buildScript -Encoding ascii

if ($configureNeeded) {
    @"
cmake -B "$BuildDir" -S "$RootDir" -G Ninja -DCMAKE_BUILD_TYPE=$Config -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
if %ERRORLEVEL% neq 0 exit /b 1
"@ | Out-File -FilePath $buildScript -Encoding ascii -Append
}

@"
ninja -C "$BuildDir"
if %ERRORLEVEL% neq 0 exit /b 1
"@ | Out-File -FilePath $buildScript -Encoding ascii -Append

cmd /c $buildScript

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed!" -ForegroundColor Red
    return
}

Write-Host ""
Write-Host "Build complete!" -ForegroundColor Green
$ExePath = Join-Path $BuildDir "sample.exe"
if (Test-Path $ExePath) {
    Write-Host "Output: $ExePath" -ForegroundColor Green
    $Size = (Get-Item $ExePath).Length / 1MB
    Write-Host ("Size: {0:N2} MB" -f $Size) -ForegroundColor Gray
}
