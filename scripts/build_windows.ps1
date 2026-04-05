# build_windows.ps1 - Build script for Windows
# Usage: .\scripts\build_windows.ps1 [-BuildType Release|Debug] [-Install]

param(
    [ValidateSet("Release", "Debug")]
    [string]$BuildType = "Release",
    [switch]$Install,
    [string]$VcpkgRoot = $env:VCPKG_ROOT
)

$ErrorActionPreference = "Stop"
$PROJECT_ROOT = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BUILD_DIR = Join-Path $PROJECT_ROOT "build"

Write-Host "=== Guitar Amp Simulator - Windows Build ===" -ForegroundColor Cyan
Write-Host "Build type: $BuildType"

# Setup dependencies first
& (Join-Path $PROJECT_ROOT "scripts\setup_dependencies.ps1")

# Create build directory
if (-Not (Test-Path $BUILD_DIR)) {
    New-Item -ItemType Directory -Path $BUILD_DIR | Out-Null
}

# Configure with CMake
Write-Host "`nConfiguring with CMake..." -ForegroundColor Yellow
$cmake_args = @("-DCMAKE_BUILD_TYPE=$BuildType")

if ($VcpkgRoot -and (Test-Path $VcpkgRoot)) {
    $toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
    if (Test-Path $toolchain) {
        $cmake_args += "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
        Write-Host "Using vcpkg toolchain: $toolchain" -ForegroundColor Green
    }
}

Push-Location $BUILD_DIR
try {
    cmake $cmake_args ..
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

    Write-Host "`nBuilding..." -ForegroundColor Yellow
    cmake --build . --config $BuildType --parallel
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }

    Write-Host "`n=== Build Successful ===" -ForegroundColor Green

    $exe_path = Join-Path $BUILD_DIR "$BuildType\amplitron.exe"
    if (-Not (Test-Path $exe_path)) {
        $exe_path = Join-Path $BUILD_DIR "amplitron.exe"
    }
    Write-Host "Binary: $exe_path"

    if ($Install) {
        Write-Host "`nInstalling..." -ForegroundColor Yellow
        cmake --install . --config $BuildType
        Write-Host "Installed successfully." -ForegroundColor Green
    }
}
finally {
    Pop-Location
}
