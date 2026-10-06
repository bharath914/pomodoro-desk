# Pomodoro Desk

A tiny always-on-top countdown timer overlay, built natively for Windows, Linux, and macOS. Each platform build is a thin UI layer over a shared, pure, unit-tested timer engine — no business logic is duplicated by hand, and the logic is fully decoupled from rendering/design.

```
core/     TimerEngine — pure C++ countdown logic, shared by windows/ and linux/, plus its test suite
windows/  Win32 + GDI UI (pomodoro.cpp)
linux/    X11 + Xft UI (pomodoro.cpp)
swift/    SwiftPM package: PomodoroCore (Swift port of the same logic, with its own test suite)
          + PomodoroDesk (AppKit/SwiftUI placeholder UI)
```

The current UI on every platform is intentionally plain — it's a placeholder pending a design refresh. The behavior (duration presets, custom durations, +5/+10 extend, pause/resume/reset) is the stable, tested part.

## Features

- Drag anywhere on the widget to move it
- Windows/Linux: controls (play/pause, +5 min, +10 min, reset, close) are hidden until you hover over the widget
- Right-click (or context menu) for duration presets: 25 / 30 / 45 min, 1 hour, 2 hours, or a custom value
- `Space` = start/pause, `Esc` = quit
- Negligible CPU/RAM use — safe on low-end machines

## Download (Windows)

Grab `pomodoro.exe` from the [Releases](../../releases) page — no installation needed, just run it.

## Build from source

**Windows (MinGW, also used to cross-compile from Linux):**
```
g++ -O2 -s -mwindows -static windows/pomodoro.cpp core/TimerEngine.cpp -o pomodoro.exe -lgdi32 -luser32 -lcomctl32
```

**Windows (MSVC):**
```
cl /O2 /EHsc windows\pomodoro.cpp core\TimerEngine.cpp user32.lib gdi32.lib comctl32.lib /link /SUBSYSTEM:WINDOWS
```

**Linux (X11):**
```
g++ -O2 -s linux/pomodoro.cpp core/TimerEngine.cpp -o pomodoro $(pkg-config --cflags --libs x11 xext xft)
```
Requires the X11/Xext/Xft dev headers (e.g. `libX11-devel libXext-devel libXft-devel` on Fedora, `libx11-dev libxext-dev libxft-dev` on Debian/Ubuntu).

**macOS (Swift):**
```
cd swift
swift run PomodoroDesk
```

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

Both suites cover the same cases (initial state, start/pause/resume, toggle round-trips, +5/+10 while running vs. paused, reset, duration presets, completion semantics, custom-minute clamping) so behavior stays identical across platforms.

## Usage

- Right-click to pick a session length (25/30/45/60/120 min) or enter a custom one.
- `+5` / `+10` extend the current session without resetting it.
- Reset returns to the currently selected duration.
