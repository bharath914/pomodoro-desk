# Pomodoro Desk

A tiny always-on-top countdown timer overlay, built natively for Windows, Linux, and macOS: black rounded card, a big "Digital Numbers" LCD-style readout, a Start/Pause/Resume pill, a ±10-minute stepper, and reset — fully resizable, everything scaling together. Each platform build is a thin UI layer over a shared, pure, unit-tested timer engine.

```
core/     TimerEngine — pure C++ countdown logic, shared by windows/ and linux/, plus its test suite
windows/  Win32 + GDI UI (pomodoro.cpp) + NSIS installer script
linux/    X11 + Xft UI (pomodoro.cpp)
swift/    SwiftPM package: PomodoroCore (Swift port of the same logic, with its own test suite)
          + PomodoroDesk (AppKit/SwiftUI UI)
assets/   Digital Numbers font (OFL-licensed), embedded/loaded at runtime on every platform —
          not installed system-wide
```

## Features

- Drag anywhere on the widget to move it; drag an edge/corner to resize it to any size (there's a minimum size that keeps every control legible)
- Right-click for duration presets: 25 / 30 / 45 min, 1 hour, 2 hours, or a custom value
- `-`/`+` stepper adjusts the current session by 10 minutes at a time, while running or paused
- `Space` = start/pause, `Esc` = quit
- Negligible CPU/RAM use — safe on low-end machines

## Windows: install, search, pin to taskbar

Download `PomodoroDeskSetup.exe` from the [Releases](../../releases) page and run it (no admin rights needed — it installs to your user profile). This adds a Start Menu entry, so you can:
- find it via Windows Search (just start typing "Pomodoro"),
- right-click it in Search/Start and choose **Pin to taskbar**.

A portable `pomodoro.exe` (just unzip and run, no installer) is also attached to the release, but it won't be indexed by Windows Search or Start — use the installer for that.

## Build from source

**Windows (MinGW, also used to cross-compile from Linux):**
```
g++ -O2 -s -mwindows -static windows/pomodoro.cpp core/TimerEngine.cpp -o pomodoro.exe -lgdi32 -luser32 -lcomctl32
```
Copy `assets/fonts/DigitalNumbers-Regular.ttf` next to the resulting `pomodoro.exe` — it's loaded privately at startup (no system font install needed).

**Windows installer (NSIS):**
```
mkdir -p dist
g++ ... -o dist/pomodoro.exe ...          # as above, output into dist/
cp assets/fonts/DigitalNumbers-Regular.ttf dist/
makensis windows/installer.nsi            # produces dist/PomodoroDeskSetup.exe
```

**Windows (MSVC):**
```
cl /O2 /EHsc windows\pomodoro.cpp core\TimerEngine.cpp user32.lib gdi32.lib comctl32.lib /link /SUBSYSTEM:WINDOWS
```

**Linux (X11):**
```
g++ -O2 -s linux/pomodoro.cpp core/TimerEngine.cpp -o pomodoro $(pkg-config --cflags --libs x11 xext xft fontconfig)
cp assets/fonts/DigitalNumbers-Regular.ttf .   # next to the binary
```
Requires the X11/Xext/Xft/fontconfig dev headers (e.g. `libX11-devel libXext-devel libXft-devel fontconfig-devel` on Fedora, `libx11-dev libxext-dev libxft-dev libfontconfig-dev` on Debian/Ubuntu).

**macOS (Swift):**
```
cd swift
swift run PomodoroDesk
```
The font is bundled as a Swift package resource and registered in-process at launch — no install needed.

## Tests

**C++ core logic** (platform-independent, runs on any OS):
```
g++ -O2 -std=c++17 core/TimerEngine.cpp core/tests/TimerEngineTests.cpp -o timer_engine_tests
./timer_engine_tests
```

**Swift core logic:**
```
cd swift
swift test
```

Both suites cover the same cases (initial state, start/pause/resume, toggle round-trips, ±10-minute stepper while running vs. paused, reset, duration presets, completion semantics, custom-minute clamping) so behavior stays identical across platforms.

## Usage

- Right-click to pick a session length (25/30/45/60/120 min) or enter a custom one.
- `-`/`+` adjust the current session by 10 minutes without resetting it.
- The pill button starts, pauses, or resumes depending on the current state; the square icon button next to the stepper mirrors the same action, and the other square button resets to the selected duration.

## Font license

`assets/fonts/DigitalNumbers-Regular.ttf` is the "Digital Numbers" font by Stephan Ahlf, licensed under the SIL Open Font License 1.1 (full text in `assets/fonts/DigitalNumbers-OFL.txt`).
