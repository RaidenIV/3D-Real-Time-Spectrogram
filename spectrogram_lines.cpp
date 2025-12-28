// 3D Waterfall Spectrogram with embedded GUI - OPTIMIZED VERSION
// Visualization displays inside the GUI window with sidebar controls
//
// OPTIMIZATIONS:
// - Texture-based 2D spectrogram rendering (10-50x faster)
// - Reduced texture size for large viewports
// - Better memory management
//
// Compile (Windows/MSYS2):
// First download ImGui: git clone https://github.com/ocornut/imgui.git
// Then compile:
// g++ -o spectrogram_gui.exe spectrogram_lines.cpp ^
// app.o imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp ^
// imgui/backends/imgui_impl_glfw.cpp imgui/backends/imgui_impl_opengl3.cpp -I./imgui -I./imgui/backends ^
// -DGLEW_STATIC -mwindows -static -static-libgcc -static-libstdc++ -lglew32 -lglfw3 -lopengl32 -lportaudio ^
// -lfftw3 -lsndfile -lvorbisenc -lvorbisfile -lvorbis -lFLAC -lmp3lame -lmpg123 -lopus -logg -lgdi32 ^
// -lwinmm -lole32 -lcomdlg32 -lsetupapi -lksuser -lpsapi -lshlwapi -std=c++11 -O2



#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <portaudio.h>
#include <fftw3.h>
#include <sndfile.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <iostream>
#include <cmath>
#include <vector>
#include <array>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <atomic>
#include <string>
#include <thread>
#include <chrono>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <commdlg.h>
#include <psapi.h>

// CPU usage tracking for Windows
static ULARGE_INTEGER lastCPU, lastSysCPU, lastUserCPU;
static int numProcessors;
static HANDLE selfProcess;

void initCPUUsage() {
    SYSTEM_INFO sysInfo;
    FILETIME ftime, fsys, fuser;

    GetSystemInfo(&sysInfo);
    numProcessors = sysInfo.dwNumberOfProcessors;

    GetSystemTimeAsFileTime(&ftime);
    memcpy(&lastCPU, &ftime, sizeof(FILETIME));

    selfProcess = GetCurrentProcess();
    GetProcessTimes(selfProcess, &ftime, &ftime, &fsys, &fuser);
    memcpy(&lastSysCPU, &fsys, sizeof(FILETIME));
    memcpy(&lastUserCPU, &fuser, sizeof(FILETIME));
}

double getCurrentCPUUsage() {
    FILETIME ftime, fsys, fuser;
    ULARGE_INTEGER now, sys, user;
    double percent;

    GetSystemTimeAsFileTime(&ftime);
    memcpy(&now, &ftime, sizeof(FILETIME));

    GetProcessTimes(selfProcess, &ftime, &ftime, &fsys, &fuser);
    memcpy(&sys, &fsys, sizeof(FILETIME));
    memcpy(&user, &fuser, sizeof(FILETIME));

    percent = (sys.QuadPart - lastSysCPU.QuadPart) + (user.QuadPart - lastUserCPU.QuadPart);
    percent /= (now.QuadPart - lastCPU.QuadPart);
    percent /= numProcessors;
    lastCPU = now;
    lastUserCPU = user;
    lastSysCPU = sys;

    return percent * 100.0;
}

std::string openFileDialog(GLFWwindow* window) {
    OPENFILENAMEA ofn;
    char szFile[512] = {0};

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = glfwGetWin32Window(window);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Audio Files\0*.WAV;*.wav;*.mp3;*.MP3;*.flac;*.FLAC;*.ogg;*.OGG;*.aiff;*.AIFF;*.m4a;*.M4A\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(ofn.lpstrFile);
    }
    return "";
}
#else
void initCPUUsage() {}
double getCurrentCPUUsage() { return 0.0; }

std::string openFileDialog(GLFWwindow* window) {
    std::string path;
    std::cout << "Enter WAV file path: ";
    std::getline(std::cin, path);
    return path;
}
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ===================== Audio file reading (supports WAV, MP3, FLAC, OGG, etc.) =====================
// IMPORTANT: audioData is stored as MONO (one float per frame).
// sourceChannels stores the original channel count from the file.
struct WAVFile {
    std::vector<float> audioData;     // MONO samples (one float per frame)
    uint32_t sampleRate = 0;

    uint16_t sourceChannels = 0;      // channels in the original file
    uint16_t numChannels = 1;         // channels in audioData (ALWAYS 1 here)
    uint16_t bitsPerSample = 16;

    std::string getFormatName(const std::string& filename) {
        size_t dotPos = filename.find_last_of('.');
        if (dotPos == std::string::npos) return "Unknown";
        std::string ext = filename.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
        return ext;
    }

    bool load(const std::string& filename) {
        SF_INFO sfinfo;
        memset(&sfinfo, 0, sizeof(sfinfo));

        SNDFILE* sndfile = sf_open(filename.c_str(), SFM_READ, &sfinfo);
        if (!sndfile) {
            std::cerr << "Error: Could not open file: " << filename << "\n";
            std::cerr << "libsndfile error: " << sf_strerror(NULL) << "\n";
            return false;
        }

        sampleRate = (uint32_t)sfinfo.samplerate;
        sourceChannels = (uint16_t)sfinfo.channels;
        numChannels = 1; // store mono

        size_t totalFrames = (size_t)sfinfo.frames;
        std::vector<float> buffer(totalFrames * (size_t)sourceChannels);

        sf_count_t framesRead = sf_readf_float(sndfile, buffer.data(), (sf_count_t)totalFrames);
        sf_close(sndfile);

        if (framesRead <= 0) {
            std::cerr << "Error: No audio data read\n";
            return false;
        }

        // Convert to mono by averaging channels
        audioData.resize((size_t)framesRead);
        for (size_t i = 0; i < (size_t)framesRead; i++) {
            float sum = 0.0f;
            for (int ch = 0; ch < (int)sourceChannels; ch++) {
                sum += buffer[i * (size_t)sourceChannels + (size_t)ch];
            }
            audioData[i] = sum / (float)sourceChannels;
        }

        std::cout << "Audio File Info:\n";
        std::cout << "  Format: " << getFormatName(filename) << "\n";
        std::cout << "  Sample Rate: " << sampleRate << " Hz\n";
        std::cout << "  Channels: " << sourceChannels << " (downmixed to mono)\n";
        std::cout << "  Duration: " << (float)audioData.size() / (float)sampleRate << " seconds\n";

        return true;
    }
};

// ===================== CONFIG  =====================
static int FFT_SIZE = 4096;  // Variable, default 4096
static int NUM_BARS = 1000;  // Number of frequency bins to display
static int HISTORY_LINES = 140;  // Changed to variable so it can be modified, default 140
static constexpr float MIN_FREQ = 20.0f;
static constexpr float MAG_GAIN = 140.0f;
static constexpr float PLANE_SPAN = 2.6f*2;
static constexpr float X_SPAN = PLANE_SPAN;
static constexpr float Z_SPAN = PLANE_SPAN;
static constexpr float Y_SCALE = 1.20f;
static constexpr float COLOR_BRIGHTNESS = 2.1f;
static constexpr float COLOR_GAMMA = 0.45f;
static constexpr float COLOR_SAT = 1.0f*2;
static constexpr int FRAMES_PER_BUFFER = 256;

// ===================== Globals =====================
WAVFile wavFile;
std::vector<float> magnitudes;  // Dynamic size based on FFT_SIZE
std::atomic<uint64_t> playbackPosition{0};  // MONO sample index (one per frame)
std::atomic<bool> isPaused{false};

fftw_plan fftPlan = nullptr;
double* fftInput = nullptr;
fftw_complex* fftOutput = nullptr;

// Texture-based spectrogram rendering (OPTIMIZATION)
static GLuint spectrogramTexture = 0;
static std::vector<unsigned char> textureData;
static int texWidth = 0;
static int texHeight = 0;

// Helper to get getNumFrequencies() based on current FFT_SIZE
int getNumFrequencies() {
    return FFT_SIZE / 2;
}

// Forward declaration
static void buildFrequencyMapping();

// Reinitialize FFT when size changes
void reinitializeFFT() {
    // Clean up old FFTW plan
    if (fftPlan) {
        fftw_destroy_plan(fftPlan);
        fftw_free(fftInput);
        fftw_free(fftOutput);
    }

    // Allocate new buffers
    fftInput = (double*)fftw_malloc(sizeof(double) * FFT_SIZE);
    fftOutput = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (FFT_SIZE / 2 + 1));
    fftPlan = fftw_plan_dft_r2c_1d(FFT_SIZE, fftInput, fftOutput, FFTW_ESTIMATE);

    // Resize magnitudes vector
    magnitudes.resize(getNumFrequencies(), 0.0f);

    // Rebuild frequency mapping if audio is loaded
    if (!wavFile.audioData.empty()) {
        buildFrequencyMapping();
    }
}

// Load and set window icon from PNG file
void setWindowIcon(GLFWwindow* window, const char* iconPath) {
    int width, height, channels;
    unsigned char* pixels = stbi_load(iconPath, &width, &height, &channels, 4);

    if (pixels) {
        GLFWimage icon;
        icon.width = width;
        icon.height = height;
        icon.pixels = pixels;

        glfwSetWindowIcon(window, 1, &icon);

        stbi_image_free(pixels);
        std::cout << "Window icon loaded successfully: " << iconPath << "\n";
    } else {
        std::cerr << "Failed to load window icon: " << iconPath << "\n";
    }
}

static constexpr int MAX_HISTORY_LINES = 560;  // Maximum allowed lines
std::vector<float> lineHistory(MAX_HISTORY_LINES * NUM_BARS, 0.0f);
std::vector<float> currentLine(NUM_BARS, 0.0f);
static int historyFillCount = 0;  // Track how many lines have been pushed to history

static std::vector<float> gBarBinF(NUM_BARS, 0.0f);
static std::vector<float> gBarX(NUM_BARS, 0.0f);
static std::vector<float> gBarHue(NUM_BARS, 0.0f);

static int gLatencySamplesBase = 0;
static int gLatencyAdjust = 0;

// Camera
static bool gDragging = false;
static double gLastX = 0.0, gLastY = 0.0;
static float gYaw = -45.0f;
static float gPitch = 35.0f;
static float gDist = 9.0f;

// Line color select
static float bgColor[3] = {0.02f, 0.02f, 0.03f};  // Background color RGB
static float lineColor[3] = {0.0f, 1.0f, 0.0f};  // Line color RGB (pure green by default)
static bool useCustomLineColor = true;  // Toggle between colormap and custom color

// GUI state
static bool useColormap = false;  // Colormap disabled at startup

// Current colormap selection
enum ColormapType {
    COLORMAP_VIRIDIS,
    COLORMAP_PLASMA,
    COLORMAP_INFERNO,
    COLORMAP_MAGMA,
    COLORMAP_HOT,
    COLORMAP_COOL,
    COLORMAP_JET,
    COLORMAP_TURBO,
    COLORMAP_OCEAN,
    COLORMAP_RAINBOW,
    COLORMAP_GRAYSCALE,
    COLORMAP_ICE,
    COLORMAP_FIRE,
    COLORMAP_SEISMIC,
    COLORMAP_TWILIGHT,
    COLORMAP_CIVIDIS
};

static ColormapType currentColormap = COLORMAP_INFERNO;  // Default to Inferno

static bool isPlaying = false;
static bool isFullscreen = false;
static char filePathBuffer[512] = "";
static std::string loadedFileName = "";
static PaStream* audioStream = nullptr;
static bool showGrid = false;  // Grid visibility control
static bool autoRotate = false;  // Auto-rotate camera
static bool showFPS = true;  // FPS counter visibility
static bool loopAudio = true;  // Loop audio playback
static float lineWidth = 1.0f;  // Line width control, default 1.0
// Color controls - using pure colormaps with optimal distribution
static float yScale = 1.20f;  // Y-axis height scale
static float yOffset = 0.0f;  // Y-axis offset (raise/lower entire visualization), default 0.0
static float volume = 1.0f;  // Volume control (0.0 to 1.0)
static bool isMuted = false;  // Mute state
static float volumeBeforeMute = 1.0f;  // Store volume before muting


