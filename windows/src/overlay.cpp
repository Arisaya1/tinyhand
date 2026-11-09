#include "overlay.h"

#include "prefs.h"

#include <mmsystem.h>

namespace {

const wchar_t *kClass = L"TinyHandOverlay";
constexpr UINT kBusyMs = 8;
constexpr UINT kIdleMs = 50;
constexpr UINT_PTR kTimerId = 1;
constexpr int kEscHotkey = 1;

// State shared with the mouse hook thread. The hook runs on its own thread
// (like the macOS event tap) so a busy frame never delays the user's mouse.
struct HookState {
    CRITICAL_SECTION cs;
    HWND target = nullptr;
    bool active = false;
    int interactVk = 0;
    bool interactPress = false;
    bool passingThrough = false;
    bool pressing = false;
    bool idle = false;
    bool dragPosted = false;
    std::vector<RECT> passRects;
    std::vector<POINT> points;
    HHOOK hook = nullptr;
    HANDLE thread = nullptr;
    DWORD threadId = 0;
    HookState() { InitializeCriticalSection(&cs); }
};

HookState hs;

struct Lock {
    Lock() { EnterCriticalSection(&hs.cs); }
    ~Lock() { LeaveCriticalSection(&hs.cs); }
};

bool in_rects(const std::vector<RECT> &rects, POINT p) {
    for (const RECT &r : rects) {
        if (PtInRect(&r, p)) {
            return true;
        }
    }
    return false;
}

LRESULT CALLBACK mouse_proc(int code, WPARAM wp, LPARAM lp) {
    if (code != HC_ACTION) {
        return CallNextHookEx(nullptr, code, wp, lp);
    }
    const auto *m = reinterpret_cast<const MSLLHOOKSTRUCT *>(lp);
    const bool down = wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN || wp == WM_XBUTTONDOWN;
    const bool up = wp == WM_LBUTTONUP || wp == WM_RBUTTONUP || wp == WM_MBUTTONUP || wp == WM_XBUTTONUP;
    bool swallow = false;
    UINT post = 0;
    LPARAM postL = MAKELPARAM(m->pt.x, m->pt.y);
    HWND target;
    {
        Lock lock;
        target = hs.target;
        if (wp == WM_MOUSEMOVE) {
            if (hs.active && hs.pressing) {
                if (hs.points.size() < 16384) {
                    hs.points.push_back(m->pt);
                }
                if (!hs.dragPosted) {
                    hs.dragPosted = true;
                    post = WM_TH_DRAG;
                }
            } else if (hs.idle) {
                hs.idle = false;
                post = WM_TH_WAKE;
            }
        } else if (hs.active && (down || up)) {
            if (down && !hs.interactPress && !hs.pressing && !hs.passingThrough && hs.interactVk &&
                (GetAsyncKeyState(hs.interactVk) & 0x8000)) {
                hs.interactPress = true;
                post = WM_TH_INTERACT_DOWN;
            } else if (hs.interactPress) {
                if (up) {
                    hs.interactPress = false;
                    post = WM_TH_INTERACT_UP;
                }
            } else {
                if (down && !hs.pressing && !hs.passingThrough && in_rects(hs.passRects, m->pt)) {
                    hs.passingThrough = true;
                    post = WM_TH_PASSTHROUGH;
                }
                if (hs.passingThrough) {
                    if (up) {
                        hs.passingThrough = false;
                    }
                } else if (down) {
                    swallow = true;
                    if (!hs.pressing) {
                        hs.pressing = true;
                        hs.points.clear();
                        hs.points.push_back(m->pt);
                        post = WM_TH_MOUSEDOWN;
                    }
                } else {
                    swallow = true;
                    if (hs.pressing) {
                        hs.pressing = false;
                        post = WM_TH_MOUSEUP;
                    }
                }
            }
        }
    }
    if (post && target) {
        PostMessageW(target, post, 0, postL);
    }
    return swallow ? 1 : CallNextHookEx(nullptr, code, wp, lp);
}

DWORD WINAPI hook_thread(LPVOID ready) {
    MSG msg;
    PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, mouse_proc, GetModuleHandleW(nullptr), 0);
    {
        Lock lock;
        hs.hook = hook;
    }
    SetEvent(static_cast<HANDLE>(ready));
    if (!hook) {
        return 1;
    }
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    }
    UnhookWindowsHookEx(hook);
    Lock lock;
    hs.hook = nullptr;
    return 0;
}

