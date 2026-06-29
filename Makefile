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

# OpenCV flags (MSYS2 MINGW64: pacman -S mingw-w64-x86_64-opencv).
# Resolve via pkg-config so it works regardless of where MSYS2 is installed
# (e.g. the GitHub Actions runner is NOT at C:/msys64). Fall back to the default
# local MINGW64 location only if pkg-config/opencv4.pc is unavailable.
OPENCV_CFLAGS := $(shell pkg-config --cflags opencv4 2>/dev/null)
OPENCV_LIBS := $(shell pkg-config --libs opencv4 2>/dev/null)
ifeq ($(strip $(OPENCV_CFLAGS)),)
OPENCV_CFLAGS := -IC:/msys64/mingw64/include/opencv4
endif
ifeq ($(strip $(OPENCV_LIBS)),)
OPENCV_LIBS := -LC:/msys64/mingw64/lib -lopencv_imgproc -lopencv_imgcodecs -lopencv_core
endif

# Linker flags
LDFLAGS_DLL = -shared -Wl,--out-implib,build/libhook.a
LDFLAGS_EXE = -mwindows -L./build -lhook -lcomctl32 -lpsapi -lole32 -loleaut32 -lgdi32 -lwinhttp
LDFLAGS_WGC = $(LDFLAGS_EXE) -ld3d11 -ldxgi -lwindowsapp -lruntimeobject

# Directories
SRC_HOOK = src/hook
SRC_LOADER = src/loader
RES_DIR = res
BUILD_DIR = build
DIST_DIR = dist

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

.PHONY: all clean rebuild dll exe dirs farm farm-release farm-replay

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

# ─── Farm build: full Auto Wheelspin Farm pipeline in FocusKeeper.exe ──
# WGC capture + OpenCV template matching + OCR + farm flows/pipeline.
# Usage: make farm
FARM_CXX_OBJS = $(BUILD_DIR)/template_match.o \
                $(BUILD_DIR)/screen_capture_wgc.o \
                $(BUILD_DIR)/ocr_engine_wrt.o \
                $(BUILD_DIR)/xbox_textentry.o
FARM_C_SRC = $(filter-out %/screen_capture_gdi.c,$(LOADER_SRC)) \
             $(SRC_LOADER)/game_input.c \
             $(SRC_LOADER)/farm_flow.c \
             $(SRC_LOADER)/farm_pipeline.c \
             $(SRC_LOADER)/farm_economy.c \
             $(SRC_LOADER)/car_profile.c

