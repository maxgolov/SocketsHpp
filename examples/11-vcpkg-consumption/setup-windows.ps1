#!/usr/bin/env pwsh
# Copyright Max Golovanov.
# SPDX-License-Identifier: Apache-2.0

<#
.SYNOPSIS
    Setup script for SocketsHpp vcpkg consumption example on Windows
.DESCRIPTION
    This script automates the installation of SocketsHpp via vcpkg overlay ports,
    builds the example, and runs it.
.PARAMETER SkipBuild
    Skip building the example after installing dependencies
.PARAMETER CleanBuild
    Remove build directory before building
#>

param(
    [switch]$SkipBuild,
    [switch]$CleanBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# Configuration
$ExampleName = "11-vcpkg-consumption"
$RepoRoot = (Get-Item $PSScriptRoot).Parent.Parent.FullName
$ExampleDir = $PSScriptRoot
$BuildDir = Join-Path $ExampleDir "build"
$OverlayPortsDir = Join-Path $RepoRoot "ports"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host " SocketsHpp vcpkg Consumption Setup" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Example Directory: $ExampleDir" -ForegroundColor Gray
Write-Host "Repository Root: $RepoRoot" -ForegroundColor Gray
Write-Host "Overlay Ports: $OverlayPortsDir" -ForegroundColor Gray
Write-Host ""

# Step 1: Check for vcpkg
Write-Host "[1/5] Checking for vcpkg..." -ForegroundColor Yellow
$VcpkgCmd = Get-Command vcpkg -ErrorAction SilentlyContinue

if (-not $VcpkgCmd) {
    Write-Host "ERROR: vcpkg not found in PATH" -ForegroundColor Red
    Write-Host ""
    Write-Host "Please install vcpkg:" -ForegroundColor Yellow
    Write-Host "  git clone https://github.com/microsoft/vcpkg.git" -ForegroundColor White
    Write-Host "  cd vcpkg" -ForegroundColor White
    Write-Host "  .\bootstrap-vcpkg.bat" -ForegroundColor White
    Write-Host "  Add vcpkg to PATH" -ForegroundColor White
    exit 1
}

$VcpkgRoot = (Get-Item $VcpkgCmd.Source).Directory.FullName
Write-Host "Found vcpkg at: $VcpkgRoot" -ForegroundColor Green
Write-Host ""

# Step 2: Verify overlay ports directory
Write-Host "[2/5] Verifying overlay ports..." -ForegroundColor Yellow
if (-not (Test-Path $OverlayPortsDir)) {
    Write-Host "ERROR: Overlay ports directory not found: $OverlayPortsDir" -ForegroundColor Red
    exit 1
}

$SocketsHppPort = Join-Path $OverlayPortsDir "socketshpp"
if (-not (Test-Path $SocketsHppPort)) {
    Write-Host "ERROR: SocketsHpp port not found: $SocketsHppPort" -ForegroundColor Red
    exit 1
}

Write-Host "Found SocketsHpp port at: $SocketsHppPort" -ForegroundColor Green
Write-Host ""

# Step 3: Install SocketsHpp via vcpkg overlay
Write-Host "[3/5] Installing SocketsHpp via vcpkg..." -ForegroundColor Yellow
Write-Host "Command: vcpkg install socketshpp --overlay-ports=$OverlayPortsDir" -ForegroundColor Gray

try {
    & vcpkg install socketshpp "--overlay-ports=$OverlayPortsDir"
    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg install failed with exit code $LASTEXITCODE"
    }
    Write-Host "SocketsHpp installed successfully!" -ForegroundColor Green
}
catch {
    Write-Host "ERROR: Failed to install SocketsHpp" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
Write-Host ""

# Step 4: Build the example
if ($SkipBuild) {
    Write-Host "[4/5] Skipping build (--SkipBuild flag set)" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Setup complete! To build manually:" -ForegroundColor Green
    Write-Host "  cd $ExampleDir" -ForegroundColor White
    Write-Host "  cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot/scripts/buildsystems/vcpkg.cmake" -ForegroundColor White
    Write-Host "  cmake --build build --config Release" -ForegroundColor White
    exit 0
}

Write-Host "[4/5] Building example..." -ForegroundColor Yellow

# Clean build if requested
if ($CleanBuild -and (Test-Path $BuildDir)) {
    Write-Host "Removing old build directory..." -ForegroundColor Gray
    Remove-Item -Recurse -Force $BuildDir
}

# Configure with CMake
Write-Host "Configuring CMake..." -ForegroundColor Gray
$ToolchainFile = Join-Path $VcpkgRoot "scripts/buildsystems/vcpkg.cmake"
try {
    & cmake -B $BuildDir -S $ExampleDir -DCMAKE_TOOLCHAIN_FILE=$ToolchainFile
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed with exit code $LASTEXITCODE"
    }
}
catch {
    Write-Host "ERROR: CMake configuration failed" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}

# Build
Write-Host "Building..." -ForegroundColor Gray
try {
    & cmake --build $BuildDir --config Release
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE"
    }
    Write-Host "Build successful!" -ForegroundColor Green
}
catch {
    Write-Host "ERROR: Build failed" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
Write-Host ""

# Step 5: Run the example
Write-Host "[5/5] Running example..." -ForegroundColor Yellow
Write-Host "Starting HTTP server on http://localhost:9000" -ForegroundColor Gray
Write-Host "Press Ctrl+C to stop" -ForegroundColor Gray
Write-Host ""

$ExePath = Join-Path $BuildDir "Release/vcpkg-consumer.exe"
if (-not (Test-Path $ExePath)) {
    # Try Debug build
    $ExePath = Join-Path $BuildDir "Debug/vcpkg-consumer.exe"
}

if (Test-Path $ExePath) {
    & $ExePath
}
else {
    Write-Host "ERROR: Executable not found" -ForegroundColor Red
    Write-Host "Expected: $ExePath" -ForegroundColor Gray
    exit 1
}