bool hook_install() {
    if (hs.thread) {
        return hs.hook != nullptr;
    }
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    hs.thread = CreateThread(nullptr, 0, hook_thread, ready, 0, &hs.threadId);
    if (!hs.thread) {
        CloseHandle(ready);
        return false;
    }
    WaitForSingleObject(ready, 2000);
    CloseHandle(ready);
    if (!hs.hook) {
        WaitForSingleObject(hs.thread, 1000);
        CloseHandle(hs.thread);
        hs.thread = nullptr;
        return false;
    }
    return true;
}

void hook_remove() {
    if (!hs.thread) {
        return;
    }
    PostThreadMessageW(hs.threadId, WM_QUIT, 0, 0);
    WaitForSingleObject(hs.thread, 1000);
    CloseHandle(hs.thread);
    hs.thread = nullptr;
}

void hook_set_active(bool active) {
    Lock lock;
    hs.active = active;
    hs.interactPress = false;
    hs.passingThrough = false;
    hs.pressing = false;
    hs.points.clear();
}

RECT union_rect(const RECT &a, const RECT &b) {
    if (a.right <= a.left || a.bottom <= a.top) {
        return b;
    }
    if (b.right <= b.left || b.bottom <= b.top) {
        return a;
    }
    return RECT{std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right),
                std::max(a.bottom, b.bottom)};
}

bool class_is(HWND hwnd, std::initializer_list<const wchar_t *> names) {
    wchar_t cls[96];
    if (!GetClassNameW(hwnd, cls, int(std::size(cls)))) {
        return false;
    }
    for (const wchar_t *n : names) {
        if (wcscmp(cls, n) == 0) {
            return true;
        }
    }
    return false;
}

struct ShellScan {
    std::vector<RECT> menus;
    std::vector<RECT> shell;
    HWND foreground;
};

BOOL CALLBACK scan_window(HWND hwnd, LPARAM lp) {
    auto *scan = reinterpret_cast<ShellScan *>(lp);
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }
    RECT r;
    if (!GetWindowRect(hwnd, &r) || r.right <= r.left || r.bottom <= r.top) {
        return TRUE;
    }
    if (class_is(hwnd, {L"#32768"})) {
        scan->menus.push_back(r);
    } else if (class_is(hwnd, {L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"NotifyIconOverflowWindow",
                               L"TopLevelWindowForOverflowXamlIsland"})) {
        scan->shell.push_back(r);
    } else if (hwnd == scan->foreground &&
               class_is(hwnd, {L"Windows.UI.Core.CoreWindow", L"XamlExplorerHostIslandWindow",
                               L"Shell_InputSwitchTopLevelWindow"})) {
        scan->menus.push_back(r);
    }
    return TRUE;
}

} // namespace

namespace cursor {

static bool hidden = false;

void hide() {
    if (hidden) {
        return;
    }
    hidden = true;
    prefs::set_cursor_hidden_flag(true);
    BYTE andMask[32 * 4];
    BYTE xorMask[32 * 4];
    memset(andMask, 0xFF, sizeof(andMask));
    memset(xorMask, 0x00, sizeof(xorMask));
    HCURSOR blank = CreateCursor(GetModuleHandleW(nullptr), 0, 0, 32, 32, andMask, xorMask);
    const DWORD ids[] = {32512, 32513, 32514, 32515, 32516, 32642, 32643, 32644,
                         32645, 32646, 32648, 32649, 32650, 32651, 32671, 32672};
    int failed = 0;
    for (DWORD id : ids) {
        if (!SetSystemCursor(CopyCursor(blank), id)) {
            failed++;
        }
    }
    DestroyCursor(blank);
    if (failed) {
        th::log(L"could not hide %d system cursors (error %lu)", failed, GetLastError());
    }
}

void show() {
    if (!hidden) {
        return;
    }
    hidden = false;
    SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
    prefs::set_cursor_hidden_flag(false);
}

void repair_if_needed() {
    if (prefs::cursor_hidden_flag()) {
        th::log(L"restoring cursors left hidden by a previous run");
        SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
        prefs::set_cursor_hidden_flag(false);
    }
}

} // namespace cursor

