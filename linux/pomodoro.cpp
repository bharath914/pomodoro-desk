// Minimal always-on-top focus-timer overlay for Linux/X11 (Xlib + Xft, no toolkit deps).
// All countdown logic lives in ../core/TimerEngine — this file is UI/rendering only.
//
// Build: g++ -O2 -s linux/pomodoro.cpp core/TimerEngine.cpp -o pomodoro $(pkg-config --cflags --libs x11 xext xft)
//
// DigitalNumbers-Regular.ttf must sit next to the binary (loaded in-process, not installed system-wide).
//
// Drag anywhere to move, drag an edge/corner to resize. Space = start/pause. Right-click = menu (duration/custom/quit).

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/extensions/shape.h>
#include <X11/Xft/Xft.h>
#include <fontconfig/fontconfig.h>
#include <sys/select.h>
#include <unistd.h>
#include <time.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "../core/TimerEngine.h"

// ---------- base design layout, in logical units (scaled uniformly to fit the window) ----------
static const int BASE_W = 320, BASE_H = 198;
static const int MIN_W = 220, MIN_H = 150;
static const int EDGE = 8;  // px margin around the window edge that grabs for resize

struct Rect { int x, y, w, h; };
static bool PtIn(const Rect& r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

struct Layout {
    double scale;
    Rect time, startBtn, stepper, stepMinus, stepPlus, pauseBtn, resetBtn;
};

static Rect Xform(double s, int ox, int oy, int bx, int by, int bw, int bh) {
    return { ox + (int)(bx * s), oy + (int)(by * s), (int)(bw * s), (int)(bh * s) };
}

static Layout ComputeLayout(int cw, int ch) {
    double sx = (double)cw / BASE_W, sy = (double)ch / BASE_H;
    double s = sx < sy ? sx : sy;
    int offX = (int)((cw - BASE_W * s) / 2);
    int offY = (int)((ch - BASE_H * s) / 2);

    Layout L;
    L.scale = s;
    L.time     = Xform(s, offX, offY, 0, 14, BASE_W, 86);
    L.startBtn = Xform(s, offX, offY, 75, 112, 170, 40);
    L.stepper  = Xform(s, offX, offY, 27, 164, 150, 34);
    L.pauseBtn = Xform(s, offX, offY, 185, 164, 50, 34);
    L.resetBtn = Xform(s, offX, offY, 243, 164, 50, 34);
    int quarter = L.stepper.w / 4;
    L.stepMinus = { L.stepper.x, L.stepper.y, quarter, L.stepper.h };
    L.stepPlus  = { L.stepper.x + L.stepper.w - quarter, L.stepper.y, quarter, L.stepper.h };
    return L;
}

enum { HIT_NONE = -1, HIT_START, HIT_STEP_MINUS, HIT_STEP_PLUS, HIT_PAUSE, HIT_RESET };

static int HitButton(const Layout& L, int x, int y) {
    if (PtIn(L.startBtn, x, y)) return HIT_START;
    if (PtIn(L.stepMinus, x, y)) return HIT_STEP_MINUS;
    if (PtIn(L.stepPlus, x, y)) return HIT_STEP_PLUS;
    if (PtIn(L.pauseBtn, x, y)) return HIT_PAUSE;
    if (PtIn(L.resetBtn, x, y)) return HIT_RESET;
    return HIT_NONE;
}

// ---------- X state ----------
static Display* dpy;
static int screen;
static Window root, win;
static Pixmap backbuffer;
static XftDraw* xftDraw;
static Visual* visual;
static Colormap colormap;
static XftFont *fTime, *fStart, *fStep, *fIcon, *fPopup;
static int winX = 60, winY = 60, curW = BASE_W, curH = BASE_H;
static bool appRunning = true;

static TimerEngine g_timer(25);
static int g_shownSecs = -1;

static int64_t Now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int ClampMinutesInput(const std::string& s) {
    int v = 0;
    for (char c : s) if (c >= '0' && c <= '9') v = v * 10 + (c - '0');
    return ClampMinutes(v);
}

// ---------- colors ----------
static XftColor cBg, cFg, cBlack;

static void MakeColor(XftColor* out, int r, int g, int b) {
    XRenderColor rc = { (unsigned short)(r * 257), (unsigned short)(g * 257), (unsigned short)(b * 257), 0xffff };
    XftColorAllocValue(dpy, visual, colormap, &rc, out);
}

static void FillRect(Drawable target, XftColor* c, int x, int y, int w, int h) {
    GC gc = XCreateGC(dpy, target, 0, nullptr);
    XSetForeground(dpy, gc, c->pixel);
    XFillRectangle(dpy, target, gc, x, y, w, h);
    XFreeGC(dpy, gc);
}

// Pill: fully rounded ends. Rounded-square: modest corner radius. Outline-only when fill==border==outline color scheme handled by caller.
static void FillRoundedRect(Drawable target, XftColor* fill, int x, int y, int w, int h, int radius) {
    GC gc = XCreateGC(dpy, target, 0, nullptr);
    XSetForeground(dpy, gc, fill->pixel);
    if (radius > h / 2) radius = h / 2;
    if (radius > w / 2) radius = w / 2;
    XFillRectangle(dpy, target, gc, x + radius, y, w - 2 * radius, h);
    XFillRectangle(dpy, target, gc, x, y + radius, w, h - 2 * radius);
    XFillArc(dpy, target, gc, x, y, radius * 2, radius * 2, 90 * 64, 90 * 64);
    XFillArc(dpy, target, gc, x + w - radius * 2, y, radius * 2, radius * 2, 0 * 64, 90 * 64);
    XFillArc(dpy, target, gc, x, y + h - radius * 2, radius * 2, radius * 2, 180 * 64, 90 * 64);
    XFillArc(dpy, target, gc, x + w - radius * 2, y + h - radius * 2, radius * 2, radius * 2, 270 * 64, 90 * 64);
    XFreeGC(dpy, gc);
}

static void StrokeRoundedRect(Drawable target, XftColor* border, int x, int y, int w, int h, int radius) {
    GC gc = XCreateGC(dpy, target, 0, nullptr);
    XSetForeground(dpy, gc, border->pixel);
    if (radius > h / 2) radius = h / 2;
    if (radius > w / 2) radius = w / 2;
    XDrawLine(dpy, target, gc, x + radius, y, x + w - radius, y);
    XDrawLine(dpy, target, gc, x + radius, y + h - 1, x + w - radius, y + h - 1);
    XDrawLine(dpy, target, gc, x, y + radius, x, y + h - radius);
    XDrawLine(dpy, target, gc, x + w - 1, y + radius, x + w - 1, y + h - radius);
    XDrawArc(dpy, target, gc, x, y, radius * 2, radius * 2, 90 * 64, 90 * 64);
    XDrawArc(dpy, target, gc, x + w - radius * 2 - 1, y, radius * 2, radius * 2, 0 * 64, 90 * 64);
    XDrawArc(dpy, target, gc, x, y + h - radius * 2 - 1, radius * 2, radius * 2, 180 * 64, 90 * 64);
    XDrawArc(dpy, target, gc, x + w - radius * 2 - 1, y + h - radius * 2 - 1, radius * 2, radius * 2, 270 * 64, 90 * 64);
    XFreeGC(dpy, gc);
}

static void DrawCentered(Drawable target, XftFont* font, XftColor* c, const Rect& r, const char* text) {
    XGlyphInfo ext;
    XftTextExtentsUtf8(dpy, font, (const FcChar8*)text, strlen(text), &ext);
    XftDraw* d = XftDrawCreate(dpy, target, visual, colormap);
    int tx = r.x + (r.w - (int)ext.xOff) / 2;
    int ty = r.y + (r.h - (font->ascent + font->descent)) / 2 + font->ascent;
    XftDrawStringUtf8(d, c, font, tx, ty, (const FcChar8*)text, strlen(text));
    XftDrawDestroy(d);
}

// ---------- timer actions ----------
static void RedrawMain();

static void UiToggle() { g_timer.toggle(Now()); RedrawMain(); }
static void UiReset() { g_timer.reset(); RedrawMain(); }
static void UiAddMinutes(int m) { g_timer.addMinutes(m); RedrawMain(); }
static void UiSetDuration(int minutes) { g_timer.setDuration(minutes); RedrawMain(); }

static const char* StartLabel() {
    switch (g_timer.state()) {
    case TimerState::Running: return "Pause";
    case TimerState::Paused:  return "Resume";
    default:                  return "Start";
    }
}

static std::string FontPathNextToExe(const char* filename) {
    char exePath[4096];
    ssize_t n = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (n <= 0) return filename;
    exePath[n] = 0;
    char* slash = strrchr(exePath, '/');
    if (slash) *(slash + 1) = 0;
    return std::string(exePath) + filename;
}

static void RebuildFonts(double scale) {
    if (fTime) XftFontClose(dpy, fTime);
    if (fStart) XftFontClose(dpy, fStart);
    if (fStep) XftFontClose(dpy, fStep);
    if (fIcon) XftFontClose(dpy, fIcon);
    char pat[64];
    snprintf(pat, sizeof(pat), "Digital Numbers:size=%d", (int)(68 * scale));
    fTime = XftFontOpenName(dpy, screen, pat);
    snprintf(pat, sizeof(pat), "Sans:bold:size=%d", (int)(17 * scale));
    fStart = XftFontOpenName(dpy, screen, pat);
    snprintf(pat, sizeof(pat), "Sans:bold:size=%d", (int)(15 * scale));
    fStep = XftFontOpenName(dpy, screen, pat);
    snprintf(pat, sizeof(pat), "Sans:size=%d", (int)(16 * scale));
    fIcon = XftFontOpenName(dpy, screen, pat);
}

static void RecreateBackbuffer() {
    if (xftDraw) XftDrawDestroy(xftDraw);
    if (backbuffer) XFreePixmap(dpy, backbuffer);
    backbuffer = XCreatePixmap(dpy, win, curW, curH, DefaultDepth(dpy, screen));
    xftDraw = XftDrawCreate(dpy, backbuffer, visual, colormap);
}

static void ApplyRoundedShape(int w, int h) {
    int radius = (int)(22 * ComputeLayout(w, h).scale);
    int maxR = (w < h ? w : h) / 2;
    if (radius > maxR) radius = maxR;
    if (radius < 1) radius = 1;
    Pixmap mask = XCreatePixmap(dpy, win, w, h, 1);
    GC mgc = XCreateGC(dpy, mask, 0, nullptr);
    XSetForeground(dpy, mgc, 0);
    XFillRectangle(dpy, mask, mgc, 0, 0, w, h);
    XSetForeground(dpy, mgc, 1);
    XFillRectangle(dpy, mask, mgc, radius, 0, w - 2 * radius, h);
    XFillRectangle(dpy, mask, mgc, 0, radius, w, h - 2 * radius);
    XFillArc(dpy, mask, mgc, 0, 0, radius * 2, radius * 2, 0, 360 * 64);
    XFillArc(dpy, mask, mgc, w - radius * 2, 0, radius * 2, radius * 2, 0, 360 * 64);
    XFillArc(dpy, mask, mgc, 0, h - radius * 2, radius * 2, radius * 2, 0, 360 * 64);
    XFillArc(dpy, mask, mgc, w - radius * 2, h - radius * 2, radius * 2, radius * 2, 0, 360 * 64);
    XShapeCombineMask(dpy, win, ShapeBounding, 0, 0, mask, ShapeSet);
    XFreeGC(dpy, mgc);
    XFreePixmap(dpy, mask);
}

static void SetOpacity(Window target, double opacity) {
    Atom atom = XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", False);
    unsigned long value = (unsigned long)(opacity * 0xFFFFFFFFUL);
    XChangeProperty(dpy, target, atom, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&value, 1);
}

static Window MakeOverrideRedirectWindow(int x, int y, int w, int h) {
    XSetWindowAttributes attrs = {};
    attrs.override_redirect = True;
    attrs.background_pixel = cBg.pixel;
    attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask;
    return XCreateWindow(dpy, root, x, y, w, h, 0, DefaultDepth(dpy, screen), InputOutput,
                          visual, CWOverrideRedirect | CWBackPixel | CWEventMask, &attrs);
}

// ---------- context menu ----------
struct MenuItem { const char* text; int id; };
enum { ID_DUR_25 = 1, ID_DUR_30, ID_DUR_45, ID_DUR_60, ID_DUR_120, ID_CUSTOM, ID_QUIT };

static void ShowCustomPopup(int nearX, int nearY);

static int RunContextMenu(int rootX, int rootY) {
    static const MenuItem items[] = {
        { "25 minutes", ID_DUR_25 }, { "30 minutes", ID_DUR_30 }, { "45 minutes", ID_DUR_45 },
        { "1 hour", ID_DUR_60 }, { "2 hours", ID_DUR_120 }, { "Custom\xe2\x80\xa6", ID_CUSTOM }, { "Quit", ID_QUIT },
    };
    const int n = sizeof(items) / sizeof(items[0]);
    const int itemH = 24, menuW = 150;
    int menuH = n * itemH + 8;

    int sw = DisplayWidth(dpy, screen), sh = DisplayHeight(dpy, screen);
    int x = rootX, y = rootY;
    if (x + menuW > sw) x = sw - menuW;
    if (y + menuH > sh) y = sh - menuH;

    Window menu = MakeOverrideRedirectWindow(x, y, menuW, menuH);
    XMapRaised(dpy, menu);
    XGrabPointer(dpy, menu, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XGrabKeyboard(dpy, menu, True, GrabModeAsync, GrabModeAsync, CurrentTime);

    auto draw = [&](int hoverIdx) {
        FillRect(menu, &cBg, 0, 0, menuW, menuH);
        for (int i = 0; i < n; i++) {
            int iy = 4 + i * itemH;
            if (i == hoverIdx) FillRect(menu, &cBlack, 2, iy, menuW - 4, itemH);
            DrawCentered(menu, fPopup, &cFg, { 0, iy, menuW, itemH }, items[i].text);
        }
        XFlush(dpy);
    };

    int result = -1;
    int hoverIdx = -1;
    draw(hoverIdx);
    while (result == -1) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == Expose && ev.xexpose.window == menu) draw(hoverIdx);
        else if (ev.type == MotionNotify) {
            int idx = (ev.xmotion.y - 4) / itemH;
            bool inside = ev.xmotion.x >= 0 && ev.xmotion.x < menuW && idx >= 0 && idx < n;
            int newHover = inside ? idx : -1;
            if (newHover != hoverIdx) { hoverIdx = newHover; draw(hoverIdx); }
        } else if (ev.type == ButtonRelease) {
            int idx = (ev.xbutton.y - 4) / itemH;
            bool inside = ev.xbutton.x >= 0 && ev.xbutton.x < menuW && idx >= 0 && idx < n;
            result = inside ? items[idx].id : 0;
        } else if (ev.type == KeyPress) {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            if (ks == XK_Escape) result = 0;
        }
    }

    XUngrabKeyboard(dpy, CurrentTime);
    XUngrabPointer(dpy, CurrentTime);
    XDestroyWindow(dpy, menu);
    return result;
}

