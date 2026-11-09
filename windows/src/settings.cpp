#include "settings.h"

#include "hands.h"
#include "sound.h"
#include "version.h"

#include <cwctype>
#include <shellapi.h>

namespace {

const wchar_t *kClass = L"TinyHandSettings";

constexpr float kWidth = 780;
constexpr float kHeight = 560;
constexpr float kSidebar = 210;
constexpr float kPad = 28;
constexpr float kX0 = kSidebar + kPad;

enum Glyph { Gear, Hand, Sparkles, Pencil, Speaker, Info, Wave, Mute, Plus, Arrow, Chevrons, Cross, Minus };

struct SectionInfo {
    const wchar_t *title;
    uint32_t tint;
    int glyph;
};

const SectionInfo kSections[] = {
    {L"General", 0x8C8C8C, Gear},     {L"Pointers", 0x8C6BF2, Hand}, {L"Appearance", 0xF27399, Sparkles},
    {L"Drawing", 0xF59433, Pencil},   {L"Sound", 0x338CFA, Speaker}, {L"About", 0x737373, Info},
};

const std::pair<const wchar_t *, double> kFadeChoices[] = {
    {L"After 3 seconds", 3}, {L"After 6 seconds", 6}, {L"After 10 seconds", 10}, {L"Keep until cleared", 0}};

const std::pair<const wchar_t *, InteractKey> kInteractChoices[] = {
    {L"Alt", InteractKey::Alt}, {L"Ctrl", InteractKey::Ctrl}, {L"Shift", InteractKey::Shift}, {L"Off", InteractKey::Off}};

const wchar_t *kKeepOne = L"Keep at least one pointer in the toolbar.";

uint32_t mix(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int shift) {
        float x = float((a >> shift) & 0xFF) * (1 - t) + float((b >> shift) & 0xFF) * t;
        return uint32_t(std::lround(x)) << shift;
    };
    return ch(16) | ch(8) | ch(0);
}

void round_fill(Gdiplus::Graphics &g, const Gdiplus::RectF &r, float radius, Gdiplus::Color c) {
    Gdiplus::GraphicsPath p;
    th::add_rounded_rect(p, r, radius);
    Gdiplus::SolidBrush b(c);
    g.FillPath(&b, &p);
}

void round_stroke(Gdiplus::Graphics &g, const Gdiplus::RectF &r, float radius, Gdiplus::Color c, float width) {
    Gdiplus::GraphicsPath p;
    Gdiplus::RectF inner(r.X + width / 2, r.Y + width / 2, r.Width - width, r.Height - width);
    th::add_rounded_rect(p, inner, radius);
    Gdiplus::Pen pen(c, width);
    g.DrawPath(&pen, &p);
}

void soft_shadow(Gdiplus::Graphics &g, const Gdiplus::RectF &r, float radius, float strength) {
    for (int i = 3; i >= 1; i--) {
        float grow = float(i) * 1.2f;
        Gdiplus::RectF s(r.X - grow, r.Y - grow + 1.2f, r.Width + 2 * grow, r.Height + 2 * grow);
        round_fill(g, s, radius + grow, th::black(strength / float(i + 1)));
    }
}

Gdiplus::Color with_alpha(Gdiplus::Color c, float k) {
    return Gdiplus::Color(BYTE(std::lround(c.GetA() * std::clamp(k, 0.0f, 1.0f))), c.GetR(), c.GetG(), c.GetB());
}

void draw_tinted(Gdiplus::Graphics &g, Gdiplus::Bitmap *image, const Gdiplus::RectF &r, Gdiplus::Color c) {
    if (!image) {
        return;
    }
    float a = c.GetA() / 255.0f;
    Gdiplus::ColorMatrix m = {{{0, 0, 0, 0, 0},
                               {0, 0, 0, 0, 0},
                               {0, 0, 0, 0, 0},
                               {0, 0, 0, a, 0},
                               {c.GetR() / 255.0f, c.GetG() / 255.0f, c.GetB() / 255.0f, 0, 1}}};
    Gdiplus::ImageAttributes attrs;
    attrs.SetColorMatrix(&m);
    g.DrawImage(image, r, 0, 0, Gdiplus::REAL(image->GetWidth()), Gdiplus::REAL(image->GetHeight()), Gdiplus::UnitPixel, &attrs);
}

void draw_faded(Gdiplus::Graphics &g, Gdiplus::Bitmap *image, const Gdiplus::RectF &r, float alpha) {
    if (!image) {
        return;
    }
    if (alpha >= 0.999f) {
        g.DrawImage(image, r);
        return;
    }
    Gdiplus::ColorMatrix m = {{{1, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, alpha, 0}, {0, 0, 0, 0, 1}}};
    Gdiplus::ImageAttributes attrs;
    attrs.SetColorMatrix(&m);
    g.DrawImage(image, r, 0, 0, Gdiplus::REAL(image->GetWidth()), Gdiplus::REAL(image->GetHeight()), Gdiplus::UnitPixel, &attrs);
}

void laser_icon(Gdiplus::Graphics &g, const Gdiplus::RectF &r, float alpha) {
    float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2, k = r.Width / 64.0f;
    float glow = 18 * k;
    Gdiplus::GraphicsPath halo;
    halo.AddEllipse(cx - glow, cy - glow, 2 * glow, 2 * glow);
    Gdiplus::PathGradientBrush brush(&halo);
    brush.SetCenterColor(th::rgba(kInkRed, 0.55f * alpha));
    Gdiplus::Color edge = th::rgba(kInkRed, 0);
    INT n = 1;
    brush.SetSurroundColors(&edge, &n);
    g.FillPath(&brush, &halo);
    Gdiplus::SolidBrush red(th::rgba(kInkRed, alpha));
    g.FillEllipse(&red, cx - 7 * k, cy - 7 * k, 14 * k, 14 * k);
    Gdiplus::SolidBrush core(Gdiplus::Color(BYTE(242 * alpha), 255, 219, 209));
    g.FillEllipse(&core, cx - 3 * k, cy - 3 * k, 6 * k, 6 * k);
}

void pointer_icon(Gdiplus::Graphics &g, const std::wstring &style, const Gdiplus::RectF &r, float alpha = 1) {
    if (style_is_laser(style)) {
        laser_icon(g, r, alpha);
    } else {
        draw_faded(g, hands::cached_thumbnail(style), r, alpha);
    }
}

std::vector<std::wstring> keycaps(const Hotkey &h) {
    std::vector<std::wstring> caps;
    if (h.mods & MOD_CONTROL) {
        caps.push_back(L"Ctrl");
    }
    if (h.mods & MOD_ALT) {
        caps.push_back(L"Alt");
    }
    if (h.mods & MOD_SHIFT) {
        caps.push_back(L"Shift");
    }
    if (h.mods & MOD_WIN) {
        caps.push_back(L"Win");
    }
    Hotkey keyOnly{0, h.vk};
    caps.push_back(keyOnly.display());
    return caps;
}

} // namespace

Gdiplus::Color SettingsWindow::Palette::ink(float alpha) const { return dark ? th::white(alpha) : th::black(alpha); }

// ---------------------------------------------------------------------------
// Window plumbing

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        reinterpret_cast<SettingsWindow *>(cs->lpCreateParams)->hwnd_ = hwnd;
    }
    auto *self = reinterpret_cast<SettingsWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->proc(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

void SettingsWindow::create() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP));
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    POINT center{GetSystemMetrics(SM_CXSCREEN) / 2, GetSystemMetrics(SM_CYSCREEN) / 2};
    scale_ = th::dpi_scale_at(center);
    CreateWindowExW(0, kClass, L"TinyHand Settings", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                    CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr, nullptr, instance_, this);
    scale_ = th::dpi_scale_window(hwnd_);
    resize();
}

