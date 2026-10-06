// Minimal always-on-top focus-timer overlay for Windows (Win32 + GDI, no dependencies).
// All countdown logic lives in ../core/TimerEngine — this file is UI/rendering only.
//
// Build (MinGW):  g++ -O2 -s -mwindows -static windows/pomodoro.cpp core/TimerEngine.cpp -o pomodoro.exe -lgdi32 -luser32 -lcomctl32
// Build (MSVC):   cl /O2 /EHsc windows\pomodoro.cpp core\TimerEngine.cpp user32.lib gdi32.lib comctl32.lib /link /SUBSYSTEM:WINDOWS
//
// DigitalNumbers-Regular.ttf must sit next to the exe (loaded as a private, non-installed font).
//
// Drag anywhere to move, drag an edge/corner to resize. Space = start/pause. Right-click = menu (duration/custom/quit).

#define UNICODE
#define _UNICODE
#include <windows.h>
#include "../core/TimerEngine.h"

// ---------- palette ----------
static const COLORREF kBg   = RGB(0, 0, 0);
static const COLORREF kFg   = RGB(255, 255, 255);
static const COLORREF kBlack = RGB(0, 0, 0);

static HINSTANCE   g_hinst;
static HWND        g_main = nullptr;
static HWND        g_popup = nullptr;
static HWND        g_editCtl = nullptr;

static TimerEngine g_timer(25);
static int         g_shownSecs = -1;
static int         g_dpi = 96;
static HFONT       g_fTime, g_fStart, g_fStep, g_fIcon, g_fPopup;
static int         g_curW = 0, g_curH = 0;  // fonts sized for this client size; recreated when it changes

static int S(int v) { return MulDiv(v, g_dpi, 96); }
static int64_t Now() { return (int64_t)GetTickCount64(); }

// ---------- base design layout, in 96-dpi logical units (scaled uniformly to fit the window) ----------
static const int BASE_W = 320, BASE_H = 198;
static const int MIN_W = 220, MIN_H = 150;

enum { ID_DUR_25 = 2001, ID_DUR_30, ID_DUR_45, ID_DUR_60, ID_DUR_120, ID_CUSTOM, ID_QUIT, ID_EDIT = 3001, ID_SET = 3002 };

struct Layout {
    double scale;
    RECT time, startBtn, stepper, stepMinus, stepPlus, pauseBtn, resetBtn;
};

static RECT Xform(int cw, int ch, double s, int ox, int oy, int bx, int by, int bw, int bh) {
    (void)cw; (void)ch;
    RECT r;
    r.left   = ox + (int)(bx * s);
    r.top    = oy + (int)(by * s);
    r.right  = ox + (int)((bx + bw) * s);
    r.bottom = oy + (int)((by + bh) * s);
    return r;
}

static Layout ComputeLayout(int cw, int ch) {
    int baseW = S(BASE_W), baseH = S(BASE_H);
    double sx = (double)cw / baseW, sy = (double)ch / baseH;
    double s = sx < sy ? sx : sy;
    int offX = (int)(cw - baseW * s) / 2;
    int offY = (int)(ch - baseH * s) / 2;

    Layout L;
    L.scale = s;
    L.time     = Xform(cw, ch, s, offX, offY, 0, 14, BASE_W, 86);
    L.startBtn = Xform(cw, ch, s, offX, offY, 75, 112, 170, 40);
    L.stepper  = Xform(cw, ch, s, offX, offY, 27, 164, 150, 34);
    L.pauseBtn = Xform(cw, ch, s, offX, offY, 185, 164, 50, 34);
    L.resetBtn = Xform(cw, ch, s, offX, offY, 243, 164, 50, 34);
    int stepW = L.stepper.right - L.stepper.left;
    int quarter = stepW / 4;
    L.stepMinus = { L.stepper.left, L.stepper.top, L.stepper.left + quarter, L.stepper.bottom };
    L.stepPlus  = { L.stepper.right - quarter, L.stepper.top, L.stepper.right, L.stepper.bottom };
    return L;
}

enum { HIT_NONE = -1, HIT_START, HIT_STEP_MINUS, HIT_STEP_PLUS, HIT_PAUSE, HIT_RESET };