static void ShowContextMenu(int rootX, int rootY) {
    int id = RunContextMenu(rootX, rootY);
    switch (id) {
    case ID_DUR_25:  UiSetDuration(25); break;
    case ID_DUR_30:  UiSetDuration(30); break;
    case ID_DUR_45:  UiSetDuration(45); break;
    case ID_DUR_60:  UiSetDuration(60); break;
    case ID_DUR_120: UiSetDuration(120); break;
    case ID_CUSTOM:  ShowCustomPopup(winX, winY + curH + 4); break;
    case ID_QUIT:    appRunning = false; break;
    }
}

// ---------- custom duration popup ----------
static void ShowCustomPopup(int nearX, int nearY) {
    const int w = 180, h = 70;
    Window popup = MakeOverrideRedirectWindow(nearX, nearY, w, h);
    XMapRaised(dpy, popup);
    XGrabPointer(dpy, popup, True, ButtonPressMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XGrabKeyboard(dpy, popup, True, GrabModeAsync, GrabModeAsync, CurrentTime);

    char initial[8];
    snprintf(initial, sizeof(initial), "%d", (int)(g_timer.durationMs() / 60000));
    std::string buf = initial;

    auto draw = [&]() {
        FillRect(popup, &cBg, 0, 0, w, h);
        DrawCentered(popup, fPopup, &cFg, { 10, 8, 160, 18 }, "Minutes (1-999):");
        std::string shown = buf + "|";
        DrawCentered(popup, fPopup, &cFg, { 10, 36, 160, 24 }, shown.c_str());
        XFlush(dpy);
    };

    bool done = false;
    draw();
    while (!done) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == Expose && ev.xexpose.window == popup) {
            draw();
        } else if (ev.type == KeyPress) {
            char kbuf[32];
            KeySym ks;
            XLookupString(&ev.xkey, kbuf, sizeof(kbuf), &ks, nullptr);
            if (ks >= XK_0 && ks <= XK_9 && buf.size() < 3) {
                buf += (char)('0' + (ks - XK_0));
                draw();
            } else if (ks == XK_BackSpace) {
                if (!buf.empty()) buf.pop_back();
                draw();
            } else if (ks == XK_Return || ks == XK_KP_Enter) {
                UiSetDuration(ClampMinutesInput(buf));
                done = true;
            } else if (ks == XK_Escape) {
                done = true;
            }
        } else if (ev.type == ButtonPress) {
            bool inside = ev.xbutton.x >= 0 && ev.xbutton.x < w && ev.xbutton.y >= 0 && ev.xbutton.y < h;
            if (!inside) done = true;
        }
    }

    XUngrabKeyboard(dpy, CurrentTime);
    XUngrabPointer(dpy, CurrentTime);
    XDestroyWindow(dpy, popup);
}