// Theme control
static bool useDarkTheme = true;  // Default to dark theme

// View mode control
static bool useTraditionalView = false;  // Default to 3D waterfall view
static int saved3D_FFT_SIZE = 4096;  // Save 3D settings when switching to 2D
static int saved3D_HISTORY_LINES = 140;
static float saved3D_lineWidth = 1.0f;  // Save line width
static float saved3D_yScale = 1.20f;    // Save Y scale
static float saved3D_yOffset = 0.0f;    // Save Y offset
static bool saved3D_showGrid = true;    // Save show grid
static bool saved3D_autoRotate = false; // Save auto-rotate
static bool saved3D_useCustomLineColor = true;  // Save custom color state
static bool saved3D_useColormap = false;         // Save colormap state
static ColormapType saved3D_currentColormap = COLORMAP_VIRIDIS;  // Save colormap selection


// Recent files
static std::vector<std::string> recentFiles;
static const int MAX_RECENT_FILES = 10;

// Viewport area for mouse interaction
static int viewportX = 0, viewportY = 0, viewportW = 800, viewportH = 600;

// FPS tracking
static double lastFPSTime = 0.0;
static int frameCount = 0;
static float currentFPS = 0.0f;
static float currentCPU = 0.0f;
static bool showCPU = true;  // Show CPU usage
static bool showMetadata = true;  // Show metadata overlay
static bool showWaveform = true;  // Show waveform display
static bool needsRedraw = true;  // Track if need to redraw
static bool compactMode = false;  // Compact sidebar mode (icons only)

// PortAudio output channels (mono or stereo; stereo duplicates the mono mix)
static int gOutputChannels = 1;

// ===================== Helper functions =====================
static inline float clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

static inline float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

static inline size_t wrapIndex(int64_t idx, size_t size) {
    if (size == 0) return 0;
    int64_t m = idx % (int64_t)size;
    if (m < 0) m += (int64_t)size;
    return (size_t)m;
}

static inline void hsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    h = std::fmod(std::max(0.0f, h), 1.0f);
    s = std::max(0.0f, s);  // Only clamp lower bound, allow s > 1.0 for oversaturation
    v = clamp01(v);

    float c = v * s;
    float x = c * (1.0f - std::fabs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    float m = v - c;

    float rp = 0, gp = 0, bp = 0;
    int seg = (int)std::floor(h * 6.0f);

    switch (seg) {
        case 0: rp = c; gp = x; bp = 0; break;
        case 1: rp = x; gp = c; bp = 0; break;
        case 2: rp = 0; gp = c; bp = x; break;
        case 3: rp = 0; gp = x; bp = c; break;
        case 4: rp = x; gp = 0; bp = c; break;
        default: rp = c; gp = 0; bp = x; break;
    }

    r = rp + m; g = gp + m; b = bp + m;
}

// Mouse Wheel Helper for Sliders
// Global flag to prevent viewport zoom when slider uses scroll
static bool sliderConsumedScroll = false;

static bool SliderFloatWithWheel(const char* label, float* v, float v_min, float v_max, const char* format = "%.2f", float wheel_speed = 0.05f) {
    bool changed = ImGui::SliderFloat(label, v, v_min, v_max, format);

    if (ImGui::IsItemHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            float range = v_max - v_min;
            *v += wheel * range * wheel_speed;
            *v = clampf(*v, v_min, v_max);
            changed = true;
            sliderConsumedScroll = true;  // Mark that scroll is used
        }
    }

    return changed;
}

// Color lookup table for optimized color mapping (declared before preset functions)
static std::vector<std::array<unsigned char, 3>> colorLUT;
static const int COLOR_LUT_SIZE = 1024;
static bool colorLUTDirty = true;

// ===================== Proper Colormap Implementation =====================
// RGB lookup tables for accurate scientific colormaps
// Each colormap is defined by key control points that are interpolated

struct ColormapPoint {
    float position;  // 0.0 to 1.0
    float r, g, b;   // RGB values 0.0 to 1.0
};

// Interpolate between colormap control points
static void getColormapColor(const ColormapPoint* points, int numPoints, float value, float& r, float& g, float& b) {
    value = clamp01(value);

    // Find the two points to interpolate between
    if (value <= points[0].position) {
        r = points[0].r;
        g = points[0].g;
        b = points[0].b;
        return;
    }

    if (value >= points[numPoints - 1].position) {
        r = points[numPoints - 1].r;
        g = points[numPoints - 1].g;
        b = points[numPoints - 1].b;
        return;
    }

    for (int i = 0; i < numPoints - 1; i++) {
        if (value >= points[i].position && value <= points[i + 1].position) {
            float t = (value - points[i].position) / (points[i + 1].position - points[i].position);
            r = points[i].r + t * (points[i + 1].r - points[i].r);
            g = points[i].g + t * (points[i + 1].g - points[i].g);
            b = points[i].b + t * (points[i + 1].b - points[i].b);
            return;
        }
    }
}

// Viridis colormap (perceptually uniform)
static const ColormapPoint viridisMap[] = {
    {0.0f, 0.267f, 0.005f, 0.329f},
    {0.25f, 0.283f, 0.141f, 0.458f},
    {0.5f, 0.128f, 0.567f, 0.551f},
    {0.75f, 0.369f, 0.788f, 0.383f},
    {1.0f, 0.993f, 0.906f, 0.144f}
};

// Plasma colormap (perceptually uniform)
static const ColormapPoint plasmaMap[] = {
    {0.0f, 0.051f, 0.029f, 0.528f},
    {0.25f, 0.507f, 0.006f, 0.658f},
    {0.5f, 0.849f, 0.203f, 0.478f},
    {0.75f, 0.966f, 0.544f, 0.235f},
    {1.0f, 0.940f, 0.976f, 0.131f}
};

// Inferno colormap (perceptually uniform)
static const ColormapPoint infernoMap[] = {
    {0.0f, 0.001f, 0.000f, 0.014f},
    {0.25f, 0.258f, 0.039f, 0.407f},
    {0.5f, 0.610f, 0.157f, 0.379f},
    {0.75f, 0.941f, 0.459f, 0.153f},
    {1.0f, 0.988f, 0.998f, 0.645f}
};

// Magma colormap (perceptually uniform)
static const ColormapPoint magmaMap[] = {
    {0.0f, 0.001f, 0.000f, 0.014f},
    {0.25f, 0.282f, 0.088f, 0.472f},
    {0.5f, 0.717f, 0.215f, 0.475f},
    {0.75f, 0.989f, 0.527f, 0.384f},
    {1.0f, 0.987f, 0.991f, 0.750f}
};

// Hot colormap (black-red-orange-yellow-white)
static const ColormapPoint hotMap[] = {
    {0.0f, 0.0f, 0.0f, 0.0f},      // Black
    {0.375f, 1.0f, 0.0f, 0.0f},    // Red
    {0.75f, 1.0f, 1.0f, 0.0f},     // Yellow
    {1.0f, 1.0f, 1.0f, 1.0f}       // White
};

// Cool colormap (cyan-magenta)
static const ColormapPoint coolMap[] = {
    {0.0f, 0.0f, 1.0f, 1.0f},
    {0.5f, 0.5f, 0.5f, 1.0f},
    {1.0f, 1.0f, 0.0f, 1.0f}
};

// Jet colormap (blue-cyan-yellow-red)
static const ColormapPoint jetMap[] = {
    {0.0f, 0.0f, 0.0f, 0.5f},
    {0.125f, 0.0f, 0.0f, 1.0f},
    {0.375f, 0.0f, 1.0f, 1.0f},
    {0.625f, 1.0f, 1.0f, 0.0f},
    {0.875f, 1.0f, 0.0f, 0.0f},
    {1.0f, 0.5f, 0.0f, 0.0f}
};

// Turbo colormap (Google's improved rainbow - accurate definition)
static const ColormapPoint turboMap[] = {
    {0.0f, 0.19f, 0.07f, 0.23f},     // Dark blue
    {0.13f, 0.09f, 0.44f, 0.71f},    // Blue
    {0.25f, 0.11f, 0.64f, 0.85f},    // Light blue
    {0.38f, 0.25f, 0.83f, 0.78f},    // Cyan
    {0.50f, 0.52f, 0.90f, 0.52f},    // Green
    {0.63f, 0.83f, 0.89f, 0.21f},    // Yellow-green
    {0.75f, 0.99f, 0.72f, 0.15f},    // Yellow
    {0.88f, 0.99f, 0.38f, 0.12f},    // Orange
    {1.0f, 0.90f, 0.15f, 0.12f}      // Red
};

// Ocean colormap (green-blue-white)
static const ColormapPoint oceanMap[] = {
    {0.0f, 0.0f, 0.5f, 0.0f},
    {0.5f, 0.0f, 0.5f, 1.0f},
    {1.0f, 1.0f, 1.0f, 1.0f}
};

// Rainbow colormap (spectral order) - adjusted to ensure red is visible
static const ColormapPoint rainbowMap[] = {
    {0.0f, 0.5f, 0.0f, 1.0f},   // Violet
    {0.16f, 0.0f, 0.0f, 1.0f},  // Blue
    {0.33f, 0.0f, 1.0f, 1.0f},  // Cyan
    {0.50f, 0.0f, 1.0f, 0.0f},  // Green
    {0.66f, 1.0f, 1.0f, 0.0f},  // Yellow
    {0.83f, 1.0f, 0.5f, 0.0f},  // Orange
    {1.0f, 1.0f, 0.0f, 0.0f}    // Red
};

// Grayscale
static const ColormapPoint grayscaleMap[] = {
    {0.0f, 0.0f, 0.0f, 0.0f},
    {1.0f, 1.0f, 1.0f, 1.0f}
};

// Ice colormap (blue-cyan-white)
static const ColormapPoint iceMap[] = {
    {0.0f, 0.0f, 0.0f, 0.2f},
    {0.5f, 0.0f, 0.5f, 1.0f},
    {1.0f, 1.0f, 1.0f, 1.0f}
};

// Fire colormap (black-red-orange-yellow)
static const ColormapPoint fireMap[] = {
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.33f, 0.5f, 0.0f, 0.0f},
    {0.66f, 1.0f, 0.5f, 0.0f},
    {1.0f, 1.0f, 1.0f, 0.5f}
};

// Seismic colormap (blue-white-red)
static const ColormapPoint seismicMap[] = {
    {0.0f, 0.0f, 0.0f, 0.3f},
    {0.25f, 0.0f, 0.0f, 1.0f},
    {0.5f, 1.0f, 1.0f, 1.0f},
    {0.75f, 1.0f, 0.0f, 0.0f},
    {1.0f, 0.5f, 0.0f, 0.0f}
};

// Twilight colormap (cyclic)
static const ColormapPoint twilightMap[] = {
    {0.0f, 0.886f, 0.859f, 0.937f},
    {0.25f, 0.329f, 0.153f, 0.533f},
    {0.5f, 0.051f, 0.039f, 0.090f},
    {0.75f, 0.345f, 0.537f, 0.686f},
    {1.0f, 0.886f, 0.859f, 0.937f}
};

// Cividis colormap (colorblind-friendly)
static const ColormapPoint cividisMap[] = {
    {0.0f, 0.0f, 0.135f, 0.304f},
    {0.25f, 0.184f, 0.310f, 0.424f},
    {0.5f, 0.467f, 0.479f, 0.408f},
    {0.75f, 0.796f, 0.674f, 0.424f},
    {1.0f, 0.992f, 0.906f, 0.574f}
};

