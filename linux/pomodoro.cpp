// Minimal always-on-top Pomodoro overlay for Linux/X11 (Xlib + Xft, no toolkit deps).
// All countdown/cycle logic lives in ../core/TimerEngine — this file is UI/rendering only.
//
// Build: g++ -O2 -s linux/pomodoro.cpp core/TimerEngine.cpp -o pomodoro $(pkg-config --cflags --libs x11 xext xft)
//
// A normal (WM-managed, undecorated) window: shows up in the taskbar/pager and can be
// minimized, unlike a plain override-redirect popup. Drag anywhere to move, drag an
// edge/corner to resize. Space = start/pause. Esc or the close button = quit.

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
#include <cmath>
#include <string>
#include "../core/TimerEngine.h"

// ---------- base design layout, in logical units (scaled uniformly to fit the window) ----------
static const int BASE_W = 466, BASE_H = 568;
static const int MIN_W = 300, MIN_H = 360;
static const int EDGE = 8;  // px margin around the window edge that grabs for resize
static const int TOPBAR_H = 32;

struct Rect { int x, y, w, h; };
static bool PtIn(const Rect& r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

struct Layout {
    double scale;
    Rect topbarClose, topbarMin;
    Rect segTrack;
    Rect tabs[3];
    int ringCx, ringCy, ringR, ringThickness;
    Rect startBtn, resetBtn;
    int dotsY, dotR, dotGap;
};

static const char* kTabLabels[3] = { "Focus", "Short break", "Long break" };

static Rect Xform(double s, int ox, int oy, int bx, int by, int bw, int bh) {
    return { ox + (int)(bx * s), oy + (int)(by * s), (int)(bw * s), (int)(bh * s) };
}

// forward decl needed by ComputeLayout for tab text widths
static int TextWidthPx(const char* text, double scale);

static Layout ComputeLayout(int cw, int ch) {
    double sx = (double)cw / BASE_W, sy = (double)ch / BASE_H;
    double s = sx < sy ? sx : sy;
    int offX = (int)((cw - BASE_W * s) / 2);
    int offY = (int)((ch - BASE_H * s) / 2);

    Layout L;
    L.scale = s;
    L.topbarClose = Xform(s, offX, offY, 422, 6, 28, 28);
    L.topbarMin   = Xform(s, offX, offY, 388, 6, 28, 28);

    int tabPad = (int)(14 * s);
    int tabH = (int)(46 * s);
    int tabW[3];
    int totalTabW = 0;
    for (int i = 0; i < 3; i++) {
        tabW[i] = TextWidthPx(kTabLabels[i], s) + tabPad * 2;
        totalTabW += tabW[i];
    }
    int trackPad = (int)(5 * s);
    int trackW = totalTabW + trackPad * 2;
    int trackX = offX + (int)(BASE_W * s) / 2 - trackW / 2;
    int trackY = offY + (int)(54 * s);
    L.segTrack = { trackX, trackY, trackW, tabH + trackPad * 2 };
    int tx = trackX + trackPad;
    for (int i = 0; i < 3; i++) {
        L.tabs[i] = { tx, trackY + trackPad, tabW[i], tabH };
        tx += tabW[i];
    }

    L.ringCx = offX + (int)(233 * s);
    L.ringCy = offY + (int)(264 * s);
    L.ringR = (int)(115 * s);
    L.ringThickness = (int)(13 * s);

    L.startBtn = Xform(s, offX, offY, 107, 420, 120, 59);
    L.resetBtn = Xform(s, offX, offY, 239, 420, 120, 59);

    L.dotsY = offY + (int)(523 * s);
    L.dotR = (int)(5.5 * s);
    L.dotGap = (int)(8 * s);
    return L;
}

enum { HIT_NONE = -1, HIT_CLOSE, HIT_MIN, HIT_TAB0, HIT_TAB1, HIT_TAB2, HIT_START, HIT_RESET };

static int HitButton(const Layout& L, int x, int y) {
    if (PtIn(L.topbarClose, x, y)) return HIT_CLOSE;
    if (PtIn(L.topbarMin, x, y)) return HIT_MIN;
    for (int i = 0; i < 3; i++) if (PtIn(L.tabs[i], x, y)) return HIT_TAB0 + i;
    if (PtIn(L.startBtn, x, y)) return HIT_START;
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
static XftFont *fTime, *fMode, *fTab, *fButton, *fSessions, *fIcon;
static int winX = 80, winY = 60, curW = BASE_W, curH = BASE_H;
static bool appRunning = true;
static Atom wmDeleteAtom;

static TimerEngine g_timer;
static int g_shownSecs = -1;

static int64_t Now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// ---------- colors ----------
static XftColor cBg, cSegTrack, cResetBg, cRing, cSecondary, cWhite, cBlack;

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

static void FillRoundedRect(Drawable target, XftColor* fill, int x, int y, int w, int h, int radius) {
    GC gc = XCreateGC(dpy, target, 0, nullptr);
    XSetForeground(dpy, gc, fill->pixel);
    if (radius > h / 2) radius = h / 2;
    if (radius > w / 2) radius = w / 2;
    if (radius < 1) radius = 1;
    XFillRectangle(dpy, target, gc, x + radius, y, w - 2 * radius, h);
    XFillRectangle(dpy, target, gc, x, y + radius, w, h - 2 * radius);
    XFillArc(dpy, target, gc, x, y, radius * 2, radius * 2, 90 * 64, 90 * 64);
    XFillArc(dpy, target, gc, x + w - radius * 2, y, radius * 2, radius * 2, 0 * 64, 90 * 64);
    XFillArc(dpy, target, gc, x, y + h - radius * 2, radius * 2, radius * 2, 180 * 64, 90 * 64);
    XFillArc(dpy, target, gc, x + w - radius * 2, y + h - radius * 2, radius * 2, radius * 2, 270 * 64, 90 * 64);
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

static int TextWidthPx(const char* text, double scale) {
    if (!fTab) return (int)(60 * scale);  // fonts not loaded yet (first layout pass); refined next frame
    XGlyphInfo ext;
    XftTextExtentsUtf8(dpy, fTab, (const FcChar8*)text, strlen(text), &ext);
    return (int)ext.xOff;
}

// ---------- timer actions ----------
static void RedrawMain();

static void UiToggle() { g_timer.toggle(Now()); RedrawMain(); }
static void UiReset() { g_timer.reset(); RedrawMain(); }
static void UiSelectMode(Mode m) { g_timer.selectMode(m); RedrawMain(); }

static const char* StartLabel() {
    switch (g_timer.state()) {
    case RunState::Running: return "Pause";
    case RunState::Paused:  return "Resume";
    default:                return "Start";
    }
}

static const char* ModeLabel(Mode m) {
    switch (m) {
    case Mode::Focus:      return "Focus";
    case Mode::ShortBreak: return "Short break";
    case Mode::LongBreak:  return "Long break";
    }
    return "Focus";
}

static void RebuildFonts(double scale) {
    if (fTime) XftFontClose(dpy, fTime);
    if (fMode) XftFontClose(dpy, fMode);
    if (fTab) XftFontClose(dpy, fTab);
    if (fButton) XftFontClose(dpy, fButton);
    if (fSessions) XftFontClose(dpy, fSessions);
    if (fIcon) XftFontClose(dpy, fIcon);
    char pat[64];
    auto open = [&](const char* fmt, double px) -> XftFont* {
        snprintf(pat, sizeof(pat), fmt, (int)(px * scale));
        return XftFontOpenName(dpy, screen, pat);
    };
    fTime     = open("Sans:bold:pixelsize=%d", 46);
    fMode     = open("Sans:pixelsize=%d", 14);
    fTab      = open("Sans:bold:pixelsize=%d", 13);
    fButton   = open("Sans:bold:pixelsize=%d", 15);
    fSessions = open("Sans:pixelsize=%d", 12);
    fIcon     = open("Sans:bold:pixelsize=%d", 16);
}

static void RecreateBackbuffer() {
    if (xftDraw) XftDrawDestroy(xftDraw);
    if (backbuffer) XFreePixmap(dpy, backbuffer);
    backbuffer = XCreatePixmap(dpy, win, curW, curH, DefaultDepth(dpy, screen));
    xftDraw = XftDrawCreate(dpy, backbuffer, visual, colormap);
}

static void ApplyRoundedShape(int w, int h) {
    double s = ComputeLayout(w, h).scale;
    int radius = (int)(24 * s);
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

// ---------- rendering ----------
static void DrawRing(int cx, int cy, int r, int thickness, double progress) {
    GC gc = XCreateGC(dpy, backbuffer, 0, nullptr);
    XSetLineAttributes(dpy, gc, thickness, LineSolid, CapButt, JoinRound);
    XSetForeground(dpy, gc, cRing.pixel);
    XDrawArc(dpy, backbuffer, gc, cx - r, cy - r, r * 2, r * 2, 0, 360 * 64);
    if (progress > 0.0015) {
        if (progress > 1.0) progress = 1.0;
        XSetForeground(dpy, gc, cWhite.pixel);
        int sweep = (int)(progress * 360 * 64);
        XDrawArc(dpy, backbuffer, gc, cx - r, cy - r, r * 2, r * 2, 90 * 64, -sweep);
    }
    XFreeGC(dpy, gc);
}

static void RedrawMain() {
    Layout L = ComputeLayout(curW, curH);
    FillRect(backbuffer, &cBg, 0, 0, curW, curH);

    DrawCentered(backbuffer, fIcon, &cSecondary, L.topbarMin, "\xe2\x80\x94");   // —
    DrawCentered(backbuffer, fIcon, &cSecondary, L.topbarClose, "\xc3\x97");     // ×

    FillRoundedRect(backbuffer, &cSegTrack, L.segTrack.x, L.segTrack.y, L.segTrack.w, L.segTrack.h, L.segTrack.h / 2);
    Mode cur = g_timer.mode();
    for (int i = 0; i < 3; i++) {
        bool selected = (int)cur == i;
        if (selected) FillRoundedRect(backbuffer, &cWhite, L.tabs[i].x, L.tabs[i].y, L.tabs[i].w, L.tabs[i].h, L.tabs[i].h / 2);
        DrawCentered(backbuffer, fTab, selected ? &cBlack : &cSecondary, L.tabs[i], kTabLabels[i]);
    }

    int64_t now = Now();
    double total = (double)TimerEngine::durationForMode(cur);
    double remain = (double)g_timer.remainingMs(now);
    double progress = total > 0 ? 1.0 - (remain / total) : 0.0;
    DrawRing(L.ringCx, L.ringCy, L.ringR, L.ringThickness, progress);

    int s = g_timer.secsLeft(now);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", s / 60, s % 60);
    int timeBoxH = (int)(56 * L.scale);
    Rect timeBox = { L.ringCx - L.ringR, L.ringCy - timeBoxH - (int)(2 * L.scale), L.ringR * 2, timeBoxH };
    DrawCentered(backbuffer, fTime, &cWhite, timeBox, buf);
    Rect modeBox = { L.ringCx - L.ringR, L.ringCy + (int)(6 * L.scale), L.ringR * 2, (int)(22 * L.scale) };
    DrawCentered(backbuffer, fMode, &cSecondary, modeBox, ModeLabel(cur));

    FillRoundedRect(backbuffer, &cWhite, L.startBtn.x, L.startBtn.y, L.startBtn.w, L.startBtn.h, L.startBtn.h / 2);
    DrawCentered(backbuffer, fButton, &cBlack, L.startBtn, StartLabel());
    FillRoundedRect(backbuffer, &cResetBg, L.resetBtn.x, L.resetBtn.y, L.resetBtn.w, L.resetBtn.h, L.resetBtn.h / 2);
    DrawCentered(backbuffer, fButton, &cSecondary, L.resetBtn, "Reset");

    int cyclePos = g_timer.cyclePosition();
    int dotsTotalW = 4 * (L.dotR * 2) + 3 * L.dotGap;
    char sessionsText[32];
    int sessions = g_timer.completedSessions();
    snprintf(sessionsText, sizeof(sessionsText), "%d session%s", sessions, sessions == 1 ? "" : "s");
    XGlyphInfo ext;
    XftTextExtentsUtf8(dpy, fSessions, (const FcChar8*)sessionsText, strlen(sessionsText), &ext);
    int clusterW = dotsTotalW + (int)(12 * L.scale) + (int)ext.xOff;
    int clusterX = (curW - clusterW) / 2;

    GC dotGc = XCreateGC(dpy, backbuffer, 0, nullptr);
    for (int i = 0; i < 4; i++) {
        int dcx = clusterX + L.dotR + i * (L.dotR * 2 + L.dotGap);
        XSetForeground(dpy, dotGc, (i < cyclePos ? cWhite : cRing).pixel);
        XFillArc(dpy, backbuffer, dotGc, dcx - L.dotR, L.dotsY - L.dotR, L.dotR * 2, L.dotR * 2, 0, 360 * 64);
    }
    XFreeGC(dpy, dotGc);
    Rect sessionsBox = { clusterX + dotsTotalW + (int)(12 * L.scale), L.dotsY - (int)(10 * L.scale), (int)ext.xOff + 4, (int)(20 * L.scale) };
    DrawCentered(backbuffer, fSessions, &cSecondary, sessionsBox, sessionsText);

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
    case ClientMessage:
        if ((Atom)ev.xclient.data.l[0] == wmDeleteAtom) appRunning = false;
        break;
    case ButtonPress: {
        const XButtonEvent& be = ev.xbutton;
        if (be.button == Button1) {
            bool onEdge = be.x < EDGE || be.x >= curW - EDGE || be.y < EDGE || be.y >= curH - EDGE;
            int hit = HitButton(ComputeLayout(curW, curH), be.x, be.y);
            if (onEdge) {
                BeginResize(be.x_root, be.y_root, be.x, be.y);
            } else if (hit != HIT_NONE) {
                switch (hit) {
                case HIT_CLOSE: appRunning = false; break;
                case HIT_MIN:   XIconifyWindow(dpy, win, screen); break;
                case HIT_TAB0:  UiSelectMode(Mode::Focus); break;
                case HIT_TAB1:  UiSelectMode(Mode::ShortBreak); break;
                case HIT_TAB2:  UiSelectMode(Mode::LongBreak); break;
                case HIT_START: UiToggle(); break;
                case HIT_RESET: UiReset(); break;
                }
            } else if (be.y > TOPBAR_H * curH / BASE_H) {
                // avoid stealing drags from the topbar icon row's empty space, but allow everywhere else
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
            // Aspect ratio is locked to BASE_W:BASE_H so the whole layout always visibly
            // scales together, even when the user only drags a single left/right or
            // top/bottom edge rather than a corner.
            int dx = ev.xmotion.x_root - dragStartRootX;
            int dy = ev.xmotion.y_root - dragStartRootY;
            int rawW = dragStartW, rawH = dragStartH;
            if (resizeRight) rawW = dragStartW + dx;
            if (resizeLeft) rawW = dragStartW - dx;
            if (resizeBottom) rawH = dragStartH + dy;
            if (resizeTop) rawH = dragStartH - dy;

            double scaleW = (double)rawW / dragStartW;
            double scaleH = (double)rawH / dragStartH;
            bool horiz = resizeLeft || resizeRight;
            bool vert = resizeTop || resizeBottom;
            double scale = (horiz && vert) ? (scaleW > scaleH ? scaleW : scaleH) : (horiz ? scaleW : scaleH);

            int newW = (int)(dragStartW * scale);
            int newH = (int)(dragStartH * scale);
            if (newW < MIN_W) { scale = (double)MIN_W / dragStartW; newW = MIN_W; newH = (int)(dragStartH * scale); }
            if (newH < MIN_H) { scale = (double)MIN_H / dragStartH; newH = MIN_H; newW = (int)(dragStartW * scale); }

            int newX = dragStartWinX, newY = dragStartWinY;
            if (resizeLeft) newX = dragStartWinX + (dragStartW - newW);
            if (resizeTop) newY = dragStartWinY + (dragStartH - newH);
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

    MakeColor(&cBg, 11, 11, 11);
    MakeColor(&cSegTrack, 29, 29, 31);
    MakeColor(&cResetBg, 21, 21, 23);
    MakeColor(&cRing, 58, 57, 62);
    MakeColor(&cSecondary, 155, 155, 158);
    MakeColor(&cWhite, 255, 255, 255);
    MakeColor(&cBlack, 0, 0, 0);

    RebuildFonts(1.0);

    // Normal, WM-managed, undecorated window — shows in the taskbar/pager and can be minimized,
    // unlike an override-redirect popup.
    XSetWindowAttributes attrs = {};
    attrs.background_pixel = cBg.pixel;
    attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                        KeyPressMask | StructureNotifyMask;
    win = XCreateWindow(dpy, root, winX, winY, curW, curH, 0, DefaultDepth(dpy, screen), InputOutput,
                         visual, CWBackPixel | CWEventMask, &attrs);

    XStoreName(dpy, win, "Pomodoro Desk");
    XClassHint* classHint = XAllocClassHint();
    classHint->res_name = (char*)"pomodoro";
    classHint->res_class = (char*)"PomodoroDesk";
    XSetClassHint(dpy, win, classHint);
    XFree(classHint);

    XSizeHints sizeHints = {};
    sizeHints.flags = PPosition | PMinSize | PAspect;
    sizeHints.x = winX; sizeHints.y = winY;
    sizeHints.min_width = MIN_W; sizeHints.min_height = MIN_H;
    sizeHints.min_aspect.x = sizeHints.max_aspect.x = BASE_W;
    sizeHints.min_aspect.y = sizeHints.max_aspect.y = BASE_H;
    XSetWMNormalHints(dpy, win, &sizeHints);

    Atom motif = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
    struct { unsigned long flags, functions, decorations; long input_mode; unsigned long status; } motifHints = {};
    motifHints.flags = 2;  // MWM_HINTS_DECORATIONS
    motifHints.decorations = 0;
    XChangeProperty(dpy, win, motif, motif, 32, PropModeReplace, (unsigned char*)&motifHints, 5);

    Atom winType = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom normalType = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NORMAL", False);
    XChangeProperty(dpy, win, winType, XA_ATOM, 32, PropModeReplace, (unsigned char*)&normalType, 1);

    wmDeleteAtom = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wmDeleteAtom, 1);

    XMapWindow(dpy, win);
    XFlush(dpy);

    Atom stateAtom = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom aboveAtom = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
    XEvent xev = {};
    xev.xclient.type = ClientMessage;
    xev.xclient.window = win;
    xev.xclient.message_type = stateAtom;
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = 1;  // _NET_WM_STATE_ADD
    xev.xclient.data.l[1] = (long)aboveAtom;
    xev.xclient.data.l[3] = 1;
    XSendEvent(dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &xev);

    std::fprintf(stderr, "pomodoro window=0x%lx\n", win);

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
