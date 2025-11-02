# Build and run the TypeScript/C++ MCP interop example
param(
    [switch]$BuildOnly
)

$ErrorActionPreference = "Stop"

Write-Host "======================================"
Write-Host "TypeScript/C++ MCP Interop Example"
Write-Host "======================================"
Write-Host ""

# Check if Node.js is installed
if (!(Get-Command node -ErrorAction SilentlyContinue)) {
    Write-Host "Error: Node.js is not installed. Please install Node.js 18+ first." -ForegroundColor Red
    exit 1
}

Write-Host "Step 1: Building C++ components..."
Write-Host "-----------------------------------"

# Create build directory
if (!(Test-Path build)) {
    New-Item -ItemType Directory -Path build | Out-Null
}

Set-Location build

# Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) {
    Write-Host "CMake configure failed!" -ForegroundColor Red
    exit 1
}

# Build
cmake --build . --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}

Set-Location ..

Write-Host ""
Write-Host "Step 2: Installing TypeScript dependencies..."
Write-Host "----------------------------------------------"

# Install TS server dependencies
Set-Location ts_server
npm install
if ($LASTEXITCODE -ne 0) {
    Write-Host "npm install failed for ts_server!" -ForegroundColor Red
    exit 1
}
Set-Location ..

# Install TS client dependencies
Set-Location ts_client
npm install
if ($LASTEXITCODE -ne 0) {
    Write-Host "npm install failed for ts_client!" -ForegroundColor Red
    exit 1
}
Set-Location ..

Write-Host ""
Write-Host "✅ Build completed successfully!" -ForegroundColor Green

if ($BuildOnly) {
    exit 0
}

Write-Host ""
Write-Host "======================================"
Write-Host "Running Demo"
Write-Host "======================================"
Write-Host ""

Write-Host "Demo 1: TypeScript Client → C++ MCP Server"
Write-Host "-------------------------------------------"

# Start C++ server in background
$cppServer = Start-Process -FilePath ".\build\Release\cpp_server.exe" -PassThru -NoNewWindow
if (!$cppServer) {
    $cppServer = Start-Process -FilePath ".\build\cpp_server.exe" -PassThru -NoNewWindow
}

# Wait for server to start
Start-Sleep -Seconds 2

# Run TS client
try {
    Set-Location ts_client
    npx tsx client.ts
    Set-Location ..
} finally {
    # Stop C++ server
    if ($cppServer) {
        Stop-Process -Id $cppServer.Id -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Seconds 1
}

Write-Host ""
Write-Host "Demo 2: C++ Client → TypeScript MCP Server"
Write-Host "-------------------------------------------"

# Start TS server in background
Set-Location ts_server
$tsServer = Start-Process -FilePath "npx" -ArgumentList "tsx","server.ts" -PassThru -NoNewWindow
Set-Location ..

# Wait for server to start
Start-Sleep -Seconds 2

# Run C++ client
try {
    $clientPath = ".\build\Release\cpp_client.exe"
    if (!(Test-Path $clientPath)) {
        $clientPath = ".\build\cpp_client.exe"
    }
    & $clientPath
} finally {
    # Stop TS server
    if ($tsServer) {
        Stop-Process -Id $tsServer.Id -Force -ErrorAction SilentlyContinue
    }
}

Write-Host ""
Write-Host "======================================"
Write-Host "✅ All demos completed successfully!" -ForegroundColor Green
Write-Host "======================================"