// Get color from current colormap
static void getCurrentColormapColor(float value, float& r, float& g, float& b) {
    switch (currentColormap) {
        case COLORMAP_VIRIDIS:
            getColormapColor(viridisMap, 5, value, r, g, b);
            break;
        case COLORMAP_PLASMA:
            getColormapColor(plasmaMap, 5, value, r, g, b);
            break;
        case COLORMAP_INFERNO:
            getColormapColor(infernoMap, 5, value, r, g, b);
            break;
        case COLORMAP_MAGMA:
            getColormapColor(magmaMap, 5, value, r, g, b);
            break;
        case COLORMAP_HOT:
            getColormapColor(hotMap, 4, value, r, g, b);
            break;
        case COLORMAP_COOL:
            getColormapColor(coolMap, 3, value, r, g, b);
            break;
        case COLORMAP_JET:
            getColormapColor(jetMap, 6, value, r, g, b);
            break;
        case COLORMAP_TURBO:
            getColormapColor(turboMap, 9, value, r, g, b);
            break;
        case COLORMAP_OCEAN:
            getColormapColor(oceanMap, 3, value, r, g, b);
            break;
        case COLORMAP_RAINBOW:
            getColormapColor(rainbowMap, 7, value, r, g, b);
            break;
        case COLORMAP_GRAYSCALE:
            getColormapColor(grayscaleMap, 2, value, r, g, b);
            break;
        case COLORMAP_ICE:
            getColormapColor(iceMap, 3, value, r, g, b);
            break;
        case COLORMAP_FIRE:
            getColormapColor(fireMap, 4, value, r, g, b);
            break;
        case COLORMAP_SEISMIC:
            getColormapColor(seismicMap, 5, value, r, g, b);
            break;
        case COLORMAP_TWILIGHT:
            getColormapColor(twilightMap, 5, value, r, g, b);
            break;
        case COLORMAP_CIVIDIS:
            getColormapColor(cividisMap, 5, value, r, g, b);
            break;
    }
}

// Apply Theme Function
static void applyTheme(bool dark) {
    ImGuiStyle& style = ImGui::GetStyle();

    if (dark) {
        // Dark theme
        ImGui::StyleColorsDark();
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.12f, 0.95f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.2f, 0.2f, 0.22f, 1.0f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.3f, 0.3f, 0.32f, 1.0f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.15f, 0.15f, 0.17f, 1.0f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.15f, 0.17f, 1.0f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.2f, 0.2f, 0.22f, 1.0f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.4f, 0.6f, 0.9f, 1.0f);
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.5f, 0.7f, 1.0f, 1.0f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.25f, 0.45f, 0.7f, 1.0f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.55f, 0.8f, 1.0f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.2f, 0.4f, 0.65f, 1.0f);

        // Note: Viewport background (bgColor) is NOT changed - user controls it separately
    } else {
        // Light theme
        ImGui::StyleColorsLight();
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.94f, 0.94f, 0.96f, 0.95f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.96f, 0.96f, 0.98f, 1.0f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.88f, 0.88f, 0.9f, 1.0f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.82f, 0.82f, 0.85f, 1.0f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.9f, 0.9f, 0.92f, 1.0f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.9f, 0.9f, 0.92f, 1.0f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.85f, 0.85f, 0.88f, 1.0f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.3f, 0.5f, 0.8f, 1.0f);
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.2f, 0.4f, 0.7f, 1.0f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.4f, 0.6f, 0.9f, 1.0f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.5f, 0.7f, 1.0f, 1.0f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.3f, 0.5f, 0.8f, 1.0f);
        style.Colors[ImGuiCol_Text] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);

        // Note: Viewport background (bgColor) is NOT changed - user controls it separately
    }
}

// Recent files functions
static void addToRecentFiles(const std::string& filepath) {
    // Remove if already exists
    auto it = std::find(recentFiles.begin(), recentFiles.end(), filepath);
    if (it != recentFiles.end()) {
        recentFiles.erase(it);
    }

    // Add to front
    recentFiles.insert(recentFiles.begin(), filepath);

    // Limit size
    if (recentFiles.size() > MAX_RECENT_FILES) {
        recentFiles.resize(MAX_RECENT_FILES);
    }

    // Save to file
    std::ofstream out("recent_files.txt");
    for (const auto& file : recentFiles) {
        out << file << "\n";
    }
}

static void loadRecentFiles() {
    std::ifstream in("recent_files.txt");
    std::string line;
    while (std::getline(in, line) && recentFiles.size() < MAX_RECENT_FILES) {
        if (!line.empty()) {
            recentFiles.push_back(line);
        }
    }
}

static void setPerspective(float fovyDeg, float aspect, float zNear, float zFar) {
    float fovyRad = fovyDeg * (float)M_PI / 180.0f;
    float top = zNear * std::tan(fovyRad * 0.5f);
    float bottom = -top;
    float right = top * aspect;
    float left = -right;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(left, right, bottom, top, zNear, zFar);
}

static void buildFrequencyMapping() {
    const float sr = (float)wavFile.sampleRate;
    const float maxFreq = sr * 0.5f;
    const float minF = std::max(MIN_FREQ, 1.0f);
    const float maxF = std::max(minF * 1.001f, maxFreq);
    const float ratio = maxF / minF;

    for (int i = 0; i < NUM_BARS; i++) {
        float t = (NUM_BARS == 1) ? 0.0f : (float)i / (float)(NUM_BARS - 1);
        float freq = minF * std::pow(ratio, t);
        float binF = freq * (float)FFT_SIZE / sr;
        binF = std::max(1.0f, std::min(binF, (float)(getNumFrequencies() - 2)));

        gBarBinF[i] = binF;
        gBarX[i] = (-X_SPAN * 0.5f) + t * X_SPAN;
        gBarHue[i] = t * 0.66f;
    }
}

// ===================== Audio Callback =====================
static int audioCallback(const void* inputBuffer, void* outputBuffer,
                         unsigned long framesPerBuffer,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void* userData) {

    float* out = (float*)outputBuffer;
    const bool paused = isPaused.load(std::memory_order_relaxed);

    uint64_t pos = playbackPosition.load(std::memory_order_relaxed);
    const size_t N = wavFile.audioData.size();
    const int outCh = std::max(1, gOutputChannels);

    for (unsigned long i = 0; i < framesPerBuffer; i++) {
        if (paused || N == 0) {
            for (int ch = 0; ch < outCh; ch++) out[i * outCh + ch] = 0.0f;
            continue;
        }

        if (pos < N) {
            float s = wavFile.audioData[(size_t)pos] * volume; // ONE mono sample
            for (int ch = 0; ch < outCh; ch++) out[i * outCh + ch] = s;
            pos += 1; // IMPORTANT: advance by 1 sample per frame
        } else {
            if (loopAudio) {
                pos = 0;
                float s = wavFile.audioData[0] * volume;
                for (int ch = 0; ch < outCh; ch++) out[i * outCh + ch] = s;
                pos += 1;
            } else {
                for (int ch = 0; ch < outCh; ch++) out[i * outCh + ch] = 0.0f;
            }
        }
    }

    playbackPosition.store(pos, std::memory_order_relaxed);
    return paContinue;
}

// ===================== FFT Processing =====================
static void processAudioFrameSynced() {
    if (wavFile.audioData.empty()) return;

    int64_t writeHead = (int64_t)playbackPosition.load(std::memory_order_relaxed);
    int64_t latencySamples = (int64_t)(gLatencySamplesBase + gLatencyAdjust);

    // CRITICAL FIX: The FFT window analyzes audio from playHead to playHead+FFT_SIZE
    // The frequency content represents the CENTER of this window, not the end
    // So it needs to shift back by half the FFT window to sync with audio
    int64_t fftWindowCenter = FFT_SIZE / 2;
    int64_t playHeadEstimate = writeHead - latencySamples - fftWindowCenter;

    const size_t N = wavFile.audioData.size();

    if (N < (size_t)FFT_SIZE) {
        for (int i = 0; i < FFT_SIZE; i++) fftInput[i] = 0.0;
        for (size_t i = 0; i < N; i++) {
            double w = 0.5 * (1.0 - std::cos(2.0 * M_PI * (double)i / (FFT_SIZE - 1)));
            fftInput[i] = (double)wavFile.audioData[i] * w;
        }
    } else {
        size_t start = wrapIndex(playHeadEstimate, N);
        for (int i = 0; i < FFT_SIZE; i++) {
            double w = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (FFT_SIZE - 1)));
            size_t idx = (start + (size_t)i) % N;
            fftInput[i] = (double)wavFile.audioData[idx] * w;
        }
    }

    fftw_execute(fftPlan);

    for (int i = 0; i < getNumFrequencies(); i++) {
        double re = fftOutput[i][0];
        double im = fftOutput[i][1];
        magnitudes[i] = (float)(std::sqrt(re * re + im * im) / FFT_SIZE);
    }
}

static void buildCurrentLine() {
    const float logDen = std::log10(1.0f + MAG_GAIN);

    for (int i = 0; i < NUM_BARS; i++) {
        float binF = gBarBinF[i];
        int bin0 = (int)std::floor(binF);
        float frac = binF - (float)bin0;

        bin0 = std::max(0, std::min(getNumFrequencies() - 2, bin0));
        int bin1 = bin0 + 1;

        float m = magnitudes[bin0] * (1.0f - frac) + magnitudes[bin1] * frac;
        float v = std::log10(1.0f + m * MAG_GAIN) / logDen;
        currentLine[i] = clamp01(v);
    }
}

static void pushLineToHistory() {
    std::memmove(&lineHistory[NUM_BARS], &lineHistory[0],
                 sizeof(float) * (HISTORY_LINES - 1) * NUM_BARS);
    std::memcpy(&lineHistory[0], &currentLine[0], sizeof(float) * NUM_BARS);

    // Track how many lines have been filled (up to HISTORY_LINES)
    if (historyFillCount < HISTORY_LINES) {
        historyFillCount++;
    }
}

// ===================== Rendering =====================
static void render3DWaterfall(int vpX, int vpY, int vpW, int vpH) {
    // CRITICAL: Disable blend first, ImGui might leave it on
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);

    // Viewport is already set and scissor enabled from main loop
    glViewport(vpX, vpY, vpW, vpH);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float aspect = (float)vpW / (float)vpH;
    setPerspective(55.0f, aspect, 0.05f, 100.0f);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glTranslatef(0.0f, 0.0f, -gDist);
    glRotatef(gPitch, 1.0f, 0.0f, 0.0f);
    glRotatef(gYaw, 0.0f, 1.0f, 0.0f);
    glTranslatef(0.0f, yOffset, 0.0f);  // Use variable Y offset

    // Draw grid if enabled
    if (showGrid) {
        const float gridSpanMul = 1.75f;
        const int gridDivs = 12;

        const float gridX = X_SPAN * gridSpanMul;
        const float gridZ = Z_SPAN * gridSpanMul;

        const float gridY = -0.5f;   // <--- LOWER the grid (more negative = lower)

        glColor4f(0.10f, 0.10f, 0.12f, 1.0f);
        glBegin(GL_LINES);

        for (int i = 0; i <= gridDivs; i++) {
            float t = (float)i / (float)gridDivs;

            float x = -gridX * 0.5f + t * gridX;
            glVertex3f(x, gridY, -gridZ * 0.5f);
            glVertex3f(x, gridY,  gridZ * 0.5f);

            float z = -gridZ * 0.5f + t * gridZ;
            glVertex3f(-gridX * 0.5f, gridY, z);
            glVertex3f( gridX * 0.5f, gridY, z);
        }

        glEnd();
    }

    glLineWidth(lineWidth);  // Use variable line width

    // Waterfall lines
    for (int row = 0; row < HISTORY_LINES; row++) {
        float tRow = (HISTORY_LINES == 1) ? 0.0f : (float)row / (float)(HISTORY_LINES - 1);
        float z = (Z_SPAN * 0.5f) - tRow * Z_SPAN;
        float ageFade = 1.0f - 0.75f * tRow;

        glBegin(GL_LINE_STRIP);
        for (int i = 0; i < NUM_BARS; i++) {
            float v = lineHistory[row * NUM_BARS + i];
            float x = gBarX[i];
            float y = v * yScale;  // Use variable Y scale instead of constant

            float r, g, b;
            
            if (useCustomLineColor) {
                // Use custom solid color
                r = lineColor[0];
                g = lineColor[1];
                b = lineColor[2];
            } else {
                // Use colormap
                // Apply extremely aggressive power curve to absolutely reach end of colormap
                float vGamma = std::pow(v, 0.2f);

                // Get pure color from colormap
                getCurrentColormapColor(vGamma, r, g, b);

                // Apply strong saturation boost for vibrant colors
                float saturationBoost = 1.0f;  // 1.5 = 50% more saturated

                float maxC = std::max(r, std::max(g, b));
                float minC = std::min(r, std::min(g, b));
                float delta = maxC - minC;

                if (delta > 0.001f) {
                    float chroma = delta * saturationBoost;
                    chroma = std::min(chroma, maxC);
                    float scale = chroma / delta;
                    r = minC + (r - minC) * scale;
                    g = minC + (g - minC) * scale;
                    b = minC + (b - minC) * scale;
                }

                // Apply color fade for depth perception
                float colorFade = 1.0f - 0.75f * tRow;
                r *= colorFade;
                g *= colorFade;
                b *= colorFade;
            }

            // Opacity fades from full (1.0) at front to more transparent at back
            float a = 1.0f - 0.8f * tRow;

            glColor4f(r, g, b, a);
            glVertex3f(x, y, z);
        }
        glEnd();
    }

    glDisable(GL_BLEND);
}

