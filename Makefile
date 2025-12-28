# Makefile for Spectrogram Viewer
# Supports Windows (MSYS2/MinGW), Linux, and macOS

# Detect OS
ifeq ($(OS),Windows_NT)
    DETECTED_OS := Windows
else
    DETECTED_OS := $(shell uname -s)
endif

# Compiler
CXX = g++

# Common flags
CXXFLAGS = -std=c++11 -O2 -I./imgui -I./imgui/backends
SOURCES = spectrogram_lines.cpp \
          imgui/imgui.cpp \
          imgui/imgui_draw.cpp \
          imgui/imgui_tables.cpp \
          imgui/imgui_widgets.cpp \
          imgui/backends/imgui_impl_glfw.cpp \
          imgui/backends/imgui_impl_opengl3.cpp

# Platform-specific settings
ifeq ($(DETECTED_OS),Windows)
    TARGET = spectrogram_gui.exe
    RESOURCE_OBJ = app.o
    CXXFLAGS += -DGLEW_STATIC -mwindows -static -static-libgcc -static-libstdc++
    LDFLAGS = -lglew32 -lglfw3 -lopengl32 -lportaudio -lfftw3 -lsndfile \
              -lvorbisenc -lvorbisfile -lvorbis -lFLAC -lmp3lame -lmpg123 \
              -lopus -logg -lgdi32 -lwinmm -lole32 -lcomdlg32 -lsetupapi \
              -lksuser -lpsapi -lshlwapi
else ifeq ($(DETECTED_OS),Darwin)
    TARGET = spectrogram_gui
    RESOURCE_OBJ =
    CXXFLAGS += -I/opt/homebrew/include
    LDFLAGS = -L/opt/homebrew/lib -lGLEW -lglfw -framework OpenGL \
              -lportaudio -lfftw3 -lsndfile
else
    TARGET = spectrogram_gui
    RESOURCE_OBJ =
    LDFLAGS = -lGLEW -lglfw -lGL -lportaudio -lfftw3 -lsndfile -lpthread -ldl
endif

# Targets
.PHONY: all clean imgui resource

all: imgui resource $(TARGET)

# Compile the main program
$(TARGET): $(SOURCES) $(RESOURCE_OBJ)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES) $(RESOURCE_OBJ) $(LDFLAGS)

# Compile Windows resource file
resource:
ifeq ($(DETECTED_OS),Windows)
	@if not exist app.o windres app.rc -o app.o
endif

# Clone ImGui if not present
imgui:
	@if [ ! -d "imgui" ]; then \
		echo "ImGui not found. Cloning..."; \
		git clone https://github.com/ocornut/imgui.git; \
	fi

# Clean build files
clean:
ifeq ($(DETECTED_OS),Windows)
	@if exist $(TARGET) del $(TARGET)
	@if exist app.o del app.o
	@if exist app.res del app.res
else
	rm -f $(TARGET) app.o app.res
endif

# Clean everything including ImGui
distclean: clean
	rm -rf imgui

# Help target
help:
	@echo "Makefile for Spectrogram Viewer"
	@echo ""
	@echo "Targets:"
	@echo "  all        - Build the project (default)"
	@echo "  clean      - Remove compiled files"
	@echo "  distclean  - Remove compiled files and ImGui directory"
	@echo "  imgui      - Clone ImGui if not present"
	@echo "  help       - Show this help message"
	@echo ""
	@echo "Detected OS: $(DETECTED_OS)"