static int HitButton(const Layout& L, POINT p) {
    if (PtInRect(&L.startBtn, p)) return HIT_START;
    if (PtInRect(&L.stepMinus, p)) return HIT_STEP_MINUS;
    if (PtInRect(&L.stepPlus, p)) return HIT_STEP_PLUS;
    if (PtInRect(&L.pauseBtn, p)) return HIT_PAUSE;
    if (PtInRect(&L.resetBtn, p)) return HIT_RESET;
    return HIT_NONE;
}

static int ParseInt(const wchar_t* s) {
    int v = 0;
    for (; *s; s++) {
        if (*s < L'0' || *s > L'9') break;
        v = v * 10 + (*s - L'0');
    }
    return v;
}

static void UiToggle(HWND h) { g_timer.toggle(Now()); InvalidateRect(h, nullptr, FALSE); }
static void UiReset(HWND h) { g_timer.reset(); InvalidateRect(h, nullptr, FALSE); }
static void UiAddMinutes(HWND h, int m) { g_timer.addMinutes(m); InvalidateRect(h, nullptr, FALSE); }
static void UiSetDuration(HWND h, int minutes) { g_timer.setDuration(minutes); InvalidateRect(h, nullptr, FALSE); }

static void CommitCustom() {
    if (!g_popup) return;
    wchar_t buf[16];
    GetWindowTextW(g_editCtl, buf, 16);
    int mins = ClampMinutes(ParseInt(buf));
    UiSetDuration(g_main, mins);
    DestroyWindow(g_popup);
}

static LRESULT CALLBACK PopupProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        wchar_t init[8];
        wsprintfW(init, L"%d", (int)(g_timer.durationMs() / 60000));
        HWND lbl = CreateWindowW(L"STATIC", L"Minutes (1-999):", WS_CHILD | WS_VISIBLE,
                                  S(10), S(8), S(160), S(18), h, nullptr, g_hinst, nullptr);
        g_editCtl = CreateWindowW(L"EDIT", init, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL,
                                   S(10), S(30), S(80), S(24), h, (HMENU)ID_EDIT, g_hinst, nullptr);
        HWND btn = CreateWindowW(L"BUTTON", L"Set", WS_CHILD | WS_VISIBLE,
                                  S(100), S(30), S(60), S(24), h, (HMENU)ID_SET, g_hinst, nullptr);
        SendMessage(lbl, WM_SETFONT, (WPARAM)g_fPopup, TRUE);
        SendMessage(g_editCtl, WM_SETFONT, (WPARAM)g_fPopup, TRUE);
        SendMessage(btn, WM_SETFONT, (WPARAM)g_fPopup, TRUE);
        SetFocus(g_editCtl);
        SendMessage(g_editCtl, EM_SETSEL, 0, -1);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == ID_SET && HIWORD(w) == BN_CLICKED) CommitCustom();
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE) DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_popup = nullptr;
        g_editCtl = nullptr;
        return 0;
    }
    return DefWindowProc(h, m, w, l);
}

static void ShowCustomPopup(HWND owner) {
    if (g_popup) { SetFocus(g_editCtl); return; }
    RECT r; GetWindowRect(owner, &r);
    g_popup = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"PomoInput", L"Set Timer",
                               WS_POPUP | WS_BORDER, r.left, r.bottom + S(4), S(180), S(90),
                               owner, nullptr, g_hinst, nullptr);
    ShowWindow(g_popup, SW_SHOW);
}

static HMENU BuildMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_DUR_25, L"25 minutes");
    AppendMenuW(m, MF_STRING, ID_DUR_30, L"30 minutes");
    AppendMenuW(m, MF_STRING, ID_DUR_45, L"45 minutes");
    AppendMenuW(m, MF_STRING, ID_DUR_60, L"1 hour");
    AppendMenuW(m, MF_STRING, ID_DUR_120, L"2 hours");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_CUSTOM, L"Custom…");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_QUIT, L"Quit");
    return m;
}

static const wchar_t* StartLabel() {
    switch (g_timer.state()) {
    case TimerState::Running: return L"Pause";
    case TimerState::Paused:  return L"Resume";
    default:                  return L"Start";
    }
}

static void DrawCentered(HDC dc, const wchar_t* t, RECT r, HFONT f, COLORREF c) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

