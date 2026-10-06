// Minimal always-on-top focus-timer overlay for Linux/X11 (Xlib + Xft, no toolkit deps).
// All countdown logic lives in ../core/TimerEngine — this file is UI/rendering only.
//
// Build: g++ -O2 -s linux/pomodoro.cpp core/TimerEngine.cpp -o pomodoro $(pkg-config --cflags --libs x11 xext xft)
//
// Drag anywhere to move. Hover to reveal controls. Space = start/pause. Right-click = menu (duration/custom/quit).

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/extensions/shape.h>
#include <X11/Xft/Xft.h>
#include <sys/select.h>
#include <time.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "../core/TimerEngine.h"

// ---------- layout ----------
static const int W = 216, H = 130;
static const int BTN_W = 36, BTN_H = 26, BTN_GAP = 4, BTN_Y = 96;
enum { B_PLAYPAUSE, B_PLUS5, B_PLUS10, B_RESET, B_CLOSE, B_COUNT };

struct Rect { int x, y, w, h; };

static Rect BtnRect(int i) {
    int total = B_COUNT * BTN_W + (B_COUNT - 1) * BTN_GAP;
    int x = (W - total) / 2 + i * (BTN_W + BTN_GAP);
    return { x, BTN_Y, BTN_W, BTN_H };
}
static bool PtIn(const Rect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// ---------- X state ----------
static Display* dpy;
static int screen;
static Window root, win;
static Pixmap backbuffer;
static XftDraw* xftDraw;
static Visual* visual;
static Colormap colormap;
static XftFont *fTime, *fLabel, *fBtnIcon, *fBtnText, *fPopup;
static int winX = 40, winY = 40;
static bool appRunning = true;

static TimerEngine g_timer(25);
static bool g_hover = false;
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

// ---------- colors (flat, Apple-dark inspired) ----------
static XftColor cBg, cBtnRow, cBtn, cFg, cBlue, cGray, cRed, cOrange;

static void MakeColor(XftColor* out, int r, int g, int b) {
    XRenderColor rc = { (unsigned short)(r * 257), (unsigned short)(g * 257), (unsigned short)(b * 257), 0xffff };
    XftColorAllocValue(dpy, visual, colormap, &rc, out);
}

static void FillRect(Pixmap target, XftColor* c, int x, int y, int w, int h) {
    GC gc = XCreateGC(dpy, target, 0, nullptr);
    XSetForeground(dpy, gc, c->pixel);
    XFillRectangle(dpy, target, gc, x, y, w, h);
    XFreeGC(dpy, gc);
}

static void DrawCentered(XftFont* font, XftColor* c, int top, int height, const char* text) {
    XGlyphInfo extents;
    XftTextExtentsUtf8(dpy, font, (const FcChar8*)text, strlen(text), &extents);
    int x = (W - (int)extents.xOff) / 2;
    int y = top + (height - (font->ascent + font->descent)) / 2 + font->ascent;
    XftDrawStringUtf8(xftDraw, c, font, x, y, (const FcChar8*)text, strlen(text));
}

// ---------- timer actions ----------
static void RedrawMain();

static void UiToggle() { g_timer.toggle(Now()); RedrawMain(); }
static void UiReset() { g_timer.reset(); RedrawMain(); }
static void UiAddMinutes(int m) { g_timer.addMinutes(m); RedrawMain(); }
static void UiSetDuration(int minutes) { g_timer.setDuration(minutes); RedrawMain(); }

static const char* StateLabel() {
    switch (g_timer.state()) {
    case TimerState::Running: return "R U N N I N G";
    case TimerState::Done:    return "D O N E";
    case TimerState::Paused:  return "P A U S E D";
    default:                  return "R E A D Y";
    }
}

static XftColor* StateColor() {
    switch (g_timer.state()) {
    case TimerState::Running: return &cBlue;
    case TimerState::Done:    return &cRed;
    default:                  return &cGray;
    }
}

static void RedrawMain() {
    FillRect(backbuffer, &cBg, 0, 0, W, H);
    DrawCentered(fLabel, StateColor(), 8, 20, StateLabel());

    int64_t now = Now();
    int s = g_timer.secsLeft(now);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", s / 60, s % 60);
    XftColor* timeColor = &cFg;
    if (g_timer.state() == TimerState::Done) timeColor = &cRed;
    else if (g_timer.isRunning() && s <= 60) timeColor = &cOrange;
    DrawCentered(fTime, timeColor, 26, 58, buf);

    if (g_hover) {
        FillRect(backbuffer, &cBtnRow, 0, BTN_Y - 6, W, H - (BTN_Y - 6));
        const char* glyph[B_COUNT] = { g_timer.isRunning() ? "||" : ">", "+5", "+10", "R", "X" };
        XftFont* fonts[B_COUNT] = { fBtnIcon, fBtnText, fBtnText, fBtnIcon, fBtnIcon };
        for (int i = 0; i < B_COUNT; i++) {
            Rect r = BtnRect(i);
            FillRect(backbuffer, &cBtn, r.x, r.y, r.w, r.h);
            XGlyphInfo ext;
            XftTextExtentsUtf8(dpy, fonts[i], (const FcChar8*)glyph[i], strlen(glyph[i]), &ext);
            int tx = r.x + (r.w - (int)ext.xOff) / 2;
            int ty = r.y + (r.h - (fonts[i]->ascent + fonts[i]->descent)) / 2 + fonts[i]->ascent;
            XftDrawStringUtf8(xftDraw, &cFg, fonts[i], tx, ty, (const FcChar8*)glyph[i], strlen(glyph[i]));
        }
    }

    XCopyArea(dpy, backbuffer, win, DefaultGC(dpy, screen), 0, 0, W, H, 0, 0);
    XFlush(dpy);
}

static int HitButton(int x, int y) {
    if (!g_hover) return -1;
    for (int i = 0; i < B_COUNT; i++) if (PtIn(BtnRect(i), x, y)) return i;
    return -1;
}

static void ApplyRoundedShape(Window target, int w, int h, int radius) {
    Pixmap mask = XCreatePixmap(dpy, target, w, h, 1);
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
    XShapeCombineMask(dpy, target, ShapeBounding, 0, 0, mask, ShapeSet);
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
    Window ww = XCreateWindow(dpy, root, x, y, w, h, 0, DefaultDepth(dpy, screen), InputOutput,
                               visual, CWOverrideRedirect | CWBackPixel | CWEventMask, &attrs);
    return ww;
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
        FillRect(menu, &cBtnRow, 0, 0, menuW, menuH);
        for (int i = 0; i < n; i++) {
            int iy = 4 + i * itemH;
            if (i == hoverIdx) FillRect(menu, &cBtn, 2, iy, menuW - 4, itemH);
            XGlyphInfo ext;
            XftTextExtentsUtf8(dpy, fPopup, (const FcChar8*)items[i].text, strlen(items[i].text), &ext);
            int ty = iy + (itemH - (fPopup->ascent + fPopup->descent)) / 2 + fPopup->ascent;
            XftDraw* d = XftDrawCreate(dpy, menu, visual, colormap);
            XftDrawStringUtf8(d, &cFg, fPopup, 10, ty, (const FcChar8*)items[i].text, strlen(items[i].text));
            XftDrawDestroy(d);
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
            result = inside ? items[idx].id : 0;  // 0 = dismissed
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
    case ID_CUSTOM:  ShowCustomPopup(winX, winY + H + 4); break;
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
        XftDraw* d = XftDrawCreate(dpy, popup, visual, colormap);
        XftDrawStringUtf8(d, &cGray, fPopup, 10, 22, (const FcChar8*)"Minutes (1-999):", 16);
        std::string shown = buf + "|";
        XftDrawStringUtf8(d, &cFg, fPopup, 10, 50, (const FcChar8*)shown.c_str(), (int)shown.size());
        XftDrawDestroy(d);
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
            if (!inside) done = true;  // click outside cancels, like losing focus
        }
    }

    XUngrabKeyboard(dpy, CurrentTime);
    XUngrabPointer(dpy, CurrentTime);
    XDestroyWindow(dpy, popup);
}

