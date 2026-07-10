# FH6 FocusKeeper

**Forza Horizon 6 Anti-Pause + Full Auto Farm** — Keeps the game running when it loses focus, and can farm skill points, buy cars, run super wheelspins, and clear the garage in the background.

[🌐 中文](../README.md)

## Features

- **Anti-Pause**: DLL hook intercepts focus-loss messages so the game does not pause in the background
- **Full Auto Farm**: One-click loop of race → read balance → buy cars → super wheelspin → remove cars, entirely in the background
- **True Background**: Window capture + PostMessage injection — the game need not be focused; you can use the PC normally
- **Two Profile Types**: Race profiles (share code / race car) and car profiles (buy / spin / remove car) are separate
- **Mute Control**: Mute or unmute game audio anytime
- **Universal**: Steam and Microsoft Store builds
- **Multilingual**: Simplified Chinese / Traditional Chinese / English (follows system language by default)
- **High-DPI**: Scales for 2K / 4K
- **System Tray**: Minimize to tray
- **Hotkeys**: Ctrl+F12 toggles anti-pause; F8 stops / F9 pauses the farm pipeline
- **Prevent Sleep**: Optional while anti-pause is active
- **Version Check**: Notifies when a newer GitHub release is available

## Prerequisites

> ⚠️ **Run the game in windowed mode** (Alt+Enter). This tool only works in windowed mode.

- Windows 10 / Windows 11
- Forza Horizon 6 (Steam or Microsoft Store)
- **Run as Administrator** (required to inject into the game process)
- Full package: `FocusKeeper.exe`, `hook.dll`, `assets/templates/`, `profiles/` (including `cars/`)  
  Do not copy only the exe — missing templates or profiles will break farming

## Usage

### 1. First Launch

1. Extract the release zip and keep the folder layout intact
2. **Right-click → Run as administrator** on `FocusKeeper.exe`
3. Start the game and switch to **windowed mode** (Alt+Enter)
4. Open the **Status** tab → click **Find Game Window**
5. Click **Enable Anti-Pause** (or press **Ctrl+F12**)

The game will no longer pause when it loses focus. Use **Mute Game** on the same tab if you want silence.

> The farm pipeline does **not** enable anti-pause by itself. Turn it on before AFK, or the game may pause when you switch away.

### 2. Full Auto Farm (main feature)

Open the **Auto Race** tab — this is the farm pipeline UI.

#### What one cycle does

```
Race (farm SP) → Read CR/SP → Buy cars → Super wheelspin → Remove cars → next cycle
```

- **Race**: Enters EventLab with the share code from the race profile and farms skill points
- **Buy / Spin / Remove**: Uses the **car** profile to buy consumable cars, spin, then clear them

The car you race and the car you buy for wheelspins are configured separately.

#### Pick both profiles

| Dropdown | Files | Controls |
|----------|-------|----------|
| **Car** | `profiles/cars/<id>/` | Which car to buy / spin / remove; CR/SP cost; skill-tree path |
| **Race** | `profiles/*.ini` | EventLab share code; SP per lap; race-select car name + PI |

Bundled examples:

- Car: `profiles/cars/22B/` (Subaru 22B STI)
- Race: `profiles/341075827.ini` (share code `341075827`)

#### Parameters

| Option | Meaning |
|--------|---------|
| **Cycle steps** | Enable Race / Buy / Spin / Remove for this run |
| **Target SP** | Skill-point goal for the race step (e.g. 999); works with `SPPerLap` in the race ini for closed-loop top-ups |
| **Cycles** | How many full loops to run (default 1) |
| **Auto Qty** | When checked, buy count = `min(CR÷CostCR, SP÷CostSP)`; when unchecked, use **Manual Qty** |
| **Manual Qty** | Shared count for buy / spin / remove when Auto Qty is off |

Click **Refresh CR/SP** first to see whether the “can buy” number looks right.

#### Start farming

1. Leave the game near the **main menu** (the pipeline navigates itself, but avoid loading screens)
2. Enable **auto-steer assist** in-game for more stable racing
3. Click **Start Full Loop**
4. To stop: **Stop (F8)** or press **F8**; **F9** pauses / resumes

Use **Single Step** to run only race, buy, spin, or remove when debugging.

A yellow capture border on the game window while running is normal; it disappears when the pipeline stops.

### 3. Writing Profiles

#### Race profile (`profiles/*.ini`)

