// Minimal always-on-top focus-timer overlay for Windows (Win32 + GDI, no dependencies)
//
// Build (MinGW):  g++ -O2 -s -mwindows -static pomodoro.cpp -o pomodoro.exe -lgdi32 -luser32 -lcomctl32
// Build (MSVC):   cl /O2 /EHsc pomodoro.cpp user32.lib gdi32.lib comctl32.lib /link /SUBSYSTEM:WINDOWS
//
// Drag anywhere to move. Hover to reveal controls. Space = start/pause. Right-click = menu (duration/custom/quit).

#define UNICODE
#define _UNICODE
#include <windows.h>

// ---------- palette (flat, Apple-dark inspired) ----------
static const COLORREF kBg      = RGB(22, 22, 24);
static const COLORREF kBtnRow  = RGB(38, 38, 40);
static const COLORREF kBtn     = RGB(54, 54, 58);
static const COLORREF kFg      = RGB(245, 245, 247);
static const COLORREF kBlue    = RGB(10, 132, 255);
static const COLORREF kGray    = RGB(142, 142, 147);
static const COLORREF kRed     = RGB(255, 69, 58);
static const COLORREF kOrange  = RGB(255, 159, 10);

static HINSTANCE g_hinst;
static HWND      g_main = nullptr;
static HWND      g_popup = nullptr;
static HWND      g_editCtl = nullptr;

static bool       g_running = false;
static bool       g_hover = false;
static long long  g_durationMs = 25 * 60 * 1000LL;  // currently selected preset
static long long  g_remainMs   = g_durationMs;       // authoritative when paused
static ULONGLONG  g_endTick = 0;                      // authoritative when running
static int        g_shownSecs = -1;
static int        g_dpi = 96;
static HFONT      g_fTime, g_fLabel, g_fBtnIcon, g_fBtnText, g_fPopup;

static int S(int v) { return MulDiv(v, g_dpi, 96); }

// ---------- layout (96-dpi units) ----------
static const int W = 216, H = 130;
static const int BTN_W = 36, BTN_H = 26, BTN_GAP = 4, BTN_Y = 96;
enum { B_PLAYPAUSE, B_PLUS5, B_PLUS10, B_RESET, B_CLOSE, B_COUNT };

enum {
    ID_DUR_25 = 2001, ID_DUR_30, ID_DUR_45, ID_DUR_60, ID_DUR_120, ID_CUSTOM, ID_QUIT,
    ID_EDIT = 3001, ID_SET = 3002,
};

static RECT BtnRect(int i) {
    int total = B_COUNT * BTN_W + (B_COUNT - 1) * BTN_GAP;
    int x = (W - total) / 2 + i * (BTN_W + BTN_GAP);
    RECT r = { S(x), S(BTN_Y), S(x + BTN_W), S(BTN_Y + BTN_H) };
    return r;
}

static int HitButton(POINT p) {
    if (!g_hover) return -1;
    for (int i = 0; i < B_COUNT; i++) {
        RECT r = BtnRect(i);
        if (PtInRect(&r, p)) return i;
    }
    return -1;
}

static int ParseInt(const wchar_t* s) {
    int v = 0;
    for (; *s; s++) {
        if (*s < L'0' || *s > L'9') break;
        v = v * 10 + (*s - L'0');
    }
    return v;
}

static int SecsLeft() {
    long long ms = g_running ? (long long)(g_endTick - GetTickCount64()) : g_remainMs;
    if (ms < 0) ms = 0;
    return (int)((ms + 999) / 1000);
}

static void Toggle() {
    if (g_running) {
        g_remainMs = (long long)(g_endTick - GetTickCount64());
        if (g_remainMs < 0) g_remainMs = 0;
        g_running = false;
    } else {
        if (g_remainMs <= 0) g_remainMs = g_durationMs;
        g_endTick = GetTickCount64() + g_remainMs;
        g_running = true;
    }
}

static void Reset(HWND h) {
    g_running = false;
    g_remainMs = g_durationMs;
    InvalidateRect(h, nullptr, FALSE);
}