// Pill: fully rounded ends (radius = half height). Rounded-square: modest corner radius.
static void DrawPill(HDC dc, RECT r, COLORREF fill, COLORREF border) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBr = SelectObject(dc, br);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    int radius = r.bottom - r.top;
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);
}

static void DrawRoundedSquare(HDC dc, RECT r, COLORREF fill, COLORREF border) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBr = SelectObject(dc, br);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    int radius = (int)((r.bottom - r.top) * 0.4);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);
}

static void RebuildFonts(double scale) {
    if (g_fTime) DeleteObject(g_fTime);
    if (g_fStart) DeleteObject(g_fStart);
    if (g_fStep) DeleteObject(g_fStep);
    if (g_fIcon) DeleteObject(g_fIcon);
    auto px = [&](int base96) { return -(int)(S(base96) * scale); };
    g_fTime  = CreateFontW(px(68), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Digital Numbers");
    g_fStart = CreateFontW(px(17), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fStep  = CreateFontW(px(15), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fIcon  = CreateFontW(px(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI Symbol");
}

static void Paint(HWND h) {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(h, &ps);
    RECT cr; GetClientRect(h, &cr);
    int cw = cr.right, ch = cr.bottom;
    if (cw <= 0 || ch <= 0) { EndPaint(h, &ps); return; }

    if (cw != g_curW || ch != g_curH) {
        g_curW = cw; g_curH = ch;
        Layout tmp = ComputeLayout(cw, ch);
        RebuildFonts(tmp.scale);
    }
    Layout L = ComputeLayout(cw, ch);

    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, cw, ch);
    HGDIOBJ old = SelectObject(dc, bmp);
    SetBkMode(dc, TRANSPARENT);

    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(dc, &cr, bg);
    DeleteObject(bg);

    int s = g_timer.secsLeft(Now());
    wchar_t buf[16];
    wsprintfW(buf, L"%02d:%02d", s / 60, s % 60);
    DrawCentered(dc, buf, L.time, g_fTime, kFg);

    DrawPill(dc, L.startBtn, kFg, kFg);
    DrawCentered(dc, StartLabel(), L.startBtn, g_fStart, kBlack);

    DrawPill(dc, L.stepper, kBlack, kFg);
    RECT midStep = { L.stepMinus.right, L.stepper.top, L.stepPlus.left, L.stepper.bottom };
    DrawCentered(dc, L"−", L.stepMinus, g_fStep, kFg);
    DrawCentered(dc, L"10m", midStep, g_fStep, kFg);
    DrawCentered(dc, L"+", L.stepPlus, g_fStep, kFg);

    DrawRoundedSquare(dc, L.pauseBtn, kBlack, kFg);
    DrawCentered(dc, g_timer.isRunning() ? L"❚❚" : L"▶", L.pauseBtn, g_fIcon, kFg);

    DrawRoundedSquare(dc, L.resetBtn, kBlack, kFg);
    DrawCentered(dc, L"↺", L.resetBtn, g_fIcon, kFg);

    BitBlt(wdc, 0, 0, cw, ch, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(h, &ps);
}

static void ApplyRoundedRegion(HWND h, int cw, int ch) {
    int radius = (int)(S(22) * ComputeLayout(cw, ch).scale);
    int maxRadius = (cw < ch ? cw : ch) / 2;
    if (radius > maxRadius) radius = maxRadius;
    SetWindowRgn(h, CreateRoundRectRgn(0, 0, cw + 1, ch + 1, radius, radius), TRUE);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_NCCALCSIZE:
        if (w) return 0;  // claim the whole window as client area (no OS-drawn frame)
        break;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)l;
        mmi->ptMinTrackSize.x = S(MIN_W);
        mmi->ptMinTrackSize.y = S(MIN_H);
        return 0;
    }
    case WM_NCHITTEST: {
        POINT p = { (short)LOWORD(l), (short)HIWORD(l) };
        RECT wr; GetWindowRect(h, &wr);
        const int edge = S(8);
        bool left = p.x < wr.left + edge, right = p.x >= wr.right - edge;
        bool top = p.y < wr.top + edge, bottom = p.y >= wr.bottom - edge;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;

        POINT cp = p;
        ScreenToClient(h, &cp);
        RECT cr; GetClientRect(h, &cr);
        Layout L = ComputeLayout(cr.right, cr.bottom);
        return HitButton(L, cp) != HIT_NONE ? HTCLIENT : HTCAPTION;
    }
    case WM_SIZE:
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_WINDOWPOSCHANGED: {
        RECT cr; GetClientRect(h, &cr);
        if (cr.right > 0 && cr.bottom > 0) ApplyRoundedRegion(h, cr.right, cr.bottom);
        break;
    }
    case WM_LBUTTONDOWN: {
        POINT p = { (short)LOWORD(l), (short)HIWORD(l) };
        RECT cr; GetClientRect(h, &cr);
        Layout L = ComputeLayout(cr.right, cr.bottom);
        switch (HitButton(L, p)) {
        case HIT_START:      UiToggle(h); break;
        case HIT_STEP_MINUS: UiAddMinutes(h, -10); break;
        case HIT_STEP_PLUS:  UiAddMinutes(h, 10); break;
        case HIT_PAUSE:      UiToggle(h); break;
        case HIT_RESET:      UiReset(h); break;
        }
        return 0;
    }
    case WM_CONTEXTMENU: {
        int x = (short)LOWORD(l), y = (short)HIWORD(l);
        if (x == -1 && y == -1) {
            POINT p; GetCursorPos(&p); x = p.x; y = p.y;
        }
        HMENU menu = BuildMenu();
        SetForegroundWindow(h);
        int id = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, h, nullptr);
        DestroyMenu(menu);
        switch (id) {
        case ID_DUR_25:  UiSetDuration(h, 25); break;
        case ID_DUR_30:  UiSetDuration(h, 30); break;
        case ID_DUR_45:  UiSetDuration(h, 45); break;
        case ID_DUR_60:  UiSetDuration(h, 60); break;
        case ID_DUR_120: UiSetDuration(h, 120); break;
        case ID_CUSTOM:  ShowCustomPopup(h); break;
        case ID_QUIT:    DestroyWindow(h); break;
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_SPACE) UiToggle(h);
        else if (w == VK_ESCAPE) DestroyWindow(h);
        return 0;
    case WM_TIMER:
        if (g_timer.isRunning()) {
            int64_t now = Now();
            if (g_timer.update(now)) {
                MessageBeep(MB_ICONEXCLAMATION);
                FlashWindow(h, TRUE);
                g_shownSecs = -1;
                InvalidateRect(h, nullptr, FALSE);
            } else if (g_timer.secsLeft(now) != g_shownSecs) {
                g_shownSecs = g_timer.secsLeft(now);
                InvalidateRect(h, nullptr, FALSE);
            }
        }
        return 0;
    case WM_PAINT:      Paint(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_DESTROY:    PostQuitMessage(0); return 0;
    }
    return DefWindowProc(h, m, w, l);
}

static void LoadEmbeddedFont() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t* slash = wcsrchr(exePath, L'\\');
    if (slash) *(slash + 1) = 0;
    wchar_t fontPath[MAX_PATH];
    wsprintfW(fontPath, L"%sDigitalNumbers-Regular.ttf", exePath);
    AddFontResourceExW(fontPath, FR_PRIVATE, 0);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int) {
    g_hinst = hi;
    SetProcessDPIAware();
    HDC sdc = GetDC(nullptr);
    g_dpi = GetDeviceCaps(sdc, LOGPIXELSX);
    ReleaseDC(nullptr, sdc);

    LoadEmbeddedFont();
    RebuildFonts(1.0);
    g_fPopup = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"PomoOverlay";
    RegisterClassW(&wc);

    WNDCLASSW pc = {};
    pc.lpfnWndProc = PopupProc;
    pc.hInstance = hi;
    pc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    pc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    pc.lpszClassName = L"PomoInput";
    RegisterClassW(&pc);

    g_main = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW, wc.lpszClassName, L"Pomodoro",
                              WS_POPUP | WS_THICKFRAME, S(60), S(60), S(BASE_W), S(BASE_H),
                              nullptr, nullptr, hi, nullptr);
    SetLayeredWindowAttributes(g_main, 0, 250, LWA_ALPHA);
    SetTimer(g_main, 1, 200, nullptr);
    ShowWindow(g_main, SW_SHOWNOACTIVATE);
    UpdateWindow(g_main);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (g_popup && msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_RETURN) { CommitCustom(); continue; }
            if (msg.wParam == VK_ESCAPE) { DestroyWindow(g_popup); continue; }
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
