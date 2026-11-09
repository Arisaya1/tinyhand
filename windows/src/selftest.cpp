#include "selftest.h"

#include "app.h"
#include "arm.h"
#include "ink.h"
#include "prefs.h"
#include "toolbar.h"

#include <cstdarg>
#include <cstdio>

namespace selftest {

void print(const char *fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 2, fmt, args);
    va_end(args);
    if (n < 0) {
        return;
    }
    n = std::min(n, int(sizeof(buf) - 3));
    buf[n++] = '\n';
    buf[n] = 0;
    static HANDLE out = [] {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        if ((!h || h == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS)) {
            h = GetStdHandle(STD_OUTPUT_HANDLE);
        }
        return h;
    }();
    if (out && out != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(out, buf, DWORD(n), &written, nullptr);
    }
    buf[n - 1] = 0;
    wchar_t wide[2048];
    MultiByteToWideChar(CP_UTF8, 0, buf, -1, wide, int(std::size(wide)));
    th::log(L"selftest: %ls", wide);
}

namespace {

struct Rng {
    uint32_t s;
    float next() {
        s = s * 1664525u + 1013904223u;
        return float(s >> 8) / 16777216.0f;
    }
    float range(float a, float b) { return a + (b - a) * next(); }
};

using Pts = std::vector<Gdiplus::PointF>;

// A hand-drawn looking trace through the given corners: uneven spacing,
// jitter and a slow wobble, with a small overshoot when it closes.
Pts trace(const Pts &corners, bool closed, Rng &r, float jitter = 2.0f) {
    Pts out;
    Pts path = corners;
    if (closed) {
        path.push_back(corners[0]);
        Gdiplus::PointF a = corners[0], b = corners[1];
        path.push_back(Gdiplus::PointF(a.X + (b.X - a.X) * 0.12f, a.Y + (b.Y - a.Y) * 0.12f));
    }
    float phase = r.range(0, 6.28f);
    float walked = 0;
    for (size_t i = 0; i + 1 < path.size(); i++) {
        Gdiplus::PointF a = path[i], b = path[i + 1];
        float len = std::hypot(b.X - a.X, b.Y - a.Y);
        float nx = -(b.Y - a.Y) / std::max(1.0f, len), ny = (b.X - a.X) / std::max(1.0f, len);
        for (float d = 0; d < len; d += r.range(3, 9)) {
            float t = d / len;
            float wobble = 2.5f * std::sin(phase + (walked + d) * 0.03f);
            out.emplace_back(a.X + (b.X - a.X) * t + nx * wobble + r.range(-jitter, jitter),
                             a.Y + (b.Y - a.Y) * t + ny * wobble + r.range(-jitter, jitter));
        }
        walked += len;
    }
    out.push_back(path.back());
    return out;
}

Pts regular(Gdiplus::PointF c, float radius, int n, float startDeg) {
    Pts v;
    for (int i = 0; i < n; i++) {
        float a = float((startDeg + 360.0 * i / n) * th::kPi / 180.0);
        v.emplace_back(c.X + radius * std::cos(a), c.Y + radius * std::sin(a));
    }
    return v;
}

Pts curve(Gdiplus::PointF c, float rx, float ry, Rng &r) {
    Pts out;
    float start = r.range(0, 6.28f);
    int n = 70;
    for (int i = 0; i <= n + 7; i++) {
        float a = start + float(2 * th::kPi) * float(i) / float(n);
        float wob = 1 + 0.025f * std::sin(a * 3 + start);
        out.emplace_back(c.X + rx * wob * std::cos(a) + r.range(-1.5f, 1.5f),
                         c.Y + ry * wob * std::sin(a) + r.range(-1.5f, 1.5f));
    }
    return out;
}

struct Sample {
    const char *name;
    th_shape_kind expect;
};

const Sample kSamples[] = {
    {"line", TH_SHAPE_LINE},         {"arrow", TH_SHAPE_ARROW},       {"circle", TH_SHAPE_CIRCLE},
    {"ellipse", TH_SHAPE_ELLIPSE},   {"triangle", TH_SHAPE_TRIANGLE}, {"square", TH_SHAPE_SQUARE},
    {"rectangle", TH_SHAPE_RECTANGLE}, {"diamond", TH_SHAPE_DIAMOND}, {"pentagon", TH_SHAPE_PENTAGON},
    {"hexagon", TH_SHAPE_HEXAGON},   {"star", TH_SHAPE_STAR},         {"heart", TH_SHAPE_HEART},
    {"scribble", TH_SHAPE_NONE},
};

Pts make_sample(int index, Gdiplus::PointF c, float s, Rng &r) {
    switch (kSamples[index].expect) {
    case TH_SHAPE_LINE:
        return trace({{c.X - s * 1.3f, c.Y + s * 0.05f}, {c.X + s * 1.3f, c.Y - s * 0.07f}}, false, r);
    case TH_SHAPE_ARROW: {
        Gdiplus::PointF tail(c.X - s * 1.2f, c.Y + s * 0.5f), tip(c.X + s * 1.1f, c.Y - s * 0.4f);
        float ang = std::atan2(tip.Y - tail.Y, tip.X - tail.X);
        float head = s * 0.45f;
        Gdiplus::PointF b1(tip.X - head * std::cos(ang - 0.5f), tip.Y - head * std::sin(ang - 0.5f));
        Gdiplus::PointF b2(tip.X - head * std::cos(ang + 0.5f), tip.Y - head * std::sin(ang + 0.5f));
        return trace({tail, tip, b1, tip, b2}, false, r, 1.2f);
    }
    case TH_SHAPE_CIRCLE:
        return curve(c, s, s, r);
    case TH_SHAPE_ELLIPSE:
        return curve(c, s * 1.35f, s * 0.7f, r);
    case TH_SHAPE_TRIANGLE:
        return trace(regular(c, s, 3, -90 + r.range(-4, 4)), true, r);
    case TH_SHAPE_SQUARE:
        return trace(regular(c, s * 1.05f, 4, 45 + r.range(-3, 3)), true, r);
    case TH_SHAPE_RECTANGLE:
        return trace({{c.X - s * 1.4f, c.Y - s * 0.7f},
                      {c.X + s * 1.4f, c.Y - s * 0.72f},
                      {c.X + s * 1.38f, c.Y + s * 0.7f},
                      {c.X - s * 1.42f, c.Y + s * 0.68f}},
                     true, r);
    case TH_SHAPE_DIAMOND:
        return trace({{c.X, c.Y - s}, {c.X + s * 0.75f, c.Y}, {c.X, c.Y + s}, {c.X - s * 0.75f, c.Y}}, true, r);
    case TH_SHAPE_PENTAGON:
        return trace(regular(c, s, 5, -90 + r.range(-4, 4)), true, r);
    case TH_SHAPE_HEXAGON:
        return trace(regular(c, s, 6, r.range(-4, 4)), true, r);
    case TH_SHAPE_STAR: {
        Pts v;
        for (int i = 0; i < 10; i++) {
            float a = float((-90 + 36.0 * i) * th::kPi / 180.0);
            float rad = i % 2 ? s * 0.42f : s;
            v.emplace_back(c.X + rad * std::cos(a), c.Y + rad * std::sin(a));
        }
        return trace(v, true, r, 1.2f);
    }
    case TH_SHAPE_HEART: {
        Pts out;
        float k = s / 16.0f;
        for (int i = 0; i <= 80; i++) {
            float t = float(2 * th::kPi) * float(i) / 80.0f;
            float x = 16 * std::pow(std::sin(t), 3.0f);
            float y = 13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t);
            out.emplace_back(c.X + x * k + r.range(-1.2f, 1.2f), c.Y - y * k + r.range(-1.2f, 1.2f));
        }
        return out;
    }
    default: {
        Pts out;
        Gdiplus::PointF p = c;
        float a = 0;
        for (int i = 0; i < 90; i++) {
            a += r.range(-1.4f, 1.4f);
            p.X = std::clamp(p.X + 6 * std::cos(a), c.X - s * 1.4f, c.X + s * 1.4f);
            p.Y = std::clamp(p.Y + 6 * std::sin(a), c.Y - s, c.Y + s);
            out.push_back(p);
        }
        return out;
    }
    }
}