void SettingsWindow::resize() {
    RECT r{0, 0, LONG(std::lround(kWidth * scale_)), LONG(std::lround(kHeight * scale_))};
    AdjustWindowRectEx(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);
    SetWindowPos(hwnd_, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void SettingsWindow::show(HINSTANCE instance, int section) {
    instance_ = instance;
    if (!listening_) {
        listening_ = true;
        prefs::listen([this] { redraw(); });
    }
    if (!hwnd_) {
        create();
    }
    if (section >= 0 && section < SectionCount) {
        section_ = section;
    }
    shortcutError_.clear();
    pinMessage_.clear();
    picking_ = false;
    bool dark = th::apps_use_dark();
    BOOL useDark = dark;
    using SetAttr = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
        if (auto fn = reinterpret_cast<SetAttr>(reinterpret_cast<void *>(GetProcAddress(dwm, "DwmSetWindowAttribute")))) {
            fn(hwnd_, 20, &useDark, sizeof(useDark));
        }
    }
    std::vector<std::wstring> all;
    for (const auto &s : kStyles) {
        all.push_back(s.id);
    }
    hands::preload_thumbnails(all, hwnd_, WM_TH_THUMBS);
    if (!IsWindowVisible(hwnd_)) {
        RECT r;
        GetWindowRect(hwnd_, &r);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY), &mi);
        int w = r.right - r.left, h = r.bottom - r.top;
        SetWindowPos(hwnd_, nullptr, (mi.rcWork.left + mi.rcWork.right - w) / 2,
                     (mi.rcWork.top + mi.rcWork.bottom - h) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    redraw();
}

void SettingsWindow::redraw() {
    if (hwnd_ && IsWindowVisible(hwnd_)) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

LRESULT SettingsWindow::proc(UINT msg, WPARAM wp, LPARAM lp) {
    auto px = [this](LPARAM l) { return Gdiplus::PointF(GET_X_LPARAM(l) / scale_, GET_Y_LPARAM(l) / scale_); };
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd_, &ps);
        RECT rc;
        GetClientRect(hwnd_, &rc);
        if (surface_.resize(rc.right, rc.bottom)) {
            {
                Gdiplus::Graphics g(surface_.bitmap.get());
                g.ScaleTransform(scale_, scale_);
                paint(g, float(rc.right) / scale_, float(rc.bottom) / scale_);
            }
            GdiFlush();
            BitBlt(dc, 0, 0, rc.right, rc.bottom, surface_.dc, 0, 0, SRCCOPY);
        }
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_TH_THUMBS:
        redraw();
        return 0;
    case WM_LBUTTONDOWN: {
        SetCapture(hwnd_);
        SetFocus(hwnd_);
        auto p = px(lp);
        mouseDown(p.X, p.Y);
        return 0;
    }
    case WM_MOUSEMOVE: {
        auto p = px(lp);
        mouseMove(p.X, p.Y);
        TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_LBUTTONUP: {
        ReleaseCapture();
        auto p = px(lp);
        mouseUp(p.X, p.Y);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (hover_ != -1 && pressed_ < 0) {
            hover_ = -1;
            redraw();
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            bool link = hover_ >= 0 && hover_ < int(widgets_.size()) &&
                        (widgets_[size_t(hover_)].kind == Kind::LinkRow || widgets_[size_t(hover_)].kind == Kind::TextButton);
            SetCursor(LoadCursorW(nullptr, link ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (recording_ >= 0) {
            recordKey(UINT(wp));
            return 0;
        }
        if (wp == VK_ESCAPE) {
            if (picking_) {
                picking_ = false;
                redraw();
            } else {
                ShowWindow(hwnd_, SW_HIDE);
            }
            return 0;
        }
        break;
    case WM_CHAR:
    case WM_SYSCHAR:
        if (recording_ >= 0) {
            return 0;
        }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) {
            stopRecording();
        }
        break;
    case WM_DPICHANGED: {
        scale_ = float(HIWORD(wp)) / 96.0f;
        fontScale_ = 0;
        const RECT *r = reinterpret_cast<const RECT *>(lp);
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        resize();
        redraw();
        return 0;
    }
    case WM_SETTINGCHANGE:
        redraw();
        break;
    case WM_CLOSE:
        stopRecording();
        ShowWindow(hwnd_, SW_HIDE);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Layout helpers

float SettingsWindow::textHeight(Gdiplus::Graphics &g, const std::wstring &s, int font, float width) {
    Gdiplus::RectF bound;
    Gdiplus::StringFormat fmt;
    g.MeasureString(s.c_str(), -1, fonts_[font].get(), Gdiplus::RectF(0, 0, width, 10000), &fmt, &bound);
    return std::ceil(bound.Height);
}

float SettingsWindow::textWidth(Gdiplus::Graphics &g, const std::wstring &s, int font) {
    Gdiplus::RectF bound;
    Gdiplus::StringFormat fmt(Gdiplus::StringFormatFlagsNoWrap);
    g.MeasureString(s.c_str(), -1, fonts_[font].get(), Gdiplus::PointF(0, 0), &fmt, &bound);
    return std::ceil(bound.Width);
}

SettingsWindow::Widget &SettingsWindow::add(Kind kind, Gdiplus::RectF r) {
    widgets_.emplace_back();
    Widget &w = widgets_.back();
    w.kind = kind;
    w.r = r;
    w.enabled = !dim_;
    return w;
}

SettingsWindow::Widget &SettingsWindow::text(Gdiplus::Graphics &g, const std::wstring &s, int font, float x, float y,
                                             float width, bool secondary) {
    float h = textHeight(g, s, font, width);
    Widget &w = add(Kind::Text, Gdiplus::RectF(x, y, width, h + 1));
    w.text = s;
    w.font = font;
    w.secondary = secondary;
    return w;
}

float SettingsWindow::card(Gdiplus::Graphics &g, float y, const wchar_t *title,
                           std::vector<std::function<float(float)>> rows) {
    const float cw = kWidth - kX0 - kPad;
    if (title) {
        text(g, title, Header, kX0 + 4, y, cw - 8, true);
        y += 16 + 8;
    }
    size_t index = widgets_.size();
    add(Kind::Card, Gdiplus::RectF(kX0, y, cw, 0));
    float top = y;
    for (size_t i = 0; i < rows.size(); i++) {
        if (i > 0) {
            add(Kind::Separator, Gdiplus::RectF(kX0 + 14, y, cw - 14, 1));
        }
        y = rows[i](y);
    }
    widgets_[index].r.Height = y - top;
    return y;
}

float SettingsWindow::row(Gdiplus::Graphics &g, float y, const std::wstring &title, const std::wstring &detail,
                          Kind trailing, float trailW, float trailH, std::function<void(Widget &)> setup) {
    const float cw = kWidth - kX0 - kPad;
    float textW = cw - 28 - trailW - 12;
    float th1 = textHeight(g, title, Body, textW);
    float dh = detail.empty() ? 0 : textHeight(g, detail, Detail, textW);
    float block = th1 + (detail.empty() ? 0 : 2 + dh);
    float h = std::max(44.0f, block + 20);
    float by = y + (h - block) / 2;
    text(g, title, Body, kX0 + 14, by, textW);
    if (!detail.empty()) {
        text(g, detail, Detail, kX0 + 14, by + th1 + 2, textW, true);
    }
    if (trailW > 0 && setup) {
        setup(add(trailing, Gdiplus::RectF(kX0 + cw - 14 - trailW, y + (h - trailH) / 2, trailW, trailH)));
    }
    return y + h;
}

// ---------------------------------------------------------------------------
// Pages

void SettingsWindow::build(Gdiplus::Graphics &g, float width, float height) {
    (void)width;
    if (fontScale_ != scale_ || !family_) {
        fontScale_ = scale_;
        for (const wchar_t *name : {L"Segoe UI", L"Tahoma", L"Arial"}) {
            auto f = std::make_unique<Gdiplus::FontFamily>(name);
            if (f->IsAvailable()) {
                family_ = std::move(f);
                break;
            }
        }
        if (!family_) {
            family_.reset(Gdiplus::FontFamily::GenericSansSerif()->Clone());
        }
        auto semi = std::make_unique<Gdiplus::FontFamily>(L"Segoe UI Semibold");
        bool haveSemi = semi->IsAvailable();
        if (haveSemi) {
            semibold_ = std::move(semi);
        }
        auto make = [&](float size, bool strong, bool bold) {
            if (bold) {
                return std::make_unique<Gdiplus::Font>(family_.get(), size, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            }
            if (strong && haveSemi) {
                return std::make_unique<Gdiplus::Font>(semibold_.get(), size, Gdiplus::FontStyleRegular,
                                                       Gdiplus::UnitPixel);
            }
            return std::make_unique<Gdiplus::Font>(family_.get(), size,
                                                   strong ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
                                                   Gdiplus::UnitPixel);
        };
        fonts_[Body] = make(13, false, false);
        fonts_[BodyBold] = make(13, true, false);
        fonts_[Detail] = make(11, false, false);
        fonts_[Header] = make(12, true, false);
        fonts_[Title] = make(22, false, true);
        fonts_[Name] = make(18, false, true);
        fonts_[Caption] = make(11, false, false);
        fonts_[Medium] = make(12, false, false);
        fonts_[Key] = make(11.5f, true, false);
    }

    widgets_.clear();
    dim_ = false;
    float y = 16;
    for (int s = 0; s < SectionCount; s++) {
        Widget &w = add(Kind::Sidebar, Gdiplus::RectF(10, y, kSidebar - 20, 36));
        w.id = s;
        w.text = kSections[s].title;
        w.selected = s == section_;
        w.click = [this, s] {
            section_ = s;
            picking_ = false;
            pinMessage_.clear();
            stopRecording();
            redraw();
        };
        y += 38;
    }
    add(Kind::AppIcon, Gdiplus::RectF(20, height - 16 - 22, 22, 22));
    text(g, L"TinyHand", Header, 50, height - 16 - 22 + 3, kSidebar - 60);

    text(g, kSections[section_].title, Title, kX0, 22, kWidth - kX0 - kPad);
    y = 22 + 30 + 22;
    switch (section_) {
    case General:
        pageGeneral(g, y);
        break;
    case Pointers:
        pagePointers(g, y);
        break;
    case Appearance:
        pageAppearance(g, y);
        break;
    case Drawing:
        pageDrawing(g, y);
        break;
    case Sound:
        pageSound(g, y);
        break;
    default:
        pageAbout(g, y);
        break;
    }
}

void SettingsWindow::pageGeneral(Gdiplus::Graphics &g, float y) {
    Prefs &p = prefs::get();
    auto recorderRow = [&](const wchar_t *title, int which) {
        return [&, title, which](float ry) {
            return row(g, ry, title, L"", Kind::Recorder, 150, 30, [this, which](Widget &w) {
                w.id = which;
                w.selected = recording_ == which;
                w.click = [this, which] { startRecording(which); };
            });
        };
    };
    y = card(g, y, L"Shortcuts", {recorderRow(L"Turn the pointer on or off", 0), recorderRow(L"Clear the drawing", 1)});
    if (!shortcutError_.empty()) {
        y += 8;
        Widget &e = text(g, shortcutError_, Medium, kX0 + 4, y, kWidth - kX0 - kPad - 8);
        e.error = true;
        y = e.r.Y + e.r.Height;
    }
    y += 22;

    auto popup = [&](float ry, const std::wstring &title, const std::wstring &detail, std::vector<std::wstring> options,
                     int current, std::function<void(int)> pick) {
        float w = 0;
        for (const auto &o : options) {
            w = std::max(w, textWidth(g, o, Medium));
        }
        return row(g, ry, title, detail, Kind::Popup, w + 40, 24, [&](Widget &widget) {
            widget.options = options;
            widget.id = current;
            widget.text = options[size_t(std::max(0, current))];
            widget.pick = pick;
        });
    };
    std::vector<std::function<float(float)>> clicking;
    clicking.push_back([&](float ry) {
        std::vector<std::wstring> opts;
        int current = 0;
        for (size_t i = 0; i < std::size(kInteractChoices); i++) {
            opts.push_back(kInteractChoices[i].first);
            if (p.interactKey == kInteractChoices[i].second) {
                current = int(i);
            }
        }
        return popup(ry, L"Hold to use the app", L"Clicks, drags and scrolls reach the app.", opts, current, [](int i) {
            prefs::get().interactKey = kInteractChoices[i].second;
            prefs::changed();
        });
    });
    if (p.interactKey != InteractKey::Off) {
        clicking.push_back([&](float ry) {
            return popup(ry, L"While holding",
                         p.interactKeepsHand ? L"The hand stays, in menus too." : L"The normal cursor comes back, in menus too.",
                         {L"Show the cursor", L"Keep the hand"}, p.interactKeepsHand ? 1 : 0, [](int i) {
                             prefs::get().interactKeepsHand = i == 1;
                             prefs::changed();
                         });
        });
    }
    y = card(g, y, L"Clicking for real", clicking);
    y += 22;
    card(g, y, L"Startup", {[&](float ry) {
             return row(g, ry, L"Open TinyHand when you log in", L"", Kind::Switch, 38, 22, [](Widget &w) {
                 w.on = prefs::launch_at_login();
                 w.click = [] { prefs::set_launch_at_login(!prefs::launch_at_login()); };
             });
         }});
}

void SettingsWindow::pagePointers(Gdiplus::Graphics &g, float y) {
    const float cw = kWidth - kX0 - kPad;
    const std::vector<std::wstring> &pinned = prefs::get().pinned;
    text(g, L"In your toolbar", Header, kX0 + 4, y, 200, true);
    float rw = textWidth(g, L"Reset", Caption) + 2;
    Widget &reset = add(Kind::TextButton, Gdiplus::RectF(kX0 + cw - 4 - rw, y + 1, rw, 15));
    reset.text = L"Reset";
    reset.click = [this] {
        prefs::get().pinned = prefs::default_pinned();
        pinMessage_.clear();
        prefs::changed();
    };
    y += 16 + 8;

    slotRow_ = Gdiplus::RectF(kX0, y, cw, 78);
    add(Kind::SlotRow, slotRow_);
    slotStep_ = (cw - 20 + 6) / float(prefs::kMaxPinned);
    const float slotW = slotStep_ - 6;
    const std::vector<std::wstring> &order = dragging_ ? draft_ : pinned;
    const std::wstring dragged = dragging_ && dragIndex_ >= 0 && dragIndex_ < int(pinned.size()) ? pinned[size_t(dragIndex_)]
                                                                                                  : L"";
    bool full = order.size() >= prefs::kMaxPinned;
    size_t slot = 0;
    for (; slot < order.size(); slot++) {
        if (dragging_ && order[slot] == dragged) {
            continue;
        }
        Widget &s = add(Kind::Slot, Gdiplus::RectF(kX0 + 10 + float(slot) * slotStep_, y + 10, slotW, 58));
        s.id = int(slot);
        s.text = order[slot];
    }
    if (!full) {
        Widget &a = add(Kind::AddSlot, Gdiplus::RectF(kX0 + 10 + float(slot) * slotStep_, y + 10, slotW, 58));
        a.text = L"+";
        a.selected = picking_;
        a.click = [this] {
            picking_ = !picking_;
            redraw();
        };
        slot++;
    }
    for (; slot < prefs::kMaxPinned; slot++) {
        add(Kind::EmptySlot, Gdiplus::RectF(kX0 + 10 + float(slot) * slotStep_, y + 10, slotW, 58));
    }
    y += 78 + 8;
    text(g,
         pinned.size() >= prefs::kMaxPinned ? L"The toolbar is full. Remove a pointer to add another. Drag to reorder."
                                            : L"Press + to add a pointer. Drag to reorder, or out to remove.",
         Caption, kX0 + 4, y, cw - 8, true);
    y += 18;
    if (!pinMessage_.empty()) {
        y += 4;
        text(g, pinMessage_, Medium, kX0 + 4, y, cw - 8).error = true;
    }

    if (dragging_) {
        Widget &f = add(Kind::Slot, Gdiplus::RectF(0, 0, slotW, 58));
        f.id = -1;
        f.text = dragged;
        f.selected = true;
        f.error = dragOut_;
        if (dragOut_) {
            f.r.X = dragX_ - slotW / 2;
            f.r.Y = dragY_ - 29;
        } else {
            f.r.X = std::clamp(dragX_ - slotW / 2, slotRow_.X + 4, slotRow_.X + slotRow_.Width - slotW - 4);
            f.r.Y = slotRow_.Y + 10;
        }
    }

    if (picking_) {
        const int cols = 8;
        const float cell = 56, cellH = 54;
        int count = int(kStyles.size());
        int rows = (count + cols - 1) / cols;
        float pw = cols * cell + 24, ph = float(rows) * cellH + 24;
        Gdiplus::RectF panel(kX0 + cw - pw, slotRow_.Y + slotRow_.Height + 8, pw, ph);
        add(Kind::Picker, panel);
        for (int k = 0; k < count; k++) {
            std::wstring id = kStyles[size_t(k)].id;
            Widget &c = add(Kind::PickerCell, Gdiplus::RectF(panel.X + 12 + float(k % cols) * cell,
                                                             panel.Y + 12 + float(k / cols) * cellH, cell, cellH));
            c.text = id;
            c.selected = std::find(pinned.begin(), pinned.end(), id) != pinned.end();
            c.click = [this, id] {
                auto &pin = prefs::get().pinned;
                if (std::find(pin.begin(), pin.end(), id) != pin.end() || pin.size() >= prefs::kMaxPinned) {
                    return;
                }
                pin.push_back(id);
                picking_ = false;
                pinMessage_.clear();
                prefs::changed();
            };
        }
    }
}

void SettingsWindow::pageAppearance(Gdiplus::Graphics &g, float y) {
    card(g, y, L"Pointer",
         {[&](float ry) {
              return row(g, ry, L"Size", L"", Kind::Slider, 226, 22, [](Widget &w) {
                  double size = prefs::get().pointerSize;
                  w.value = float((size - prefs::kMinSize) / (prefs::kMaxSize - prefs::kMinSize));
                  w.slide = [](float t) {
                      prefs::get().pointerSize = prefs::kMinSize + double(t) * (prefs::kMaxSize - prefs::kMinSize);
                      prefs::changed();
                  };
              });
          },
          [&](float ry) {
              return row(g, ry, L"Shadow under the pointer", L"", Kind::Switch, 38, 22, [](Widget &w) {
                  w.on = prefs::get().showShadow;
                  w.click = [] {
                      prefs::get().showShadow = !prefs::get().showShadow;
                      prefs::changed();
                  };
              });
          }});
}

void SettingsWindow::pageDrawing(Gdiplus::Graphics &g, float y) {
    Prefs &p = prefs::get();
    std::wstring inkDetail = L"Matches each pointer.";
    for (const auto &c : kInkChoices) {
        if (p.inkChoice == c.id && !c.match) {
            std::wstring name = c.title;
            for (auto &ch : name) {
                ch = wchar_t(towlower(ch));
            }
            inkDetail = L"All pointers draw in " + name + L".";
        }
    }
    auto toggle = [&](float ry, const wchar_t *title, const wchar_t *detail, bool *value) {
        return row(g, ry, title, detail, Kind::Switch, 38, 22, [value](Widget &w) {
            w.on = *value;
            w.click = [value] {
                *value = !*value;
                prefs::changed();
            };
        });
    };
    card(g, y, L"Ink",
         {[&](float ry) {
              const float step = 34;
              float total = float(kInkChoices.size()) * 26 + float(kInkChoices.size() - 1) * 8;
              return row(g, ry, L"Ink colour", inkDetail, Kind::Swatch, total, 26, [this, step](Widget &first) {
                  Gdiplus::RectF r(first.r.X, first.r.Y, 26, 26);
                  bool enabled = first.enabled;
                  for (size_t i = 0; i < kInkChoices.size(); i++) {
                      Widget &w = i == 0 ? first : add(Kind::Swatch, Gdiplus::RectF(r.X + float(i) * step, r.Y, 26, 26));
                      w.r.Width = 26;
                      w.enabled = enabled;
                      w.id = int(i);
                      w.selected = prefs::get().inkChoice == kInkChoices[i].id;
                      w.text = kInkChoices[i].title;
                      std::wstring id = kInkChoices[i].id;
                      w.click = [id] {
                          prefs::get().inkChoice = id;
                          prefs::changed();
                      };
                  }
              });
          },
          [&](float ry) {
              std::vector<std::wstring> opts;
              int current = 0;
              for (size_t i = 0; i < std::size(kFadeChoices); i++) {
                  opts.push_back(kFadeChoices[i].first);
                  if (std::fabs(p.fadeSeconds - kFadeChoices[i].second) < 0.01) {
                      current = int(i);
                  }
              }
              float w = 0;
              for (const auto &o : opts) {
                  w = std::max(w, textWidth(g, o, Medium));
              }
              return row(g, ry, L"Lines fade", L"Clear them any time with " + p.clearKey.display() + L".", Kind::Popup,
                         w + 40, 24, [&](Widget &widget) {
                             widget.options = opts;
                             widget.id = current;
                             widget.text = opts[size_t(current)];
                             widget.pick = [](int i) {
                                 prefs::get().fadeSeconds = kFadeChoices[i].second;
                                 prefs::changed();
                             };
                         });
          },
          [&](float ry) {
              return toggle(ry, L"Snap shapes",
                            L"Rough lines, arrows, circles, boxes, triangles, stars and hearts turn into clean shapes.",
                            &p.snapShapes);
          },
          [&](float ry) {
              return toggle(ry, L"Ink button in the toolbar", L"Change the colour while you present.", &p.showInkButton);
          }});
}

void SettingsWindow::pageSound(Gdiplus::Graphics &g, float y) {
    const float cw = kWidth - kX0 - kPad;
    Prefs &p = prefs::get();
    text(g, L"Tap sound", Header, kX0 + 4, y, 200, true);
    y += 16 + 8;
    const float gap = 8, tw = (cw - 3 * gap) / 4, th = 74;
    for (size_t k = 0; k <= kTapSounds.size(); k++) {
        Widget &t = add(Kind::SoundTile, Gdiplus::RectF(kX0 + float(k % 4) * (tw + gap), y + float(k / 4) * (th + gap), tw, th));
        t.id = int(k);
        if (k == 0) {
            t.text = L"Off";
            t.selected = !p.handSound;
            t.click = [] {
                prefs::get().handSound = false;
                prefs::changed();
            };
        } else {
            const TapSoundInfo &s = kTapSounds[k - 1];
            t.text = s.title;
            t.selected = p.handSound && p.tapSound == s.id;
            std::wstring id = s.id;
            t.click = [id] {
                prefs::get().tapSound = id;
                prefs::get().handSound = true;
                TapSound::preview(id);
                prefs::changed();
            };
        }
    }
    y += 2 * th + gap + 8;
    text(g, L"Click a sound to hear it. It plays each time the hand taps.", Caption, kX0 + 4, y, cw - 8, true);
    y += 18 + 22;
    dim_ = !p.handSound;
    card(g, y, nullptr, {[&](float ry) {
             return row(g, ry, L"Laser dot taps too", L"Off by default, for quiet pointing.", Kind::Switch, 38, 22,
                        [](Widget &w) {
                            w.on = prefs::get().laserSound;
                            w.click = [] {
                                if (!prefs::get().handSound) {
                                    return;
                                }
                                prefs::get().laserSound = !prefs::get().laserSound;
                                prefs::changed();
                            };
                        });
         }});
    dim_ = false;
}

void SettingsWindow::pageAbout(Gdiplus::Graphics &g, float y) {
    const float cw = kWidth - kX0 - kPad;
    add(Kind::AppIcon, Gdiplus::RectF(kX0, y, 72, 72));
    text(g, L"TinyHand", Name, kX0 + 88, y + 10, 300);
    text(g, std::wstring(L"Version ") + TH_VERSION, Medium, kX0 + 88, y + 36, 300, true);
    text(g, L"A tiny hand for your PC. Free and open source.", Medium, kX0 + 88, y + 53, cw - 88, true);
    y += 72 + 22;
    struct Link {
        const wchar_t *title, *detail, *url;
    };
    const Link links[] = {
        {L"Website", L"tinyhand.app", L"https://tinyhand.app"},
        {L"Source code", L"github.com/nilbuild/tinyhand", L"https://github.com/nilbuild/tinyhand"},
        {L"Made by", L"@nilbuild on X", L"https://x.com/nilbuild"},
        {L"License", L"MIT", L"https://github.com/nilbuild/tinyhand/blob/main/LICENSE"},
    };
    std::vector<std::function<float(float)>> rows;
    for (const Link &l : links) {
        rows.push_back([this, l, cw](float ry) {
            Widget &w = add(Kind::LinkRow, Gdiplus::RectF(kX0, ry, cw, 44));
            w.text = l.title;
            w.detail = l.detail;
            std::wstring url = l.url;
            w.click = [url] { ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL); };
            return ry + 44;
        });
    }
    y = card(g, y, nullptr, rows);
    y += 22;
    std::wstring label = L"Reset all settings…";
    float rw = textWidth(g, label, Caption) + 2;
    Widget &reset = add(Kind::TextButton, Gdiplus::RectF(kX0 + cw - 4 - rw, y, rw, 15));
    reset.text = label;
    reset.click = [this] {
        if (MessageBoxW(hwnd_, L"Reset all TinyHand settings to how they started?", L"TinyHand",
                        MB_YESNO | MB_ICONQUESTION) != IDYES) {
            return;
        }
        prefs::reset();
        if (applyHotkey) {
            applyHotkey(prefs::get().toggleKey, true);
            applyHotkey(prefs::get().clearKey, false);
        }
        redraw();
    };
}

// ---------------------------------------------------------------------------
// Drawing

void SettingsWindow::paint(Gdiplus::Graphics &g, float width, float height) {
    bool dark = forceTheme_ >= 0 ? forceTheme_ == 1 : th::apps_use_dark();
    pal_ = dark ? Palette{0x2B2B2D, 0x232325, 0xF2F2F7, 0x98989F, 0x0A84FF, 0x48484A, 0x343436, true}
                           : Palette{0xECECEC, 0xE2E2E2, 0x1D1D1F, 0x6E6E73, 0x0A7AFF, 0xFFFFFF, 0xF7F7F7, false};
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    build(g, width, height);
    g.Clear(th::rgba(pal_.window));
    Gdiplus::SolidBrush side(th::rgba(pal_.sidebar));
    g.FillRectangle(&side, 0.0f, 0.0f, kSidebar, height);
    for (size_t i = 0; i < widgets_.size(); i++) {
        draw(g, widgets_[i], int(i));
    }
}

void SettingsWindow::drawGlyph(Gdiplus::Graphics &g, int glyph, const Gdiplus::RectF &r, Gdiplus::Color c) {
    const float w = r.Width, h = r.Height, x = r.X, y = r.Y;
    auto P = [&](float u, float v) { return Gdiplus::PointF(x + u * w, y + v * h); };
    Gdiplus::Pen pen(c, std::max(1.2f, w * 0.1f));
    pen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::SolidBrush brush(c);
    switch (glyph) {
    case Gear: {
        Gdiplus::Pen ring(c, w * 0.13f);
        g.DrawEllipse(&ring, x + w * 0.31f, y + h * 0.31f, w * 0.38f, h * 0.38f);
        Gdiplus::Pen tooth(c, w * 0.14f);
        for (int i = 0; i < 8; i++) {
            float a = float(i) * float(th::kPi) / 4;
            g.DrawLine(&tooth, x + w * (0.5f + 0.24f * std::cos(a)), y + h * (0.5f + 0.24f * std::sin(a)),
                       x + w * (0.5f + 0.37f * std::cos(a)), y + h * (0.5f + 0.37f * std::sin(a)));
        }
        break;
    }
    case Hand:
        draw_tinted(g, hands::cached_thumbnail(L"glove"), Gdiplus::RectF(x + w * 0.08f, y + h * 0.08f, w * 0.84f, h * 0.84f), c);
        break;
    case Sparkles: {
        auto star = [&](float cx, float cy, float rad) {
            Gdiplus::PointF pts[8];
            for (int i = 0; i < 8; i++) {
                float a = float(i) * float(th::kPi) / 4 - float(th::kPi) / 2;
                float rr = i % 2 ? rad * 0.3f : rad;
                pts[i] = Gdiplus::PointF(x + w * cx + rr * w * std::cos(a), y + h * cy + rr * h * std::sin(a));
            }
            g.FillPolygon(&brush, pts, 8);
        };
        star(0.56f, 0.56f, 0.34f);
        star(0.27f, 0.27f, 0.15f);
        break;
    }
    case Pencil: {
        Gdiplus::PointF nib[] = {P(0.3f, 0.82f), P(0.5f, 0.18f), P(0.7f, 0.82f)};
        Gdiplus::Pen thick(c, w * 0.12f);
        thick.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
        thick.SetLineJoin(Gdiplus::LineJoinRound);
        g.DrawLines(&thick, nib, 3);
        g.FillEllipse(&brush, x + w * 0.45f, y + h * 0.44f, w * 0.1f, h * 0.1f);
        break;
    }
    case Speaker:
    case Mute: {
        Gdiplus::PointF body[] = {P(0.12f, 0.38f), P(0.3f, 0.38f), P(0.52f, 0.18f), P(0.52f, 0.82f), P(0.3f, 0.62f),
                                  P(0.12f, 0.62f)};
        g.FillPolygon(&brush, body, 6);
        if (glyph == Speaker) {
            g.DrawArc(&pen, x + w * 0.42f, y + h * 0.32f, w * 0.36f, h * 0.36f, -55, 110);
            g.DrawArc(&pen, x + w * 0.34f, y + h * 0.16f, w * 0.6f, h * 0.68f, -55, 110);
        } else {
            g.DrawLine(&pen, P(0.62f, 0.38f), P(0.88f, 0.64f));
            g.DrawLine(&pen, P(0.62f, 0.64f), P(0.88f, 0.38f));
        }
        break;
    }
    case Info: {
        Gdiplus::Pen ring(c, w * 0.09f);
        g.DrawEllipse(&ring, x + w * 0.16f, y + h * 0.16f, w * 0.68f, h * 0.68f);
        g.FillEllipse(&brush, x + w * 0.45f, y + h * 0.29f, w * 0.1f, h * 0.1f);
        Gdiplus::Pen bar(c, w * 0.11f);
        bar.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
        g.DrawLine(&bar, P(0.5f, 0.47f), P(0.5f, 0.7f));
        break;
    }
    case Wave: {
        const float heights[] = {0.3f, 0.55f, 0.85f, 0.45f, 0.85f, 0.55f, 0.3f};
        Gdiplus::Pen bar(c, w * 0.07f);
        bar.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
        for (int i = 0; i < 7; i++) {
            float u = 0.11f + 0.13f * float(i);
            g.DrawLine(&bar, P(u, 0.5f - heights[i] / 2), P(u, 0.5f + heights[i] / 2));
        }
        break;
    }
    case Plus:
        g.DrawLine(&pen, P(0.5f, 0.15f), P(0.5f, 0.85f));
        g.DrawLine(&pen, P(0.15f, 0.5f), P(0.85f, 0.5f));
        break;
    case Arrow:
        g.DrawLine(&pen, P(0.22f, 0.78f), P(0.78f, 0.22f));
        g.DrawLine(&pen, P(0.36f, 0.22f), P(0.78f, 0.22f));
        g.DrawLine(&pen, P(0.78f, 0.22f), P(0.78f, 0.64f));
        break;
    case Chevrons: {
        Gdiplus::PointF up[] = {P(0.2f, 0.4f), P(0.5f, 0.15f), P(0.8f, 0.4f)};
        Gdiplus::PointF down[] = {P(0.2f, 0.6f), P(0.5f, 0.85f), P(0.8f, 0.6f)};
        g.DrawLines(&pen, up, 3);
        g.DrawLines(&pen, down, 3);
        break;
    }
    case Cross:
        g.DrawLine(&pen, P(0.25f, 0.25f), P(0.75f, 0.75f));
        g.DrawLine(&pen, P(0.25f, 0.75f), P(0.75f, 0.25f));
        break;
    case Minus:
        g.DrawLine(&pen, P(0.25f, 0.5f), P(0.75f, 0.5f));
        break;
    default:
        break;
    }
}

void SettingsWindow::drawShortcut(Gdiplus::Graphics &g, const Widget &w) {
    const Hotkey &h = w.id == 0 ? prefs::get().toggleKey : prefs::get().clearKey;
    bool hovered = &w == (hover_ >= 0 ? &widgets_[size_t(hover_)] : nullptr);
    round_fill(g, w.r, 7, pal_.ink(w.selected ? 0.1f : hovered ? 0.08f : 0.05f));
    if (w.selected) {
        round_stroke(g, w.r, 7, th::rgba(pal_.accent, 0.8f), 1.5f);
        Gdiplus::StringFormat fmt;
        fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
        fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::SolidBrush b(th::rgba(pal_.secondary));
        g.DrawString(L"Press keys", -1, fonts_[Medium].get(), w.r, &fmt, &b);
        return;
    }
    std::vector<std::wstring> caps = keycaps(h);
    std::vector<float> widths;
    float total = 0;
    for (const auto &c : caps) {
        widths.push_back(std::max(22.0f, textWidth(g, c, Key) + 12));
        total += widths.back();
    }
    total += 4 * float(caps.size() - 1);
    float x = w.r.X + (w.r.Width - total) / 2;
    float y = w.r.Y + (w.r.Height - 22) / 2;
    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::SolidBrush textBrush(th::rgba(pal_.text, 0.85f));
    for (size_t i = 0; i < caps.size(); i++) {
        Gdiplus::RectF cap(x, y, widths[i], 22);
        round_fill(g, Gdiplus::RectF(cap.X, cap.Y + 1, cap.Width, cap.Height), 5, pal_.ink(0.12f));
        round_fill(g, cap, 5, th::rgba(pal_.dark ? 0x5A5A5E : 0xFBFBFB));
        round_stroke(g, cap, 5, pal_.ink(0.16f), 1);
        g.DrawString(caps[i].c_str(), -1, fonts_[Key].get(), cap, &fmt, &textBrush);
        x += widths[i] + 4;
    }
}

void SettingsWindow::draw(Gdiplus::Graphics &g, const Widget &w, int index) {
    const bool hovered = index == hover_;
    const float alpha = w.enabled ? 1.0f : 0.5f;
    Gdiplus::StringFormat center;
    center.SetAlignment(Gdiplus::StringAlignmentCenter);
    center.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::StringFormat leftMid;
    leftMid.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    leftMid.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    Gdiplus::SolidBrush textBrush(th::rgba(pal_.text, alpha));
    Gdiplus::SolidBrush secondaryBrush(th::rgba(pal_.secondary, alpha));

    switch (w.kind) {
    case Kind::Sidebar: {
        if (w.selected || hovered) {
            round_fill(g, w.r, 8, pal_.ink(w.selected ? 0.1f : 0.05f));
        }
        Gdiplus::RectF tile(w.r.X + 8, w.r.Y + 6, 24, 24);
        uint32_t tint = kSections[w.id].tint;
        Gdiplus::LinearGradientBrush grad(tile, th::rgba(mix(tint, 0xFFFFFF, 0.15f)), th::rgba(tint),
                                          Gdiplus::LinearGradientModeVertical);
        Gdiplus::GraphicsPath p;
        th::add_rounded_rect(p, tile, 6);
        g.FillPath(&grad, &p);
        drawGlyph(g, kSections[w.id].glyph, Gdiplus::RectF(tile.X + 4, tile.Y + 4, 16, 16), th::white(1));
        g.DrawString(w.text.c_str(), -1, fonts_[w.selected ? BodyBold : Body].get(),
                     Gdiplus::RectF(tile.X + 34, w.r.Y, w.r.Width - 50, w.r.Height), &leftMid, &textBrush);
        return;
    }
    case Kind::Text: {
        Gdiplus::StringFormat fmt;
        if (w.right) {
            fmt.SetAlignment(Gdiplus::StringAlignmentFar);
        }
        Gdiplus::SolidBrush red(th::rgba(0xE5484D, alpha));
        g.DrawString(w.text.c_str(), -1, fonts_[w.font].get(), Gdiplus::RectF(w.r.X, w.r.Y, w.r.Width, w.r.Height + 4),
                     &fmt, w.error ? &red : w.secondary ? &secondaryBrush : &textBrush);
        return;
    }
    case Kind::Card:
        round_fill(g, w.r, 12, pal_.ink(0.045f));
        return;
    case Kind::Separator: {
        Gdiplus::SolidBrush b(pal_.ink(0.07f));
        g.FillRectangle(&b, w.r);
        return;
    }
    case Kind::Switch: {
        Gdiplus::RectF r = w.r;
        round_fill(g, r, r.Height / 2,
                   with_alpha(w.on ? th::rgba(pal_.accent) : pal_.ink(pal_.dark ? 0.2f : 0.13f), alpha));
        float d = r.Height - 4;
        float kx = w.on ? r.X + r.Width - 2 - d : r.X + 2;
        Gdiplus::SolidBrush shade(th::black(0.12f * alpha));
        g.FillEllipse(&shade, kx - 0.5f, r.Y + 2.5f, d + 1, d + 1);
        Gdiplus::SolidBrush knob(with_alpha(th::white(1), alpha));
        g.FillEllipse(&knob, kx, r.Y + 2, d, d);
        return;
    }
    case Kind::Popup: {
        soft_shadow(g, w.r, 6, 0.05f);
        round_fill(g, w.r, 6, th::rgba(hovered ? mix(pal_.control, pal_.dark ? 0xFFFFFF : 0x000000, 0.04f) : pal_.control));
        round_stroke(g, w.r, 6, pal_.ink(0.1f), 1);
        g.DrawString(w.text.c_str(), -1, fonts_[Medium].get(),
                     Gdiplus::RectF(w.r.X + 10, w.r.Y, w.r.Width - 30, w.r.Height), &leftMid, &textBrush);
        drawGlyph(g, Chevrons, Gdiplus::RectF(w.r.X + w.r.Width - 19, w.r.Y + 6, 10, 12), th::rgba(pal_.text, 0.7f));
        return;
    }
    case Kind::Slider: {
        Gdiplus::RectF small(w.r.X, w.r.Y + 6, 10, 10), big(w.r.X + w.r.Width - 16, w.r.Y + 3, 16, 16);
        draw_tinted(g, hands::cached_thumbnail(L"glove"), small, th::rgba(pal_.secondary));
        draw_tinted(g, hands::cached_thumbnail(L"glove"), big, th::rgba(pal_.secondary));
        float tx = w.r.X + 20, tw = w.r.Width - 46, cy = w.r.Y + w.r.Height / 2;
        round_fill(g, Gdiplus::RectF(tx, cy - 2, tw, 4), 2, pal_.ink(0.15f));
        float kx = tx + tw * std::clamp(w.value, 0.0f, 1.0f);
        round_fill(g, Gdiplus::RectF(tx, cy - 2, kx - tx, 4), 2, th::rgba(pal_.accent));
        Gdiplus::SolidBrush shade(th::black(0.16f));
        g.FillEllipse(&shade, kx - 10.5f, cy - 9.5f, 21.0f, 21.0f);
        Gdiplus::SolidBrush knob(th::rgba(pal_.dark ? 0xE5E5EA : 0xFFFFFF));
        g.FillEllipse(&knob, kx - 10, cy - 10, 20.0f, 20.0f);
        return;
    }
    case Kind::Recorder:
        drawShortcut(g, w);
        return;
    case Kind::Swatch: {
        const InkChoiceInfo &c = kInkChoices[size_t(w.id)];
        Gdiplus::RectF dot(w.r.X + 3, w.r.Y + 3, 20, 20);
        if (hovered && !w.selected) {
            dot = Gdiplus::RectF(w.r.X + 2.2f, w.r.Y + 2.2f, 21.6f, 21.6f);
        }
        if (c.match) {
            for (int i = 0; i < 24; i++) {
                float hue = float(i) / 24.0f;
                auto channel = [hue](float offset) {
                    float v = std::fabs(std::fmod(hue * 6 + offset, 6.0f) - 3) - 1;
                    return std::clamp(v, 0.0f, 1.0f);
                };
                uint32_t rgb = (uint32_t(channel(0) * 255) << 16) | (uint32_t(channel(4) * 255) << 8) |
                               uint32_t(channel(2) * 255);
                Gdiplus::SolidBrush slice(th::rgba(rgb, alpha));
                g.FillPie(&slice, dot, -90.0f + 15.0f * float(i), 15.5f);
            }
        } else {
            Gdiplus::SolidBrush fill(th::rgba(c.color, alpha));
            g.FillEllipse(&fill, dot);
            Gdiplus::Pen edge(pal_.ink(c.color == 0x1D1D21 || c.color == 0xFFFFFF ? 0.3f : 0.15f), 1);
            g.DrawEllipse(&edge, dot.X + 0.5f, dot.Y + 0.5f, dot.Width - 1, dot.Height - 1);
        }
        if (w.selected) {
            Gdiplus::Pen ring(th::rgba(pal_.accent), 2);
            g.DrawEllipse(&ring, w.r.X + 1, w.r.Y + 1, w.r.Width - 2, w.r.Height - 2);
        }
        return;
    }
    case Kind::SoundTile: {
        round_fill(g, w.r, 12, w.selected ? th::rgba(pal_.accent, 0.12f) : pal_.ink(hovered ? 0.07f : 0.035f));
        if (w.selected) {
            round_stroke(g, w.r, 12, th::rgba(pal_.accent, 0.7f), 1.5f);
        }
        Gdiplus::Color glyph = w.selected ? th::rgba(pal_.accent) : th::rgba(pal_.secondary);
        drawGlyph(g, w.id == 0 ? Mute : Wave, Gdiplus::RectF(w.r.X + w.r.Width / 2 - 11, w.r.Y + 14, 22, 22), glyph);
        g.DrawString(w.text.c_str(), -1, fonts_[w.selected ? Header : Medium].get(),
                     Gdiplus::RectF(w.r.X, w.r.Y + 44, w.r.Width, 18), &center,
                     w.selected ? &textBrush : &secondaryBrush);
        return;
    }
    case Kind::SlotRow:
        round_fill(g, w.r, 14, pal_.ink(0.045f));
        return;
    case Kind::Slot: {
        bool lifted = w.selected;
        Gdiplus::RectF r = w.r;
        if (lifted) {
            r = Gdiplus::RectF(r.X - r.Width * 0.03f, r.Y - r.Height * 0.03f, r.Width * 1.06f, r.Height * 1.06f);
            soft_shadow(g, Gdiplus::RectF(r.X, r.Y + 3, r.Width, r.Height), 10, 0.12f);
        } else {
            soft_shadow(g, r, 10, 0.05f);
        }
        round_fill(g, r, 10, th::rgba(pal_.dark ? 0x3A3A3C : 0xF7F7F7, 0.97f));
        pointer_icon(g, w.text, Gdiplus::RectF(r.X + (r.Width - 40) / 2, r.Y + (r.Height - 40) / 2, 40, 40));
        if (lifted && w.error) {
            Gdiplus::RectF badge(r.X + r.Width - 12, r.Y - 5, 17, 17);
            Gdiplus::SolidBrush red(th::rgba(0xFF3B30));
            g.FillEllipse(&red, badge);
            drawGlyph(g, Minus, badge, th::white(1));
        } else if (hovered && !dragging_ && w.id >= 0) {
            Gdiplus::RectF badge(r.X + r.Width - 11, r.Y - 5, 16, 16);
            Gdiplus::SolidBrush dark(th::black(0.55f));
            g.FillEllipse(&dark, badge);
            drawGlyph(g, Cross, Gdiplus::RectF(badge.X + 3, badge.Y + 3, 10, 10), th::white(1));
        }
        return;
    }
    case Kind::AddSlot: {
        bool on = hovered || w.selected;
        round_fill(g, w.r, 10, pal_.ink(on ? 0.09f : 0.05f));
        Gdiplus::GraphicsPath p;
        th::add_rounded_rect(p, Gdiplus::RectF(w.r.X + 0.5f, w.r.Y + 0.5f, w.r.Width - 1, w.r.Height - 1), 10);
        Gdiplus::Pen dash(pal_.ink(0.16f), 1);
        Gdiplus::REAL pattern[] = {4, 3};
        dash.SetDashPattern(pattern, 2);
        g.DrawPath(&dash, &p);
        drawGlyph(g, Plus, Gdiplus::RectF(w.r.X + w.r.Width / 2 - 7, w.r.Y + w.r.Height / 2 - 7, 14, 14),
                  on ? th::rgba(pal_.text) : th::rgba(pal_.secondary));
        return;
    }
    case Kind::EmptySlot:
        round_fill(g, w.r, 10, pal_.ink(0.035f));
        return;
    case Kind::TextButton: {
        Gdiplus::StringFormat fmt(Gdiplus::StringFormatFlagsNoWrap);
        g.DrawString(w.text.c_str(), -1, fonts_[Caption].get(), Gdiplus::PointF(w.r.X, w.r.Y),
                     &fmt, hovered ? &textBrush : &secondaryBrush);
        return;
    }
    case Kind::LinkRow: {
        if (hovered) {
            Gdiplus::SolidBrush b(pal_.ink(0.04f));
            g.FillRectangle(&b, w.r);
        }
        g.DrawString(w.text.c_str(), -1, fonts_[Body].get(), Gdiplus::RectF(w.r.X + 14, w.r.Y, w.r.Width, w.r.Height),
                     &leftMid, &textBrush);
        float dw = textWidth(g, w.detail, Medium);
        g.DrawString(w.detail.c_str(), -1, fonts_[Medium].get(),
                     Gdiplus::RectF(w.r.X + w.r.Width - 32 - dw, w.r.Y, dw + 4, w.r.Height), &leftMid, &secondaryBrush);
        drawGlyph(g, Arrow, Gdiplus::RectF(w.r.X + w.r.Width - 26, w.r.Y + w.r.Height / 2 - 5, 10, 10),
                  th::rgba(pal_.secondary));
        return;
    }
    case Kind::Picker:
        soft_shadow(g, Gdiplus::RectF(w.r.X, w.r.Y + 4, w.r.Width, w.r.Height), 12, 0.16f);
        round_fill(g, w.r, 12, th::rgba(pal_.popover));
        round_stroke(g, w.r, 12, pal_.ink(0.1f), 1);
        return;
    case Kind::PickerCell:
        if (hovered && !w.selected) {
            round_fill(g, Gdiplus::RectF(w.r.X + 2, w.r.Y + 2, w.r.Width - 4, w.r.Height - 4), 10, pal_.ink(0.08f));
        }
        pointer_icon(g, w.text, Gdiplus::RectF(w.r.X + (w.r.Width - 40) / 2, w.r.Y + (w.r.Height - 40) / 2, 40, 40),
                     w.selected ? 0.35f : 1.0f);
        return;
    case Kind::AppIcon: {
        static std::unique_ptr<Gdiplus::Bitmap> icon(th::load_png_resource(IDR_APP_PNG));
        if (icon) {
            g.DrawImage(icon.get(), w.r);
        }
        return;
    }
    case Kind::Glyph:
        return;
    }
}

// ---------------------------------------------------------------------------
// Input

int SettingsWindow::hit(float x, float y) const {
    for (int i = int(widgets_.size()) - 1; i >= 0; i--) {
        const Widget &w = widgets_[size_t(i)];
        switch (w.kind) {
        case Kind::Text:
        case Kind::Card:
        case Kind::Separator:
        case Kind::SlotRow:
        case Kind::EmptySlot:
        case Kind::AppIcon:
        case Kind::Glyph:
            continue;
        case Kind::Picker:
            if (w.r.Contains(x, y)) {
                return -1;
            }
            continue;
        default:
            break;
        }
        if (w.kind == Kind::Slot && w.id < 0) {
            continue;
        }
        Gdiplus::RectF r = w.r;
        if (w.kind == Kind::Slot) {
            r = Gdiplus::RectF(r.X, r.Y - 6, r.Width + 6, r.Height + 6);
        }
        if (r.Contains(x, y)) {
            return i;
        }
    }
    return -1;
}

void SettingsWindow::mouseDown(float x, float y) {
    leftDown_ = true;
    if (picking_) {
        bool inPicker = false;
        for (const Widget &w : widgets_) {
            if (w.kind == Kind::Picker && w.r.Contains(x, y)) {
                inPicker = true;
            }
        }
        if (!inPicker) {
            picking_ = false;
            pressed_ = -1;
            redraw();
            return;
        }
    }
    int h = hit(x, y);
    if (recording_ >= 0 && (h < 0 || widgets_[size_t(h)].kind != Kind::Recorder)) {
        stopRecording();
    }
    pressed_ = h;
    if (h < 0) {
        return;
    }
    Widget &w = widgets_[size_t(h)];
    if (w.kind == Kind::Slider) {
        sliding_ = h;
        mouseMove(x, y);
    } else if (w.kind == Kind::Slot) {
        dragIndex_ = w.id;
        dragStartX_ = dragX_ = x;
        dragStartY_ = dragY_ = y;
        dragging_ = false;
        dragOut_ = false;
        draft_ = prefs::get().pinned;
    }
}

void SettingsWindow::mouseMove(float x, float y) {
    if (sliding_ >= 0 && sliding_ < int(widgets_.size())) {
        const Widget &w = widgets_[size_t(sliding_)];
        float tx = w.r.X + 20, tw = w.r.Width - 46;
        float t = std::clamp((x - tx) / tw, 0.0f, 1.0f);
        if (w.slide) {
            w.slide(t);
        }
        redraw();
        return;
    }
    if (dragIndex_ >= 0 && leftDown_) {
        if (!dragging_ && std::hypot(x - dragStartX_, y - dragStartY_) > 3) {
            dragging_ = true;
        }
        if (dragging_) {
            dragX_ = x;
            dragY_ = y;
            const auto &pinned = prefs::get().pinned;
            std::wstring style = pinned[size_t(dragIndex_)];
            std::vector<std::wstring> base;
            for (const auto &s : pinned) {
                if (s != style) {
                    base.push_back(s);
                }
            }
            Gdiplus::RectF zone(slotRow_.X - 16, slotRow_.Y - 28, slotRow_.Width + 32, slotRow_.Height + 56);
            if (zone.Contains(x, y)) {
                int slot = std::clamp(int((x - slotRow_.X - 10) / slotStep_), 0, int(base.size()));
                base.insert(base.begin() + slot, style);
                draft_ = base;
                dragOut_ = false;
            } else {
                dragOut_ = pinned.size() > 1;
                draft_ = dragOut_ ? base : pinned;
            }
            redraw();
            return;
        }
    }
    int h = hit(x, y);
    if (h != hover_) {
        hover_ = h;
        redraw();
    }
}

void SettingsWindow::commitDrag() {
    auto &pinned = prefs::get().pinned;
    Gdiplus::RectF zone(slotRow_.X - 16, slotRow_.Y - 28, slotRow_.Width + 32, slotRow_.Height + 56);
    bool outside = !zone.Contains(dragX_, dragY_);
    if (outside && pinned.size() <= 1) {
        pinMessage_ = kKeepOne;
    } else {
        pinned = draft_;
        pinMessage_.clear();
    }
    dragging_ = false;
    dragOut_ = false;
    dragIndex_ = -1;
    prefs::changed();
    redraw();
}

void SettingsWindow::mouseUp(float x, float y) {
    leftDown_ = false;
    if (sliding_ >= 0) {
        sliding_ = -1;
        pressed_ = -1;
        return;
    }
    if (dragging_) {
        pressed_ = -1;
        commitDrag();
        return;
    }
    int h = hit(x, y);
    int was = pressed_;
    pressed_ = -1;
    if (dragIndex_ >= 0) {
        int index = dragIndex_;
        dragIndex_ = -1;
        if (h == was && h >= 0) {
            const Widget &w = widgets_[size_t(h)];
            bool onBadge = x >= w.r.X + w.r.Width - 13 && y <= w.r.Y + 13;
            if (onBadge) {
                auto &pinned = prefs::get().pinned;
                if (pinned.size() <= 1) {
                    pinMessage_ = kKeepOne;
                    redraw();
                } else {
                    pinned.erase(pinned.begin() + index);
                    pinMessage_.clear();
                    prefs::changed();
                }
            }
        }
        return;
    }
    if (h < 0 || h != was) {
        return;
    }
    Widget w = widgets_[size_t(h)];
    if (w.kind == Kind::Popup && w.pick) {
        HMENU menu = CreatePopupMenu();
        for (size_t i = 0; i < w.options.size(); i++) {
            AppendMenuW(menu, MF_STRING | (int(i) == w.id ? MF_CHECKED : 0), UINT(i + 1), w.options[i].c_str());
        }
        POINT p{LONG(std::lround(w.r.X * scale_)), LONG(std::lround((w.r.Y + w.r.Height + 2) * scale_))};
        ClientToScreen(hwnd_, &p);
        UINT cmd = UINT(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, p.x, p.y, 0, hwnd_, nullptr));
        DestroyMenu(menu);
        if (cmd > 0) {
            w.pick(int(cmd - 1));
        }
        redraw();
        return;
    }
    if (w.click && w.enabled) {
        w.click();
        redraw();
    }
}

bool SettingsWindow::locate(const std::wstring &label, bool control, RECT *out) const {
    for (size_t i = 0; i < widgets_.size(); i++) {
        const Widget &w = widgets_[i];
        const Widget *target = nullptr;
        if (!control && w.text == label &&
            (w.kind == Kind::Sidebar || w.kind == Kind::SoundTile || (w.kind == Kind::Slot && w.id >= 0) ||
             w.kind == Kind::PickerCell || w.kind == Kind::TextButton || w.kind == Kind::AddSlot)) {
            target = &w;
        } else if (control && w.kind == Kind::Text && w.font == Body && w.text == label) {
            for (size_t k = i + 1; k < widgets_.size() && !target; k++) {
                Kind kind = widgets_[k].kind;
                if (kind == Kind::Switch || kind == Kind::Slider || kind == Kind::Popup || kind == Kind::Recorder ||
                    kind == Kind::Swatch) {
                    target = &widgets_[k];
                }
            }
        }
        if (target) {
            out->left = LONG(std::lround(target->r.X * scale_));
            out->top = LONG(std::lround(target->r.Y * scale_));
            out->right = LONG(std::lround((target->r.X + target->r.Width) * scale_));
            out->bottom = LONG(std::lround((target->r.Y + target->r.Height) * scale_));
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Shortcut recording

void SettingsWindow::startRecording(int which) {
    if (recording_ == which) {
        stopRecording();
        return;
    }
    stopRecording();
    recording_ = which;
    shortcutError_.clear();
    if (onRecording) {
        onRecording(true);
    }
    SetFocus(hwnd_);
    redraw();
}

void SettingsWindow::stopRecording() {
    if (recording_ < 0) {
        return;
    }
    recording_ = -1;
    if (onRecording) {
        onRecording(false);
    }
    redraw();
}

bool SettingsWindow::recordKey(UINT vk) {
    switch (vk) {
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_LWIN:
    case VK_RWIN:
        return true;
    default:
        break;
    }
    UINT mods = 0;
    if (GetKeyState(VK_CONTROL) < 0) {
        mods |= MOD_CONTROL;
    }
    if (GetKeyState(VK_MENU) < 0) {
        mods |= MOD_ALT;
    }
    if (GetKeyState(VK_SHIFT) < 0) {
        mods |= MOD_SHIFT;
    }
    if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) {
        mods |= MOD_WIN;
    }
    if (vk == VK_ESCAPE && mods == 0) {
        stopRecording();
        return true;
    }
    if (!(mods & (MOD_CONTROL | MOD_ALT | MOD_WIN))) {
        shortcutError_ = L"Use Ctrl, Alt or Win together with a key.";
        redraw();
        return true;
    }
    Hotkey h{mods, vk};
    int which = recording_;
    stopRecording();
    bool ok = applyHotkey ? applyHotkey(h, which == 0) : false;
    shortcutError_ = ok ? L"" : h.display() + L" is already used by another app. Pick a different shortcut.";
    redraw();
    return true;
}

// ---------------------------------------------------------------------------
// Offscreen snapshot for --settings-test

bool SettingsWindow::snapshot(const std::wstring &path, int section, bool dark, bool picker) {
    for (const auto &s : kStyles) {
        if (!style_is_laser(s.id)) {
            hands::thumbnail(s.id);
        }
    }
    hands::thumbnail(L"glove");
    section_ = std::clamp(section, 0, int(SectionCount) - 1);
    picking_ = picker;
    scale_ = 1;
    fontScale_ = 0;
    std::unique_ptr<Gdiplus::Bitmap> canvas(th::new_pargb(int(kWidth), int(kHeight)));
    if (!canvas) {
        return false;
    }
    forceTheme_ = dark ? 1 : 0;
    {
        Gdiplus::Graphics g(canvas.get());
        paint(g, kWidth, kHeight);
    }
    forceTheme_ = -1;
    return th::save_png(canvas.get(), path);
}
