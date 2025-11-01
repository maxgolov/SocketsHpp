# Install-Tools.ps1
# Installs development tools for SocketsHpp on Windows
# Requires PowerShell 5.1+ and Administrator privileges

#Requires -RunAsAdministrator

param(
    [switch]$SkipVS2022,
    [switch]$SkipLLVM,
    [switch]$SkipCMake,
    [switch]$SkipNinja,
    [switch]$SkipVcpkg
)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "SocketsHpp Development Tools Installer" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check if running as Administrator
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Error "This script must be run as Administrator"
    exit 1
}

# Install Chocolatey if not already installed
function Install-Chocolatey {
    if (!(Get-Command choco -ErrorAction SilentlyContinue)) {
        Write-Host "Installing Chocolatey package manager..." -ForegroundColor Yellow
        Set-ExecutionPolicy Bypass -Scope Process -Force
        [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
        Invoke-Expression ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
        
        # Refresh environment variables
        $env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")
        
        Write-Host "Chocolatey installed successfully!" -ForegroundColor Green
    } else {
        Write-Host "Chocolatey is already installed" -ForegroundColor Green
    }
}

# Install Visual Studio 2022 Community Edition
function Install-VisualStudio2022 {
    if ($SkipVS2022) {
        Write-Host "Skipping Visual Studio 2022 installation (--SkipVS2022 specified)" -ForegroundColor Yellow
        return
    }

    Write-Host "`nChecking for Visual Studio 2022..." -ForegroundColor Cyan
    
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWhere) {
        $vsInstallations = & $vsWhere -version "[17.0,18.0)" -products * -property installationPath
        if ($vsInstallations) {
            Write-Host "Visual Studio 2022 is already installed at: $vsInstallations" -ForegroundColor Green
            return
        }
    }

    Write-Host "Installing Visual Studio 2022 Community Edition..." -ForegroundColor Yellow
    Write-Host "This will install VS 2022 with C++ Desktop Development workload" -ForegroundColor Yellow
    
    # Download VS installer
    $installerUrl = "https://aka.ms/vs/17/release/vs_community.exe"
    $installerPath = "$env:TEMP\vs_community.exe"
    
    Write-Host "Downloading Visual Studio installer..." -ForegroundColor Yellow
    Invoke-WebRequest -Uri $installerUrl -OutFile $installerPath
    
    # Install with C++ workload
    Write-Host "Installing Visual Studio 2022 (this may take a while)..." -ForegroundColor Yellow
    $vsArgs = @(
        "--quiet",
        "--wait",
        "--norestart",
        "--add", "Microsoft.VisualStudio.Workload.NativeDesktop",
        "--add", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
        "--add", "Microsoft.VisualStudio.Component.VC.CMake.Project",
        "--add", "Microsoft.VisualStudio.Component.Windows11SDK.22621"
    )
    
    Start-Process -FilePath $installerPath -ArgumentList $vsArgs -Wait -NoNewWindow
    
    Remove-Item $installerPath -Force
    Write-Host "Visual Studio 2022 installed successfully!" -ForegroundColor Green
}

# Install LLVM
function Install-LLVM {
    if ($SkipLLVM) {
        Write-Host "`nSkipping LLVM installation (--SkipLLVM specified)" -ForegroundColor Yellow
        return
    }

    Write-Host "`nChecking for LLVM..." -ForegroundColor Cyan
    
    if (Get-Command clang -ErrorAction SilentlyContinue) {
        $clangVersion = & clang --version | Select-Object -First 1
        Write-Host "LLVM is already installed: $clangVersion" -ForegroundColor Green
        return
    }

    Write-Host "Installing latest LLVM via Chocolatey..." -ForegroundColor Yellow
    choco install llvm -y
    
    # Refresh PATH
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")
    
    Write-Host "LLVM installed successfully!" -ForegroundColor Green
}

# Install CMake
function Install-CMake {
    if ($SkipCMake) {
        Write-Host "`nSkipping CMake installation (--SkipCMake specified)" -ForegroundColor Yellow
        return
    }

    Write-Host "`nChecking for CMake..." -ForegroundColor Cyan
    
    if (Get-Command cmake -ErrorAction SilentlyContinue) {
        $cmakeVersion = & cmake --version | Select-Object -First 1
        Write-Host "CMake is already installed: $cmakeVersion" -ForegroundColor Green
        
        # Update if needed
        Write-Host "Upgrading CMake to latest version..." -ForegroundColor Yellow
        choco upgrade cmake -y
    } else {
        Write-Host "Installing CMake via Chocolatey..." -ForegroundColor Yellow
        choco install cmake --installargs 'ADD_CMAKE_TO_PATH=System' -y
    }
    
    # Refresh PATH
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")
    
    Write-Host "CMake is ready!" -ForegroundColor Green
}