std::unique_ptr<Gdiplus::FontFamily> family() {
    for (const wchar_t *name : {L"Segoe UI", L"Tahoma", L"Arial"}) {
        auto f = std::make_unique<Gdiplus::FontFamily>(name);
        if (f->IsAvailable()) {
            return f;
        }
    }
    return std::unique_ptr<Gdiplus::FontFamily>(Gdiplus::FontFamily::GenericSansSerif()->Clone());
}

} // namespace

int render(const std::wstring &out) {
    prefs::set_persist(false);
    const int cellW = 284, cellH = 250, cols = 7;
    const int panelW = 200, panelH = 300, panelCols = 10;
    const int W = 2000;
    const int shapesH = cellH * 2 + 20;
    const int panelRows = int((kStyles.size() + panelCols - 1) / panelCols);
    const int H = shapesH + panelRows * panelH;
    std::unique_ptr<Gdiplus::Bitmap> canvas(th::new_pargb(W, H));
    if (!canvas) {
        return 3;
    }
    Gdiplus::Graphics g(canvas.get());
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    g.Clear(Gdiplus::Color(255, 250, 250, 248));
    auto fam = family();
    Gdiplus::Font font(fam.get(), 15, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush text(Gdiplus::Color(255, 60, 60, 66));
    Gdiplus::SolidBrush bad(Gdiplus::Color(255, 200, 40, 30));

    int failures = 0;
    prefs::get().snapShapes = true;
    prefs::get().fadeSeconds = 0;
    for (int i = 0; i < int(std::size(kSamples)); i++) {
        int col = i % cols, row = i / cols;
        Gdiplus::PointF c(float(col * cellW + cellW / 2), float(row * cellH + cellH / 2 - 10));
        Rng r{uint32_t(1234 + i * 77)};
        Pts raw = make_sample(i, c, 62, r);
        Gdiplus::Pen thin(Gdiplus::Color(255, 190, 190, 196), 2);
        g.DrawLines(&thin, raw.data(), INT(raw.size()));
        Ink ink;
        ink.begin(raw[0], 5, ink_for(L"lavender"));
        for (size_t k = 1; k < raw.size(); k++) {
            ink.extend(raw[k]);
        }
        th_shape_kind got = ink.end(th::now());
        ink.draw(g);
        bool ok = got == kSamples[i].expect ||
                  (kSamples[i].expect == TH_SHAPE_CIRCLE && got == TH_SHAPE_ELLIPSE) ||
                  (kSamples[i].expect == TH_SHAPE_SQUARE && got == TH_SHAPE_RECTANGLE);
        failures += !ok;
        std::string label = std::string(kSamples[i].name) + " -> " + th_shape_name(got);
        std::wstring wl(label.begin(), label.end());
        g.DrawString(wl.c_str(), -1, &font, Gdiplus::PointF(float(col * cellW + 16), float(row * cellH + cellH - 34)),
                     ok ? &text : &bad);
        print("shape %-10s -> %-10s %s", kSamples[i].name, th_shape_name(got), ok ? "ok" : "MISMATCH");
    }

    prefs::get().snapShapes = false;
    int loaded = 0;
    for (size_t i = 0; i < kStyles.size(); i++) {
        int col = int(i) % panelCols, row = int(i) / panelCols;
        Gdiplus::GraphicsState saved = g.Save();
        g.TranslateTransform(float(col * panelW), float(shapesH + row * panelH));
        g.SetClip(Gdiplus::Rect(0, 0, panelW, panelH));
        g.Clear(Gdiplus::Color(255, 237, 240, 235));
        Arm arm;
        arm.setSize(panelW, panelH);
        arm.setStyle(kStyles[i].id);
        loaded += arm.style() == kStyles[i].id;
        arm.press();
        arm.held = true;
        Gdiplus::PointF target(float(panelW - 80), 120.0f);
        Ink ink;
        Gdiplus::PointF center(target.X + 40 * 0.5f, target.Y + 8);
        for (int k = 0; k <= 60; k++) {
            float a = float(std::atan2(target.Y - center.Y, target.X - center.X) + 2 * th::kPi * k / 60.0);
            Gdiplus::PointF p(center.X + 40 * 0.5f * std::cos(a), center.Y + 32 * 0.5f * std::sin(a));
            if (k == 0) {
                ink.begin(p, arm.inkWidth(), ink_for(arm.style()));
            } else {
                ink.extend(p);
            }
        }
        ink.extend(target);
        ink.draw(g);
        arm.aim(target.X, target.Y, 1.0);
        arm.draw(g);
        if (!arm.isLaser()) {
            Gdiplus::SolidBrush marker(Gdiplus::Color(255, 242, 204, 26));
            g.FillEllipse(&marker, target.X - 3, target.Y - 3, 6.0f, 6.0f);
        }
        g.ResetClip();
        g.Restore(saved);
    }
    print("hands drawn: %d of %d styles loaded from %ls", loaded, int(kStyles.size()), hands::asset_dir().c_str());
    bool saved = th::save_png(canvas.get(), out);
    print("rendered %ls %dx%d", out.c_str(), W, H);
    if (!saved) {
        return 4;
    }
    return failures + (loaded == int(kStyles.size()) ? 0 : 100);
}

int toolbar(const std::wstring &out) {
    prefs::set_persist(false);
    Toolbar bar;
    if (!bar.create(GetModuleHandleW(nullptr))) {
        return 3;
    }
    bar.select(L"lavender");
    RECT mon{0, 0, 1280, 800}, work{0, 0, 1280, 760};
    bar.place(mon, work, 1.0f);
    bool ok = bar.renderPng(out, false, false);
    std::wstring hint = out.substr(0, out.size() - 4) + L"-hint.png";
    std::wstring tray = out.substr(0, out.size() - 4) + L"-ink.png";
    ok = bar.renderPng(hint, true, false) && ok;
    ok = bar.renderPng(tray, true, true) && ok;
    print("rendered %ls, %ls, %ls", out.c_str(), hint.c_str(), tray.c_str());
    return ok ? 0 : 4;
}

int settings(HINSTANCE instance, const std::wstring &out) {
    (void)instance;
    prefs::set_persist(false);
    SettingsWindow window;
    const wchar_t *names[] = {L"general", L"pointers", L"appearance", L"drawing", L"sound", L"about"};
    std::wstring base = out.size() > 4 ? out.substr(0, out.size() - 4) : out;
    bool ok = true;
    for (int s = 0; s < SettingsWindow::SectionCount; s++) {
        std::wstring path = base + L"-" + names[s] + L".png";
        ok = window.snapshot(path, s, false) && ok;
        print("rendered %ls", path.c_str());
    }
    std::wstring picker = base + L"-pointers-picker.png";
    ok = window.snapshot(picker, SettingsWindow::Pointers, false, true) && ok;
    std::wstring dark = base + L"-drawing-dark.png";
    ok = window.snapshot(dark, SettingsWindow::Drawing, true) && ok;
    print("rendered %ls and %ls", picker.c_str(), dark.c_str());
    return ok ? 0 : 4;
}

namespace {

void pump_for(double seconds) {
    double until = th::now() + seconds;
    MSG msg;
    do {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(4);
    } while (th::now() < until);
}

} // namespace

// Drives the Settings window with window messages (the real mouse never
// moves) and checks that each control changes the setting it shows.
int settings_ui(HINSTANCE instance) {
    prefs::set_persist(false);
    prefs::get() = Prefs();
    prefs::get().pinned = prefs::default_pinned();
    SettingsWindow window;
    window.applyHotkey = [](const Hotkey &, bool) { return true; };
    window.show(instance, SettingsWindow::General);
    HWND hwnd = window.hwnd();
    int failures = 0;
    auto check = [&failures](const char *name, bool ok) {
        print("%s %s", ok ? "PASS" : "FAIL", name);
        failures += !ok;
    };
    auto settle = [hwnd] {
        UpdateWindow(hwnd);
        pump_for(0.05);
        UpdateWindow(hwnd);
    };
    auto at = [&](const std::wstring &label, bool control, POINT *p, RECT *rect = nullptr) {
        settle();
        RECT r;
        if (!window.locate(label, control, &r)) {
            print("  could not find %ls", label.c_str());
            return false;
        }
        p->x = (r.left + r.right) / 2;
        p->y = (r.top + r.bottom) / 2;
        if (rect) {
            *rect = r;
        }
        return true;
    };
    auto press = [&](POINT p, POINT release) {
        SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(p.x, p.y));
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(p.x, p.y));
        for (int i = 1; i <= 6; i++) {
            LONG x = p.x + (release.x - p.x) * i / 6, y = p.y + (release.y - p.y) * i / 6;
            SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x, y));
        }
        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(release.x, release.y));
        settle();
    };
    auto click = [&](const std::wstring &label, bool control = false) {
        POINT p;
        if (!at(label, control, &p)) {
            return false;
        }
        press(p, p);
        return true;
    };
    Prefs &p = prefs::get();

    check("Drawing page opens from the sidebar", click(L"Drawing") && window.section() == SettingsWindow::Drawing);
    bool snap = p.snapShapes;
    check("Snap shapes switch flips the setting", click(L"Snap shapes", true) && p.snapShapes == !snap);
    click(L"Snap shapes", true);
    POINT sw;
    RECT swatches;
    if (at(L"Ink colour", true, &sw, &swatches)) {
        double k = double(swatches.right - swatches.left) / 26.0;
        POINT red{swatches.left + LONG(std::lround((34 + 13) * k)), sw.y};
        press(red, red);
    }
    check("Second swatch picks red ink", p.inkChoice == L"red");

    check("Sound page opens", click(L"Sound") && window.section() == SettingsWindow::Sound);
    check("Pop tile picks the Pop sound", click(L"Pop") && p.tapSound == L"pop" && p.handSound);
    check("Off tile turns tap sound off", click(L"Off") && !p.handSound);
    bool laser = p.laserSound;
    click(L"Laser dot taps too", true);
    check("Laser switch is disabled while sound is off", p.laserSound == laser);

    check("Pointers page opens", click(L"Pointers") && window.section() == SettingsWindow::Pointers);
    POINT from, to;
    if (at(L"lavender", false, &from) && at(L"plush", false, &to)) {
        press(from, to);
    }
    auto &pin = p.pinned;
    check("Dragging a hand reorders the toolbar", pin.size() == 6 && pin[0] == L"glove" && pin[3] == L"lavender");
    RECT slot;
    POINT c;
    if (at(L"glove", false, &c, &slot)) {
        POINT badge{slot.right - 4, slot.top + 3};
        SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(c.x, c.y));
        press(badge, badge);
    }
    check("The x on a hand removes it", pin.size() == 5 &&
                                            std::find(pin.begin(), pin.end(), L"glove") == pin.end());
    check("+ opens the picker and a hand gets added", click(L"+") && click(L"pink") && pin.size() == 6 &&
                                                          pin.back() == L"pink");
    if (at(L"pink", false, &from)) {
        press(from, POINT{from.x, from.y + LONG(150 * th::dpi_scale_window(hwnd))});
    }
    check("Dragging a hand out of the row removes it", pin.size() == 5 &&
                                                           std::find(pin.begin(), pin.end(), L"pink") == pin.end());
    check("Reset puts the default hands back", click(L"Reset") && pin == prefs::default_pinned());

    check("Appearance page opens", click(L"Appearance") && window.section() == SettingsWindow::Appearance);
    RECT slider;
    POINT mid;
    if (at(L"Size", true, &mid, &slider)) {
        POINT left{slider.left + 2, mid.y};
        POINT right{slider.right - 2, mid.y};
        press(mid, right);
        bool max = std::fabs(p.pointerSize - prefs::kMaxSize) < 0.001;
        press(mid, left);
        bool min = std::fabs(p.pointerSize - prefs::kMinSize) < 0.001;
        check("Size slider reaches both ends", max && min);
    } else {
        check("Size slider reaches both ends", false);
    }
    bool shadow = p.showShadow;
    check("Shadow switch flips the setting", click(L"Shadow under the pointer", true) && p.showShadow == !shadow);

    SendMessageW(hwnd, WM_CLOSE, 0, 0);
    print("%s: %d failure(s)", failures ? "SETTINGS UI FAILED" : "SETTINGS UI PASSED", failures);
    return failures;
}

