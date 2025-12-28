#!/bin/bash
# Build script for Spectrogram Viewer (Linux/macOS)

set -e  # Exit on error

echo "========================================"
echo "Building Spectrogram Viewer"
echo "========================================"
echo ""

# Detect OS
OS="$(uname -s)"
case "${OS}" in
    Linux*)     PLATFORM=Linux;;
    Darwin*)    PLATFORM=macOS;;
    *)          PLATFORM="UNKNOWN:${OS}"
esac

echo "Detected platform: ${PLATFORM}"
echo ""

# Check if imgui directory exists
if [ ! -d "imgui" ]; then
    echo "ImGui not found. Cloning from GitHub..."
    git clone https://github.com/ocornut/imgui.git
fi

# Platform-specific compilation
echo "Compiling application..."

if [ "${PLATFORM}" = "Linux" ]; then
    g++ -o spectrogram_gui spectrogram_lines.cpp \
    imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp \
    imgui/backends/imgui_impl_glfw.cpp imgui/backends/imgui_impl_opengl3.cpp -I./imgui -I./imgui/backends \
    -lGLEW -lglfw -lGL -lportaudio -lfftw3 -lsndfile -lpthread -ldl -std=c++11 -O2

elif [ "${PLATFORM}" = "macOS" ]; then
    # Check for Homebrew installation paths
    if [ -d "/opt/homebrew" ]; then
        BREW_PREFIX="/opt/homebrew"
    else
        BREW_PREFIX="/usr/local"
    fi
    
    g++ -o spectrogram_gui spectrogram_lines.cpp \
    imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp \
    imgui/backends/imgui_impl_glfw.cpp imgui/backends/imgui_impl_opengl3.cpp -I./imgui -I./imgui/backends \
    -I${BREW_PREFIX}/include -L${BREW_PREFIX}/lib \
    -lGLEW -lglfw -framework OpenGL -lportaudio -lfftw3 -lsndfile -std=c++11 -O2
else
    echo "Unsupported platform: ${PLATFORM}"
    exit 1
fi

echo ""
echo "========================================"
echo "Build successful!"
echo "========================================"
echo "Executable: spectrogram_gui"
echo ""
echo "Run with: ./spectrogram_gui"
