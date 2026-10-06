# Pomodoro Desk

A tiny always-on-top Pomodoro timer, built natively for Windows, Linux, and macOS: dark card, Focus/Short break/Long break tabs, a circular progress ring, Start/Reset, and a 4-dot session tracker — fully resizable (aspect-locked, so the whole layout always scales together), shows up in the taskbar/Dock, and can be minimized or closed normally. Each platform build is a thin UI layer over a shared, pure, unit-tested timer engine.

```
core/     TimerEngine — pure C++ Pomodoro-cycle logic, shared by windows/ and linux/, plus its test suite
windows/  Win32 + GDI UI (pomodoro.cpp), app icon (pomodoro.ico/.rc) + NSIS installer script
assets/   icon.png (1024px master) and make_icon.ps1, which regenerates it and the .ico
linux/    X11 + Xft UI (pomodoro.cpp)
swift/    SwiftPM package: PomodoroCore (Swift port of the same logic, with its own test suite)
          + PomodoroDesk (AppKit/SwiftUI UI)
```

## Features

- Focus (25 min) / Short break (5 min) / Long break (15 min) tabs — tap any tab to switch directly
- Automatic cycle: after a Focus session, moves to a Short break (or a Long break every 4th session), then back to Focus — tracked by the 4-dot indicator and session counter
- Circular progress ring fills in as the current session counts down
- Drag anywhere on the widget to move it; drag an edge/corner to resize — the aspect ratio is locked, so a single-edge drag still scales the whole layout, down to a sensible minimum size
- Shows up in the taskbar (Windows/Linux) / Dock (macOS); has working minimize and close controls
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
windres windows/pomodoro.rc -O coff -o pomodoro_res.o
g++ -O2 -s -mwindows -static windows/pomodoro.cpp core/TimerEngine.cpp pomodoro_res.o -o pomodoro.exe -lgdi32 -luser32
```

**Windows installer (NSIS):**
```
mkdir -p dist
g++ ... -o dist/pomodoro.exe ...          # as above, output into dist/
makensis windows/installer.nsi            # produces dist/PomodoroDeskSetup.exe
```

**Windows (MSVC):**
```
rc windowspomodoro.rc
cl /O2 /EHsc windowspomodoro.cpp coreTimerEngine.cpp windowspomodoro.res user32.lib gdi32.lib /link /SUBSYSTEM:WINDOWS
```

**Linux (X11):**
```
g++ -O2 -s linux/pomodoro.cpp core/TimerEngine.cpp -o pomodoro $(pkg-config --cflags --libs x11 xext xft)
```
Requires the X11/Xext/Xft dev headers (e.g. `libX11-devel libXext-devel libXft-devel` on Fedora, `libx11-dev libxext-dev libxft-dev` on Debian/Ubuntu). It's a normal, window-manager-managed window with decorations disabled via Motif hints (not an override-redirect popup), so it appears in the taskbar/pager like any other app.

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

Both suites cover the same cases (initial state, start/pause/resume, toggle round-trips, mode switching, reset, the full Focus → Short break → ... → Long break → Focus cycle including the 4-session dot tracker, and completion semantics) so behavior stays identical across platforms.

## Usage

- Tap a tab to jump directly to Focus, Short break, or Long break.
- The pill button starts, pauses, or resumes depending on the current state.
- Completing a Focus session automatically advances to the next break (Long break every 4th); completing a break returns to Focus. Reset restarts the current session without affecting the session count.
