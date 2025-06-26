@echo off
setlocal enabledelayedexpansion

REM leetcli Build Script for Windows
REM This script builds leetcli from source without installing

echo 🔨 Building leetcli...

REM Check if we're in the right directory
if not exist "CMakeLists.txt" (
    echo ❌ Error: Please run this script from the leetcli root directory
    pause
    exit /b 1
)

REM Check for required tools
where cmake >nul 2>nul
if %errorlevel% neq 0 (
    echo ❌ Error: cmake is required but not installed
    echo Please install cmake first from: https://cmake.org/download/
    pause
    exit /b 1
)

where git >nul 2>nul
if %errorlevel% neq 0 (
    echo ❌ Error: git is required but not installed
    echo Please install git first from: https://git-scm.com/download/win
    pause
    exit /b 1
)

REM Check for Visual Studio or Build Tools
where cl >nul 2>nul
if %errorlevel% neq 0 (
    echo ❌ Error: Visual Studio or Build Tools are required but not found
    echo Please install Visual Studio 2019/2022 with C++ workload or Build Tools
    echo Download from: https://visualstudio.microsoft.com/downloads/
    pause
    exit /b 1
)

REM Install vcpkg if not present
if not exist "vcpkg" (
    echo 📦 Installing vcpkg...
    git clone https://github.com/microsoft/vcpkg.git
    call vcpkg\bootstrap-vcpkg.bat
)

REM Install dependencies
echo 📦 Installing dependencies...
call vcpkg\vcpkg.exe install cpr nlohmann-json

REM Build the project
echo 🔨 Building leetcli...
if not exist "build" mkdir build
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=%cd%\vcpkg\scripts\buildsystems\vcpkg.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

echo ✅ leetcli has been successfully built!
echo.
echo 🎉 The executable is located at: build\Release\leetcli.exe
echo.
echo 📚 To run leetcli:
echo    build\Release\leetcli.exe init
echo.
echo 📚 For more information, run: build\Release\leetcli.exe help
echo.
pause 