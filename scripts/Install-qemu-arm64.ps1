#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Build and test SocketsHpp on ARM64 architecture using cross-compilation

.DESCRIPTION
    This script sets up ARM64 cross-compilation toolchain in WSL,
    builds GTest for ARM64, compiles SocketsHpp for ARM64, and runs
    tests using QEMU ARM64 emulation.

.PARAMETER SkipBuild
    Skip building, only run tests on existing ARM64 build

.EXAMPLE
    .\Install-qemu-arm64.ps1
    Build and test SocketsHpp on ARM64

.EXAMPLE
    .\Install-qemu-arm64.ps1 -SkipBuild
    Run ARM64 tests only (assumes build exists)
#>

param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

Write-Host "========================================"
Write-Host "ARM64 Cross-Compilation for SocketsHpp"
Write-Host "========================================"
Write-Host ""

if ($SkipBuild) {
    Write-Host "Skipping build, running tests only..."
    wsl -d Ubuntu-24.04 bash -c "export QEMU_LD_PREFIX=/usr/aarch64-linux-gnu && cd /mnt/c/build/maxgolov/SocketsHpp/build-arm64 && ctest --output-on-failure"
} else {
    Write-Host "Building and testing on ARM64..."
    wsl -d Ubuntu-24.04 bash -c "cd /mnt/c/build/maxgolov/SocketsHpp && ./scripts/build-arm64.sh"
}

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "========================================"
    Write-Host "ARM64 Build and Test Complete!"
    Write-Host "========================================"
    Write-Host ""
    Write-Host "Architecture: aarch64 (ARM64)"
    Write-Host "Build directory: build-arm64/"
    Write-Host "Emulation: QEMU ARM64"
    Write-Host ""
} else {
    Write-Error "ARM64 build or test failed!"
    exit 1
}