// ---------- rendering ----------
static void RedrawMain() {
    Layout L = ComputeLayout(curW, curH);
    FillRect(backbuffer, &cBg, 0, 0, curW, curH);

    int s = g_timer.secsLeft(Now());
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", s / 60, s % 60);
    DrawCentered(backbuffer, fTime, &cFg, L.time, buf);

    FillRoundedRect(backbuffer, &cFg, L.startBtn.x, L.startBtn.y, L.startBtn.w, L.startBtn.h, L.startBtn.h / 2);
    DrawCentered(backbuffer, fStart, &cBlack, L.startBtn, StartLabel());

    FillRoundedRect(backbuffer, &cBg, L.stepper.x, L.stepper.y, L.stepper.w, L.stepper.h, L.stepper.h / 2);
    StrokeRoundedRect(backbuffer, &cFg, L.stepper.x, L.stepper.y, L.stepper.w, L.stepper.h, L.stepper.h / 2);
    Rect mid = { L.stepMinus.x + L.stepMinus.w, L.stepper.y, L.stepper.w - 2 * L.stepMinus.w, L.stepper.h };
    DrawCentered(backbuffer, fStep, &cFg, L.stepMinus, "-");
    DrawCentered(backbuffer, fStep, &cFg, mid, "10m");
    DrawCentered(backbuffer, fStep, &cFg, L.stepPlus, "+");

    int sqR = (int)(L.pauseBtn.h * 0.4);
    FillRoundedRect(backbuffer, &cBg, L.pauseBtn.x, L.pauseBtn.y, L.pauseBtn.w, L.pauseBtn.h, sqR);
    StrokeRoundedRect(backbuffer, &cFg, L.pauseBtn.x, L.pauseBtn.y, L.pauseBtn.w, L.pauseBtn.h, sqR);
    DrawCentered(backbuffer, fIcon, &cFg, L.pauseBtn, g_timer.isRunning() ? "||" : ">");

    FillRoundedRect(backbuffer, &cBg, L.resetBtn.x, L.resetBtn.y, L.resetBtn.w, L.resetBtn.h, sqR);
    StrokeRoundedRect(backbuffer, &cFg, L.resetBtn.x, L.resetBtn.y, L.resetBtn.w, L.resetBtn.h, sqR);
    DrawCentered(backbuffer, fIcon, &cFg, L.resetBtn, "R");

    XCopyArea(dpy, backbuffer, win, DefaultGC(dpy, screen), 0, 0, curW, curH, 0, 0);
    XFlush(dpy);
}