int welcome(const std::wstring &out) {
    prefs::set_persist(false);
    WelcomeWindow card;
    std::wstring dark = out.size() > 4 ? out.substr(0, out.size() - 4) + L"-dark.png" : out + L"-dark";
    bool ok = card.snapshot(out, false) && card.snapshot(dark, true);
    print("rendered %ls and %ls", out.c_str(), dark.c_str());
    return ok ? 0 : 4;
}

namespace {

class Catcher {
public:
    int clicks = 0;
    HWND hwnd = nullptr;

    bool create(HINSTANCE instance, RECT r) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = Proc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"TinyHandCatcher";
        RegisterClassExW(&wc);
        hwnd = CreateWindowExW(WS_EX_TOPMOST, L"TinyHandCatcher", L"TinyHand e2e target", WS_POPUP | WS_VISIBLE,
                               r.left, r.top, r.right - r.left, r.bottom - r.top, nullptr, nullptr, instance, this);
        return hwnd != nullptr;
    }

private:
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_NCCREATE) {
            SetWindowLongPtrW(h, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams));
        }
        auto *self = reinterpret_cast<Catcher *>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (self && (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN)) {
            self->clicks++;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }
};

void pump(double seconds) {
    double until = th::now() + seconds;
    MSG msg;
    do {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(4);
    } while (th::now() < until);
}

