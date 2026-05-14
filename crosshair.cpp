/*
 * RecoilCrosshair — Win32 + GDI layered window overlay
 *
 * Rendering: UpdateLayeredWindow() paints directly into the DWM compositor.
 *   - No WM_PAINT, no flicker, hardware-composited alpha blending.
 *   - Window is only 320x320 px (centered on screen) — not fullscreen.
 *
 * Click-through: WS_EX_TRANSPARENT | WS_EX_LAYERED
 * Always on top:  WS_EX_TOPMOST
 * Mouse hook:     SetWindowsHookEx(WH_MOUSE_LL) — detects LMB globally
 * Hotkey:         RegisterHotKey — Ctrl+Shift+X toggles visibility
 * Timer:          ~16ms (60fps) drives animation
 *
 * Build (MinGW):
 *   g++ -O2 -mwindows -o RecoilCrosshair.exe crosshair.cpp -lgdi32 -lwinmm -lshell32 -luser32
 */

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <math.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Gun profiles — pick one by setting GUN_PROFILE
// 0 = Vandal | 1 = Phantom | 2 = Spectre | 3 = Bulldog
// ---------------------------------------------------------------------------
static const int GUN_PROFILE = 0;

struct GunProfile {
    const wchar_t* name;
    float duration_ms;  // full spray duration in milliseconds
    float delay_ms;     // delay before bloom starts (ms)
};

static const GunProfile GUNS[] = {
    { L"Vandal",  1600.0f, 100.0f },
    { L"Phantom", 1600.0f, 100.0f },
    { L"Spectre", 1600.0f, 100.0f },
    { L"Bulldog", 1600.0f, 100.0f },
};

// ---------------------------------------------------------------------------
// Crosshair geometry — exact values from CrosshairX UI
//   Length:    4
//   Thickness: 2
//   Gap idle:  4  → fires to: 136  (Direction: Normal = outward)
//   Duration:  1.6s,  Delay: 0.1s,  Easing: Linear
//
// CrosshairX gap units are NOT 1:1 screen pixels.
// At 1080p CrosshairX renders crosshairs at 0.5x scale internally,
// so 1 crosshair unit = 0.5 screen pixels.
// Scale factor: adjust if your resolution differs.
//   1080p → 0.5,  1440p → 0.667,  4K → 1.0
// ---------------------------------------------------------------------------
static const int   CH_LENGTH = 6;     // arm length in crosshair units (+2)
static const int   CH_THICK  = 3;     // arm thickness in crosshair units (+1)
static const int   CH_OFFSET = 2;     // idle gap in crosshair units

static const float CH_SCALE  = 0.5f;  // crosshair units → screen pixels

// Derived screen-pixel values
static const int   SCR_LENGTH = (int)(CH_LENGTH * CH_SCALE + 0.5f);  // 2px
static const int   SCR_THICK  = (int)(CH_THICK  * CH_SCALE + 0.5f);  // 1px  (min 1)
static const int   SCR_OFFSET = (int)(CH_OFFSET * CH_SCALE + 0.5f);  // 2px

// Animation: gap goes from 4 → 136 crosshair units = 2px → 68px screen pixels
static const float ANIM_START = (float)CH_OFFSET * CH_SCALE;   //   2.0px
static const float ANIM_END   = 136.0f            * CH_SCALE;  //  68.0px

// Layer 1 — top arm only (#00FFF5)
static const BYTE L1_R = 0x00, L1_G = 0xFF, L1_B = 0xF5;

// Layer 2 — left/right/bottom idle (#00FFFF)
static const BYTE L2_R = 0x00, L2_G = 0xFF, L2_B = 0xFF;

// Layer 2 — firing color (#FF0000)
static const BYTE L2F_R = 0xFF, L2F_G = 0x00, L2F_B = 0x00;

// ---------------------------------------------------------------------------
// Canvas — 320x320 centered on primary monitor
// ---------------------------------------------------------------------------
static const int CANVAS = 320;
static const int HALF   = CANVAS / 2;

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static HWND           g_hwnd      = NULL;
static HINSTANCE      g_hInst     = NULL;
static HHOOK          g_mouseHook = NULL;
static NOTIFYICONDATA g_nid       = {};