# Install Ninja
function Install-Ninja {
    if ($SkipNinja) {
        Write-Host "`nSkipping Ninja installation (--SkipNinja specified)" -ForegroundColor Yellow
        return
    }

    Write-Host "`nChecking for Ninja..." -ForegroundColor Cyan
    
    if (Get-Command ninja -ErrorAction SilentlyContinue) {
        $ninjaVersion = & ninja --version
        Write-Host "Ninja is already installed: $ninjaVersion" -ForegroundColor Green
        return
    }

    Write-Host "Installing Ninja via Chocolatey..." -ForegroundColor Yellow
    choco install ninja -y
    
    # Refresh PATH
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")
    
    Write-Host "Ninja installed successfully!" -ForegroundColor Green
}

# Install vcpkg
function Install-Vcpkg {
    if ($SkipVcpkg) {
        Write-Host "`nSkipping vcpkg installation (--SkipVcpkg specified)" -ForegroundColor Yellow
        return
    }

    Write-Host "`nChecking for vcpkg..." -ForegroundColor Cyan
    
    $vcpkgRoot = "C:\vcpkg"
    
    if (Test-Path "$vcpkgRoot\vcpkg.exe") {
        Write-Host "vcpkg is already installed at: $vcpkgRoot" -ForegroundColor Green
        
        # Update vcpkg
        Write-Host "Updating vcpkg..." -ForegroundColor Yellow
        Push-Location $vcpkgRoot
        git pull
        .\bootstrap-vcpkg.bat
        Pop-Location
    } else {
        Write-Host "Installing vcpkg to $vcpkgRoot..." -ForegroundColor Yellow
        
        # Clone vcpkg
        if (!(Test-Path $vcpkgRoot)) {
            git clone https://github.com/Microsoft/vcpkg.git $vcpkgRoot
        }
        
        # Bootstrap vcpkg
        Push-Location $vcpkgRoot
        .\bootstrap-vcpkg.bat
        Pop-Location
        
        # Set environment variable
        [System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", $vcpkgRoot, [System.EnvironmentVariableTarget]::Machine)
        $env:VCPKG_ROOT = $vcpkgRoot
    }
    
    # Integrate vcpkg with Visual Studio
    Write-Host "Integrating vcpkg with Visual Studio..." -ForegroundColor Yellow
    & "$vcpkgRoot\vcpkg.exe" integrate install
    
    # Install GTest
    Write-Host "Installing Google Test via vcpkg..." -ForegroundColor Yellow
    & "$vcpkgRoot\vcpkg.exe" install gtest:x64-windows
    
    Write-Host "vcpkg installed and configured successfully!" -ForegroundColor Green
}

# Main installation flow
try {
    Write-Host "Starting installation process...`n" -ForegroundColor Cyan
    
    # Install Chocolatey first
    Install-Chocolatey
    
    # Install all components
    Install-VisualStudio2022
    Install-LLVM
    Install-CMake
    Install-Ninja
    Install-Vcpkg
    
    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "Installation Complete!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "`nInstalled components:" -ForegroundColor Cyan
    
    if (!$SkipVS2022) { Write-Host "  ✓ Visual Studio 2022 Community Edition" -ForegroundColor Green }
    if (!$SkipLLVM) { Write-Host "  ✓ LLVM/Clang" -ForegroundColor Green }
    if (!$SkipCMake) { Write-Host "  ✓ CMake" -ForegroundColor Green }
    if (!$SkipNinja) { Write-Host "  ✓ Ninja Build System" -ForegroundColor Green }
    if (!$SkipVcpkg) { Write-Host "  ✓ vcpkg Package Manager" -ForegroundColor Green }
    
    Write-Host "`nNext steps:" -ForegroundColor Cyan
    Write-Host "  1. Restart your terminal to refresh environment variables" -ForegroundColor Yellow
    Write-Host "  2. Navigate to the project directory" -ForegroundColor Yellow
    Write-Host "  3. Run: cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake" -ForegroundColor Yellow
    Write-Host "  4. Run: cmake --build build --config Debug" -ForegroundColor Yellow
    Write-Host "  5. Run: cd build && ctest -C Debug --output-on-failure" -ForegroundColor Yellow
    
} catch {
    Write-Host "`nError during installation: $_" -ForegroundColor Red
    exit 1
}
