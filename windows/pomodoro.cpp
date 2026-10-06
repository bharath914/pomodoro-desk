// Minimal always-on-top Pomodoro overlay for Windows (Win32 + GDI, no dependencies).
// All countdown/cycle logic lives in ../core/TimerEngine — this file is UI/rendering only.
//
// Build (MinGW):  windres windows/pomodoro.rc -O coff -o pomodoro_res.o && g++ -O2 -s -mwindows -static windows/pomodoro.cpp core/TimerEngine.cpp pomodoro_res.o -o pomodoro.exe -lgdi32 -luser32
// Build (MSVC):   rc windows\pomodoro.rc && cl /O2 /EHsc windows\pomodoro.cpp core\TimerEngine.cpp windows\pomodoro.res user32.lib gdi32.lib /link /SUBSYSTEM:WINDOWS
//
// Shows up in the taskbar (can be minimized back from there). Drag anywhere to move,
// drag an edge/corner to resize (aspect ratio locked, so it always scales as a whole).
// Space = start/pause. Esc or the close button = quit.

#define UNICODE
#define _UNICODE
#include <windows.h>
#include <cmath>
#include "../core/TimerEngine.h"

// ---------- palette ----------
static const COLORREF kBg        = RGB(11, 11, 11);
static const COLORREF kSegTrack  = RGB(29, 29, 31);
static const COLORREF kResetBg   = RGB(21, 21, 23);
static const COLORREF kRing      = RGB(58, 57, 62);
static const COLORREF kSecondary = RGB(155, 155, 158);
static const COLORREF kWhite     = RGB(255, 255, 255);
static const COLORREF kBlack     = RGB(0, 0, 0);

static HINSTANCE g_hinst;
static HWND      g_main = nullptr;
static TimerEngine g_timer;
static int        g_shownSecs = -1;
static int        g_dpi = 96;
static int        g_curW = 0, g_curH = 0;
static HFONT      g_fTime, g_fMode, g_fTab, g_fButton, g_fSessions, g_fIcon;

static int S(int v) { return MulDiv(v, g_dpi, 96); }
static int64_t Now() { return (int64_t)GetTickCount64(); }

// ---------- base design layout, in 96-dpi logical units (scaled uniformly to fit the window) ----------
static const int BASE_W = 466, BASE_H = 568;
static const int MIN_W = 300, MIN_H = 360;
static const int EDGE = 8;

static const wchar_t* kTabLabels[3] = { L"Focus", L"Short break", L"Long break" };

struct Layout {
    double scale;
    RECT topbarClose, topbarMin;
    RECT segTrack;
    RECT tabs[3];
    int ringCx, ringCy, ringR, ringThickness;
    RECT startBtn, resetBtn;
    int dotsY, dotR, dotGap;
};

static RECT Xform(double s, int ox, int oy, int bx, int by, int bw, int bh) {
    RECT r;
    r.left = ox + (int)(bx * s);
    r.top = oy + (int)(by * s);
    r.right = ox + (int)((bx + bw) * s);
    r.bottom = oy + (int)((by + bh) * s);
    return r;
}

static int TextWidthPx(HDC dc, HFONT f, const wchar_t* text) {
    HGDIOBJ old = SelectObject(dc, f);
    SIZE sz;
    GetTextExtentPoint32W(dc, text, (int)wcslen(text), &sz);
    SelectObject(dc, old);
    return sz.cx;
}

