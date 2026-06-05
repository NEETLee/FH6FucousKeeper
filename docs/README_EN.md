# FH6 FocusKeeper

**Forza Horizon 6 Anti-Pause Tool** — Prevents the game from pausing when the window loses focus.

[🌐 中文](../README.md)

## Features

- **Anti-Pause**: Intercepts window focus messages via DLL Hook so the game keeps running in the background
- **Mute Control**: Optionally mute game audio while anti-pause is active (great for AFK scenarios)
- **Universal**: Works with both Steam and Microsoft Store versions
- **Bilingual UI**: Chinese/English interface with auto system language detection
- **System Tray**: Minimizes to tray for silent background operation
- **Global Hotkey**: Ctrl+F12 to toggle anti-pause

## How It Works

Uses `SetWindowsHookEx(WH_CALLWNDPROC)` to install a global message hook, injecting `hook.dll` into the game process. The DLL subclasses the game window and silently drops these messages when the game loses focus:

| Message | Purpose |
|---------|---------|
| `WM_ACTIVATEAPP` | Application activation/deactivation |
| `WM_KILLFOCUS` | Keyboard focus loss |
| `WM_NCACTIVATE` | Non-client area activation change |
| `WM_ACTIVATE` | Window activation change |

The game "thinks" it always has focus. If DLL injection fails, it falls back to message replay mode (periodically sending activation messages).

## Building

### Requirements

- [w64devkit](https://github.com/skeeto/w64devkit) (portable MinGW-w64 toolchain, no installation needed)

### Build

```bash
# Open w64devkit terminal, navigate to project directory
cd /path/to/FH6FocusKeeper

# Build
make

# Output:
# build/FocusKeeper.exe  - Main application
# build/hook.dll         - Hook DLL (must be in the same directory as exe)
```

### Other Targets

```bash
make clean    # Remove build artifacts
make rebuild  # Clean + build
make debug    # Debug build (with symbols)
```

## Usage

1. Place `FocusKeeper.exe` and `hook.dll` in the same directory
2. **Run as Administrator** (required to inject into the game process)
3. Launch the game, then click "Find Game Window"
4. Click "Enable Anti-Pause" or press Ctrl+F12
5. For muted AFK mode, click "Anti-Pause + Mute"

## Project Structure

```
FH6FocusKeeper/
├── src/
│   ├── hook/
│   │   ├── hook.h          # Hook DLL interface
│   │   ├── hook.c          # Window subclass + message interception
│   │   └── hook.def        # Shared section definition
│   └── loader/
│       ├── main.c          # Entry point + module coordinator (Mediator)
│       ├── gui.c/h         # Win32 tabbed GUI
│       ├── tray.c/h        # System tray icon
│       ├── hook_manager.c/h # DLL loading manager (Facade)
│       ├── msg_replay.c/h  # Message replay fallback (Active Object)
│       ├── window_finder.c/h # Window finder (Strategy)
│       ├── audio_control.c/h # WASAPI per-process mute
│       ├── i18n.c/h        # Internationalization string tables
│       ├── logger.c/h      # Logging system (Observer)
│       └── settings.c/h    # INI configuration
├── res/
│   ├── resource.h          # Control IDs
│   ├── app.rc              # Resource script
│   ├── app.manifest        # Application manifest
│   └── app.ico             # Application icon
├── Makefile
└── README.md
```

## Design Patterns

- **Strategy**: WindowFinder uses different strategies to locate the game window
- **Observer**: Logger multi-callback notifications, HookManager state callbacks
- **Facade**: HookManager encapsulates DLL loading and hook installation
- **Mediator**: main.c coordinates inter-module communication
- **Active Object**: MsgReplay runs a dedicated thread for periodic messages
- **Chain of Responsibility**: SubclassProc message filtering chain

## License

MIT License