$(BUILD_DIR)/template_match.o: $(SRC_LOADER)/template_match.cpp $(SRC_LOADER)/template_match.h
	$(CXX) $(CXXFLAGS) $(OPENCV_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/screen_capture_wgc.o: $(SRC_LOADER)/screen_capture_wgc.cpp $(SRC_LOADER)/screen_capture.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/ocr_engine_wrt.o: $(SRC_LOADER)/ocr_engine_wrt.cpp $(SRC_LOADER)/ocr_engine.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/xbox_textentry.o: $(SRC_LOADER)/xbox_textentry.cpp $(SRC_LOADER)/xbox_textentry.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

farm-assets: dirs
	@mkdir -p $(BUILD_DIR)/assets/templates
	@cp -f assets/templates/* $(BUILD_DIR)/assets/templates/ 2>/dev/null || true

# Copy per-car profiles (car.ini + car-specific templates) into the build tree.
car-profiles: dirs
	@mkdir -p $(BUILD_DIR)/profiles/cars
	@cp -rf data/profiles/cars/* $(BUILD_DIR)/profiles/cars/ 2>/dev/null || true

# `make farm`         -> DEBUG build (defines FK_DEBUG): file-routed step logs,
#                        visual decision snapshots, stop.flag self-exit, etc.
#                        Output: build/  (the dev sandbox).
# `make farm-release`  -> shippable build, FK_DEBUG undefined so every debug-only
#                        feature is removed by the preprocessor.
#                        Output: dist/  (a clean, complete, runnable package).
# The C sources are (re)compiled inline on every invocation, so switching between
# the two targets always recompiles with the correct flags (no stale objects).

# Copy the MinGW/OpenCV runtime DLLs the exe actually depends on into $(1).
define bundle_dlls
	@bash scripts/bundle_dlls.sh $(1)
endef

farm: CFLAGS += -DUSE_WGC_CAPTURE -DUSE_FARM
farm: dirs dll $(FARM_CXX_OBJS) $(RES_OBJ) profiles farm-assets car-profiles
	$(CC) $(CFLAGS) -DUSE_WGC_CAPTURE -DUSE_FARM -DFK_DEBUG -o $(EXE_OUT) \
		$(FARM_C_SRC) \
		$(FARM_CXX_OBJS) $(RES_OBJ) \
		$(LDFLAGS_WGC) $(OPENCV_LIBS) -luuid -lstdc++
	$(call bundle_dlls,$(BUILD_DIR))
	@echo "[OK] Built $(EXE_OUT) (DEBUG farm pipeline)"

# Release: assemble a clean dist/ with ONLY what ships (exe + hook.dll + runtime
# DLLs + assets + profiles). dist/ is wiped first so it always reflects the build.
farm-release: CFLAGS += -DUSE_WGC_CAPTURE -DUSE_FARM
farm-release: dirs dll $(FARM_CXX_OBJS) $(RES_OBJ)
	@rm -rf $(DIST_DIR)
	@mkdir -p $(DIST_DIR)
	$(CC) $(CFLAGS) -DUSE_WGC_CAPTURE -DUSE_FARM -o $(DIST_DIR)/FocusKeeper.exe \
		$(FARM_C_SRC) \
		$(FARM_CXX_OBJS) $(RES_OBJ) \
		$(LDFLAGS_WGC) $(OPENCV_LIBS) -luuid -lstdc++
	@cp -f $(DLL_OUT) $(DIST_DIR)/
	$(call bundle_dlls,$(DIST_DIR))
	@mkdir -p $(DIST_DIR)/assets/templates
	@cp -f assets/templates/* $(DIST_DIR)/assets/templates/ 2>/dev/null || true
	@mkdir -p $(DIST_DIR)/profiles
	@cp -f data/profiles/*.ini $(DIST_DIR)/profiles/ 2>/dev/null || true
	@mkdir -p $(DIST_DIR)/profiles/cars
	@cp -rf data/profiles/cars/* $(DIST_DIR)/profiles/cars/ 2>/dev/null || true
	@bash scripts/upx_compress.sh $(DIST_DIR)
	@echo "[OK] Release package -> $(DIST_DIR)/ (RELEASE farm pipeline, no debug code)"

# Offline replay tool: run the vision detectors on a saved PNG frame, no game
# needed. Build: make farm-replay ; Run: build/farm_replay.exe frame.png [lang]
farm-replay: dirs $(BUILD_DIR)/template_match.o $(BUILD_DIR)/ocr_engine_wrt.o
	$(CXX) $(CXXFLAGS) $(OPENCV_CFLAGS) -mconsole -municode -I$(SRC_LOADER) \
		-o $(BUILD_DIR)/farm_replay.exe \
		$(TEST_DIR)/farm_replay.cpp \
		$(BUILD_DIR)/template_match.o $(BUILD_DIR)/ocr_engine_wrt.o \
		-ld3d11 -ldxgi -lruntimeobject -lole32 -loleaut32 -lgdi32 -luser32 \
		$(OPENCV_LIBS)
	@echo "[OK] Built $(BUILD_DIR)/farm_replay.exe"
	@echo "Run: $(BUILD_DIR)/farm_replay.exe <frame.png> [lang]"

clean:
	@rm -rf $(BUILD_DIR) $(DIST_DIR)

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
	$(CC) $(CFLAGS) -DTEST_STANDALONE -mconsole -municode -o $(BUILD_DIR)/test_capture_gdi.exe \
		$(TEST_DIR)/test_capture_gdi.c \
		$(SRC_LOADER)/screen_capture_gdi.c \
		-lgdi32 -luser32
	@echo "[OK] Built $(BUILD_DIR)/test_capture_gdi.exe"

# WGC capture test (background + minimized window support)
test-capture-wgc: dirs
	$(CXX) $(CXXFLAGS) -mconsole -municode -I$(SRC_LOADER) \
		-o $(BUILD_DIR)/test_capture_wgc.exe \
		$(TEST_DIR)/test_wgc_mini.cpp \
		$(SRC_LOADER)/screen_capture_wgc.cpp \
		-ld3d11 -ldxgi -lruntimeobject -lole32 -lgdi32 -luser32
	@echo "[OK] Built $(BUILD_DIR)/test_capture_wgc.exe"
	@echo "Run: $(BUILD_DIR)/test_capture_wgc.exe"

# WGC + OCR pipeline test (capture + recognize text)
test-ocr: dirs
	$(CXX) $(CXXFLAGS) -mconsole -municode -I$(SRC_LOADER) \
		-o $(BUILD_DIR)/test_ocr_mini.exe \
		$(TEST_DIR)/test_ocr_mini.cpp \
		$(SRC_LOADER)/screen_capture_wgc.cpp \
		$(SRC_LOADER)/ocr_engine_wrt.cpp \
		-ld3d11 -ldxgi -lruntimeobject -lole32 -lgdi32 -luser32
	@echo "[OK] Built $(BUILD_DIR)/test_ocr_mini.exe"
	@echo "Run: $(BUILD_DIR)/test_ocr_mini.exe [\"Window Title\"] [lang]"

# Farm navigation test (remove car mode 2)
test-farm-remove: dirs
	$(CXX) $(CXXFLAGS) -mconsole -municode -I$(SRC_LOADER) \
		-o $(BUILD_DIR)/test_farm_remove.exe \
		$(TEST_DIR)/test_farm_remove.cpp \
		$(SRC_LOADER)/screen_capture_wgc.cpp \
		$(SRC_LOADER)/ocr_engine_wrt.cpp \
		$(SRC_LOADER)/farm_nav.c \
		$(SRC_LOADER)/input_hook_backend.c \
		$(SRC_LOADER)/logger.c \
		-ld3d11 -ldxgi -lruntimeobject -lole32 -lgdi32 -luser32
	@echo "[OK] Built $(BUILD_DIR)/test_farm_remove.exe"
	@echo "Run: $(BUILD_DIR)/test_farm_remove.exe [count]"

# Background mouse-click feasibility POC (WGC + focus-ring observation)
test-click-poc: dirs
	$(CXX) $(CXXFLAGS) -mconsole -municode -I$(SRC_LOADER) \
		-o $(BUILD_DIR)/test_click_poc.exe \
		$(TEST_DIR)/test_click_poc.cpp \
		$(SRC_LOADER)/screen_capture_wgc.cpp \
		$(SRC_LOADER)/input_hook_backend.c \
		-ld3d11 -ldxgi -lruntimeobject -lole32 -lgdi32 -luser32
	@echo "[OK] Built $(BUILD_DIR)/test_click_poc.exe"
	@echo "Run: $(BUILD_DIR)/test_click_poc.exe [\"Window Title\"]"

# Navigation exploration test (find buy car path)
test-nav-buy: dirs
	$(CXX) $(CXXFLAGS) -mconsole -municode -I$(SRC_LOADER) \
		-o $(BUILD_DIR)/test_nav_buy.exe \
		$(TEST_DIR)/test_nav_buy.cpp \
		$(SRC_LOADER)/screen_capture_wgc.cpp \
		$(SRC_LOADER)/ocr_engine_wrt.cpp \
		$(SRC_LOADER)/input_hook_backend.c \
		-ld3d11 -ldxgi -lruntimeobject -lole32 -lgdi32 -luser32
	@echo "[OK] Built $(BUILD_DIR)/test_nav_buy.exe"

# OpenCV smoke test: WGC capture + matchTemplate on a known template
test-opencv: dirs
	$(CXX) $(CXXFLAGS) $(OPENCV_CFLAGS) -mconsole -municode -I$(SRC_LOADER) \
		-o $(BUILD_DIR)/test_opencv.exe \
		$(TEST_DIR)/test_opencv_smoke.cpp \
		$(SRC_LOADER)/screen_capture_wgc.cpp \
		-ld3d11 -ldxgi -lruntimeobject -lole32 -lgdi32 -luser32 \
		$(OPENCV_LIBS)
	@echo "[OK] Built $(BUILD_DIR)/test_opencv.exe"
	@echo "Run: $(BUILD_DIR)/test_opencv.exe"

# Batch template match validation (all templates against live/fixture frame)
test-match-all: dirs
	$(CXX) $(CXXFLAGS) $(OPENCV_CFLAGS) -mconsole -municode -I$(SRC_LOADER) \
		-o $(BUILD_DIR)/test_match_all.exe \
		$(TEST_DIR)/test_match_all.cpp \
		$(SRC_LOADER)/template_match.cpp \
		$(SRC_LOADER)/screen_capture_wgc.cpp \
		-ld3d11 -ldxgi -lruntimeobject -lole32 -lgdi32 -luser32 \
		$(OPENCV_LIBS)
	@echo "[OK] Built $(BUILD_DIR)/test_match_all.exe"
	@echo "Run: $(BUILD_DIR)/test_match_all.exe [fixture.png]"

# End-to-end buy car flow test
test-buy-car: dirs
	$(CXX) $(CXXFLAGS) $(OPENCV_CFLAGS) -mconsole -municode -I$(SRC_LOADER) \
		-o $(BUILD_DIR)/test_buy_car.exe \
		$(TEST_DIR)/test_buy_car.cpp \
		$(SRC_LOADER)/template_match.cpp \
		$(SRC_LOADER)/game_input.c \
		$(SRC_LOADER)/farm_flow.c \
		$(SRC_LOADER)/screen_capture_wgc.cpp \
		-ld3d11 -ldxgi -lruntimeobject -lole32 -lgdi32 -luser32 \
		$(OPENCV_LIBS)
	@echo "[OK] Built $(BUILD_DIR)/test_buy_car.exe"
	@echo "Run: $(BUILD_DIR)/test_buy_car.exe [count]"

# Step-by-step buy car diagnostic (saves labeled screenshots)
test-buy-diag: dirs
	$(CXX) $(CXXFLAGS) $(OPENCV_CFLAGS) -mconsole -municode -I$(SRC_LOADER) \
		-o $(BUILD_DIR)/test_buy_diag.exe \
		$(TEST_DIR)/test_buy_diag.cpp \
		$(SRC_LOADER)/template_match.cpp \
		$(SRC_LOADER)/game_input.c \
		$(SRC_LOADER)/screen_capture_wgc.cpp \
		-ld3d11 -ldxgi -lruntimeobject -lole32 -lgdi32 -luser32 \
		$(OPENCV_LIBS)
	@echo "[OK] Built $(BUILD_DIR)/test_buy_diag.exe"
	@echo "Run: $(BUILD_DIR)/test_buy_diag.exe"

test-focus: dirs
	$(CXX) $(CXXFLAGS) $(OPENCV_CFLAGS) -mconsole \
		-o $(BUILD_DIR)/test_focus.exe \
		$(TEST_DIR)/test_focus.cpp \
		$(OPENCV_LIBS)
	@echo "[OK] Built $(BUILD_DIR)/test_focus.exe"

test-mouse-nav: dirs
	$(CXX) $(CXXFLAGS) $(OPENCV_CFLAGS) -mconsole -I$(SRC_LOADER) \
		-o $(BUILD_DIR)/test_mouse_nav.exe \
		$(TEST_DIR)/test_mouse_nav.cpp \
		$(SRC_LOADER)/template_match.cpp \
		$(SRC_LOADER)/game_input.c \
		$(SRC_LOADER)/screen_capture_wgc.cpp \
		-ld3d11 -ldxgi -lruntimeobject -lole32 -lgdi32 -luser32 \
		$(OPENCV_LIBS)
	@echo "[OK] Built $(BUILD_DIR)/test_mouse_nav.exe"

test-nav3: dirs
	$(CXX) $(CXXFLAGS) $(OPENCV_CFLAGS) -mconsole -I$(SRC_LOADER) \
		-o $(BUILD_DIR)/test_nav3.exe \
		$(TEST_DIR)/test_nav3.cpp \
		$(SRC_LOADER)/template_match.cpp \
		$(SRC_LOADER)/game_input.c \
		$(SRC_LOADER)/screen_capture_wgc.cpp \
		-ld3d11 -ldxgi -lruntimeobject -lole32 -lgdi32 -luser32 \
		$(OPENCV_LIBS)
	@echo "[OK] Built $(BUILD_DIR)/test_nav3.exe"

test-flows: dirs
	$(CXX) $(CXXFLAGS) $(OPENCV_CFLAGS) -mconsole -municode -I$(SRC_LOADER) \
		-o $(BUILD_DIR)/test_flows.exe \
		$(TEST_DIR)/test_flows.cpp \
		$(SRC_LOADER)/farm_flow.c \
		$(SRC_LOADER)/template_match.cpp \
		$(SRC_LOADER)/game_input.c \
		$(SRC_LOADER)/screen_capture_wgc.cpp \
		-ld3d11 -ldxgi -lruntimeobject -lole32 -lgdi32 -luser32 \
		$(OPENCV_LIBS)
	@echo "[OK] Built $(BUILD_DIR)/test_flows.exe"

# Xbox share-code popup diagnostic (enumerate windows + test input injection)
test-popup-probe: dirs
	$(CXX) -O2 -std=c++20 -mconsole -o $(BUILD_DIR)/test_popup_probe.exe \
		$(TEST_DIR)/test_popup_probe.cpp \
		-luser32 -lpsapi -lole32 -loleaut32
	@echo "[OK] Built $(BUILD_DIR)/test_popup_probe.exe"
	@echo "Run: $(BUILD_DIR)/test_popup_probe.exe [watch|list|children <hwnd>|post <hwnd> <digits>|sendinput <digits>]"

# OCR economy calibration probe: dump all OCR words + coords, save frame
test-economy: dirs
	$(CXX) $(CXXFLAGS) $(OPENCV_CFLAGS) -mconsole -municode -I$(SRC_LOADER) \
		-o $(BUILD_DIR)/test_economy.exe \
		$(TEST_DIR)/test_economy.cpp \
		$(SRC_LOADER)/farm_economy.c \
		$(SRC_LOADER)/screen_capture_wgc.cpp \
		$(SRC_LOADER)/ocr_engine_wrt.cpp \
		-ld3d11 -ldxgi -lruntimeobject -lole32 -loleaut32 -lgdi32 -luser32 \
		$(OPENCV_LIBS)
	@echo "[OK] Built $(BUILD_DIR)/test_economy.exe"