static void AddMinutes(HWND h, int m) {
    long long add = (long long)m * 60 * 1000;
    if (g_running) g_endTick += add;
    else g_remainMs += add;
    InvalidateRect(h, nullptr, FALSE);
}

static void SetDuration(HWND h, int minutes) {
    g_durationMs = (long long)minutes * 60 * 1000;
    Reset(h);
}

static void CommitCustom() {
    if (!g_popup) return;
    wchar_t buf[16];
    GetWindowTextW(g_editCtl, buf, 16);
    int mins = ParseInt(buf);
    if (mins < 1) mins = 1;
    if (mins > 999) mins = 999;
    SetDuration(g_main, mins);
    DestroyWindow(g_popup);
}

static LRESULT CALLBACK PopupProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        wchar_t init[8];
        wsprintfW(init, L"%d", (int)(g_durationMs / 60000));
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

static const wchar_t* StateLabel() {
    if (g_running) return L"RUNNING";
    if (SecsLeft() <= 0) return L"DONE";
    if (g_remainMs < g_durationMs) return L"PAUSED";
    return L"READY";
}

static COLORREF StateColor() {
    if (g_running) return kBlue;
    if (SecsLeft() <= 0) return kRed;
    return kGray;
}

static void Spaced(const wchar_t* in, wchar_t* out, size_t outCap) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 2 < outCap; i++) {
        out[j++] = in[i];
        if (in[i + 1]) out[j++] = L' ';
    }
    out[j] = 0;
}

static void DrawText2(HDC dc, const wchar_t* t, RECT r, HFONT f, COLORREF c, UINT fmt) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, t, -1, &r, fmt | DT_SINGLELINE | DT_NOPREFIX);
}

static void Paint(HWND h) {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(h, &ps);
    RECT cr; GetClientRect(h, &cr);

    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, cr.right, cr.bottom);
    HGDIOBJ old = SelectObject(dc, bmp);
    SetBkMode(dc, TRANSPARENT);

    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(dc, &cr, bg);
    DeleteObject(bg);

    wchar_t lbl[32];
    Spaced(StateLabel(), lbl, 32);
    RECT lr = { 0, S(10), cr.right, S(26) };
    DrawText2(dc, lbl, lr, g_fLabel, StateColor(), DT_CENTER | DT_VCENTER);

    int s = SecsLeft();
    wchar_t buf[16];
    wsprintfW(buf, L"%02d:%02d", s / 60, s % 60);
    COLORREF timeColor = kFg;
    if (!g_running && s <= 0) timeColor = kRed;
    else if (g_running && s <= 60) timeColor = kOrange;
    RECT tr = { 0, S(28), cr.right, S(86) };
    DrawText2(dc, buf, tr, g_fTime, timeColor, DT_CENTER | DT_VCENTER);

    if (g_hover) {
        RECT rowBg = { 0, S(BTN_Y - 6), cr.right, cr.bottom };
        HBRUSH rowBrush = CreateSolidBrush(kBtnRow);
        FillRect(dc, &rowBg, rowBrush);
        DeleteObject(rowBrush);

        const wchar_t* glyph[B_COUNT] = { g_running ? L"❚❚" : L"▶", L"+5", L"+10", L"↺", L"✕" };
        HFONT fonts[B_COUNT] = { g_fBtnIcon, g_fBtnText, g_fBtnText, g_fBtnIcon, g_fBtnIcon };
        HBRUSH bb = CreateSolidBrush(kBtn);
        for (int i = 0; i < B_COUNT; i++) {
            RECT r = BtnRect(i);
            FillRect(dc, &r, bb);
            DrawText2(dc, glyph[i], r, fonts[i], kFg, DT_CENTER | DT_VCENTER);
        }
        DeleteObject(bb);
    }

    BitBlt(wdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(h, &ps);
}

