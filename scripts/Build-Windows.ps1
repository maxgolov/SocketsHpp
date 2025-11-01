# Build-Windows.ps1
# Build and test SocketsHpp on Windows

param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    
    [switch]$SkipTests,
    [switch]$Clean,
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Building SocketsHpp on Windows" -ForegroundColor Cyan
Write-Host "Configuration: $Configuration" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Get script directory
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir

# Check for vcpkg
if (-not $env:VCPKG_ROOT) {
    if (Test-Path "C:\vcpkg\vcpkg.exe") {
        $env:VCPKG_ROOT = "C:\vcpkg"
        Write-Host "Using vcpkg from: $env:VCPKG_ROOT" -ForegroundColor Yellow
    } else {
        Write-Error "vcpkg not found. Please run Install-Tools.ps1 first or set VCPKG_ROOT environment variable."
        exit 1
    }
}

$VcpkgToolchain = "$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"

if (-not (Test-Path $VcpkgToolchain)) {
    Write-Error "vcpkg toolchain file not found at: $VcpkgToolchain"
    exit 1
}

# Clean build directory if requested
if ($Clean) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    if (Test-Path "$ProjectRoot\build") {
        Remove-Item -Recurse -Force "$ProjectRoot\build"
    }
}

# Configure with CMake
Write-Host "`nConfiguring project with CMake..." -ForegroundColor Cyan
Push-Location $ProjectRoot

$ConfigureArgs = @(
    "-B", "build",
    "-S", ".",
    "-DCMAKE_TOOLCHAIN_FILE=$VcpkgToolchain",
    "-DCMAKE_BUILD_TYPE=$Configuration"
)

Write-Host "Running: cmake $($ConfigureArgs -join ' ')" -ForegroundColor Gray
& cmake @ConfigureArgs

if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configuration failed with exit code $LASTEXITCODE"
    Pop-Location
    exit $LASTEXITCODE
}

# Build with CMake
Write-Host "`nBuilding project..." -ForegroundColor Cyan
$BuildArgs = @(
    "--build", "build",
    "--config", $Configuration
)

if ($Verbose) {
    $BuildArgs += "--verbose"
}

Write-Host "Running: cmake $($BuildArgs -join ' ')" -ForegroundColor Gray
& cmake @BuildArgs

if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed with exit code $LASTEXITCODE"
    Pop-Location
    exit $LASTEXITCODE
}

Write-Host "`nBuild completed successfully!" -ForegroundColor Green

# Run tests
if (-not $SkipTests) {
    Write-Host "`nRunning tests..." -ForegroundColor Cyan
    Push-Location "$ProjectRoot\build"
    
    $TestArgs = @(
        "-C", $Configuration,
        "--output-on-failure"
    )
    
    if ($Verbose) {
        $TestArgs += "-V"
    }
    
    Write-Host "Running: ctest $($TestArgs -join ' ')" -ForegroundColor Gray
    & ctest @TestArgs
    
    $TestExitCode = $LASTEXITCODE
    Pop-Location
    
    if ($TestExitCode -eq 0) {
        Write-Host "`nAll tests passed!" -ForegroundColor Green
    } else {
        Write-Error "Tests failed with exit code $TestExitCode"
        Pop-Location
        exit $TestExitCode
    }
} else {
    Write-Host "`nSkipping tests (--SkipTests specified)" -ForegroundColor Yellow
}

Pop-Location

Write-Host "`n========================================" -ForegroundColor Green
Write-Host "Windows Build Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