static void OnResized(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    curW = w;
    curH = h;
    RecreateBackbuffer();
    ApplyRoundedShape(w, h);
    RebuildFonts(ComputeLayout(w, h).scale);
    RedrawMain();
}

// ---------- main window event handling ----------
enum DragMode { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE };
static DragMode dragMode = DRAG_NONE;
static int dragStartRootX, dragStartRootY, dragStartWinX, dragStartWinY, dragStartW, dragStartH;
static bool resizeLeft, resizeRight, resizeTop, resizeBottom;

static void BeginResize(int rootX, int rootY, int localX, int localY) {
    dragMode = DRAG_RESIZE;
    dragStartRootX = rootX; dragStartRootY = rootY;
    dragStartWinX = winX; dragStartWinY = winY;
    dragStartW = curW; dragStartH = curH;
    resizeLeft = localX < EDGE;
    resizeRight = localX >= curW - EDGE;
    resizeTop = localY < EDGE;
    resizeBottom = localY >= curH - EDGE;
}

static void HandleEvent(const XEvent& ev) {
    switch (ev.type) {
    case Expose:
        RedrawMain();
        break;
    case ConfigureNotify:
        winX = ev.xconfigure.x;
        winY = ev.xconfigure.y;
        if (ev.xconfigure.width != curW || ev.xconfigure.height != curH) {
            OnResized(ev.xconfigure.width, ev.xconfigure.height);
        }
        break;
    case ButtonPress: {
        const XButtonEvent& be = ev.xbutton;
        if (be.button == Button3) {
            ShowContextMenu(be.x_root, be.y_root);
        } else if (be.button == Button1) {
            bool onEdge = be.x < EDGE || be.x >= curW - EDGE || be.y < EDGE || be.y >= curH - EDGE;
            int hit = HitButton(ComputeLayout(curW, curH), be.x, be.y);
            if (onEdge) {
                BeginResize(be.x_root, be.y_root, be.x, be.y);
            } else if (hit != HIT_NONE) {
                switch (hit) {
                case HIT_START:      UiToggle(); break;
                case HIT_STEP_MINUS: UiAddMinutes(-10); break;
                case HIT_STEP_PLUS:  UiAddMinutes(10); break;
                case HIT_PAUSE:      UiToggle(); break;
                case HIT_RESET:      UiReset(); break;
                }
            } else {
                dragMode = DRAG_MOVE;
                dragStartRootX = be.x_root - winX;
                dragStartRootY = be.y_root - winY;
            }
        }
        break;
    }
    case ButtonRelease:
        dragMode = DRAG_NONE;
        break;
    case MotionNotify:
        if (dragMode == DRAG_MOVE) {
            winX = ev.xmotion.x_root - dragStartRootX;
            winY = ev.xmotion.y_root - dragStartRootY;
            XMoveWindow(dpy, win, winX, winY);
        } else if (dragMode == DRAG_RESIZE) {
            int dx = ev.xmotion.x_root - dragStartRootX;
            int dy = ev.xmotion.y_root - dragStartRootY;
            int newX = dragStartWinX, newY = dragStartWinY;
            int newW = dragStartW, newH = dragStartH;
            if (resizeRight) newW = dragStartW + dx;
            if (resizeBottom) newH = dragStartH + dy;
            if (resizeLeft) { newW = dragStartW - dx; newX = dragStartWinX + dx; }
            if (resizeTop) { newH = dragStartH - dy; newY = dragStartWinY + dy; }
            if (newW < MIN_W) { if (resizeLeft) newX -= (MIN_W - newW); newW = MIN_W; }
            if (newH < MIN_H) { if (resizeTop) newY -= (MIN_H - newH); newH = MIN_H; }
            XMoveResizeWindow(dpy, win, newX, newY, newW, newH);
        }
        break;
    case KeyPress: {
        KeySym ks = XLookupKeysym((XKeyEvent*)&ev.xkey, 0);
        if (ks == XK_space) UiToggle();
        else if (ks == XK_Escape) appRunning = false;
        break;
    }
    }
}