// Pre-compute color lookup table
void updateColorLUT() {
    if (!colorLUTDirty) return;

    colorLUT.resize(COLOR_LUT_SIZE);
    for (int i = 0; i < COLOR_LUT_SIZE; i++) {
        float v = (float)i / (float)(COLOR_LUT_SIZE - 1);

        // Apply extremely aggressive power curve to absolutely reach end of colormap
        // 0.2 pushes even 50% signals to 87% through the colormap
        v = std::pow(v, 0.2f);

        // Get pure color from colormap
        float r, g, b;
        getCurrentColormapColor(v, r, g, b);

        // Apply strong saturation boost to maintain vibrancy despite aggressive gamma
        float saturationBoost = 1.5f;  // 50% more saturated for vibrant colors

        // Convert to HSV-like saturation adjustment
        float maxC = std::max(r, std::max(g, b));
        float minC = std::min(r, std::min(g, b));
        float delta = maxC - minC;

        if (delta > 0.001f) {
            // Boost saturation
            float chroma = delta * saturationBoost;
            chroma = std::min(chroma, maxC);  // Clamp to prevent overflow

            // Reconstruct RGB with boosted saturation
            float scale = chroma / delta;
            r = minC + (r - minC) * scale;
            g = minC + (g - minC) * scale;
            b = minC + (b - minC) * scale;
        }

        // Convert directly to unsigned char
        colorLUT[i][0] = (unsigned char)(clamp01(r) * 255.0f);
        colorLUT[i][1] = (unsigned char)(clamp01(g) * 255.0f);
        colorLUT[i][2] = (unsigned char)(clamp01(b) * 255.0f);
    }
    colorLUTDirty = false;
}

// Waveform data cache
static std::vector<float> waveformMinCache;
static std::vector<float> waveformMaxCache;
static int waveformCacheWidth = 0;
static bool waveformCacheDirty = true;

// Pre-compute waveform min/max values for efficient rendering
static void updateWaveformCache(int width) {
    if (wavFile.audioData.empty()) return;

    // Only rebuild if width changed or data changed
    if (waveformCacheWidth == width && !waveformCacheDirty) return;

    waveformCacheWidth = width;
    waveformCacheDirty = false;

    waveformMinCache.resize(width + 1);
    waveformMaxCache.resize(width + 1);

    const size_t totalSamples = wavFile.audioData.size();
    const int downsample = std::max(1, (int)(totalSamples / (size_t)std::max(1, width)));

    // Pre-compute min/max for each pixel column
    for (int x = 0; x <= width; x++) {
        size_t sampleIdx = (size_t)(((float)x / (float)std::max(1, width)) * (float)totalSamples);
        if (sampleIdx >= totalSamples) sampleIdx = totalSamples - 1;

        float minVal = 0.0f, maxVal = 0.0f;
        for (int i = 0; i < downsample && sampleIdx + (size_t)i < totalSamples; i++) {
            float sample = wavFile.audioData[sampleIdx + (size_t)i];
            minVal = std::min(minVal, sample);
            maxVal = std::max(maxVal, sample);
        }

        waveformMinCache[x] = minVal;
        waveformMaxCache[x] = maxVal;
    }
}

// Render waveform display centered at bottom of viewport
static void renderWaveform(int vpX, int vpY, int vpW, int vpH) {
    if (wavFile.audioData.empty()) return;

    const int waveformHeight = 80;  // Height of waveform strip
    const int waveformMaxWidth = 800;  // Maximum width for waveform
    const int bottomMargin = 10;  // Distance from bottom edge

    // Calculate actual waveform width (cap at max width)
    int waveformWidth = std::min(waveformMaxWidth, vpW - 40);

    // Center horizontally in viewport using the viewport's center point
    const int viewportCenterX = vpX + vpW / 2;
    const int waveformX = viewportCenterX - waveformWidth / 2;

    // Position at bottom of viewport with margin
    const int waveformY = vpY + bottomMargin;

    // Update waveform cache if needed
    updateWaveformCache(waveformWidth);

    // Use framebuffer coordinates consistently: viewport must be full framebuffer
    GLFWwindow* ctx = glfwGetCurrentContext();
    int fbW, fbH;
    glfwGetFramebufferSize(ctx, &fbW, &fbH);

    // Save current viewport and scissor state so it can restore after
    GLint oldViewport[4];
    glGetIntegerv(GL_VIEWPORT, oldViewport);

    GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    GLint oldScissor[4];
    glGetIntegerv(GL_SCISSOR_BOX, oldScissor);

    // IMPORTANT: disable scissor so the overlay isn't clipped to the spectrogram area
    // and set viewport to full framebuffer
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, fbW, fbH);

    // Switch to 2D orthographic projection for waveform (matches viewport)
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, fbW, 0, fbH, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Draw semi-transparent background
    glColor4f(0.0f, 0.0f, 0.0f, 0.6f);
    glBegin(GL_QUADS);
    glVertex2f((float)waveformX, (float)waveformY);
    glVertex2f((float)waveformX + waveformWidth, (float)waveformY);
    glVertex2f((float)waveformX + waveformWidth, (float)waveformY + waveformHeight);
    glVertex2f((float)waveformX, (float)waveformY + waveformHeight);
    glEnd();

    // Draw centerline
    float centerY = waveformY + waveformHeight * 0.5f;
    glColor4f(0.3f, 0.3f, 0.3f, 0.5f);
    glBegin(GL_LINES);
    glVertex2f((float)waveformX, centerY);
    glVertex2f((float)waveformX + waveformWidth, centerY);
    glEnd();

    // Draw filled waveform using cached data (OPTIMIZED)
    const float scale = 0.45f;

    glColor4f(0.2f, 0.6f, 1.0f, 0.7f);  // Brighter blue, more opaque
    glBegin(GL_QUAD_STRIP);
    for (int x = 0; x <= waveformWidth; x++) {
        float yTop = centerY + waveformMaxCache[x] * waveformHeight * scale;
        float yBottom = centerY + waveformMinCache[x] * waveformHeight * scale;

        glVertex2f((float)waveformX + x, yTop);
        glVertex2f((float)waveformX + x, yBottom);
    }
    glEnd();

    // Draw top outline using cached data (OPTIMIZED)
    glColor4f(0.3f, 0.8f, 1.0f, 0.9f);
    glLineWidth(1.5f);
    glBegin(GL_LINE_STRIP);
    for (int x = 0; x <= waveformWidth; x++) {
        float yTop = centerY + waveformMaxCache[x] * waveformHeight * scale;
        glVertex2f((float)waveformX + x, yTop);
    }
    glEnd();

    // Draw bottom outline using cached data (OPTIMIZED)
    glBegin(GL_LINE_STRIP);
    for (int x = 0; x <= waveformWidth; x++) {
        float yBottom = centerY + waveformMinCache[x] * waveformHeight * scale;
        glVertex2f((float)waveformX + x, yBottom);
    }
    glEnd();
    glLineWidth(1.0f);

    // Draw playback position line
    if (isPlaying || isPaused.load()) {
        uint64_t pos = playbackPosition.load(std::memory_order_relaxed);

        // Account for audio latency to sync with what is heard
        int64_t adjustedPos = (int64_t)pos - (int64_t)(gLatencySamplesBase + gLatencyAdjust);
        if (adjustedPos < 0) adjustedPos = 0;

        const size_t totalSamples = wavFile.audioData.size(); // MONO samples
        float progress = (totalSamples > 0) ? (float)adjustedPos / (float)totalSamples : 0.0f;
        float posX = waveformX + progress * waveformWidth;

        glLineWidth(3.0f);
        glColor4f(1.0f, 0.2f, 0.2f, 0.95f);  // Bright red position line
        glBegin(GL_LINES);
        glVertex2f(posX, (float)waveformY);
        glVertex2f(posX, (float)waveformY + waveformHeight);
        glEnd();
        glLineWidth(1.0f);
    }

    // Draw border
    glColor4f(0.5f, 0.5f, 0.5f, 0.7f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f((float)waveformX, (float)waveformY);
    glVertex2f((float)waveformX + waveformWidth, (float)waveformY);
    glVertex2f((float)waveformX + waveformWidth, (float)waveformY + waveformHeight);
    glVertex2f((float)waveformX, (float)waveformY + waveformHeight);
    glEnd();
    glLineWidth(1.0f);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);

    // Restore projection matrices
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

    // Restore viewport and scissor state
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);

    if (scissorWasEnabled) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(oldScissor[0], oldScissor[1], oldScissor[2], oldScissor[3]);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
}

// ===================== OPTIMIZED: Traditional 2D Spectrogram Rendering =====================
// Initialize or resize texture
void initSpectrogramTexture(int width, int height) {
    if (spectrogramTexture == 0) {
        glGenTextures(1, &spectrogramTexture);
    }

    if (texWidth != width || texHeight != height) {
        texWidth = width;
        texHeight = height;
        textureData.resize((size_t)width * (size_t)height * 3); // RGB format

        glBindTexture(GL_TEXTURE_2D, spectrogramTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    }
}

// OPTIMIZED: Texture-based rendering (10-50x faster than the old quad-based method)
static void renderTraditionalSpectrogram(int vpX, int vpY, int vpW, int vpH) {
    // Limit texture width for performance (optional but recommended for very wide viewports)
    const int maxTexWidth = 2048;
    int texW = std::min(vpW, maxTexWidth);
    int texH = NUM_BARS;

    // Initialize texture if needed
    initSpectrogramTexture(texW, texH);

    // Update color LUT if color settings changed
    updateColorLUT();

    int effectiveHistory = historyFillCount;
    if (effectiveHistory <= 0) effectiveHistory = 1;
    if (effectiveHistory > HISTORY_LINES) effectiveHistory = HISTORY_LINES;
    const int historyToUse = HISTORY_LINES;

    // Build texture data on CPU using color LUT (OPTIMIZED)
    // Map time so newest data (histIdx=0) appears on the RIGHT edge (matching audio playback)
    for (int px = 0; px < texW; px++) {
        // Reverse mapping: px=0 is oldest (left), px=texW-1 is newest (right)
        float t = (texW <= 1) ? 0.0f : (float)px / (float)(texW - 1);
        int histIdx = (historyToUse <= 1) ? 0 : (int)std::lround(t * (float)(historyToUse - 1));
        histIdx = std::max(0, std::min(histIdx, HISTORY_LINES - 1));

        for (int i = 0; i < NUM_BARS; i++) {
            float v = lineHistory[histIdx * NUM_BARS + i];

            // Use color LUT instead of calculating (FAST)
            int lutIdx = (int)(v * (COLOR_LUT_SIZE - 1));
            lutIdx = std::max(0, std::min(lutIdx, COLOR_LUT_SIZE - 1));

            // Store in texture data
            int texIdx = (i * texW + px) * 3;
            textureData[(size_t)texIdx + 0] = colorLUT[(size_t)lutIdx][0];
            textureData[(size_t)texIdx + 1] = colorLUT[(size_t)lutIdx][1];
            textureData[(size_t)texIdx + 2] = colorLUT[(size_t)lutIdx][2];
        }
    }

    // Upload texture to GPU (single fast DMA transfer instead of millions of vertices)
    glBindTexture(GL_TEXTURE_2D, spectrogramTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texW, texH, GL_RGB, GL_UNSIGNED_BYTE, textureData.data());

    // Set up rendering state
    int windowWidth = 0, windowHeight = 0;
    GLFWwindow* ctx = glfwGetCurrentContext();
    glfwGetWindowSize(ctx, &windowWidth, &windowHeight);

    glViewport(0, 0, windowWidth, windowHeight);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vpX, vpY, vpW, vpH);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, windowWidth, 0, windowHeight, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    // Draw SINGLE textured quad (instead of millions of quads!)
    // Standard texture coordinates: low freq (row 0) at bottom, high freq at top
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f((float)vpX, (float)vpY);                    // bottom-left
    glTexCoord2f(1.0f, 0.0f); glVertex2f((float)vpX + vpW, (float)vpY);              // bottom-right
    glTexCoord2f(1.0f, 1.0f); glVertex2f((float)vpX + vpW, (float)vpY + vpH);        // top-right
    glTexCoord2f(0.0f, 1.0f); glVertex2f((float)vpX, (float)vpY + vpH);              // top-left
    glEnd();

    glDisable(GL_TEXTURE_2D);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

    glDisable(GL_SCISSOR_TEST);
}

