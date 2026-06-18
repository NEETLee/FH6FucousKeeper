# ─── FH6 FocusKeeper - Makefile ───────────────────────────────────────
# Build with w64devkit: open w64devkit terminal, cd to project dir, run `make`
#
# Targets:
#   make          - Build both hook.dll and FocusKeeper.exe
#   make clean    - Remove build artifacts
#   make rebuild  - Clean + build
#   make dll      - Build only hook.dll
#   make exe      - Build only FocusKeeper.exe

CC = gcc
CXX = g++
WINDRES = windres

# Compiler flags (C)
CFLAGS = -O2 -Wall -Wextra -std=gnu11
CFLAGS += -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN
CFLAGS += -D_WIN32_IE=0x0600 -D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000007
CFLAGS += -I./src -I./src/loader -I./res
CFLAGS += -Wno-cast-function-type

# Compiler flags (C++ for WinRT modules)
CXXFLAGS = -O2 -Wall -Wextra -std=c++20
CXXFLAGS += -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN
CXXFLAGS += -D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000007
CXXFLAGS += -I./src -I./src/loader -I./res
CXXFLAGS += -Wno-cast-function-type

# Linker flags
LDFLAGS_DLL = -shared -Wl,--out-implib,build/libhook.a
LDFLAGS_EXE = -mwindows -L./build -lhook -lcomctl32 -lpsapi -lole32 -loleaut32 -lgdi32 -lwinhttp
LDFLAGS_WGC = $(LDFLAGS_EXE) -ld3d11 -ldxgi -lwindowsapp -lruntimeobject

# Directories
SRC_HOOK = src/hook
SRC_LOADER = src/loader
RES_DIR = res
BUILD_DIR = build

# Source files
HOOK_SRC = $(SRC_HOOK)/hook.c
HOOK_DEF = $(SRC_HOOK)/hook.def
LOADER_SRC = $(SRC_LOADER)/main.c \
             $(SRC_LOADER)/gui.c \
             $(SRC_LOADER)/tray.c \
             $(SRC_LOADER)/hook_manager.c \
             $(SRC_LOADER)/window_finder.c \
             $(SRC_LOADER)/logger.c \
             $(SRC_LOADER)/settings.c \
             $(SRC_LOADER)/audio_control.c \
             $(SRC_LOADER)/i18n.c \
             $(SRC_LOADER)/input_hook_backend.c \
             $(SRC_LOADER)/step_executor.c \
             $(SRC_LOADER)/race_profile.c \
             $(SRC_LOADER)/auto_race.c \
             $(SRC_LOADER)/race_controller.c \
             $(SRC_LOADER)/screen_detect.c \
             $(SRC_LOADER)/screen_capture_gdi.c \
             $(SRC_LOADER)/version_check.c

# C++ source files (WinRT modules - used with USE_WGC=1)
LOADER_CXX_SRC = $(SRC_LOADER)/screen_capture.cpp \
                 $(SRC_LOADER)/ocr_engine.cpp

# C++ object files
CXX_OBJS = $(BUILD_DIR)/screen_capture.o \
            $(BUILD_DIR)/ocr_engine.o

# Output
DLL_OUT = $(BUILD_DIR)/hook.dll
EXE_OUT = $(BUILD_DIR)/FocusKeeper.exe
RES_OBJ = $(BUILD_DIR)/app_res.o

# ─── Targets ──────────────────────────────────────────────────────────

.PHONY: all clean rebuild dll exe dirs

all: dirs dll exe profiles

dirs:
	@mkdir -p $(BUILD_DIR)

profiles: dirs
	@mkdir -p $(BUILD_DIR)/profiles
	@cp -n data/profiles/*.ini $(BUILD_DIR)/profiles/ 2>/dev/null || true

dll: dirs $(DLL_OUT)

exe: dirs $(EXE_OUT)

$(DLL_OUT): $(HOOK_SRC) $(HOOK_DEF)
	$(CC) $(CFLAGS) -DHOOK_EXPORTS $(LDFLAGS_DLL) -o $@ $(HOOK_SRC) $(HOOK_DEF) -luser32

$(RES_OBJ): $(RES_DIR)/app.rc $(RES_DIR)/resource.h $(RES_DIR)/app.manifest
	$(WINDRES) -c 65001 -I$(RES_DIR) $(RES_DIR)/app.rc -o $@

# Compile C++ WinRT modules (only when USE_WGC=1)
$(BUILD_DIR)/screen_capture.o: $(SRC_LOADER)/screen_capture.cpp $(SRC_LOADER)/screen_capture.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/ocr_engine.o: $(SRC_LOADER)/ocr_engine.cpp $(SRC_LOADER)/ocr_engine.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Default build: GDI capture (pure C, no WinRT dependency)
$(EXE_OUT): $(LOADER_SRC) $(RES_OBJ) $(DLL_OUT)
	$(CC) $(CFLAGS) -o $@ $(LOADER_SRC) $(RES_OBJ) $(LDFLAGS_EXE)

# WGC build: Windows Graphics Capture + OCR (requires C++/WinRT headers)
# Usage: make wgc  (or make USE_WGC=1)
wgc: CFLAGS += -DUSE_WGC_CAPTURE
wgc: dirs dll $(CXX_OBJS) $(RES_OBJ)
	$(CC) $(CFLAGS) -DUSE_WGC_CAPTURE -o $(EXE_OUT) \
		$(filter-out %/screen_capture_gdi.c,$(LOADER_SRC)) \
		$(CXX_OBJS) $(RES_OBJ) $(LDFLAGS_WGC) -lstdc++

clean:
	@rm -rf $(BUILD_DIR)

rebuild: clean all

# ─── Development helpers ──────────────────────────────────────────────

debug: CFLAGS += -g -DDEBUG -O0
debug: CXXFLAGS += -g -DDEBUG -O0
debug: all

run: all
	@echo "[!] Running with admin privileges..."
	@$(BUILD_DIR)/FocusKeeper.exe

# ─── Test targets ─────────────────────────────────────────────────────

TEST_DIR = tests
TEST_CAPTURE_OCR = $(BUILD_DIR)/test_capture_ocr.exe

test-capture: dirs $(BUILD_DIR)/screen_capture.o $(BUILD_DIR)/ocr_engine.o
	$(CXX) $(CXXFLAGS) -mconsole -o $(TEST_CAPTURE_OCR) \
		$(TEST_DIR)/test_capture_ocr.cpp \
		$(BUILD_DIR)/screen_capture.o $(BUILD_DIR)/ocr_engine.o \
		-ld3d11 -ldxgi -lwindowsapp -lruntimeobject -lole32 -loleaut32 -lgdi32
	@echo "[OK] Built $(TEST_CAPTURE_OCR)"
	@echo "Run: $(TEST_CAPTURE_OCR) [\"Window Title\"]"

# Quick test with GDI capture (no WinRT dependency)
test-capture-gdi: dirs
	$(CC) $(CFLAGS) -DTEST_STANDALONE -mconsole -o $(BUILD_DIR)/test_capture_gdi.exe \
		$(TEST_DIR)/test_capture_gdi.c \
		$(SRC_LOADER)/screen_capture_gdi.c \
		-lgdi32 -luser32
	@echo "[OK] Built $(BUILD_DIR)/test_capture_gdi.exe"