static Layout ComputeLayout(HDC dc, int cw, int ch) {
    int baseW = BASE_W, baseH = BASE_H;
    double sx = (double)cw / baseW, sy = (double)ch / baseH;
    double s = sx < sy ? sx : sy;
    int offX = (int)(cw - baseW * s) / 2;
    int offY = (int)(ch - baseH * s) / 2;

    Layout L;
    L.scale = s;
    L.topbarClose = Xform(s, offX, offY, 422, 6, 28, 28);
    L.topbarMin   = Xform(s, offX, offY, 388, 6, 28, 28);

    int tabPad = (int)(14 * s);
    int tabH = (int)(46 * s);
    int tabW[3];
    int totalTabW = 0;
    for (int i = 0; i < 3; i++) {
        tabW[i] = TextWidthPx(dc, g_fTab, kTabLabels[i]) + tabPad * 2;
        totalTabW += tabW[i];
    }
    int trackPad = (int)(5 * s);
    int trackW = totalTabW + trackPad * 2;
    int trackX = offX + (int)(baseW * s) / 2 - trackW / 2;
    int trackY = offY + (int)(54 * s);
    L.segTrack = { trackX, trackY, trackX + trackW, trackY + tabH + trackPad * 2 };
    int tx = trackX + trackPad;
    for (int i = 0; i < 3; i++) {
        L.tabs[i] = { tx, trackY + trackPad, tx + tabW[i], trackY + trackPad + tabH };
        tx += tabW[i];
    }

    L.ringCx = offX + (int)(233 * s);
    L.ringCy = offY + (int)(264 * s);
    L.ringR = (int)(115 * s);
    L.ringThickness = (int)(13 * s);

    L.startBtn = Xform(s, offX, offY, 107, 420, 120, 59);
    L.resetBtn = Xform(s, offX, offY, 239, 420, 120, 59);

    L.dotsY = offY + (int)(523 * s);
    L.dotR = (int)(6 * s);
    L.dotGap = (int)(8 * s);
    return L;
}

enum { HIT_NONE = -1, HIT_CLOSE, HIT_MIN, HIT_TAB0, HIT_TAB1, HIT_TAB2, HIT_START, HIT_RESET };

static int HitButton(const Layout& L, POINT p) {
    if (PtInRect(&L.topbarClose, p)) return HIT_CLOSE;
    if (PtInRect(&L.topbarMin, p)) return HIT_MIN;
    for (int i = 0; i < 3; i++) if (PtInRect(&L.tabs[i], p)) return HIT_TAB0 + i;
    if (PtInRect(&L.startBtn, p)) return HIT_START;
    if (PtInRect(&L.resetBtn, p)) return HIT_RESET;
    return HIT_NONE;
}

static void UiToggle(HWND h) { g_timer.toggle(Now()); InvalidateRect(h, nullptr, FALSE); }
static void UiReset(HWND h) { g_timer.reset(); InvalidateRect(h, nullptr, FALSE); }
static void UiSelectMode(HWND h, Mode m) { g_timer.selectMode(m); InvalidateRect(h, nullptr, FALSE); }

static const wchar_t* StartLabel() {
    switch (g_timer.state()) {
    case RunState::Running: return L"Pause";
    case RunState::Paused:  return L"Resume";
    default:                return L"Start";
    }
}

static const wchar_t* ModeLabel(Mode m) {
    switch (m) {
    case Mode::Focus:      return L"Focus";
    case Mode::ShortBreak: return L"Short break";
    case Mode::LongBreak:  return L"Long break";
    }
    return L"Focus";
}

static void DrawCentered(HDC dc, const wchar_t* t, RECT r, HFONT f, COLORREF c) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static void FillRoundedRect(HDC dc, RECT r, COLORREF fill) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, fill);
    HGDIOBJ oldBr = SelectObject(dc, br);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    int radius = r.bottom - r.top;
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);
}

static void DrawRing(HDC dc, int cx, int cy, int r, int thickness, double progress) {
    LOGBRUSH lb = { BS_SOLID, kRing, 0 };
    HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT | PS_JOIN_ROUND, thickness, &lb, 0, nullptr);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    MoveToEx(dc, cx + r, cy, nullptr);
    AngleArc(dc, cx, cy, r, 0.0f, 360.0f);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    if (progress > 0.0015) {
        if (progress > 1.0) progress = 1.0;
        LOGBRUSH lb2 = { BS_SOLID, kWhite, 0 };
        HPEN pen2 = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT | PS_JOIN_ROUND, thickness, &lb2, 0, nullptr);
        SelectObject(dc, pen2);
        MoveToEx(dc, cx, cy - r, nullptr);
        AngleArc(dc, cx, cy, r, 90.0f, (float)(-progress * 360.0));
        SelectObject(dc, oldPen);
        DeleteObject(pen2);
    }
}