int main() {
    dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        std::fprintf(stderr, "Cannot open X display\n");
        return 1;
    }
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    visual = DefaultVisual(dpy, screen);
    colormap = DefaultColormap(dpy, screen);

    MakeColor(&cBg, 0, 0, 0);
    MakeColor(&cFg, 255, 255, 255);
    MakeColor(&cBlack, 0, 0, 0);

    std::string fontPath = FontPathNextToExe("DigitalNumbers-Regular.ttf");
    FcConfigAppFontAddFile(FcConfigGetCurrent(), (const FcChar8*)fontPath.c_str());
    fPopup = XftFontOpenName(dpy, screen, "Sans:size=12");
    RebuildFonts(1.0);

    win = MakeOverrideRedirectWindow(winX, winY, curW, curH);
    XSelectInput(dpy, win, ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                               KeyPressMask | StructureNotifyMask);
    SetOpacity(win, 0.98);
    XMapRaised(dpy, win);

    RecreateBackbuffer();
    ApplyRoundedShape(curW, curH);
    RedrawMain();

    int xfd = XConnectionNumber(dpy);
    while (appRunning) {
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            HandleEvent(ev);
            if (!appRunning) break;
        }
        if (!appRunning) break;

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(xfd, &fds);
        struct timeval tv = { 0, 200000 };
        int rv = select(xfd + 1, &fds, nullptr, nullptr, &tv);
        if (rv == 0 && g_timer.isRunning()) {
            int64_t now = Now();
            if (g_timer.update(now)) {
                XBell(dpy, 0);
                g_shownSecs = -1;
                RedrawMain();
            } else if (g_timer.secsLeft(now) != g_shownSecs) {
                g_shownSecs = g_timer.secsLeft(now);
                RedrawMain();
            }
        }
    }

    XftDrawDestroy(xftDraw);
    XFreePixmap(dpy, backbuffer);
    XCloseDisplay(dpy);
    return 0;
}
