#!/bin/bash

# leetcli Build Script for Linux/macOS
# This script builds leetcli from source without installing

set -e  # Exit on any error

echo "🔨 Building leetcli..."

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    echo "❌ Error: Please run this script from the leetcli root directory"
    exit 1
fi

# Check for required tools
if ! command -v cmake &> /dev/null; then
    echo "❌ Error: cmake is required but not installed"
    echo "Please install cmake first:"
    echo "  Ubuntu/Debian: sudo apt install cmake"
    echo "  macOS: brew install cmake"
    exit 1
fi

if ! command -v git &> /dev/null; then
    echo "❌ Error: git is required but not installed"
    exit 1
fi

# Install vcpkg if not present
if [ ! -d "vcpkg" ]; then
    echo "📦 Installing vcpkg..."
    git clone https://github.com/microsoft/vcpkg.git
    ./vcpkg/bootstrap-vcpkg.sh
fi

# Install dependencies
echo "📦 Installing dependencies..."
./vcpkg/vcpkg install cpr nlohmann-json

# Build the project
echo "🔨 Building leetcli..."
mkdir -p build
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$(pwd)/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

echo "✅ leetcli has been successfully built!"
echo ""
echo "🎉 The executable is located at: build/leetcli"
echo ""
echo "📚 To run leetcli:"
echo "   ./build/leetcli init"
echo ""
echo "📚 For more information, run: ./build/leetcli help" 