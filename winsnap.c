/*
 * WinSnap - Win+LeftClick to drag, Win+RightClick to resize by quadrant.
 *
 * Build: cl winsnap.c /link user32.lib shell32.lib /SUBSYSTEM:WINDOWS /MANIFESTUAC:"level='requireAdministrator'"
 *    or: gcc winsnap.c -o winsnap.exe -mwindows -lshell32
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#define WM_TRAYICON   (WM_USER + 1)
#define ID_TRAY_EXIT  1001

enum Action { ACTION_NONE, ACTION_MOVE, ACTION_RESIZE };

static HHOOK          g_mouseHook;
static HHOOK          g_kbHook;
static enum Action    g_action;
static BOOL           g_needTaint;    /* an action happened, taint next Win key-up */
static BOOL           g_wasZoomed;    /* window was zoomed at drag start, defer restore */
static HWND           g_dragWindow;
static POINT          g_dragStart;
static POINT          g_windowStart;  /* top-left at drag start (for move) */
static RECT           g_windowRect;   /* full rect at drag start (for resize) */
static int            g_resizeEdgeX;  /* -1 = left, +1 = right */
static int            g_resizeEdgeY;  /* -1 = top,  +1 = bottom */
static DWORD          g_lastClickTime;  /* for double-click detection */
static POINT          g_lastClickPt;
static HWND           g_hwndMsg;
static NOTIFYICONDATA g_nid;

static BOOL IsWinKeyDown(void) {
    return (GetAsyncKeyState(VK_LWIN) & 0x8000) ||
           (GetAsyncKeyState(VK_RWIN) & 0x8000);
}

static HWND GetTopLevelWindow(HWND hwnd) {
    while (hwnd) {
        LONG style = GetWindowLong(hwnd, GWL_STYLE);
        if (style & (WS_OVERLAPPED | WS_POPUP))
            break;
        HWND parent = GetParent(hwnd);
        if (!parent || parent == GetDesktopWindow())
            break;
        hwnd = parent;
    }
    return hwnd;
}

static BOOL IsDraggableWindow(HWND hwnd) {
    if (!hwnd || hwnd == GetDesktopWindow() || hwnd == GetShellWindow())
        return FALSE;

    TCHAR cls[64] = {0};
    GetClassName(hwnd, cls, 64);
    if (lstrcmp(cls, TEXT("WorkerW")) == 0 ||
        lstrcmp(cls, TEXT("Progman")) == 0 ||
        lstrcmp(cls, TEXT("Shell_TrayWnd")) == 0 ||
        lstrcmp(cls, TEXT("Shell_SecondaryTrayWnd")) == 0)
        return FALSE;

    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    if (!(style & WS_THICKFRAME))
        return FALSE;

    return TRUE;
}