bool Overlay::create(HINSTANCE instance, Toolbar *toolbar) {
    toolbar_ = toolbar;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                            kClass, L"TinyHand", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, instance, this);
    if (!hwnd_) {
        return false;
    }
    {
        Lock lock;
        hs.target = hwnd_;
    }
    arm_.setStyle(prefs::get().pointerStyle);
    if (arm_.style() != prefs::get().pointerStyle) {
        prefs::get().pointerStyle = arm_.style();
    }
    toolbar_->onPick = [this](const std::wstring &s) { pick(s); };
    toolbar_->onToggleSound = [this] { setSoundOn(!soundOn()); };
    toolbar_->onExit = [this] { exitPointer(); };
    toolbar_->select(arm_.style());
    syncSound();
    prefs::listen([this] { syncPrefs(); });
    return true;
}

LRESULT CALLBACK Overlay::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        reinterpret_cast<Overlay *>(cs->lpCreateParams)->hwnd_ = hwnd;
    }
    auto *self = reinterpret_cast<Overlay *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->proc(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Overlay::proc(UINT msg, WPARAM wp, LPARAM lp) {
    double t = th::now();
    switch (msg) {
    case WM_TIMER:
        if (wp == kTimerId && active_) {
            tick();
        }
        return 0;
    case WM_TH_MOUSEDOWN: {
        if (!active_) {
            return 0;
        }
        presses_++;
        arm_.press();
        arm_.held = true;
        sound_.play();
        toolbar_->hideHint(t);
        down_ = true;
        downPoint_ = local(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        lastButtonEvent_ = t;
        wake();
        return 0;
    }
    case WM_TH_DRAG:
        drainPoints();
        wake();
        return 0;
    case WM_TH_MOUSEUP:
        drainPoints();
        release();
        lastButtonEvent_ = t;
        wake();
        return 0;
    case WM_TH_INTERACT_DOWN:
        arm_.press();
        arm_.held = true;
        if (prefs::get().interactKey == InteractKey::Alt) {
            // Stops the Alt release from opening the focused app's menu bar.
            INPUT in[2] = {};
            in[0].type = in[1].type = INPUT_KEYBOARD;
            in[0].ki.wVk = in[1].ki.wVk = 0xE8;
            in[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, in, sizeof(INPUT));
        }
        lastButtonEvent_ = t;
        wake();
        return 0;
    case WM_TH_INTERACT_UP:
        arm_.held = false;
        lastButtonEvent_ = t;
        wake();
        return 0;
    case WM_TH_PASSTHROUGH: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (in_rects(shellRects_, p)) {
            menuOpeningUntil_ = t + 1;
        }
        lastButtonEvent_ = t;
        wake();
        return 0;
    }
    case WM_TH_WAKE:
        wake();
        return 0;
    case WM_HOTKEY:
        if (wp == kEscHotkey) {
            exitPointer();
        }
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        monitor_ = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

bool Overlay::hookInstalled() const {
    Lock lock;
    return hs.hook != nullptr;
}

Gdiplus::PointF Overlay::local(POINT p) const {
    return Gdiplus::PointF(float(p.x - monRect_.left), float(p.y - monRect_.top));
}

void Overlay::syncSound() {
    sound_.enabled = soundOn();
    toolbar_->setSoundOn(soundOn());
    if (onSoundChanged) {
        onSoundChanged();
    }
}

void Overlay::syncPrefs() {
    {
        Lock lock;
        hs.interactVk = interact_vk(prefs::get().interactKey);
    }
    if (arm_.style() != prefs::get().pointerStyle) {
        pick(prefs::get().pointerStyle);
    }
    toolbar_->refresh();
    syncSound();
}

bool Overlay::soundOn() const {
    return arm_.isLaser() ? prefs::get().laserSound : prefs::get().handSound;
}

void Overlay::setSoundOn(bool on) {
    if (arm_.isLaser()) {
        prefs::get().laserSound = on;
    } else {
        prefs::get().handSound = on;
    }
    prefs::changed();
}

void Overlay::pick(const std::wstring &style) {
    arm_.setStyle(style);
    prefs::get().pointerStyle = arm_.style();
    prefs::save();
    toolbar_->select(arm_.style());
    syncSound();
}

void Overlay::toggle() {
    if (active_) {
        deactivate();
    } else {
        activate();
    }
}

void Overlay::exitPointer() {
    clearInk();
    deactivate();
}

void Overlay::clearInk() {
    ink_.clear();
    if (active_) {
        render(false, false);
    }
}

void Overlay::suspendInput(bool suspended) {
    if (!active_) {
        return;
    }
    ownMenuOpen_ = suspended;
    Lock lock;
    hs.active = !suspended && hs.hook != nullptr;
}

bool Overlay::activate() {
    if (active_) {
        return hookInstalled();
    }
    bool installed = hook_install();
    active_ = true;
    monitor_ = nullptr;
    lastTick_ = 0;
    normalCursor_ = false;
    arm_.hidden = false;
    arm_.held = false;
    down_ = false;
    {
        Lock lock;
        hs.interactVk = interact_vk(prefs::get().interactKey);
        hs.idle = false;
    }
    hook_set_active(installed);
    if (!periodRaised_) {
        timeBeginPeriod(1);
        periodRaised_ = true;
    }
    cursor::hide();
    tick();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    toolbar_->show();
    render(true, true);

    double t = th::now();
    stillSince_ = t;
    timerMs_ = 0;
    runTimer(kBusyMs);
    revealUntil_ = t + 3;
    if (installed && !prefs::get().hintShown) {
        prefs::get().hintShown = true;
        prefs::save();
        std::vector<std::pair<std::wstring, std::wstring>> parts = {{L"Click", L"to tap"}, {L"Drag", L"to draw"}};
        if (prefs::get().interactKey != InteractKey::Off) {
            parts.push_back({std::wstring(L"Hold ") + interact_key_name(prefs::get().interactKey), L"to click for real"});
        }
        parts.push_back({L"Esc", L"to exit"});
        toolbar_->showHint(parts, t);
        revealUntil_ = t + 10;
        hintHideAt_ = t + 10;
    }
    setEscEnabled(true);
    th::log(L"activate hookInstalled=%d monitor=%ldx%ld scale=%.2f", installed, monRect_.right - monRect_.left,
            monRect_.bottom - monRect_.top, scale_);
    if (onActiveChanged) {
        onActiveChanged();
    }
    return installed;
}

void Overlay::deactivate() {
    if (!active_) {
        return;
    }
    active_ = false;
    KillTimer(hwnd_, kTimerId);
    timerMs_ = 0;
    hook_set_active(false);
    hook_remove();
    arm_.held = false;
    down_ = false;
    ink_.clear();
    ink_.takeDirty();
    surface_.clear();
    surface_.present(hwnd_, POINT{monRect_.left, monRect_.top}, 255);
    ShowWindow(hwnd_, SW_HIDE);
    toolbar_->hide();
    normalCursor_ = false;
    arm_.hidden = false;
    {
        Lock lock;
        hs.passRects.clear();
    }
    setEscEnabled(false);
    ownMenuOpen_ = false;
    cursor::show();
    if (periodRaised_) {
        timeEndPeriod(1);
        periodRaised_ = false;
    }
    monitor_ = nullptr;
    th::log(L"deactivate");
    if (onActiveChanged) {
        onActiveChanged();
    }
}

void Overlay::setEscEnabled(bool enabled) {
    if (enabled && active_ && !escRegistered_) {
        escRegistered_ = RegisterHotKey(hwnd_, kEscHotkey, MOD_NOREPEAT, VK_ESCAPE) != FALSE;
    }
    if (!enabled && escRegistered_) {
        UnregisterHotKey(hwnd_, kEscHotkey);
        escRegistered_ = false;
    }
}

void Overlay::drainPoints() {
    std::vector<POINT> pts;
    {
        Lock lock;
        pts.swap(hs.points);
        hs.dragPosted = false;
    }
    if (!down_) {
        return;
    }
    for (POINT p : pts) {
        Gdiplus::PointF lp = local(p);
        if (!ink_.drawing()) {
            if (std::hypot(lp.X - downPoint_.X, lp.Y - downPoint_.Y) < 4 * scale_) {
                continue;
            }
            ink_.begin(downPoint_, arm_.inkWidth(), ink_for(arm_.style()));
        }
        ink_.extend(lp);
    }
}

void Overlay::release() {
    arm_.held = false;
    down_ = false;
    ink_.end(th::now());
}

void Overlay::scanShell(double now) {
    ShellScan scan;
    scan.foreground = GetForegroundWindow();
    EnumWindows(scan_window, reinterpret_cast<LPARAM>(&scan));
    menuRects_ = std::move(scan.menus);
    shellRects_ = std::move(scan.shell);
    lastScan_ = now;
}

void Overlay::updateZones(POINT mouse, double now) {
    const float s = scale_;
    RECT bar = toolbar_->frameRect();
    RECT wide = bar;
    InflateRect(&wide, LONG(80 * s), 0);
    RECT above = bar;
    OffsetRect(&above, 0, -LONG(90 * s));
    RECT nearZone = union_rect(wide, above);
    InflateRect(&nearZone, 0, LONG(10 * s));
    bool drawing = ink_.drawing();
    toolbar_->setRevealed(!drawing && (PtInRect(&nearZone, mouse) || now < revealUntil_));

    bool nearShell = false;
    for (RECT r : shellRects_) {
        InflateRect(&r, 0, LONG(40 * s));
        nearShell = nearShell || PtInRect(&r, mouse);
    }
    bool menuLikely = !menuRects_.empty() || ownMenuOpen_ || now < menuOpeningUntil_ ||
                      now - lastButtonEvent_ < 2 || nearShell;
    if (now - lastScan_ > (menuLikely ? 0.05 : 0.25)) {
        scanShell(now);
    }
    bool overMenu = in_rects(menuRects_, mouse) || in_rects(shellRects_, mouse) || now < menuOpeningUntil_;
    int vk = interact_vk(prefs::get().interactKey);
    bool interactPress;
    {
        Lock lock;
        interactPress = hs.interactPress;
    }
    bool interacting = (vk && (GetAsyncKeyState(vk) & 0x8000)) || interactPress;
    bool normal = !drawing && !prefs::get().interactKeepsHand && (interacting || overMenu || ownMenuOpen_);
    setEscEnabled(menuRects_.empty() && !ownMenuOpen_);

    if (normal != normalCursor_) {
        th::log(normal ? L"real cursor (%ls) at %ld,%ld" : L"pointer back%ls at %ld,%ld",
                normal ? (interacting ? L"interact key" : L"menu or taskbar") : L"", mouse.x, mouse.y);
        normalCursor_ = normal;
        arm_.hidden = normal;
        if (normal) {
            cursor::show();
        } else {
            cursor::hide();
        }
    }

    std::vector<RECT> rects = shellRects_;
    rects.insert(rects.end(), menuRects_.begin(), menuRects_.end());
    if (toolbar_->revealed()) {
        rects.push_back(toolbar_->frameRect());
    }
    Lock lock;
    hs.passRects = std::move(rects);
}

void Overlay::render(bool armChanged, bool full) {
    const int w = monRect_.right - monRect_.left, h = monRect_.bottom - monRect_.top;
    if (w <= 0 || h <= 0 || !surface_.bitmap) {
        return;
    }
    RECT dirty{0, 0, 0, 0};
    if (full) {
        dirty = RECT{0, 0, w, h};
    }
    if (armChanged || full) {
        dirty = union_rect(dirty, armBounds_);
        armBounds_ = arm_.bounds();
        dirty = union_rect(dirty, armBounds_);
    }
    dirty = union_rect(dirty, ink_.takeDirty());
    if (dirty.right <= dirty.left || dirty.bottom <= dirty.top) {
        return;
    }
    InflateRect(&dirty, 2, 2);
    dirty.left = std::max(0L, dirty.left);
    dirty.top = std::max(0L, dirty.top);
    dirty.right = std::min(LONG(w), dirty.right);
    dirty.bottom = std::min(LONG(h), dirty.bottom);
    if (dirty.right <= dirty.left || dirty.bottom <= dirty.top) {
        return;
    }
    surface_.clear(dirty);
    {
        Gdiplus::Graphics g(surface_.bitmap.get());
        g.SetClip(Gdiplus::Rect(dirty.left, dirty.top, dirty.right - dirty.left, dirty.bottom - dirty.top));
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        ink_.draw(g);
        arm_.draw(g);
    }
    surface_.present(hwnd_, POINT{monRect_.left, monRect_.top}, 255, full ? nullptr : &dirty);
}

void Overlay::runTimer(UINT ms) {
    if (timerMs_ == ms) {
        return;
    }
    SetTimer(hwnd_, kTimerId, ms, nullptr);
    timerMs_ = ms;
}

void Overlay::wake() {
    double t = th::now();
    stillSince_ = t;
    if (!active_ || timerMs_ == kBusyMs) {
        return;
    }
    {
        Lock lock;
        hs.idle = false;
    }
    runTimer(kBusyMs);
    tick();
}

void Overlay::updatePace(bool busy, double now) {
    if (timerMs_ == kIdleMs) {
        if (busy) {
            wake();
        }
        return;
    }
    bool still = now - stillSince_ > 0.5;
    if (!still || busy || arm_.held || arm_.settling() || ink_.drawing() || down_) {
        return;
    }
    {
        Lock lock;
        hs.idle = true;
    }
    runTimer(kIdleMs);
}

void Overlay::tick() {
    if (!active_) {
        return;
    }
    double t = th::now();
    double dt = lastTick_ > 0 ? t - lastTick_ : 1.0 / 120.0;
    lastTick_ = t;
    POINT p;
    GetCursorPos(&p);
    HMONITOR m = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
    bool full = false;
    if (m != monitor_) {
        bool moving = monitor_ != nullptr;
        monitor_ = m;
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(m, &mi);
        monRect_ = mi.rcMonitor;
        workRect_ = mi.rcWork;
        scale_ = th::dpi_scale_at(p);
        if (moving) {
            release();
            revealUntil_ = t + 1.5;
        }
        ink_.clear();
        ink_.takeDirty();
        int w = monRect_.right - monRect_.left, h = monRect_.bottom - monRect_.top;
        surface_.resize(w, h);
        surface_.clear();
        armBounds_ = RECT{0, 0, 0, 0};
        SetWindowPos(hwnd_, HWND_TOPMOST, monRect_.left, monRect_.top, w, h, SWP_NOACTIVATE);
        arm_.setSize(w, h);
        toolbar_->place(monRect_, workRect_, scale_);
        full = true;
    }
    if (p.x != lastMouse_.x || p.y != lastMouse_.y) {
        lastMouse_ = p;
        stillSince_ = t;
    }
    updateZones(p, t);
    if (hintHideAt_ > 0 && t >= hintHideAt_) {
        hintHideAt_ = 0;
        toolbar_->hideHint(t);
    }
    Gdiplus::PointF l = local(p);
    bool armChanged = arm_.aim(l.X, l.Y, dt);
    ink_.update(t);
    toolbar_->animate(t);
    render(armChanged, full);
    updatePace(toolbar_->animating() || ink_.fading(t) || hintHideAt_ > 0, t);
}