// ===================== Audio Control =====================
void startAudio() {
    if (wavFile.audioData.empty()) return;
    if (audioStream) return;

    // Output mono for mono sources; stereo for anything else (duplicate mono to L/R)
    gOutputChannels = (wavFile.sourceChannels >= 2) ? 2 : 1;

    PaError err = Pa_OpenDefaultStream(&audioStream,
                                       0,
                                       gOutputChannels,
                                       paFloat32,
                                       wavFile.sampleRate,
                                       FRAMES_PER_BUFFER,
                                       audioCallback,
                                       nullptr);
    if (err == paNoError) {
        err = Pa_StartStream(audioStream);
        if (err == paNoError) {
            isPlaying = true;
            isPaused.store(false, std::memory_order_relaxed);

            const PaStreamInfo* info = Pa_GetStreamInfo(audioStream);
            double outLatencySec = info ? info->outputLatency : 0.0;
            int outLatencySamples = (int)std::llround(outLatencySec * (double)wavFile.sampleRate);

            // playbackPosition is mono samples; latency is in frames -> same unit here.
            gLatencySamplesBase = outLatencySamples + FRAMES_PER_BUFFER;
        }
    }
}

void stopAudio() {
    if (audioStream) {
        Pa_StopStream(audioStream);
        Pa_CloseStream(audioStream);
        audioStream = nullptr;
    }
    isPlaying = false;
    isPaused.store(false, std::memory_order_relaxed);
    playbackPosition.store(0, std::memory_order_relaxed);
}