static void ArmLeave(HWND h, bool nc) {
    TRACKMOUSEEVENT tme = { sizeof(tme) };
    tme.dwFlags = TME_LEAVE | (nc ? TME_NONCLIENT : 0);
    tme.hwndTrack = h;
    TrackMouseEvent(&tme);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_NCHITTEST: {
        POINT p = { (short)LOWORD(l), (short)HIWORD(l) };
        ScreenToClient(h, &p);
        return HitButton(p) >= 0 ? HTCLIENT : HTCAPTION;   // drag from anywhere but buttons
    }
    case WM_MOUSEMOVE:
        if (!g_hover) { g_hover = true; InvalidateRect(h, nullptr, FALSE); }
        ArmLeave(h, false);
        return 0;
    case WM_NCMOUSEMOVE:
        if (!g_hover) { g_hover = true; InvalidateRect(h, nullptr, FALSE); }
        ArmLeave(h, true);
        return 0;
    case WM_MOUSELEAVE:
    case WM_NCMOUSELEAVE:
        g_hover = false;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        POINT p = { (short)LOWORD(l), (short)HIWORD(l) };
        switch (HitButton(p)) {
        case B_PLAYPAUSE: Toggle(); InvalidateRect(h, nullptr, FALSE); break;
        case B_PLUS5:  AddMinutes(h, 5); break;
        case B_PLUS10: AddMinutes(h, 10); break;
        case B_RESET:  Reset(h); break;
        case B_CLOSE:  DestroyWindow(h); break;
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
        case ID_DUR_25:  SetDuration(h, 25); break;
        case ID_DUR_30:  SetDuration(h, 30); break;
        case ID_DUR_45:  SetDuration(h, 45); break;
        case ID_DUR_60:  SetDuration(h, 60); break;
        case ID_DUR_120: SetDuration(h, 120); break;
        case ID_CUSTOM:  ShowCustomPopup(h); break;
        case ID_QUIT:    DestroyWindow(h); break;
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_SPACE) { Toggle(); InvalidateRect(h, nullptr, FALSE); }
        else if (w == VK_ESCAPE) DestroyWindow(h);
        return 0;
    case WM_TIMER:
        if (g_running) {
            long long left = (long long)(g_endTick - GetTickCount64());
            if (left <= 0) {
                g_running = false;
                g_remainMs = 0;
                MessageBeep(MB_ICONEXCLAMATION);
                FlashWindow(h, TRUE);
                g_shownSecs = -1;
                InvalidateRect(h, nullptr, FALSE);
            } else {
                g_remainMs = left;
                if (SecsLeft() != g_shownSecs) { g_shownSecs = SecsLeft(); InvalidateRect(h, nullptr, FALSE); }
            }
        }
        return 0;
    case WM_PAINT:      Paint(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_DESTROY:    PostQuitMessage(0); return 0;
    }
    return DefWindowProc(h, m, w, l);
}

static HFONT MakeFont(int px, int weight, const wchar_t* face) {
    return CreateFontW(-S(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int) {
    g_hinst = hi;
    SetProcessDPIAware();
    HDC sdc = GetDC(nullptr);
    g_dpi = GetDeviceCaps(sdc, LOGPIXELSX);
    ReleaseDC(nullptr, sdc);

    g_fTime    = MakeFont(34, FW_NORMAL, L"Segoe UI Light");
    g_fLabel   = MakeFont(11, FW_SEMIBOLD, L"Segoe UI");
    g_fBtnIcon = MakeFont(13, FW_NORMAL, L"Segoe UI Symbol");
    g_fBtnText = MakeFont(12, FW_SEMIBOLD, L"Segoe UI");
    g_fPopup   = MakeFont(14, FW_NORMAL, L"Segoe UI");

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
                              WS_POPUP, S(40), S(40), S(W), S(H), nullptr, nullptr, hi, nullptr);
    SetLayeredWindowAttributes(g_main, 0, 250, LWA_ALPHA);
    SetWindowRgn(g_main, CreateRoundRectRgn(0, 0, S(W) + 1, S(H) + 1, S(18), S(18)), TRUE);
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
