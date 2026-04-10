# WinSnap

A lightweight Windows utility that lets you manage windows using the Win key + mouse, similar to Alt+drag on Linux.

## Features

| Shortcut | Action |
|---|---|
| **Win + Left Click Drag** | Move any window by clicking anywhere on it |
| **Win + Right Click Drag** | Resize a window by dragging the nearest corner/edge |
| **Win + Double Click** | Toggle maximize/restore |
| **Win + X** | Close the window under the cursor (respects save dialogs) |
| **Drag to screen edge** | Snap left/right/maximize (simulates Win+Arrow) |

### Details

- **Move**: Hold Win, left-click anywhere on a window, and drag. Maximized windows are restored on first movement, centered on the cursor.
- **Resize**: Hold Win, right-click on a window, and drag. The nearest quadrant determines which corner moves — the opposite corner stays fixed. Does not apply to maximized windows.
- **Close**: Hold Win, hover over a window, press X. Sends `WM_CLOSE`, so applications can show "unsaved changes" dialogs.
- **Snap**: When releasing a dragged window near a screen edge, it triggers native Windows snap (top = maximize, left/right = half-screen).
- The Start menu is suppressed after any Win+click action but works normally on a plain Win tap.
- Works with admin/elevated windows when run as administrator.
- Skips non-draggable surfaces: desktop, taskbar, and non-resizable windows.

## Building

Requires MSVC (Visual Studio 2022) or MinGW GCC.

**MSVC:**
```
build.bat
```

**GCC:**
```
gcc winsnap.c -o winsnap.exe -mwindows
```

## Installation

### Run at startup (recommended)

Create a scheduled task that runs at logon with highest privileges (no UAC prompt):

```
schtasks /Create /TN "WinSnap" /TR "C:\path\to\winsnap.exe" /SC ONLOGON /RL HIGHEST /F
```

Run this from an elevated command prompt.

### Manual

Run `winsnap.exe` directly. If built with the admin manifest, it will prompt for UAC elevation.

## Stopping

Kill `winsnap.exe` from Task Manager (look under the **Details** tab).

## How it works

WinSnap uses low-level mouse and keyboard hooks (`WH_MOUSE_LL`, `WH_KEYBOARD_LL`) to intercept input system-wide. Window movement and resizing is done with `SetWindowPos` — the hook callback never blocks, avoiding the timeout issues that plague approaches based on the native modal move loop.
