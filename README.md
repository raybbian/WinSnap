# WinSnap

A lightweight Windows utility that lets you manage windows using the Win key + mouse, similar to Alt+drag on Linux.

## Features

| Shortcut | Action |
|---|---|
| **Win + Left Click Drag** | Move any window by clicking anywhere on it |
| **Win + Right Click Drag** | Resize a window by dragging the nearest corner/edge |
| **Win + Double Click** | Toggle maximize/restore |
| **Win + X** | Close the window under the cursor |

### Details

- **Move**: Maximized windows are restored on first movement, centered on the cursor.
- **Resize**: The nearest quadrant determines which corner moves — the opposite corner stays fixed. Does not apply to maximized windows.
- **Close**: Sends `WM_CLOSE`, so applications can show "unsaved changes" dialogs.

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

Create a scheduled task that runs at logon with highest privileges (no UAC prompt):

```
schtasks /Create /TN "WinSnap" /TR "C:\path\to\winsnap.exe" /SC ONLOGON /RL HIGHEST /F
```

Run this from an elevated command prompt.

## Stopping

Kill `winsnap.exe` from Task Manager (Details tab).

## How it works

Uses low-level mouse and keyboard hooks (`WH_MOUSE_LL`, `WH_KEYBOARD_LL`) to intercept input system-wide. Window movement and resizing is done with `SetWindowPos` — the hook callback never blocks, avoiding the timeout issues that plague approaches based on the native modal move loop.
