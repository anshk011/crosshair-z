# RecoilCrosshair

A lightweight, pixel-perfect crosshair overlay for Windows built with Win32 + GDI.  
Zero injection. Zero game memory access. Fully undetectable by anti-cheat.

![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-blue)
![Language](https://img.shields.io/badge/language-C%2B%2B-orange)
![License](https://img.shields.io/badge/license-MIT-green)
![Build](https://github.com/anshk011/crosshair-z/actions/workflows/build.yml/badge.svg)

---

## What It Does

Draws a transparent, always-on-top, click-through crosshair overlay at the exact
center of your primary monitor. Works on top of any game running in
**Borderless Windowed** mode.

### Crosshair Design — "Recoil"

| Layer | Arms | Idle Color | Fire Color |
|-------|------|------------|------------|
| 1 | Top only | `#00FFF5` (cyan) | — |
| 2 | Left / Right / Bottom | `#00FFFF` (cyan) | `#FF0000` (red) |

- Arm length: 4px — Thickness: 2px — Gap from center: 4px
- No center dot — No outline

### Firing Animation

When you **hold left mouse button**:
- Top arm blooms outward (4px → 136px) over the gun's spray duration
- Left / Right / Bottom arms turn **red**
- On **release** → instantly resets to idle

---

## Gun Profiles

| # | Gun | Fire Rate | Spray Duration |
|---|-----|-----------|----------------|
| 0 | **Vandal** | 9.75 rds/s | 3070ms |
| 1 | **Phantom** | 11 rds/s | 2720ms |
| 2 | **Spectre** | 13.33 rds/s | 2250ms |
| 3 | **Bulldog** | 9.15 rds/s | 3270ms |

Change the active profile in `crosshair.cpp`:
```cpp
static const int GUN_PROFILE = 0;  // 0=Vandal  1=Phantom  2=Spectre  3=Bulldog
```
Then recompile.

---

## Controls

| Action | Shortcut |
|--------|----------|
| Toggle overlay on/off | `Ctrl + Shift + X` |
| Enable / Disable / Quit | Right-click tray icon |

---

## How It Works (Technical)

```
Windows Desktop
├── Your Game  (separate process — NEVER touched)
└── RecoilCrosshair.exe  (separate process)
    ├── WS_EX_LAYERED      → hardware-composited alpha window
    ├── WS_EX_TRANSPARENT  → all mouse clicks pass through to game
    ├── WS_EX_TOPMOST      → always above game window
    ├── UpdateLayeredWindow → renders directly into DWM compositor
    ├── WH_MOUSE_LL hook   → detects LMB globally (same as Logitech/Razer software)
    └── RegisterHotKey     → Ctrl+Shift+X toggle
```

No DLL injection. No game memory reads. No kernel drivers.  
The game cannot detect this because there is nothing to detect.

---

## Build

### Requirements

- Windows 10 or 11
- **MinGW-w64** (GCC) — download from [winlibs.com](https://winlibs.com)  
  OR **Visual Studio** (any version with C++ workload)

---

### Option A — MinGW (Recommended)

**1. Install MinGW-w64**

Download the latest release from [winlibs.com](https://winlibs.com):
- Choose: `UCRT runtime` → `x86_64` → `.zip` (no installer)
- Extract to `C:\mingw64`
- Add `C:\mingw64\bin` to your system `PATH`

Verify:
```cmd
g++ --version
```

**2. Clone the repo**

```cmd
git clone https://github.com/anshk011/crosshair-z.git
cd crosshair-z
```

**3. Build**

Double-click `build.bat` OR run in cmd:
```cmd
g++ -O2 -mwindows -o RecoilCrosshair.exe crosshair.cpp -lgdi32 -lwinmm -lshell32 -luser32
```

**4. Run**

```cmd
RecoilCrosshair.exe
```

---

### Option B — Visual Studio (MSVC)

1. Open **Developer Command Prompt for VS** (search in Start menu)
2. `cd` to the repo folder
3. Run `build_msvc.bat` OR:

```cmd
cl /O2 /EHsc crosshair.cpp /link gdi32.lib winmm.lib shell32.lib user32.lib /SUBSYSTEM:WINDOWS /OUT:RecoilCrosshair.exe
```

---

### Option C — Download Pre-built .exe

Go to [Releases](https://github.com/anshk011/crosshair-z/releases) and
download the latest `RecoilCrosshair.exe`. No install required — just run it.

---

## Customization

All values are at the top of `crosshair.cpp`:

```cpp
// Switch gun profile
static const int GUN_PROFILE = 0;  // 0=Vandal 1=Phantom 2=Spectre 3=Bulldog

// Add your own gun
static const GunProfile GUNS[] = {
    { L"Vandal",  3070.0f, 0.0f, 136.0f },
    { L"Phantom", 2720.0f, 0.0f, 136.0f },
    { L"Spectre", 2250.0f, 0.0f, 136.0f },
    { L"Bulldog", 3270.0f, 0.0f, 136.0f },
    { L"MyGun",   2500.0f, 0.0f, 100.0f },  // ← add here
};

// Crosshair size
static const int CH_LENGTH = 4;   // arm length (px)
static const int CH_THICK  = 2;   // arm thickness (px)
static const int CH_OFFSET = 4;   // gap from center (px)

// Colors (RGB bytes)
static const BYTE L1_R = 0x00, L1_G = 0xFF, L1_B = 0xF5;  // top arm
static const BYTE L2_R = 0x00, L2_G = 0xFF, L2_B = 0xFF;  // idle arms
static const BYTE L2F_R = 0xFF, L2F_G = 0x00, L2F_B = 0x00; // fire color
```

---

## Anti-Cheat Safety

| Anti-Cheat | Safe? | Reason |
|------------|-------|--------|
| Vanguard (Valorant) | ✅ | No process injection, no memory access |
| VAC (CS2) | ✅ | External window only |
| EAC (Apex, Fortnite) | ✅ | No kernel hooks |
| BattlEye (R6, Warzone) | ✅ | No suspicious API patterns |

This is functionally identical to taping a dot on your monitor — the game
has zero awareness of it.

---

## Project Structure

```
RecoilCrosshair/
├── crosshair.cpp          ← entire application (single file)
├── build.bat              ← MinGW build script
├── build_msvc.bat         ← MSVC build script
├── .github/
│   └── workflows/
│       └── build.yml      ← GitHub Actions CI (auto-builds on push)
├── .gitignore
├── LICENSE
└── README.md
```

---

## License

MIT — do whatever you want, just don't claim you wrote it.
