@echo off
REM build-all.cmd
REM Build and test SocketsHpp on all supported platforms from Windows
REM Platforms: Windows x64, Linux x64 (WSL), Linux ARM64 (QEMU on WSL)

setlocal enabledelayedexpansion

echo ========================================
echo Multi-Platform Build and Test Suite
echo ========================================
echo.
echo This script will build and test SocketsHpp on:
echo   1. Windows x64 (native)
echo   2. Linux x64 (WSL2)
echo   3. Linux ARM64 (QEMU on WSL2)
echo.

set BUILD_FAILED=0
set WINDOWS_RESULT=PENDING
set LINUX_RESULT=PENDING
set ARM64_RESULT=PENDING

REM ========================================
REM Windows x64 Build
REM ========================================
echo ========================================
echo [1/3] Building for Windows x64
echo ========================================
echo.

powershell -ExecutionPolicy Bypass -File "%~dp0scripts\Build-Windows.ps1" -Configuration Debug
if %ERRORLEVEL% NEQ 0 (
    set WINDOWS_RESULT=FAILED
    set BUILD_FAILED=1
    echo.
    echo [ERROR] Windows x64 build failed!
    echo.
) else (
    set WINDOWS_RESULT=PASSED
    echo.
    echo [SUCCESS] Windows x64 build completed successfully!
    echo.
)

REM ========================================
REM Linux x64 Build (WSL)
REM ========================================
echo ========================================
echo [2/3] Building for Linux x64 (WSL2)
echo ========================================
echo.

wsl -d Ubuntu-24.04 bash -c "cd /mnt/c/build/maxgolov/SocketsHpp && ./scripts/build-linux.sh"
if %ERRORLEVEL% NEQ 0 (
    set LINUX_RESULT=FAILED
    set BUILD_FAILED=1
    echo.
    echo [ERROR] Linux x64 build failed!
    echo.
) else (
    set LINUX_RESULT=PASSED
    echo.
    echo [SUCCESS] Linux x64 build completed successfully!
    echo.
)

REM ========================================
REM Linux ARM64 Build (QEMU on WSL)
REM ========================================
echo ========================================
echo [3/3] Building for Linux ARM64 (QEMU)
echo ========================================
echo.

wsl -d Ubuntu-24.04 bash -c "cd /mnt/c/build/maxgolov/SocketsHpp && ./scripts/build-arm64.sh"
if %ERRORLEVEL% NEQ 0 (
    set ARM64_RESULT=FAILED
    set BUILD_FAILED=1
    echo.
    echo [ERROR] Linux ARM64 build failed!
    echo.
) else (
    set ARM64_RESULT=PASSED
    echo.
    echo [SUCCESS] Linux ARM64 build completed successfully!
    echo.
)

REM ========================================
REM Summary Report
REM ========================================
echo.
echo ========================================
echo Build Summary
echo ========================================
echo.
echo Platform            Status      Build Directory
echo ----------------    -------     ---------------------------
echo Windows x64         %WINDOWS_RESULT%     build\windows-x64
echo Linux x64 (WSL2)    %LINUX_RESULT%       build\linux-x64
echo Linux ARM64 (QEMU)  %ARM64_RESULT%       build\linux-arm64
echo.

if %BUILD_FAILED% EQU 1 (
    echo ========================================
    echo [FAILED] Some builds failed!
    echo ========================================
    exit /b 1
) else (
    echo ========================================
    echo [SUCCESS] All builds passed!
    echo ========================================
    echo.
    echo All platforms built and tested successfully.
    echo.
    exit /b 0
)
