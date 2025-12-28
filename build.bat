@echo off
REM Build script for Spectrogram Viewer (Windows)
REM Requires MSYS2/MinGW to be installed

echo ========================================
echo Building Spectrogram Viewer
echo ========================================
echo.

REM Check if imgui directory exists
if not exist "imgui" (
    echo ImGui not found. Cloning from GitHub...
    git clone https://github.com/ocornut/imgui.git
    if errorlevel 1 (
        echo Error: Failed to clone ImGui. Please install git and try again.
        pause
        exit /b 1
    )
)

REM Compile resource file if not already compiled
if not exist "app.o" (
    echo Compiling Windows resources...
    windres app.rc -o app.o
    if errorlevel 1 (
        echo Error: Failed to compile resources. Make sure windres is in PATH.
        pause
        exit /b 1
    )
)

echo Compiling application...
g++ -o spectrogram_gui.exe spectrogram_lines.cpp ^
app.o imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp ^
imgui/backends/imgui_impl_glfw.cpp imgui/backends/imgui_impl_opengl3.cpp -I./imgui -I./imgui/backends ^
-DGLEW_STATIC -mwindows -static -static-libgcc -static-libstdc++ -lglew32 -lglfw3 -lopengl32 -lportaudio ^
-lfftw3 -lsndfile -lvorbisenc -lvorbisfile -lvorbis -lFLAC -lmp3lame -lmpg123 -lopus -logg -lgdi32 ^
-lwinmm -lole32 -lcomdlg32 -lsetupapi -lksuser -lpsapi -lshlwapi -std=c++11 -O2

if errorlevel 1 (
    echo.
    echo ========================================
    echo Build FAILED!
    echo ========================================
    echo Make sure all dependencies are installed:
    echo - MSYS2/MinGW with required packages
    echo Run: pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-glew mingw-w64-x86_64-glfw mingw-w64-x86_64-portaudio mingw-w64-x86_64-fftw mingw-w64-x86_64-libsndfile
    pause
    exit /b 1
)

echo.
echo ========================================
echo Build successful!
echo ========================================
echo Executable: spectrogram_gui.exe
echo.
pause