void move_to(Gdiplus::PointF p) {
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = std::max(2, GetSystemMetrics(SM_CXVIRTUALSCREEN)), vh = std::max(2, GetSystemMetrics(SM_CYVIRTUALSCREEN));
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = LONG(std::lround((p.X - float(vx)) * 65535.0f / float(vw - 1)));
    in.mi.dy = LONG(std::lround((p.Y - float(vy)) * 65535.0f / float(vh - 1)));
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &in, sizeof(in));
}

void button(bool down) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof(in));
}

void key(WORD vk, bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(in));
}

void chord(std::initializer_list<WORD> keys) {
    for (WORD k : keys) {
        key(k, true);
        pump(0.02);
    }
    for (auto it = std::rbegin(keys); it != std::rend(keys); ++it) {
        key(*it, false);
        pump(0.02);
    }
}

void click(Gdiplus::PointF p) {
    move_to(p);
    pump(0.08);
    button(true);
    pump(0.05);
    button(false);
    pump(0.25);
}

void drag(const Pts &pts) {
    move_to(pts[0]);
    pump(0.06);
    button(true);
    pump(0.03);
    for (const auto &p : pts) {
        move_to(p);
        pump(0.006);
    }
    button(false);
    pump(0.3);
}

Pts offset(Pts pts, float dx, float dy) {
    for (auto &p : pts) {
        p.X += dx;
        p.Y += dy;
    }
    return pts;
}

} // namespace

