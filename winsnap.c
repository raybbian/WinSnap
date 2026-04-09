/*
 * WinSnap - Hold Alt + left-click anywhere to drag windows.
 *
 * Build: cl winsnap.c /link user32.lib shell32.lib /SUBSYSTEM:WINDOWS /MANIFESTUAC:"level='requireAdministrator'"
 *    or: gcc winsnap.c -o winsnap.exe -mwindows -lshell32
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#define WM_TRAYICON   (WM_USER + 1)
#define ID_TRAY_EXIT  1001

static HHOOK          g_mouseHook;
static HHOOK          g_kbHook;
static BOOL           g_dragging;
static BOOL           g_didDrag;
static HWND           g_dragWindow;
static POINT          g_dragStart;
static POINT          g_windowStart;
static HWND           g_hwndMsg;
static NOTIFYICONDATA g_nid;

static BOOL IsAltKeyDown(void) {
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

static LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0)
        return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);

    MSLLHOOKSTRUCT *ms = (MSLLHOOKSTRUCT *)lParam;

    if (wParam == WM_LBUTTONDOWN && IsAltKeyDown()) {
        HWND hwnd = WindowFromPoint(ms->pt);
        if (hwnd) {
            hwnd = GetTopLevelWindow(hwnd);
            if (hwnd && hwnd != GetDesktopWindow() && hwnd != GetShellWindow()) {
                /* Skip non-draggable windows: desktop, taskbar, etc. */
                TCHAR cls[64] = {0};
                GetClassName(hwnd, cls, 64);
                if (lstrcmp(cls, TEXT("WorkerW")) == 0 ||
                    lstrcmp(cls, TEXT("Progman")) == 0 ||
                    lstrcmp(cls, TEXT("Shell_TrayWnd")) == 0 ||
                    lstrcmp(cls, TEXT("Shell_SecondaryTrayWnd")) == 0)
                    goto pass_through;

                LONG style = GetWindowLong(hwnd, GWL_STYLE);
                if (!(style & WS_CAPTION) && !(style & WS_THICKFRAME))
                    goto pass_through;
                if (IsZoomed(hwnd)) {
                    RECT wr;
                    GetWindowRect(hwnd, &wr);
                    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                    MONITORINFO mi = { sizeof(mi) };
                    GetMonitorInfo(hMon, &mi);
                    int monW = mi.rcWork.right - mi.rcWork.left;
                    float pct = (float)(ms->pt.x - mi.rcWork.left) / monW;

                    ShowWindow(hwnd, SW_RESTORE);

                    GetWindowRect(hwnd, &wr);
                    int winW = wr.right - wr.left;
                    int winH = wr.bottom - wr.top;
                    int newX = ms->pt.x - (int)(pct * winW);
                    int newY = ms->pt.y - 10;
                    SetWindowPos(hwnd, NULL, newX, newY, winW, winH,
                                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                    GetWindowRect(hwnd, &wr);
                    g_windowStart.x = wr.left;
                    g_windowStart.y = wr.top;
                } else {
                    RECT wr;
                    GetWindowRect(hwnd, &wr);
                    g_windowStart.x = wr.left;
                    g_windowStart.y = wr.top;
                }

                SetForegroundWindow(hwnd);
                g_dragWindow = hwnd;
                g_dragStart  = ms->pt;
                g_dragging   = TRUE;

                return 1;
            }
        }
    }

    if (wParam == WM_MOUSEMOVE && g_dragging) {
        int dx = ms->pt.x - g_dragStart.x;
        int dy = ms->pt.y - g_dragStart.y;
        SetWindowPos(g_dragWindow, NULL,
                     g_windowStart.x + dx, g_windowStart.y + dy, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (wParam == WM_LBUTTONUP && g_dragging) {
        g_dragging   = FALSE;
        g_didDrag    = TRUE;
        g_dragWindow = NULL;

        return 1;
    }

pass_through:
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
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

static LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && (g_dragging || g_didDrag)) {
        KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT *)lParam;
        if ((kb->vkCode == VK_LWIN || kb->vkCode == VK_RWIN) && wParam == WM_KEYUP) {
            g_didDrag = FALSE;
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
    lstrcpy(g_nid.szTip, TEXT("WinSnap - Alt+Drag to move windows"));
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
