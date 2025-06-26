@echo off
setlocal enabledelayedexpansion

REM leetcli Quick Build Script for Windows
REM This script provides helpful guidance for building leetcli

echo 🚀 Quick Build for leetcli...

REM Check if we're in the right directory
if not exist "CMakeLists.txt" (
    echo ❌ Error: Please run this script from the leetcli root directory
    pause
    exit /b 1
)

REM Check for Visual Studio or Build Tools
where cl >nul 2>nul
if %errorlevel% neq 0 (
    echo ❌ Visual Studio Build Tools not found!
    echo.
    echo 📋 You have several options:
    echo.
    echo 1️⃣ EASIEST: Download pre-built release
    echo    - Go to: https://github.com/d3kanesa/leetcli/releases
    echo    - Download leetcli-windows.zip
    echo    - Extract and run install.bat
    echo.
    echo 2️⃣ INSTALL BUILD TOOLS:
    echo    - Download: https://visualstudio.microsoft.com/downloads/
    echo    - Select "Build Tools for Visual Studio 2022"
    echo    - Install with "C++ build tools" workload
    echo    - Then run: .\install.bat
    echo.
    echo 3️⃣ USE WSL (Windows Subsystem for Linux):
    echo    - Install WSL from Microsoft Store
    echo    - Run: wsl
    echo    - Then: chmod +x install.sh && ./install.sh
    echo.
    echo 4️⃣ USE DOCKER:
    echo    - Install Docker Desktop
    echo    - Run: docker-compose up --build
    echo.
    pause
    exit /b 1
)

REM If we get here, build tools are available
echo ✅ Build tools found! Proceeding with build...
echo.
echo 🔧 Running full installation...
call install.bat 