static void TaintWinKey(void) {
    INPUT inputs[2] = {0};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_NONAME;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_NONAME;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

static LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0)
        return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);

    MSLLHOOKSTRUCT *ms = (MSLLHOOKSTRUCT *)lParam;

    /* --- Start MOVE on Win + Left Click (with double-click detection) --- */
    if (wParam == WM_LBUTTONDOWN && IsWinKeyDown()) {
        HWND hwnd = WindowFromPoint(ms->pt);
        if (hwnd) {
            hwnd = GetTopLevelWindow(hwnd);
            if (IsDraggableWindow(hwnd)) {
                DWORD now = ms->time;
                DWORD dblClickTime = GetDoubleClickTime();
                int cxDbl = GetSystemMetrics(SM_CXDOUBLECLK) / 2;
                int cyDbl = GetSystemMetrics(SM_CYDOUBLECLK) / 2;
                BOOL isDblClick = (now - g_lastClickTime <= dblClickTime) &&
                    abs(ms->pt.x - g_lastClickPt.x) <= cxDbl &&
                    abs(ms->pt.y - g_lastClickPt.y) <= cyDbl;
                g_lastClickTime = now;
                g_lastClickPt   = ms->pt;

                if (isDblClick) {
                    /* Double click: toggle maximize */
                    if (IsZoomed(hwnd))
                        ShowWindow(hwnd, SW_RESTORE);
                    else
                        ShowWindow(hwnd, SW_MAXIMIZE);
                    g_needTaint  = TRUE;
                    g_action     = ACTION_NONE;
                    g_dragWindow = NULL;
                    g_lastClickTime = 0; /* prevent triple-click */
                    return 1;
                }

                /* Single click: start move */
                g_wasZoomed = IsZoomed(hwnd);

                if (!g_wasZoomed) {
                    RECT wr;
                    GetWindowRect(hwnd, &wr);
                    g_windowStart.x = wr.left;
                    g_windowStart.y = wr.top;
                }

                SetForegroundWindow(hwnd);
                g_dragWindow  = hwnd;
                g_dragStart   = ms->pt;
                g_action      = ACTION_MOVE;
                g_needTaint   = TRUE;
                return 1;
            }
        }
    }

    /* --- Start RESIZE on Win + Right Click --- */
    if (wParam == WM_RBUTTONDOWN && g_action == ACTION_NONE && IsWinKeyDown()) {
        HWND hwnd = WindowFromPoint(ms->pt);
        if (hwnd) {
            hwnd = GetTopLevelWindow(hwnd);
            if (IsDraggableWindow(hwnd) && !IsZoomed(hwnd)) {
                RECT wr;
                GetWindowRect(hwnd, &wr);
                g_windowRect = wr;

                /* Determine quadrant */
                int midX = (wr.left + wr.right) / 2;
                int midY = (wr.top + wr.bottom) / 2;
                g_resizeEdgeX = (ms->pt.x < midX) ? -1 : 1;
                g_resizeEdgeY = (ms->pt.y < midY) ? -1 : 1;

                SetForegroundWindow(hwnd);
                g_dragWindow  = hwnd;
                g_dragStart   = ms->pt;
                g_action      = ACTION_RESIZE;
                g_needTaint   = TRUE;
                return 1;
            }
        }
    }

    /* --- Mouse move --- */
    if (wParam == WM_MOUSEMOVE && g_action == ACTION_MOVE) {
        int dx = ms->pt.x - g_dragStart.x;
        int dy = ms->pt.y - g_dragStart.y;
        if (dx != 0 || dy != 0) {
            /* Deferred restore: only unzoom when the mouse actually moves */
            if (g_wasZoomed) {
                g_wasZoomed = FALSE;
                ShowWindow(g_dragWindow, SW_RESTORE);

                RECT wr;
                GetWindowRect(g_dragWindow, &wr);
                int winW = wr.right - wr.left;
                int winH = wr.bottom - wr.top;
                int newX = ms->pt.x - winW / 2;
                int newY = ms->pt.y - winH / 2;
                SetWindowPos(g_dragWindow, NULL, newX, newY, winW, winH,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                /* Reset drag origin so movement is smooth from here */
                g_windowStart.x = newX;
                g_windowStart.y = newY;
                g_dragStart = ms->pt;
            } else {
                SetWindowPos(g_dragWindow, NULL,
                             g_windowStart.x + dx, g_windowStart.y + dy, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
    }

    if (wParam == WM_MOUSEMOVE && g_action == ACTION_RESIZE) {
        int dx = ms->pt.x - g_dragStart.x;
        int dy = ms->pt.y - g_dragStart.y;

        int left   = g_windowRect.left;
        int top    = g_windowRect.top;
        int right  = g_windowRect.right;
        int bottom = g_windowRect.bottom;

        if (g_resizeEdgeX == -1) left  += dx; else right  += dx;
        if (g_resizeEdgeY == -1) top   += dy; else bottom += dy;

        /* Enforce minimum size */
        int minW = GetSystemMetrics(SM_CXMINTRACK);
        int minH = GetSystemMetrics(SM_CYMINTRACK);
        if (right - left < minW) {
            if (g_resizeEdgeX == -1) left = right - minW; else right = left + minW;
        }
        if (bottom - top < minH) {
            if (g_resizeEdgeY == -1) top = bottom - minH; else bottom = top + minH;
        }

        SetWindowPos(g_dragWindow, NULL,
                     left, top, right - left, bottom - top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    /* --- End move on left button up --- */
    if (wParam == WM_LBUTTONUP && g_action == ACTION_MOVE) {
        g_action     = ACTION_NONE;
        g_dragWindow = NULL;
        return 1;
    }

    /* --- End resize on right button up --- */
    if (wParam == WM_RBUTTONUP && g_action == ACTION_RESIZE) {
        g_action     = ACTION_NONE;
        g_dragWindow = NULL;
        return 1;
    }

    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

static LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && g_needTaint) {
        KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT *)lParam;
        if ((kb->vkCode == VK_LWIN || kb->vkCode == VK_RWIN) && wParam == WM_KEYUP) {
            g_needTaint = FALSE;
            TaintWinKey();
        }
    }
    return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON:
        if (LOWORD(lParam) == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, TEXT("Exit WinSnap"));
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_EXIT)
            PostQuitMessage(0);
        return 0;

    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    HANDLE mutex = CreateMutex(NULL, TRUE, TEXT("WinSnap_SingleInstance"));
    if (GetLastError() == ERROR_ALREADY_EXISTS)
        return 0;

    /* Create hidden message window */
    WNDCLASS wc = {0};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInst;
    wc.lpszClassName  = TEXT("WinSnapClass");
    RegisterClass(&wc);

    g_hwndMsg = CreateWindow(wc.lpszClassName, TEXT("WinSnap"), 0,
                             0, 0, 0, 0, HWND_MESSAGE, NULL, hInst, NULL);

    /* System tray icon */
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_hwndMsg;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = LoadIcon(NULL, IDI_APPLICATION);
    lstrcpy(g_nid.szTip, TEXT("WinSnap - Win+Drag/Resize windows"));
    Shell_NotifyIcon(NIM_ADD, &g_nid);

    /* Install hooks */
    g_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, hInst, 0);
    g_kbHook    = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, hInst, 0);
    if (!g_mouseHook || !g_kbHook) {
        Shell_NotifyIcon(NIM_DELETE, &g_nid);
        MessageBox(NULL, TEXT("Failed to install hooks."), TEXT("WinSnap"), MB_ICONERROR);
        return 1;
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(g_kbHook);
    UnhookWindowsHookEx(g_mouseHook);
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
