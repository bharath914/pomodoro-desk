# Pomodoro Desk

A tiny always-on-top countdown timer overlay for Windows. Single `.cpp` file, Win32 + GDI only — no runtime dependencies, no installer, ~25 KB static exe.

## Features

- Flat, minimal dark design (Segoe UI / Segoe UI Light, closest Windows equivalent to Apple's system font)
- Drag anywhere on the widget to move it
- Controls (play/pause, +5 min, +10 min, reset, close) are hidden until you hover over the widget
- Right-click for duration presets: 25 / 30 / 45 min, 1 hour, 2 hours, or a custom value
- `Space` = start/pause, `Esc` = quit
- Negligible CPU/RAM use — safe on low-end PCs

## Download

Grab `pomodoro.exe` from the [Releases](../../releases) page — no installation needed, just run it.

## Build from source

**MinGW (also used to cross-compile from Linux):**
```
g++ -O2 -s -mwindows -static pomodoro.cpp -o pomodoro.exe -lgdi32 -luser32 -lcomctl32
```

**MSVC:**
```
cl /O2 /EHsc pomodoro.cpp user32.lib gdi32.lib comctl32.lib /link /SUBSYSTEM:WINDOWS
```

## Usage

- Hover over the widget to reveal the control row.
- Right-click to pick a session length (25/30/45/60/120 min) or enter a custom one.
- `+5` / `+10` extend the current session without resetting it.
- Reset (`⟲`) returns to the currently selected duration.