Fields the pipeline actually uses:

```ini
[Profile]
Name=341075827
SPPerLap=50
ShareCode=341075827
; Race car select via OCR (independent of the car profile)
CarName=Subaru 22B STI
CarPI=834
```

| Field | Notes |
|-------|-------|
| `ShareCode` | **Required**. EventLab blueprint code; race step is skipped if empty |
| `SPPerLap` | Approx. skill points per lap; used with Target SP for closed-loop racing |
| `CarName` / `CarPI` | OCR match on the race car-select screen; if omitted, OCR is skipped |

> Older `[Step0]` / `Mode=hold` blocks are a legacy timed-key script (hotkey Ctrl+F11), **not** the vision farm path. Day-to-day AFK only needs the fields above.

#### Car profile (`profiles/cars/<id>/car.ini`)

```ini
[Car]
Name=Subaru 22B STI
CostCR=81700
CostSP=30

[SkillTree]
Dirs=RIGHT,UP,UP,UP,LEFT
```

| Field | Notes |
|-------|-------|
| `CostCR` / `CostSP` | Per-car cost for Auto Qty |
| `Dirs` | Skill-tree click path for super wheelspin |

Optional car-specific templates in the same folder (override shared assets), e.g.  
`consumablecar.png`, `CCbrand.png`, `removecarobject.png`, `newCC.png`.

To farm a different lottery car: copy `cars/22B/`, rename it, and edit `car.ini` plus templates.

### 4. Hotkeys

| Hotkey | Action |
|--------|--------|
| **Ctrl+F12** | Toggle anti-pause |
| **F8** | Stop farm pipeline |
| **F9** | Pause / resume pipeline |
| **Ctrl+F11** | Legacy: toggle timed-key auto race (usually unused) |

### 5. Troubleshooting

| Symptom | What to try |
|---------|-------------|
| Missing templates | Start from the release root; ensure `assets/templates/` exists |
| Game window not found | Windowed mode first, then **Find Game Window** on Status |
| Game pauses when unfocused | Enable anti-pause (Ctrl+F12) |
| “Can buy” is 0 | Not enough CR or SP; race first, or lower target / cheaper car |
| Race step skipped | Race ini missing `ShareCode` |
| Bought but no spins | Wrong car profile selected; spin needs that car’s templates and skill path |
| Yellow border stays | Pipeline still running or crashed; Stop, then retry or restart the tool |

## How It Works (brief)

### Anti-Pause

`SetWindowsHookEx(WH_CALLWNDPROC)` injects `hook.dll`, subclasses the game window, and drops focus-loss messages (`WM_ACTIVATEAPP`, `WM_KILLFOCUS`, `WM_NCACTIVATE`, `WM_ACTIVATE`, …).

### Auto Farm

Captures the game window (WGC), matches UI with templates / OCR, and sends input via `PostMessage` without stealing focus. Share codes: if the Xbox/Store UIA text popup is present, fill it with UI Automation; otherwise type into the game window (Steam-style `WM_CHAR`).

## Building

### Requirements

- [w64devkit](https://github.com/skeeto/w64devkit) or MSYS2 MinGW64 (OpenCV and other deps — see `Makefile`)

### Commands

```bash
# Dev / debug build (farm pipeline + debug channel)
make farm
# → build/FocusKeeper.exe

# Release package (no debug code)
make farm-release
# → dist/ runnable folder

make clean
make rebuild
```

> Plain `make` does **not** include the farm pipeline. End users should download a Release or run `make farm-release`.

## Project Structure

```
FH6FocusKeeper/
├── src/hook/                 # Anti-pause DLL
├── src/loader/               # App, GUI, farm pipeline, OCR, capture…
├── assets/templates/         # Shared UI templates
├── data/profiles/            # Race inis + cars/<id>/ car profiles
├── res/                      # Icon, manifest, resources
├── Makefile
└── README.md
```

## Acknowledgments

The auto-farm flow design and UI template images in this project draw inspiration from the open-source project [**fh6auto (YOUSTHEONE/FH6Auto)**](https://github.com/YOUSTHEONE/FH6Auto). Its modular approach to loop racing, bulk car buying, super wheelspins, and full AFK cycles — as well as share-code entry and target-car matching — directly informed this project. Sincere thanks to the fh6auto author.

## Authors

NEETLee & Claude Opus 4.6

## License

MIT License