// ---------- main window event handling ----------
static bool dragging = false;
static int dragOffX = 0, dragOffY = 0;

static void HandleEvent(const XEvent& ev) {
    switch (ev.type) {
    case Expose:
        RedrawMain();
        break;
    case EnterNotify:
        g_hover = true;
        RedrawMain();
        break;
    case LeaveNotify:
        g_hover = false;
        RedrawMain();
        break;
    case ButtonPress: {
        const XButtonEvent& be = ev.xbutton;
        if (be.button == Button3) {
            ShowContextMenu(be.x_root, be.y_root);
        } else if (be.button == Button1) {
            int hit = HitButton(be.x, be.y);
            switch (hit) {
            case B_PLAYPAUSE: UiToggle(); break;
            case B_PLUS5:  UiAddMinutes(5); break;
            case B_PLUS10: UiAddMinutes(10); break;
            case B_RESET:  UiReset(); break;
            case B_CLOSE:  appRunning = false; break;
            default:
                dragging = true;
                dragOffX = be.x_root - winX;
                dragOffY = be.y_root - winY;
            }
        }
        break;
    }
    case ButtonRelease:
        dragging = false;
        break;
    case MotionNotify:
        if (dragging) {
            winX = ev.xmotion.x_root - dragOffX;
            winY = ev.xmotion.y_root - dragOffY;
            XMoveWindow(dpy, win, winX, winY);
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

    MakeColor(&cBg, 22, 22, 24);
    MakeColor(&cBtnRow, 38, 38, 40);
    MakeColor(&cBtn, 54, 54, 58);
    MakeColor(&cFg, 245, 245, 247);
    MakeColor(&cBlue, 10, 132, 255);
    MakeColor(&cGray, 142, 142, 147);
    MakeColor(&cRed, 255, 69, 58);
    MakeColor(&cOrange, 255, 159, 10);

    fTime    = XftFontOpenName(dpy, screen, "Sans:light:size=24");
    fLabel   = XftFontOpenName(dpy, screen, "Sans:bold:size=9");
    fBtnIcon = XftFontOpenName(dpy, screen, "Sans:bold:size=13");
    fBtnText = XftFontOpenName(dpy, screen, "Sans:bold:size=10");
    fPopup   = XftFontOpenName(dpy, screen, "Sans:size=11");

    win = MakeOverrideRedirectWindow(winX, winY, W, H);
    XSelectInput(dpy, win, ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                               EnterWindowMask | LeaveWindowMask | KeyPressMask);
    ApplyRoundedShape(win, W, H, 18);
    SetOpacity(win, 0.98);
    XMapRaised(dpy, win);

    backbuffer = XCreatePixmap(dpy, win, W, H, DefaultDepth(dpy, screen));
    xftDraw = XftDrawCreate(dpy, backbuffer, visual, colormap);

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