static void RebuildFonts(double scale) {
    if (g_fTime) DeleteObject(g_fTime);
    if (g_fMode) DeleteObject(g_fMode);
    if (g_fTab) DeleteObject(g_fTab);
    if (g_fButton) DeleteObject(g_fButton);
    if (g_fSessions) DeleteObject(g_fSessions);
    if (g_fIcon) DeleteObject(g_fIcon);
    auto px = [&](int base) { return -(int)(base * scale + 0.5); };
    g_fTime     = CreateFontW(px(46), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fMode     = CreateFontW(px(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fTab      = CreateFontW(px(13), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fButton   = CreateFontW(px(15), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fSessions = CreateFontW(px(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fIcon     = CreateFontW(px(16), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

static void Paint(HWND h) {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(h, &ps);
    RECT cr; GetClientRect(h, &cr);
    int cw = cr.right, ch = cr.bottom;
    if (cw <= 0 || ch <= 0) { EndPaint(h, &ps); return; }

    if (cw != g_curW || ch != g_curH) {
        g_curW = cw; g_curH = ch;
        Layout tmp = ComputeLayout(wdc, cw, ch);
        RebuildFonts(tmp.scale);
    }
    Layout L = ComputeLayout(wdc, cw, ch);

    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, cw, ch);
    HGDIOBJ old = SelectObject(dc, bmp);
    SetBkMode(dc, TRANSPARENT);

    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(dc, &cr, bg);
    DeleteObject(bg);

    DrawCentered(dc, L"—", L.topbarMin, g_fIcon, kSecondary);
    DrawCentered(dc, L"×", L.topbarClose, g_fIcon, kSecondary);

    FillRoundedRect(dc, L.segTrack, kSegTrack);
    Mode cur = g_timer.mode();
    for (int i = 0; i < 3; i++) {
        bool selected = (int)cur == i;
        if (selected) FillRoundedRect(dc, L.tabs[i], kWhite);
        DrawCentered(dc, kTabLabels[i], L.tabs[i], g_fTab, selected ? kBlack : kSecondary);
    }

    int64_t now = Now();
    double total = (double)TimerEngine::durationForMode(cur);
    double remain = (double)g_timer.remainingMs(now);
    double progress = total > 0 ? 1.0 - (remain / total) : 0.0;
    DrawRing(dc, L.ringCx, L.ringCy, L.ringR, L.ringThickness, progress);

    int s = g_timer.secsLeft(now);
    wchar_t buf[16];
    wsprintfW(buf, L"%02d:%02d", s / 60, s % 60);
    int timeBoxH = (int)(56 * L.scale);
    RECT timeBox = { L.ringCx - L.ringR, L.ringCy - timeBoxH - (int)(2 * L.scale), L.ringCx + L.ringR, L.ringCy - (int)(2 * L.scale) };
    DrawCentered(dc, buf, timeBox, g_fTime, kWhite);
    RECT modeBox = { L.ringCx - L.ringR, L.ringCy + (int)(6 * L.scale), L.ringCx + L.ringR, L.ringCy + (int)(6 * L.scale) + (int)(22 * L.scale) };
    DrawCentered(dc, ModeLabel(cur), modeBox, g_fMode, kSecondary);

    FillRoundedRect(dc, L.startBtn, kWhite);
    DrawCentered(dc, StartLabel(), L.startBtn, g_fButton, kBlack);
    FillRoundedRect(dc, L.resetBtn, kResetBg);
    DrawCentered(dc, L"Reset", L.resetBtn, g_fButton, kSecondary);

    int cyclePos = g_timer.cyclePosition();
    int dotsTotalW = 4 * (L.dotR * 2) + 3 * L.dotGap;
    wchar_t sessionsText[32];
    int sessions = g_timer.completedSessions();
    wsprintfW(sessionsText, L"%d session%s", sessions, sessions == 1 ? L"" : L"s");
    int sessionsW = TextWidthPx(dc, g_fSessions, sessionsText);
    int gapPx = (int)(12 * L.scale);
    int clusterW = dotsTotalW + gapPx + sessionsW;
    int clusterX = (cw - clusterW) / 2;

    HBRUSH dotLit = CreateSolidBrush(kWhite);
    HBRUSH dotDim = CreateSolidBrush(kRing);
    for (int i = 0; i < 4; i++) {
        int dcx = clusterX + L.dotR + i * (L.dotR * 2 + L.dotGap);
        HGDIOBJ oldB = SelectObject(dc, i < cyclePos ? dotLit : dotDim);
        HGDIOBJ oldP = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, dcx - L.dotR, L.dotsY - L.dotR, dcx + L.dotR, L.dotsY + L.dotR);
        SelectObject(dc, oldB);
        SelectObject(dc, oldP);
    }
    DeleteObject(dotLit);
    DeleteObject(dotDim);
    RECT sessionsBox = { clusterX + dotsTotalW + gapPx, L.dotsY - (int)(10 * L.scale), clusterX + clusterW, L.dotsY + (int)(10 * L.scale) };
    DrawCentered(dc, sessionsText, sessionsBox, g_fSessions, kSecondary);

    BitBlt(wdc, 0, 0, cw, ch, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(h, &ps);
}

static void ApplyRoundedRegion(HWND h, int cw, int ch, double scale) {
    int radius = (int)(24 * scale);
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
    case WM_SIZING: {
        // Lock the aspect ratio to BASE_W:BASE_H so the whole layout always visibly
        // scales together, even when the user only drags a single edge.
        RECT* r = (RECT*)l;
        int rawW = r->right - r->left, rawH = r->bottom - r->top;
        double scaleW = (double)rawW / S(BASE_W), scaleH = (double)rawH / S(BASE_H);
        WPARAM edge = w;
        bool horiz = (edge == WMSZ_LEFT || edge == WMSZ_RIGHT || edge == WMSZ_TOPLEFT ||
                      edge == WMSZ_TOPRIGHT || edge == WMSZ_BOTTOMLEFT || edge == WMSZ_BOTTOMRIGHT);
        bool vert = (edge == WMSZ_TOP || edge == WMSZ_BOTTOM || edge == WMSZ_TOPLEFT ||
                     edge == WMSZ_TOPRIGHT || edge == WMSZ_BOTTOMLEFT || edge == WMSZ_BOTTOMRIGHT);
        double scale = (horiz && vert) ? (scaleW > scaleH ? scaleW : scaleH) : (vert ? scaleH : scaleW);
        int newW = (int)(S(BASE_W) * scale), newH = (int)(S(BASE_H) * scale);
        if (newW < S(MIN_W)) { scale = (double)S(MIN_W) / S(BASE_W); newW = S(MIN_W); newH = (int)(S(BASE_H) * scale); }
        if (newH < S(MIN_H)) { scale = (double)S(MIN_H) / S(BASE_H); newH = S(MIN_H); newW = (int)(S(BASE_W) * scale); }
        if (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT) r->left = r->right - newW;
        else r->right = r->left + newW;
        if (edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT) r->top = r->bottom - newH;
        else r->bottom = r->top + newH;
        return TRUE;
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
        HDC dc = GetDC(h);
        Layout L = ComputeLayout(dc, cr.right, cr.bottom);
        ReleaseDC(h, dc);
        return HitButton(L, cp) != HIT_NONE ? HTCLIENT : HTCAPTION;
    }
    case WM_NCLBUTTONDBLCLK:
        return 0;  // double-clicking the draggable area must not maximize the fixed-aspect widget
    case WM_SYSCOMMAND:
        if ((w & 0xFFF0) == SC_MAXIMIZE) return 0;
        break;
    case WM_DPICHANGED: {
        // Moved to a monitor with a different DPI: adopt the suggested size so the widget
        // keeps the same physical size instead of being bitmap-stretched or mis-sized.
        g_dpi = HIWORD(w);
        const RECT* r = (const RECT*)l;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        g_curW = g_curH = 0;  // force font rebuild on next paint
        return 0;
    }
    case WM_SIZE:
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_WINDOWPOSCHANGED: {
        RECT cr; GetClientRect(h, &cr);
        if (cr.right > 0 && cr.bottom > 0) {
            HDC dc = GetDC(h);
            double scale = ComputeLayout(dc, cr.right, cr.bottom).scale;
            ReleaseDC(h, dc);
            ApplyRoundedRegion(h, cr.right, cr.bottom, scale);
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        POINT p = { (short)LOWORD(l), (short)HIWORD(l) };
        RECT cr; GetClientRect(h, &cr);
        HDC dc = GetDC(h);
        Layout L = ComputeLayout(dc, cr.right, cr.bottom);
        ReleaseDC(h, dc);
        switch (HitButton(L, p)) {
        case HIT_CLOSE: DestroyWindow(h); break;
        case HIT_MIN:   ShowWindow(h, SW_MINIMIZE); break;
        case HIT_TAB0:  UiSelectMode(h, Mode::Focus); break;
        case HIT_TAB1:  UiSelectMode(h, Mode::ShortBreak); break;
        case HIT_TAB2:  UiSelectMode(h, Mode::LongBreak); break;
        case HIT_START: UiToggle(h); break;
        case HIT_RESET: UiReset(h); break;
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

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int) {
    g_hinst = hi;
    // Per-monitor-v2 DPI awareness where available (Win10 1703+), else system-aware.
    // Looked up dynamically so the build also works with older SDK headers.
    typedef BOOL (WINAPI *SetDpiCtxFn)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    SetDpiCtxFn setCtx = (SetDpiCtxFn)(void*)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (!setCtx || !setCtx((HANDLE)-4 /* PER_MONITOR_AWARE_V2 */)) SetProcessDPIAware();
    HDC sdc = GetDC(nullptr);
    g_dpi = GetDeviceCaps(sdc, LOGPIXELSX);
    ReleaseDC(nullptr, sdc);

    RebuildFonts(1.0);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"PomoOverlay";
    // Icon resource id 1 (windows/pomodoro.rc): large for taskbar/Alt-Tab, small for the window corner.
    wc.hIcon = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    wc.hIconSm = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    RegisterClassExW(&wc);

    // WS_EX_APPWINDOW (and no WS_EX_TOOLWINDOW) ensures a taskbar button even though this
    // is a WS_POPUP window, so it can be found/minimized/restored like a normal app.
    // Initial size: the base design at the current DPI, shrunk (aspect preserved) to fit the
    // work area so it is never taller/wider than the screen, then centered-ish near the top-left.
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int winW = S(BASE_W), winH = S(BASE_H);
    double fit = 1.0;
    double maxW = (wa.right - wa.left) * 0.9, maxH = (wa.bottom - wa.top) * 0.9;
    if (winW > maxW) fit = maxW / winW;
    if (winH * fit > maxH) fit = maxH / winH;
    winW = (int)(winW * fit); winH = (int)(winH * fit);
    int winX = wa.left + (wa.right - wa.left - winW) / 4;
    int winY = wa.top + (wa.bottom - wa.top - winH) / 4;
    g_main = CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW, wc.lpszClassName, L"Pomodoro Desk",
                              WS_POPUP | WS_THICKFRAME, winX, winY, winW, winH,
                              nullptr, nullptr, hi, nullptr);
    SetTimer(g_main, 1, 200, nullptr);
    ShowWindow(g_main, SW_SHOWNORMAL);
    UpdateWindow(g_main);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