// Animation state
static bool  g_firing       = false;
static bool  g_animStarted  = false;
static DWORD g_pressTime    = 0;
static DWORD g_animStart    = 0;
static float g_animProgress = 0.0f;
static float g_topOffset    = ANIM_START;
static bool  g_firingColor  = false;
static bool  g_visible      = true;

// Runtime animation params (from gun profile)
static float g_duration_ms  = 1600.0f;
static float g_delay_ms     = 100.0f;

// IDs
#define IDM_TOGGLE  1001
#define IDM_QUIT    1002
#define WM_TRAY     (WM_USER + 1)
#define HOTKEY_ID   1
#define TIMER_ID    2

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline UINT32 premul(BYTE r, BYTE g, BYTE b, BYTE a = 255) {
    return (UINT32)((a << 24) |
                    (((r * a / 255)) << 16) |
                    (((g * a / 255)) << 8)  |
                    (  b * a / 255));
}

static void FillRect32(UINT32* px, int bmpW,
                       int x, int y, int w, int h,
                       BYTE r, BYTE g, BYTE b)
{
    UINT32 color = premul(r, g, b);
    for (int row = y; row < y + h; row++)
        for (int col = x; col < x + w; col++)
            if (col >= 0 && col < bmpW && row >= 0 && row < CANVAS)
                px[row * bmpW + col] = color;
}

// ---------------------------------------------------------------------------
// Render — builds 32bpp DIB and pushes via UpdateLayeredWindow
// ---------------------------------------------------------------------------
static void RenderFrame()
{
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem    = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi              = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = CANVAS;
    bmi.bmiHeader.biHeight      = -CANVAS;  // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    UINT32* pixels = NULL;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS,
                                    (void**)&pixels, NULL, 0);
    if (!hBmp || !pixels) {
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return;
    }

    HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hBmp);
    memset(pixels, 0, CANVAS * CANVAS * sizeof(UINT32));

    if (g_visible) {
        int cx = HALF, cy = HALF;
        int ht = SCR_THICK / 2;
        if (ht < 1) ht = 1;

        // Layer 1 — top arm (#00FFF5), gap animated outward while firing
        {
            int off = (int)g_topOffset;
            FillRect32(pixels, CANVAS,
                       cx - ht, cy - off - SCR_LENGTH,
                       SCR_THICK, SCR_LENGTH,
                       L1_R, L1_G, L1_B);
        }

        // Layer 2 — left / right / bottom (color changes when firing)
        {
            BYTE r = g_firingColor ? L2F_R : L2_R;
            BYTE g = g_firingColor ? L2F_G : L2_G;
            BYTE b = g_firingColor ? L2F_B : L2_B;
            int  off = SCR_OFFSET;

            // bottom
            FillRect32(pixels, CANVAS, cx - ht, cy + off,
                       SCR_THICK, SCR_LENGTH, r, g, b);
            // left
            FillRect32(pixels, CANVAS, cx - off - SCR_LENGTH, cy - ht,
                       SCR_LENGTH, SCR_THICK, r, g, b);
            // right
            FillRect32(pixels, CANVAS, cx + off, cy - ht,
                       SCR_LENGTH, SCR_THICK, r, g, b);
        }
    }

    POINT ptSrc = {0, 0};
    SIZE  szWnd = {CANVAS, CANVAS};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    RECT wr; GetWindowRect(g_hwnd, &wr);
    POINT ptDst = {wr.left, wr.top};

    UpdateLayeredWindow(g_hwnd, hdcScreen, &ptDst, &szWnd,
                        hdcMem, &ptSrc, 0, &bf, ULW_ALPHA);

    SelectObject(hdcMem, hOld);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
}