// ===================== Main =====================
int main(int argc, char* argv[]) {
    // Load FFTW wisdom for optimized performance
    fftw_import_wisdom_from_filename("fftw_wisdom.dat");

    // Initialize FFTW
    fftInput = (double*)fftw_malloc(sizeof(double) * FFT_SIZE);
    fftOutput = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (FFT_SIZE / 2 + 1));
    fftPlan = fftw_plan_dft_r2c_1d(FFT_SIZE, fftInput, fftOutput, FFTW_ESTIMATE);

    // Save FFTW wisdom for future runs
    fftw_export_wisdom_to_filename("fftw_wisdom.dat");

    // Initialize magnitudes vector
    magnitudes.resize(getNumFrequencies(), 0.0f);

    // Load recent files list
    loadRecentFiles();

    // Initialize PortAudio
    Pa_Initialize();

    // Initialize CPU usage tracking
    initCPUUsage();

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }

    // CRITICAL: Request compatibility profile for legacy OpenGL (glBegin/glEnd)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);  // COMPATIBILITY not CORE
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);  // Start maximized

    // Get primary monitor to determine good default size
    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);

    // Create window with reasonable default size (will be maximized anyway)
    // Use 80% of screen size as default for when user un-maximizes
    int defaultWidth = (int)(mode->width * 0.8f);
    int defaultHeight = (int)(mode->height * 0.8f);

    GLFWwindow* window = glfwCreateWindow(defaultWidth, defaultHeight,
                                          "3D Real Time Spectrogram", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return -1;
    }

    // Set window icon (looks for icon.png next to the .exe)
    setWindowIcon(window, "icon.png");

    // Set up drag & drop callback
    glfwSetDropCallback(window, [](GLFWwindow* win, int count, const char** paths) {
        if (count > 0) {
            // Load the first dropped file
            stopAudio();
            strncpy(filePathBuffer, paths[0], sizeof(filePathBuffer)-1);
            filePathBuffer[sizeof(filePathBuffer)-1] = '\0';

            if (wavFile.load(paths[0])) {
                buildFrequencyMapping();
                waveformCacheDirty = true;  // Mark waveform cache as dirty
                loadedFileName = std::string(paths[0]);
                size_t pos = loadedFileName.find_last_of("/\\");
                if (pos != std::string::npos) {
                    loadedFileName = loadedFileName.substr(pos + 1);
                }
                addToRecentFiles(paths[0]);
            }
        }
    });

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // VSync on - limits to 60 FPS, saves CPU

    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW\n";
        return -1;
    }

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // Apply initial theme
    applyTheme(useDarkTheme);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Set scroll callback AFTER ImGui
    // Handle ImGui scrolling manually in main loop to avoid conflicts with slider scrolling
    glfwSetScrollCallback(window, [](GLFWwindow* win, double xoff, double yoff) {
        // Store scroll for manual handling in main loop
        ImGuiIO& io = ImGui::GetIO();
        io.MouseWheelH += (float)xoff;
        io.MouseWheel += (float)yoff;
    });

    // Keyboard handled in main loop
    bool spacePressed = false, rPressed = false, cPressed = false;

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Calculate viewport dimensions first
        // This is fully dynamic - works at any window/screen size
        int windowWidth, windowHeight;
        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        // Calculate viewport dimensions based on compact mode
        int sidebarWidth = compactMode ? 60 : 300;  // 60px for icons, 300px for full
        viewportX = sidebarWidth;
        viewportY = 0;
        viewportW = windowWidth - sidebarWidth;  // Viewport fills remaining width
        viewportH = windowHeight;                // Viewport uses full height

        // Handle mouse input manually for viewport
        ImGuiIO& io = ImGui::GetIO();
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);

        // Mouse button handling for viewport rotation (only in 3D mode)
        if (!useTraditionalView && !io.WantCaptureMouse && mx >= viewportX) {
            if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
                if (!gDragging) {
                    gDragging = true;
                    gLastX = mx;
                    gLastY = my;
                } else {
                    double dx = mx - gLastX;
                    double dy = my - gLastY;
                    gLastX = mx;
                    gLastY = my;

                    gYaw += (float)dx * 0.25f;
                    gPitch += (float)dy * 0.25f;
                    gPitch = clampf(gPitch, -89.0f, 89.0f);
                    needsRedraw = true;  // Camera moved, need redraw
                }
            } else {
                gDragging = false;
            }

            // Scroll handling for viewport zoom (only if slider didn't use it)
            if (io.MouseWheel != 0.0f && !sliderConsumedScroll) {
                gDist -= io.MouseWheel * 0.18f;
                gDist = clampf(gDist, 1.4f, 12.0f);
                needsRedraw = true;  // Zoom changed, need redraw
            }
        } else {
            gDragging = false;
        }

        // Keyboard handling
        // SPACE - Play/Pause/Start
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && !spacePressed) {
            if (isPlaying) {
                // Toggle pause if already playing
                bool nowPaused = !isPaused.load(std::memory_order_relaxed);
                isPaused.store(nowPaused, std::memory_order_relaxed);
            } else if (!wavFile.audioData.empty()) {
                // Start playback if not playing
                startAudio();
            }
            spacePressed = true;
        }
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_RELEASE) spacePressed = false;

        // R - Restart playback
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS && !rPressed && isPlaying) {
            playbackPosition.store(0, std::memory_order_relaxed);
            rPressed = true;
        }
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_RELEASE) rPressed = false;

        // C - Reset camera (3D view only)
        if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS && !cPressed) {
            gYaw = -45.0f;
            gPitch = 35.0f;
            gDist = 9.0f;
            cPressed = true;
        }
        if (glfwGetKey(window, GLFW_KEY_C) == GLFW_RELEASE) cPressed = false;

        // F - Toggle fullscreen (only if not typing in UI)
        static bool fPressed = false;
        if (!ImGui::GetIO().WantCaptureKeyboard &&
            glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS && !fPressed) {
            if (!isFullscreen) {
                GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
                isFullscreen = true;
            } else {
                glfwSetWindowMonitor(window, nullptr, 100, 100, 1400, 800, 0);
                isFullscreen = false;
            }
            fPressed = true;
        }
        if (glfwGetKey(window, GLFW_KEY_F) == GLFW_RELEASE) fPressed = false;

        // M - Toggle mute
        static bool mPressed = false;
        if (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS && !mPressed) {
            if (!isMuted) {
                volumeBeforeMute = volume;
                volume = 0.0f;
                isMuted = true;
            } else {
                volume = volumeBeforeMute;
                isMuted = false;
            }
            mPressed = true;
        }
        if (glfwGetKey(window, GLFW_KEY_M) == GLFW_RELEASE) mPressed = false;

        // + or = - Volume up
        static bool plusPressed = false;
        if ((glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS ||
             glfwGetKey(window, GLFW_KEY_KP_ADD) == GLFW_PRESS) && !plusPressed) {
            volume = clampf(volume + 0.05f, 0.0f, 1.0f);
            if (isMuted && volume > 0.0f) {
                isMuted = false;  // Unmute if increasing from muted state
            }
            plusPressed = true;
        }
        if (glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_RELEASE &&
            glfwGetKey(window, GLFW_KEY_KP_ADD) == GLFW_RELEASE) plusPressed = false;

        // - or _ - Volume down
        static bool minusPressed = false;
        if ((glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS ||
             glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS) && !minusPressed) {
            volume = clampf(volume - 0.05f, 0.0f, 1.0f);
            minusPressed = true;
        }
        if (glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_RELEASE &&
            glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_RELEASE) minusPressed = false;

        // LEFT ARROW - Seek backward 5 seconds (only if not typing in UI)
        static bool leftPressed = false;
        if (!ImGui::GetIO().WantCaptureKeyboard &&
            glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS && !leftPressed && !wavFile.audioData.empty()) {
            int64_t currentPos = (int64_t)playbackPosition.load(std::memory_order_relaxed);
            int64_t seekAmount = (int64_t)wavFile.sampleRate * 5;  // 5 seconds (mono samples)
            int64_t newPos = std::max((int64_t)0, currentPos - seekAmount);
            playbackPosition.store((uint64_t)newPos, std::memory_order_relaxed);
            leftPressed = true;
        }
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_RELEASE) leftPressed = false;

        // RIGHT ARROW - Seek forward 5 seconds (only if not typing in UI)
        static bool rightPressed = false;
        if (!ImGui::GetIO().WantCaptureKeyboard &&
            glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS && !rightPressed && !wavFile.audioData.empty()) {
            int64_t currentPos = (int64_t)playbackPosition.load(std::memory_order_relaxed);
            int64_t seekAmount = (int64_t)wavFile.sampleRate * 5;  // 5 seconds (mono samples)
            int64_t newPos = std::min((int64_t)wavFile.audioData.size(), currentPos + seekAmount);
            playbackPosition.store((uint64_t)newPos, std::memory_order_relaxed);
            rightPressed = true;
        }
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_RELEASE) rightPressed = false;

        // 1-9 - Jump to 10%, 20%, ..., 90% (both top row and numpad)
        // Only if not typing in UI
        if (!ImGui::GetIO().WantCaptureKeyboard) {
            static bool numPressed[9] = {false};
            for (int i = 0; i < 9; i++) {
                int keyTop = GLFW_KEY_1 + i;      // Top row number keys
                int keyNumpad = GLFW_KEY_KP_1 + i; // Numpad keys
                bool pressed = (glfwGetKey(window, keyTop) == GLFW_PRESS ||
                               glfwGetKey(window, keyNumpad) == GLFW_PRESS);

                if (pressed && !numPressed[i] && !wavFile.audioData.empty()) {
                    float percent = (i + 1) * 0.1f;  // 1 = 10%, 2 = 20%, etc.
                    uint64_t newPos = (uint64_t)((double)wavFile.audioData.size() * (double)percent);
                    playbackPosition.store(newPos, std::memory_order_relaxed);
                    numPressed[i] = true;
                }

                bool released = (glfwGetKey(window, keyTop) == GLFW_RELEASE &&
                                glfwGetKey(window, keyNumpad) == GLFW_RELEASE);
                if (released) numPressed[i] = false;
            }
        }

        // ESC key to exit fullscreen
        static bool escPressed = false;
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !escPressed && isFullscreen) {
            glfwSetWindowMonitor(window, nullptr, 100, 100, 1400, 800, 0);
            isFullscreen = false;
            escPressed = true;
        }
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_RELEASE) escPressed = false;

        // TAB key to toggle compact mode
        static bool tabPressed = false;
        if (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS && !tabPressed) {
            compactMode = !compactMode;
            tabPressed = true;
        }
        if (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_RELEASE) tabPressed = false;

        // Auto-rotate camera if enabled (only in 3D mode)
        if (autoRotate && !useTraditionalView) {
            gYaw += 0.05f;  // Rotate at 0.3 degrees per frame
            if (gYaw >= 360.0f) gYaw -= 360.0f;
            needsRedraw = true;  // Camera rotating, need redraw
        }

        // Calculate FPS and CPU (update 10x per second)
        double currentTime = glfwGetTime();
        frameCount++;
        if (currentTime - lastFPSTime >= 0.1) {  // Update every 0.1 seconds instead of 1.0
            currentFPS = (float)(frameCount / (currentTime - lastFPSTime));
            currentCPU = (float)getCurrentCPUUsage();
            frameCount = 0;
            lastFPSTime = currentTime;
        }

        if (!isPaused.load(std::memory_order_relaxed) && isPlaying) {
            processAudioFrameSynced();
            buildCurrentLine();
            pushLineToHistory();
            needsRedraw = true;  // New audio data, need redraw
        } else {
            // When paused/stopped, reduce update rate to save CPU
            // Sleep for a bit to lower frame rate when idle
            if (!isPlaying && !needsRedraw) {
                std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS when idle
            }
        }

        // Clear everything first
        glClearColor(bgColor[0], bgColor[1], bgColor[2], 1.0f);  // Use variable background color
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // FIRST: Render ImGui (sidebar)
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Reset slider scroll flag each frame
        sliderConsumedScroll = false;

        // Sidebar (sidebarWidth already calculated above)
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)sidebarWidth, (float)windowHeight));

        // Disable automatic mouse wheel scrolling - we'll handle it manually
        // Add NoTitleBar flag in compact mode to hide "Controls" label
        ImGuiWindowFlags sidebarFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollWithMouse;
        if (compactMode) {
            sidebarFlags |= ImGuiWindowFlags_NoTitleBar;
        }
        ImGui::Begin("Controls", nullptr, sidebarFlags);

        if (compactMode) {
            // ============ COMPACT MODE - Icons Only ============
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 8));

            // Toggle compact mode button
            if (ImGui::Button(">>", ImVec2(44, 30))) {
                compactMode = false;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Expand sidebar");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Play/Pause/Stop
            if (!isPlaying) {
                if (ImGui::Button(">", ImVec2(44, 44))) {
                    startAudio();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play (Space)");
            } else {
                if (isPaused.load()) {
                    if (ImGui::Button(">", ImVec2(44, 44))) {
                        isPaused.store(false, std::memory_order_relaxed);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Resume (Space)");
                } else {
                    if (ImGui::Button("||", ImVec2(44, 44))) {
                        isPaused.store(true, std::memory_order_relaxed);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pause (Space)");
                }
            }

            if (ImGui::Button("[]", ImVec2(44, 44))) {
                stopAudio();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop");

            ImGui::Spacing();

            // Volume indicator
            ImGui::PushItemWidth(44);
            float tempVol = volume;
            if (ImGui::VSliderFloat("##vol", ImVec2(44, 100), &tempVol, 0.0f, 1.0f, "")) {
                volume = tempVol;
                if (isMuted && volume > 0.0f) {
                    isMuted = false;
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Volume: %.0f%%", volume * 100.0f);
            ImGui::PopItemWidth();

            ImGui::Spacing();

            // 2D/3D toggle
            bool temp2D = useTraditionalView;
            if (ImGui::Checkbox("##2d", &temp2D)) {
                // Trigger the same logic as the full sidebar
                useTraditionalView = temp2D;
                if (useTraditionalView) {
                    saved3D_FFT_SIZE = FFT_SIZE;
                    saved3D_HISTORY_LINES = HISTORY_LINES;
                    saved3D_lineWidth = lineWidth;
                    saved3D_yScale = yScale;
                    saved3D_yOffset = yOffset;
                    saved3D_showGrid = showGrid;
                    saved3D_autoRotate = autoRotate;

                    // Save color settings
                    saved3D_useCustomLineColor = useCustomLineColor;
                    saved3D_useColormap = useColormap;
                    saved3D_currentColormap = currentColormap;
                    
                    // Enable colormap for heat map
                    useColormap = true;
                    useCustomLineColor = false;

                    FFT_SIZE = 16384;  // 32x
                    HISTORY_LINES = 560;  // Maximum history
                    lineWidth = 1.0f;
                    yScale = 1.0f;
                    yOffset = 0.0f;
                    showGrid = false;
                    autoRotate = false;
                    colorLUTDirty = true;  // Rebuild color LUT
                    reinitializeFFT();
                } else {
                    FFT_SIZE = saved3D_FFT_SIZE;
                    HISTORY_LINES = saved3D_HISTORY_LINES;
                    lineWidth = saved3D_lineWidth;
                    yScale = saved3D_yScale;
                    yOffset = saved3D_yOffset;
                    showGrid = saved3D_showGrid;
                    autoRotate = saved3D_autoRotate;

                    // Restore color settings
                    useCustomLineColor = saved3D_useCustomLineColor;
                    useColormap = saved3D_useColormap;
                    currentColormap = saved3D_currentColormap;

                    colorLUTDirty = true;  // Rebuild color LUT
                    reinitializeFFT();
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(useTraditionalView ? "2D Heat Map" : "3D Waterfall");

            ImGui::Spacing();

            // Fullscreen button
            if (ImGui::Button("FS", ImVec2(44, 30))) {
                if (!isFullscreen) {
                    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
                    isFullscreen = true;
                } else {
                    glfwSetWindowMonitor(window, nullptr, 100, 100, 1400, 800, 0);
                    isFullscreen = false;
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fullscreen (F)");

            ImGui::PopStyleVar();
        } else {
            // ============ FULL MODE - Complete Controls ============

            // Compact mode toggle button
            if (ImGui::Button("<<", ImVec2(280, 0))) {
                compactMode = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Compact mode - icons only");

            ImGui::Spacing();

            ImGui::Text("3D REAL TIME SPECTROGRAM");
            ImGui::Separator();
            ImGui::Spacing();

            // NEW: Theme toggle
            if (ImGui::Checkbox("Dark Theme", &useDarkTheme)) {
                applyTheme(useDarkTheme);
            }

            ImGui::Spacing();

            // NEW: Traditional spectrogram view toggle
            if (ImGui::Checkbox("2D Heat Map", &useTraditionalView)) {
                // When switching to 2D view, save current settings and lock to optimal values
                if (useTraditionalView) {
                    saved3D_FFT_SIZE = FFT_SIZE;
                    saved3D_HISTORY_LINES = HISTORY_LINES;
                    saved3D_lineWidth = lineWidth;
                    saved3D_yScale = yScale;
                    saved3D_yOffset = yOffset;
                    saved3D_showGrid = showGrid;
                    saved3D_autoRotate = autoRotate;

                    // Set optimal 2D settings - 32x FFT for good frequency resolution
                    FFT_SIZE = 16384;  // 32x (512 * 32)
                    HISTORY_LINES = 560;  // Maximum history
                    lineWidth = 1.0f;   // Not used in 2D but locked
                    yScale = 1.0f;      // Not used in 2D but locked
                    yOffset = 0.0f;     // Not used in 2D but locked
                    showGrid = false;   // Not used in 2D but locked
                    autoRotate = false; // Not used in 2D but locked

                    // Save color settings BEFORE changing them
                    saved3D_useCustomLineColor = useCustomLineColor;
                    saved3D_useColormap = useColormap;
                    saved3D_currentColormap = currentColormap;

                    // Enable colormap for heat map
                    useColormap = true;
                    useCustomLineColor = false;

                    colorLUTDirty = true;  // Rebuild color LUT
                    reinitializeFFT();
                } else {
                    // Restore 3D settings
                    FFT_SIZE = saved3D_FFT_SIZE;
                    HISTORY_LINES = saved3D_HISTORY_LINES;
                    lineWidth = saved3D_lineWidth;
                    yScale = saved3D_yScale;
                    yOffset = saved3D_yOffset;
                    showGrid = saved3D_showGrid;
                    autoRotate = saved3D_autoRotate;

                    // Restore color settings
                    useCustomLineColor = saved3D_useCustomLineColor;
                    useColormap = saved3D_useColormap;
                    currentColormap = saved3D_currentColormap;

                    colorLUTDirty = true;  // Rebuild color LUT
                    reinitializeFFT();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("?##viewhelp", ImVec2(20, 0))) {
                ImGui::OpenPopup("ViewHelp");
            }
            if (ImGui::BeginPopup("ViewHelp")) {
                ImGui::Text("2D Heat Map: Heatmap view");
                ImGui::Text("  (frequency vs time)");
                ImGui::Text("  - Locked at 32x FFT & 560 lines");
                ImGui::Text("3D View: Waterfall visualization");
                ImGui::Text("  (with camera controls)");
                ImGui::EndPopup();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // File selection
            if (ImGui::Button("Browse...", ImVec2(280, 0))) {
                std::string file = openFileDialog(window);
                if (!file.empty()) {
                    strncpy(filePathBuffer, file.c_str(), sizeof(filePathBuffer)-1);
                    filePathBuffer[sizeof(filePathBuffer)-1] = '\0';

                    // Automatically load the file
                    stopAudio();
                    if (wavFile.load(filePathBuffer)) {
                        buildFrequencyMapping();
                        waveformCacheDirty = true;  // Mark waveform cache as dirty
                        loadedFileName = std::string(filePathBuffer);
                        size_t pos = loadedFileName.find_last_of("/\\");
                        if (pos != std::string::npos) {
                            loadedFileName = loadedFileName.substr(pos + 1);
                        }
                        addToRecentFiles(filePathBuffer);
                    }
                }
            }

            ImGui::Spacing();

            if (!loadedFileName.empty()) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "Loaded: %s", loadedFileName.c_str());
            }

            // Recent Files
            if (!recentFiles.empty()) {
                ImGui::Spacing();
                if (ImGui::CollapsingHeader("Recent Files")) {
                    for (size_t i = 0; i < recentFiles.size(); i++) {
                        std::string filename = recentFiles[i];
                        size_t pos = filename.find_last_of("/\\");
                        if (pos != std::string::npos) {
                            filename = filename.substr(pos + 1);
                        }

                        // Truncate long filenames
                        if (filename.length() > 35) {
                            filename = filename.substr(0, 32) + "...";
                        }

                        if (ImGui::Selectable(filename.c_str())) {
                            stopAudio();
                            strncpy(filePathBuffer, recentFiles[i].c_str(), sizeof(filePathBuffer)-1);
                            filePathBuffer[sizeof(filePathBuffer)-1] = '\0';

                            if (wavFile.load(recentFiles[i])) {
                                buildFrequencyMapping();
                                waveformCacheDirty = true;  // Mark waveform cache as dirty
                                loadedFileName = recentFiles[i];
                                pos = loadedFileName.find_last_of("/\\");
                                if (pos != std::string::npos) {
                                    loadedFileName = loadedFileName.substr(pos + 1);
                                }
                                addToRecentFiles(recentFiles[i]);
                            }
                        }
                    }
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Playback controls
            ImGui::Text("Playback:");

            if (!isPlaying) {
                if (ImGui::Button("Play", ImVec2(135, 30))) {
                    startAudio();
                }
            } else {
                if (isPaused.load()) {
                    if (ImGui::Button("Resume", ImVec2(135, 30))) {
                        isPaused.store(false, std::memory_order_relaxed);
                    }
                } else {
                    if (ImGui::Button("Pause", ImVec2(135, 30))) {
                        isPaused.store(true, std::memory_order_relaxed);
                    }
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Stop", ImVec2(135, 30))) {
                stopAudio();
            }

            ImGui::Spacing();

            // Progress bar for scrubbing through audio (FIXED: MONO TIME BASE)
            if (!wavFile.audioData.empty()) {
                uint64_t currentPos = playbackPosition.load(std::memory_order_relaxed);
                uint64_t totalSamples = (uint64_t)wavFile.audioData.size(); // MONO samples

                float currentTime = (wavFile.sampleRate > 0) ? ((float)currentPos / (float)wavFile.sampleRate) : 0.0f;
                float totalTime   = (wavFile.sampleRate > 0) ? ((float)totalSamples / (float)wavFile.sampleRate) : 0.0f;

                // Display time
                int currentMin = (int)(currentTime / 60.0f);
                int currentSec = (int)currentTime % 60;
                int totalMin = (int)(totalTime / 60.0f);
                int totalSec = (int)totalTime % 60;

                ImGui::Text("Position: %02d:%02d / %02d:%02d", currentMin, currentSec, totalMin, totalSec);

                // Progress slider
                float progress = (totalSamples > 0) ? ((float)currentPos / (float)totalSamples) : 0.0f;
                ImGui::PushItemWidth(280);
                if (ImGui::SliderFloat("##progress", &progress, 0.0f, 1.0f, "")) {
                    // User is scrubbing - update playback position
                    uint64_t newPos = (uint64_t)((double)progress * (double)totalSamples);
                    if (newPos > totalSamples) newPos = totalSamples;
                    playbackPosition.store(newPos, std::memory_order_relaxed);
                }
                ImGui::PopItemWidth();
            } else {
                ImGui::Text("Position: --:-- / --:--");
                ImGui::PushItemWidth(280);
                float dummy = 0.0f;
                ImGui::SliderFloat("##progress", &dummy, 0.0f, 1.0f, "");
                ImGui::PopItemWidth();
            }

            ImGui::Spacing();

            if (ImGui::Button("Refresh Audio Device", ImVec2(280, 0))) {
                // Restart audio to pick up new default device
                bool wasPlaying = isPlaying;
                bool wasPaused = isPaused.load();
                uint64_t currentPos = playbackPosition.load();

                stopAudio();

                if (wasPlaying && !wavFile.audioData.empty()) {
                    playbackPosition.store(currentPos, std::memory_order_relaxed);
                    startAudio();
                    if (wasPaused) {
                        isPaused.store(true, std::memory_order_relaxed);
                    }
                }
            }

            ImGui::Spacing();

            if (ImGui::Button("Restart", ImVec2(280, 0))) {
                playbackPosition.store(0, std::memory_order_relaxed);
                // Auto-start playback if file is loaded
                if (!wavFile.audioData.empty() && !isPlaying) {
                    startAudio();
                }
            }

            ImGui::Spacing();

            if (ImGui::Button("Clear Visualization", ImVec2(280, 0))) {
                // Stop audio playback
                stopAudio();
                // Clear all history lines to zero
                std::fill(lineHistory.begin(), lineHistory.end(), 0.0f);
                std::fill(currentLine.begin(), currentLine.end(), 0.0f);
                std::fill(magnitudes.begin(), magnitudes.end(), 0.0f);
                historyFillCount = 0;  // Reset history counter
            }

            ImGui::Spacing();

            ImGui::Text("Volume:");
            ImGui::PushItemWidth(280);
            if (SliderFloatWithWheel("##volume", &volume, 0.0f, 1.0f)) {
                // Unmute if volume is changed and was muted
                if (isMuted && volume > 0.0f) {
                    isMuted = false;
                }
            }
            ImGui::PopItemWidth();

            // Show mute status
            if (isMuted) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "MUTED");
            }

            ImGui::Spacing();

            ImGui::Checkbox("Loop Audio", &loopAudio);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // FFT Size controls
            ImGui::Text("FFT Size (Line Resolution):");

            if (useTraditionalView) {
                // Show locked status in 2D mode
                ImGui::PushItemWidth(280);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.3f, 0.3f, 0.32f, 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                char lockedTextBuf[64] = "16384 (32x) - LOCKED in Heat Map";
                ImGui::InputText("##fftsize_locked", lockedTextBuf, sizeof(lockedTextBuf), ImGuiInputTextFlags_ReadOnly);
                ImGui::PopStyleColor(2);
                ImGui::PopItemWidth();
            } else {

                // Normal controls in 3D mode
                ImGui::PushItemWidth(280);
                const char* fftSizes[] = { "512", "1024 (2x)", "2048 (4x)", "4096 (8x)", "8192 (16x)", "16384 (32x)" };
                int currentFFTIndex = 0;

                // Determine current selection
                if (FFT_SIZE == 512) currentFFTIndex = 0;
                else if (FFT_SIZE == 1024) currentFFTIndex = 1;
                else if (FFT_SIZE == 2048) currentFFTIndex = 2;
                else if (FFT_SIZE == 4096) currentFFTIndex = 3;
                else if (FFT_SIZE == 8192) currentFFTIndex = 4;
                else if (FFT_SIZE == 16384) currentFFTIndex = 5;

                // Set the popup to show all items (no scrolling)
                ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, FLT_MAX));
                if (ImGui::BeginCombo("##fftsize", fftSizes[currentFFTIndex])) {
                    // Handle keyboard navigation inside the open combo
                    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                        currentFFTIndex--;
                        if (currentFFTIndex < 0) currentFFTIndex = IM_ARRAYSIZE(fftSizes) - 1;
                        switch (currentFFTIndex) {
                            case 0: FFT_SIZE = 512; break;
                            case 1: FFT_SIZE = 1024; break;
                            case 2: FFT_SIZE = 2048; break;
                            case 3: FFT_SIZE = 4096; break;
                            case 4: FFT_SIZE = 8192; break;
                            case 5: FFT_SIZE = 16384; break;
                        }
                        reinitializeFFT();
                        saved3D_FFT_SIZE = FFT_SIZE;
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                        currentFFTIndex++;
                        if (currentFFTIndex >= IM_ARRAYSIZE(fftSizes)) currentFFTIndex = 0;
                        switch (currentFFTIndex) {
                            case 0: FFT_SIZE = 512; break;
                            case 1: FFT_SIZE = 1024; break;
                            case 2: FFT_SIZE = 2048; break;
                            case 3: FFT_SIZE = 4096; break;
                            case 4: FFT_SIZE = 8192; break;
                            case 5: FFT_SIZE = 16384; break;
                        }
                        reinitializeFFT();
                        saved3D_FFT_SIZE = FFT_SIZE;
                    }
                    
                    for (int n = 0; n < IM_ARRAYSIZE(fftSizes); n++) {
                        bool is_selected = (currentFFTIndex == n);
                        if (ImGui::Selectable(fftSizes[n], is_selected)) {
                            currentFFTIndex = n;
                            switch (currentFFTIndex) {
                                case 0: FFT_SIZE = 512; break;
                                case 1: FFT_SIZE = 1024; break;
                                case 2: FFT_SIZE = 2048; break;
                                case 3: FFT_SIZE = 4096; break;
                                case 4: FFT_SIZE = 8192; break;
                                case 5: FFT_SIZE = 16384; break;
                            }
                            reinitializeFFT();
                            saved3D_FFT_SIZE = FFT_SIZE;
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                // Add scroll wheel navigation when hovering
                if (ImGui::IsItemHovered()) {
                    float wheel = ImGui::GetIO().MouseWheel;
                    if (wheel != 0.0f) {
                        if (wheel > 0.0f) {
                            currentFFTIndex--;
                            if (currentFFTIndex < 0) currentFFTIndex = IM_ARRAYSIZE(fftSizes) - 1;
                        } else {
                            currentFFTIndex++;
                            if (currentFFTIndex >= IM_ARRAYSIZE(fftSizes)) currentFFTIndex = 0;
                        }
                        switch (currentFFTIndex) {
                            case 0: FFT_SIZE = 512; break;
                            case 1: FFT_SIZE = 1024; break;
                            case 2: FFT_SIZE = 2048; break;
                            case 3: FFT_SIZE = 4096; break;
                            case 4: FFT_SIZE = 8192; break;
                            case 5: FFT_SIZE = 16384; break;
                        }
                        reinitializeFFT();
                        saved3D_FFT_SIZE = FFT_SIZE;
                        sliderConsumedScroll = true;
                    }
                }

                ImGui::PopItemWidth();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // View controls
            ImGui::Text("View:");

            if (useTraditionalView) {
                // Locked in Heat Map mode
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);  // Dim the checkbox
                bool lockedGrid = false;
                ImGui::Checkbox("Show Grid (N/A in Heat Map)", &lockedGrid);
                ImGui::PopStyleVar();
            } else {
                // Normal control in 3D mode
                if (ImGui::Checkbox("Show Grid", &showGrid)) {
                    saved3D_showGrid = showGrid;  // Save changes
                }
            }

            if (useTraditionalView) {
                // Locked in Heat Map mode
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);  // Dim the checkbox
                bool lockedRotate = false;
                ImGui::Checkbox("Auto-Rotate Camera (N/A in Heat Map)", &lockedRotate);
                ImGui::PopStyleVar();
            } else {
                // Normal control in 3D mode
                if (ImGui::Checkbox("Auto-Rotate Camera", &autoRotate)) {
                    saved3D_autoRotate = autoRotate;  // Save changes
                }
            }

            ImGui::Checkbox("Show FPS Counter", &showFPS);
            ImGui::Checkbox("Show CPU Usage", &showCPU);
            ImGui::Checkbox("Show Metadata", &showMetadata);
            ImGui::Checkbox("Show Waveform", &showWaveform);

            ImGui::Text("Number of Lines (10-560):");
            ImGui::PushItemWidth(280);
            if (useTraditionalView) {
                // Show locked status in 2D mode
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.3f, 0.3f, 0.32f, 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                char lockedBuf[64] = "560 - LOCKED in Heat Map";
                ImGui::InputText("##lines_locked", lockedBuf, sizeof(lockedBuf), ImGuiInputTextFlags_ReadOnly);
                ImGui::PopStyleColor(2);
            } else {
                // Normal control in 3D mode
                if (ImGui::InputInt("##lines", &HISTORY_LINES)) {
                    HISTORY_LINES = (int)clampf((float)HISTORY_LINES, 10.0f, (float)MAX_HISTORY_LINES);
                    saved3D_HISTORY_LINES = HISTORY_LINES;  // Save changes
                }
            }
            
            ImGui::PopItemWidth();

            ImGui::Text("Line Width (0.1-10.0):");
            ImGui::PushItemWidth(280);
            if (useTraditionalView) {
                // Show locked status in Heat Map mode
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.3f, 0.3f, 0.32f, 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                char lockedBuf[64] = "LOCKED in Heat Map";
                ImGui::InputText("##linewidth_locked", lockedBuf, sizeof(lockedBuf), ImGuiInputTextFlags_ReadOnly);
                ImGui::PopStyleColor(2);
            } else {
                // Normal control in 3D mode
                if (ImGui::InputFloat("##linewidth", &lineWidth, 0.1f, 0.5f, "%.1f")) {
                    lineWidth = clampf(lineWidth, 0.1f, 10.0f);
                    saved3D_lineWidth = lineWidth;  // Save changes
                }
            }
            ImGui::PopItemWidth();

            ImGui::Text("Y-Axis Height:");
            ImGui::PushItemWidth(280);
            if (useTraditionalView) {
                // Show locked status in Heat Map mode
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.3f, 0.3f, 0.32f, 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                char lockedBuf[64] = "LOCKED in Heat Map";
                ImGui::InputText("##yscale_locked", lockedBuf, sizeof(lockedBuf), ImGuiInputTextFlags_ReadOnly);
                ImGui::PopStyleColor(2);
            } else {
                // Normal control in 3D mode
                if (SliderFloatWithWheel("##yscale", &yScale, 0.1f, 5.0f)) {
                    saved3D_yScale = yScale;  // Save changes
                }
            }
            ImGui::PopItemWidth();

            ImGui::Text("Y-Axis Offset:");
            ImGui::PushItemWidth(280);
            if (useTraditionalView) {
                // Show locked status in Heat Map mode
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.3f, 0.3f, 0.32f, 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                char lockedBuf[64] = "LOCKED in Heat Map";
                ImGui::InputText("##yoffset_locked", lockedBuf, sizeof(lockedBuf), ImGuiInputTextFlags_ReadOnly);
                ImGui::PopStyleColor(2);
            } else {
                // Normal control in 3D mode
                if (SliderFloatWithWheel("##yoffset", &yOffset, -3.0f, 3.0f)) {
                    saved3D_yOffset = yOffset;  // Save changes
                }
            }
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Text("Colors:");

            // Use Custom Line Color checkbox - FIRST
            bool customColorChanged = ImGui::Checkbox("Use Custom Line Color", &useCustomLineColor);

            // If custom color was just enabled, disable colormap
            if (customColorChanged && useCustomLineColor) {
                useColormap = false;  // Auto-disable colormap when enabling custom color
            }
            // If trying to disable custom color, enable colormap instead
            if (customColorChanged && !useCustomLineColor) {
                useColormap = true;  // Auto-enable colormap when disabling custom color
                currentColormap = COLORMAP_VIRIDIS;  // Ensure Viridis is selected
                colorLUTDirty = true;
            }

            if (useCustomLineColor) {
                ImGui::SameLine();
                ImGui::ColorEdit3("##linecolor", lineColor, ImGuiColorEditFlags_NoInputs);
            }

            ImGui::Spacing();

            // Colormap controls - with checkbox to enable/disable
            bool colormapChanged = ImGui::Checkbox("Use Colormap", &useColormap);

            // If colormap was just enabled, disable custom line color
            if (colormapChanged && useColormap) {
                useCustomLineColor = false;  // Auto-disable custom color when enabling colormap
                currentColormap = COLORMAP_VIRIDIS;  // Ensure Viridis is selected
                colorLUTDirty = true;
            }
            // If trying to disable colormap, enable custom color instead
            if (colormapChanged && !useColormap) {
                useCustomLineColor = true;  // Auto-enable custom color when disabling colormap
            }

            if (useColormap) {
                // Color presets dropdown - only show when colormap is enabled
                ImGui::Text("Color Preset:");
                ImGui::PushItemWidth(280);

                // Preset names array - Accurate scientific colormaps
                const char* presetNames[] = {
                    "Viridis (Perceptual)",
                    "Plasma (Perceptual)",
                    "Inferno (Perceptual)",
                    "Magma (Perceptual)",
                    "Hot (Black-Red-Yellow-White)",
                    "Cool (Cyan-Magenta)",
                    "Jet (Blue-Cyan-Yellow-Red)",
                    "Turbo (Improved Rainbow)",
                    "Ocean (Green-Blue-White)",
                    "Rainbow (Spectral)",
                    "Grayscale",
                    "Ice (Blue-Cyan-White)",
                    "Fire (Black-Red-Orange)",
                    "Seismic (Blue-White-Red)",
                    "Twilight (Cyclic)",
                    "Cividis (Colorblind-Safe)"
                };

                int currentPresetIndex = (int)currentColormap;

                // Set the popup to show all items (no scrolling)
                ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, FLT_MAX));
                if (ImGui::BeginCombo("##colorpreset", presetNames[currentPresetIndex])) {
                    // Handle keyboard navigation inside the open combo
                    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                        currentPresetIndex--;
                        if (currentPresetIndex < 0) currentPresetIndex = IM_ARRAYSIZE(presetNames) - 1;
                        currentColormap = (ColormapType)currentPresetIndex;
                        colorLUTDirty = true;
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                        currentPresetIndex++;
                        if (currentPresetIndex >= IM_ARRAYSIZE(presetNames)) currentPresetIndex = 0;
                        currentColormap = (ColormapType)currentPresetIndex;
                        colorLUTDirty = true;
                    }
                    
                    for (int n = 0; n < IM_ARRAYSIZE(presetNames); n++) {
                        bool is_selected = (currentPresetIndex == n);
                        if (ImGui::Selectable(presetNames[n], is_selected)) {
                            currentPresetIndex = n;
                            currentColormap = (ColormapType)currentPresetIndex;
                            colorLUTDirty = true;
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                // Add scroll wheel navigation when hovering
                if (ImGui::IsItemHovered()) {
                    float wheel = ImGui::GetIO().MouseWheel;
                    if (wheel != 0.0f) {
                        if (wheel > 0.0f) {
                            currentPresetIndex--;
                            if (currentPresetIndex < 0) currentPresetIndex = IM_ARRAYSIZE(presetNames) - 1;
                        } else {
                            currentPresetIndex++;
                            if (currentPresetIndex >= IM_ARRAYSIZE(presetNames)) currentPresetIndex = 0;
                        }
                        currentColormap = (ColormapType)currentPresetIndex;
                        colorLUTDirty = true;
                        sliderConsumedScroll = true;
                    }
                }

                ImGui::PopItemWidth();
            } else {
                ImGui::TextDisabled("(Colormap disabled)");
            }

            ImGui::Spacing();

            ImGui::Text("Viewport Background:");
            ImGui::ColorEdit3("##bgcolor", bgColor, ImGuiColorEditFlags_NoInputs);

            ImGui::Spacing();

            if (ImGui::Button("Reset Colors", ImVec2(280, 0))) {
                currentColormap = COLORMAP_VIRIDIS;  // Reset to Viridis
                colorLUTDirty = true;
                useCustomLineColor = true;  // Reset to custom green
                lineColor[0] = 0.0f;  // Green
                lineColor[1] = 1.0f;
                lineColor[2] = 0.0f;
                useColormap = false;  // Disable colormap (back to startup state)
                
                // Reset viewport background color
                bgColor[0] = 0.02f;
                bgColor[1] = 0.02f;
                bgColor[2] = 0.03f;
            }

            ImGui::Spacing();

            if (ImGui::Button("Reset Camera", ImVec2(280, 0))) {
                gYaw = -45.0f;
                gPitch = 35.0f;
                gDist = 9.0f;
            }

            ImGui::Spacing();

            if (ImGui::Button("Reset All", ImVec2(280, 0))) {
                // Reset camera
                gYaw = -45.0f;
                gPitch = 35.0f;
                gDist = 9.0f;

                // Reset FFT and lines based on current mode
                if (useTraditionalView) {
                    // Keep 2D mode locked settings
                    FFT_SIZE = 16384;  // 32x
                    HISTORY_LINES = 560;  // Maximum history
                    lineWidth = 1.0f;
                    yScale = 1.0f;
                    yOffset = 0.0f;
                    showGrid = false;
                    autoRotate = false;
                } else {
                    // Reset 3D defaults
                    FFT_SIZE = 4096;
                    HISTORY_LINES = 140;
                    lineWidth = 1.0f;
                    yScale = 1.20f;
                    yOffset = 0.0f;
                    showGrid = true;
                    autoRotate = false;
                    saved3D_FFT_SIZE = FFT_SIZE;
                    saved3D_HISTORY_LINES = HISTORY_LINES;
                    saved3D_lineWidth = lineWidth;
                    saved3D_yScale = yScale;
                    saved3D_yOffset = yOffset;
                    saved3D_showGrid = showGrid;
                    saved3D_autoRotate = autoRotate;
                }
                reinitializeFFT();

                // Reset colors
                currentColormap = COLORMAP_RAINBOW;  // Reset to Rainbow
                colorLUTDirty = true;  // Rebuild color LUT
                // Note: Viewport background is controlled separately via "Viewport Background:" color picker
            }

            ImGui::Spacing();

            if (ImGui::Button("Fullscreen", ImVec2(280, 0))) {
                if (!isFullscreen) {
                    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
                    isFullscreen = true;
                } else {
                    glfwSetWindowMonitor(window, nullptr, 100, 100, 1400, 800, 0);
                    isFullscreen = false;
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Controls info - collapsible section (collapsed by default)
            if (ImGui::CollapsingHeader("KEYBOARD SHORTCUTS", ImGuiTreeNodeFlags_None)) {
                ImGui::BulletText("SPACE: Play/Pause/Start");
                ImGui::BulletText("R: Restart");
                ImGui::BulletText("C: Reset Camera");
                ImGui::BulletText("F: Toggle Fullscreen");
                ImGui::BulletText("TAB: Compact Mode");
                ImGui::BulletText("ESC: Exit Fullscreen");
                ImGui::BulletText("M: Mute/Unmute");
                ImGui::BulletText("+/-: Volume Up/Down");
                ImGui::BulletText("Left/Right: Seek 5 sec");
                ImGui::BulletText("1-9: Jump to 10-90%%");
                ImGui::Spacing();
                ImGui::Text("MOUSE (3D View):");
                ImGui::BulletText("Scroll on Slider: Adjust");
                ImGui::BulletText("LMB Drag: Rotate");
                ImGui::BulletText("Scroll: Zoom");
            }

        } // End of full mode section

        // Manual scrolling: only scroll sidebar if slider didn't consume the scroll
        if (!sliderConsumedScroll && ImGui::GetIO().MouseWheel != 0.0f) {
            // Check if mouse is over the sidebar window (including child windows like file path box)
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
                float scrollAmount = ImGui::GetIO().MouseWheel * 20.0f; // Adjust speed
                ImGui::SetScrollY(ImGui::GetScrollY() - scrollAmount);
            }
        }

        ImGui::End();

        // Render FPS/CPU overlay in top-right of viewport (if enabled)
        if (showFPS || showCPU) {
            ImGui::SetNextWindowPos(ImVec2((float)windowWidth - 120, 10));
            ImGui::SetNextWindowBgAlpha(0.35f);
            ImGui::Begin("Stats", nullptr,
                         ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav);
            if (showFPS) {
                ImGui::Text("FPS: %.1f", currentFPS);
            }
            if (showCPU) {
                ImGui::Text("CPU: %.1f%%", currentCPU);
            }
            ImGui::End();
        }

        // Render metadata overlay in top-left of viewport (if enabled)
        if (showMetadata && !loadedFileName.empty()) {
            ImGui::SetNextWindowPos(ImVec2((float)viewportX + 10, 10));
            ImGui::SetNextWindowBgAlpha(0.35f);
            ImGui::Begin("Metadata", nullptr,
                         ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav);
            ImGui::Text("%s", loadedFileName.c_str());
            ImGui::Text("%.1f sec | %d Hz",
                       (float)wavFile.audioData.size() / (float)wavFile.sampleRate,
                       (int)wavFile.sampleRate);
            ImGui::Text("%d ch | %d-bit", (int)wavFile.sourceChannels, (int)wavFile.bitsPerSample);
            size_t fileSizeBytes = wavFile.audioData.size() * sizeof(float);
            float fileSizeMB = (float)fileSizeBytes / (1024.0f * 1024.0f);
            ImGui::Text("%.2f MB", fileSizeMB);
            ImGui::End();
        }

        // Render spectrogram (choose between 3D and traditional view)
        if (useTraditionalView) {
            renderTraditionalSpectrogram(viewportX, viewportY, viewportW, viewportH);
        } else {
            render3DWaterfall(viewportX, viewportY, viewportW, viewportH);
        }

        // Render waveform overlay (if enabled)
        if (showWaveform && !wavFile.audioData.empty()) {
            renderWaveform(viewportX, viewportY, viewportW, viewportH);
        }

        // Render ImGui on top of everything
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);

        // Reset redraw flag after frame
        if (needsRedraw) needsRedraw = false;
    }

    // Cleanup
    stopAudio();

    // Cleanup texture (IMPORTANT!)
    if (spectrogramTexture) {
        glDeleteTextures(1, &spectrogramTexture);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (fftPlan) fftw_destroy_plan(fftPlan);
    if (fftInput) fftw_free(fftInput);
    if (fftOutput) fftw_free(fftOutput);

    Pa_Terminate();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
