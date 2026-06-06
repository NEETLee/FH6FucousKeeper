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
WINDRES = windres

# Compiler flags
CFLAGS = -O2 -Wall -Wextra -std=gnu11
CFLAGS += -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN
CFLAGS += -D_WIN32_IE=0x0600 -D_WIN32_WINNT=0x0601 -DNTDDI_VERSION=0x06010000
CFLAGS += -I./src -I./src/loader -I./res
CFLAGS += -Wno-cast-function-type

# Linker flags
LDFLAGS_DLL = -shared -Wl,--out-implib,build/libhook.a
LDFLAGS_EXE = -mwindows -L./build -lhook -lcomctl32 -lpsapi -lole32 -loleaut32 -lgdi32 -lwinhttp

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
             $(SRC_LOADER)/msg_replay.c \
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
             $(SRC_LOADER)/version_check.c

# Output
DLL_OUT = $(BUILD_DIR)/hook.dll
EXE_OUT = $(BUILD_DIR)/FocusKeeper.exe
RES_OBJ = $(BUILD_DIR)/app_res.o

# ─── Targets ──────────────────────────────────────────────────────────

.PHONY: all clean rebuild dll exe dirs

all: dirs dll exe

dirs:
	@mkdir -p $(BUILD_DIR)

dll: dirs $(DLL_OUT)

exe: dirs $(EXE_OUT)

$(DLL_OUT): $(HOOK_SRC) $(HOOK_DEF)
	$(CC) $(CFLAGS) -DHOOK_EXPORTS $(LDFLAGS_DLL) -o $@ $(HOOK_SRC) $(HOOK_DEF) -luser32

$(RES_OBJ): $(RES_DIR)/app.rc $(RES_DIR)/resource.h $(RES_DIR)/app.manifest
	$(WINDRES) -c 65001 -I$(RES_DIR) $(RES_DIR)/app.rc -o $@

$(EXE_OUT): $(LOADER_SRC) $(RES_OBJ) $(DLL_OUT)
	$(CC) $(CFLAGS) -o $@ $(LOADER_SRC) $(RES_OBJ) $(LDFLAGS_EXE)

clean:
	@rm -rf $(BUILD_DIR)

rebuild: clean all

# ─── Development helpers ──────────────────────────────────────────────

debug: CFLAGS += -g -DDEBUG -O0
debug: all

run: all
	@echo "[!] Running with admin privileges..."
	@$(BUILD_DIR)/FocusKeeper.exe