// ---------------------------------------------------------------------------
// Animation tick — called from WM_TIMER at ~60fps
// ---------------------------------------------------------------------------
static void Tick()
{
    if (!g_firing) return;

    DWORD now        = timeGetTime();
    float sincePress = (float)(now - g_pressTime);

    // Honour start delay (100ms from JSON)
    if (sincePress < g_delay_ms) return;

    if (!g_animStarted) {
        g_animStarted = true;
        g_animStart   = now;
    }

    float elapsed  = (float)(now - g_animStart);
    g_animProgress = elapsed / g_duration_ms;
    if (g_animProgress > 1.0f) g_animProgress = 1.0f;

    // Linear lerp: 2px → 136px over 1.6s
    g_topOffset   = lerp(ANIM_START, ANIM_END, g_animProgress);
    g_firingColor = true;
}

// ---------------------------------------------------------------------------
// Low-level mouse hook
// ---------------------------------------------------------------------------
static LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        if (wParam == WM_LBUTTONDOWN && !g_firing) {
            g_firing       = true;
            g_animStarted  = false;
            g_animProgress = 0.0f;
            g_pressTime    = timeGetTime();
            g_firingColor  = true;
            RenderFrame();
        }
        else if (wParam == WM_LBUTTONUP && g_firing) {
            // Reset on release
            g_firing       = false;
            g_animStarted  = false;
            g_animProgress = 0.0f;
            g_topOffset    = ANIM_START;  // snap back to idle gap
            g_firingColor  = false;
            RenderFrame();
        }
    }
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Toggle visibility
// ---------------------------------------------------------------------------
static void ToggleVisibility()
{
    g_visible = !g_visible;
    RenderFrame();
}

// ---------------------------------------------------------------------------
// Tray
// ---------------------------------------------------------------------------
static void SetupTray()
{
    wchar_t tip[64];
    wsprintfW(tip, L"RecoilCrosshair [%s]", GUNS[GUN_PROFILE].name);

    g_nid.cbSize           = sizeof(NOTIFYICONDATA);
    g_nid.hWnd             = g_hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon            = LoadIcon(NULL, IDI_APPLICATION);
    lstrcpyW(g_nid.szTip, tip);
    Shell_NotifyIcon(NIM_ADD, &g_nid);
}

static void ShowTrayMenu()
{
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, GUNS[GUN_PROFILE].name);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_TOGGLE,
                g_visible ? L"Disable (Ctrl+Shift+X)" : L"Enable (Ctrl+Shift+X)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_QUIT, L"Quit");

    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, NULL);
    DestroyMenu(hMenu);
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        // 0x4000 = MOD_NOREPEAT (not defined in older MinGW headers)
        RegisterHotKey(hwnd, HOTKEY_ID, MOD_CONTROL | MOD_SHIFT | 0x4000, 'X');
        timeBeginPeriod(1);
        SetTimer(hwnd, TIMER_ID, 16, NULL);
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_ID) { Tick(); RenderFrame(); }
        return 0;

    case WM_HOTKEY:
        if (wParam == HOTKEY_ID) ToggleVisibility();
        return 0;

    case WM_TRAY:
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP)
            ShowTrayMenu();
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDM_TOGGLE) ToggleVisibility();
        if (LOWORD(wParam) == IDM_QUIT)   PostQuitMessage(0);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        timeEndPeriod(1);
        UnregisterHotKey(hwnd, HOTKEY_ID);
        if (g_mouseHook) UnhookWindowsHookEx(g_mouseHook);
        Shell_NotifyIcon(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// WinMain
// ---------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    g_hInst       = hInst;
    g_duration_ms = GUNS[GUN_PROFILE].duration_ms;
    g_delay_ms    = GUNS[GUN_PROFILE].delay_ms;
    g_topOffset   = ANIM_START;

    WNDCLASSEX wc    = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"RecoilCrosshair";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassEx(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int wx = sw / 2 - HALF;
    int wy = sh / 2 - HALF;

    g_hwnd = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"RecoilCrosshair", L"RecoilCrosshair",
        WS_POPUP,
        wx, wy, CANVAS, CANVAS,
        NULL, NULL, hInst, NULL
    );

    if (!g_hwnd) return 1;

    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(g_hwnd);

    g_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, NULL, 0);
    SetupTray();
    RenderFrame();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