int e2e(HINSTANCE instance) {
    prefs::set_persist(false);
    Prefs &p = prefs::get();
    p.hintShown = true;
    p.welcomeShown = true;
    p.handSound = false;
    p.snapShapes = true;
    p.fadeSeconds = 0;
    p.interactKey = InteractKey::Alt;
    p.pointerStyle = L"lavender";

    int failures = 0;
    auto check = [&failures](const char *name, bool ok, const char *detail = "") {
        print("%s %-34s %s", ok ? "PASS" : "FAIL", name, detail);
        failures += !ok;
    };

    POINT saved;
    GetCursorPos(&saved);
    App app;
    if (!app.init(instance, true)) {
        print("FAIL app init");
        return 2;
    }
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    RECT m = mi.rcMonitor;
    Gdiplus::PointF c(float(m.left + m.right) / 2, float(m.top + m.bottom) / 2 - 40);
    RECT target{LONG(c.X) - 300, LONG(c.Y) - 200, LONG(c.X) + 300, LONG(c.Y) + 200};
    Catcher catcher;
    catcher.create(instance, target);
    pump(0.3);

    click(c);
    check("click reaches app while pointer off", catcher.clicks == 1);

    Overlay &o = app.overlay();
    chord({VK_CONTROL, VK_MENU, 'H'});
    pump(0.3);
    check("Ctrl+Alt+H turns the pointer on", o.active());
    check("mouse hook installed", o.hookInstalled());

    int before = catcher.clicks;
    click(c);
    char detail[128];
    snprintf(detail, sizeof(detail), "app clicks %d -> %d, taps %d", before, catcher.clicks, o.presses());
    check("click becomes a tap, app sees nothing", catcher.clicks == before && o.presses() == 1, detail);

    struct Case {
        int sample;
        float dx, dy;
        th_shape_kind alt;
    } cases[] = {
        {2, -170, -60, TH_SHAPE_ELLIPSE},   {6, 150, -60, TH_SHAPE_SQUARE}, {4, -170, 110, TH_SHAPE_NONE},
        {10, 150, 110, TH_SHAPE_NONE},      {12, 0, 20, TH_SHAPE_NONE},
    };
    int strokes = 0;
    for (const Case &k : cases) {
        Rng r{uint32_t(99 + k.sample)};
        Pts pts = offset(make_sample(k.sample, Gdiplus::PointF(0, 0), 55, r), c.X + k.dx, c.Y + k.dy);
        drag(pts);
        strokes++;
        th_shape_kind got = o.lastShape();
        bool ok = (got == kSamples[k.sample].expect || (k.alt != TH_SHAPE_NONE && got == k.alt)) &&
                  o.strokeCount() == strokes;
        snprintf(detail, sizeof(detail), "got %s, strokes %d", th_shape_name(got), o.strokeCount());
        std::string name = std::string("draw ") + kSamples[k.sample].name;
        check(name.c_str(), ok, detail);
    }

    Pts line;
    for (int i = 0; i <= 40; i++) {
        line.emplace_back(c.X - 220 + 11.0f * float(i), c.Y + 175 + (i % 3 == 0 ? 1.0f : 0.0f));
    }
    drag(line);
    strokes++;
    check("draw line", o.lastShape() == TH_SHAPE_LINE && o.strokeCount() == strokes);
    Gdiplus::PointF tip = line.back();
    Pts head;
    for (int i = 0; i <= 8; i++) {
        head.emplace_back(tip.X - 28 + 3.5f * float(i), tip.Y - 22 + 2.75f * float(i));
    }
    for (int i = 1; i <= 8; i++) {
        head.emplace_back(tip.X - 3.5f * float(i), tip.Y + 2.75f * float(i));
    }
    drag(head);
    snprintf(detail, sizeof(detail), "got %s, strokes %d", th_shape_name(o.lastShape()), o.strokeCount());
    check("V at the end turns the line into an arrow", o.lastShape() == TH_SHAPE_ARROW && o.strokeCount() == strokes,
          detail);

    before = catcher.clicks;
    key(VK_MENU, true);
    pump(0.05);
    click(c);
    key(VK_MENU, false);
    pump(0.1);
    snprintf(detail, sizeof(detail), "app clicks %d -> %d", before, catcher.clicks);
    check("Alt+click goes through for real", catcher.clicks == before + 1, detail);

    chord({VK_CONTROL, VK_MENU, 'X'});
    pump(0.2);
    check("Ctrl+Alt+X clears the drawing", o.strokeCount() == 0);

    chord({VK_ESCAPE});
    pump(0.2);
    check("Esc turns the pointer off", !o.active());

    before = catcher.clicks;
    click(c);
    check("clicks work again after Esc", catcher.clicks == before + 1);

    DestroyWindow(catcher.hwnd);
    app.shutdown();
    SetCursorPos(saved.x, saved.y);
    print("%s: %d failure(s)", failures ? "E2E FAILED" : "E2E PASSED", failures);
    return failures;
}

} // namespace selftest
