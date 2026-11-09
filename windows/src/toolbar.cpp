#include "toolbar.h"

#include "hands.h"
#include "prefs.h"

namespace {

const wchar_t *kClass = L"TinyHandToolbar";

std::unique_ptr<Gdiplus::FontFamily> font_family() {
    for (const wchar_t *name : {L"Segoe UI", L"Tahoma", L"Arial"}) {
        auto f = std::make_unique<Gdiplus::FontFamily>(name);
        if (f->IsAvailable()) {
            return f;
        }
    }
    return std::unique_ptr<Gdiplus::FontFamily>(Gdiplus::FontFamily::GenericSansSerif()->Clone());
}

// Icons are drawn in a 64-unit box (like the macOS 64 pt images) scaled into
// the button.
struct Box {
    Gdiplus::RectF r;
    float k() const { return r.Width / 64.0f; }
    float x(float u) const { return r.X + u * k(); }
    float y(float u) const { return r.Y + u * k(); }
};

void ink_dot(Gdiplus::Graphics &g, const Gdiplus::RectF &rect, uint32_t color) {
    Box b{rect};
    float r = 13 * b.k(), cx = b.x(32), cy = b.y(32);
    Gdiplus::SolidBrush shade(th::black(0.35f));
    g.FillEllipse(&shade, cx - r - 1.5f * b.k(), cy - r - 0.5f * b.k(), 2 * r + 3 * b.k(), 2 * r + 3 * b.k());
    Gdiplus::SolidBrush fill(th::rgba(color));
    g.FillEllipse(&fill, cx - r, cy - r, 2 * r, 2 * r);
    Gdiplus::SolidBrush shine(th::white(0.35f));
    g.FillEllipse(&shine, cx - r * 0.55f, cy - r * 0.65f, r * 0.7f, r * 0.5f);
}

void rainbow_dot(Gdiplus::Graphics &g, const Gdiplus::RectF &rect) {
    Box b{rect};
    float r = 13 * b.k(), cx = b.x(32), cy = b.y(32);
    Gdiplus::SolidBrush shade(th::black(0.35f));
    g.FillEllipse(&shade, cx - r - 1.5f * b.k(), cy - r - 0.5f * b.k(), 2 * r + 3 * b.k(), 2 * r + 3 * b.k());
    const uint32_t colors[] = {0xFF3B30, 0xFF9500, 0xFFCC00, 0x34C759, 0x007AFF, 0xAF52DE};
    for (int i = 0; i < 6; i++) {
        Gdiplus::SolidBrush slice(th::rgba(colors[i]));
        g.FillPie(&slice, cx - r, cy - r, 2 * r, 2 * r, -90.0f - 60.0f * float(i), -60.0f);
    }
}

void laser_icon(Gdiplus::Graphics &g, const Gdiplus::RectF &rect) {
    Box b{rect};
    float cx = b.x(32), cy = b.y(32);
    float glow = 18 * b.k();
    Gdiplus::GraphicsPath halo;
    halo.AddEllipse(cx - glow, cy - glow, 2 * glow, 2 * glow);
    Gdiplus::PathGradientBrush brush(&halo);
    brush.SetCenterColor(th::rgba(kInkRed, 0.55f));
    Gdiplus::Color edge = th::rgba(kInkRed, 0);
    INT n = 1;
    brush.SetSurroundColors(&edge, &n);
    g.FillPath(&brush, &halo);
    Gdiplus::SolidBrush red(th::rgba(kInkRed));
    g.FillEllipse(&red, cx - 7 * b.k(), cy - 7 * b.k(), 14 * b.k(), 14 * b.k());
    Gdiplus::SolidBrush core(Gdiplus::Color(242, 255, 219, 209));
    g.FillEllipse(&core, cx - 3 * b.k(), cy - 3 * b.k(), 6 * b.k(), 6 * b.k());
}

void speaker_icon(Gdiplus::Graphics &g, const Gdiplus::RectF &rect, bool on, float scale) {
    float s = scale;
    float cx = rect.X + rect.Width / 2, cy = rect.Y + rect.Height / 2;
    Gdiplus::Color color = th::white(on ? 0.85f : 0.45f);
    Gdiplus::SolidBrush brush(color);
    float ox = cx - 9 * s, oy = cy - 8 * s;
    Gdiplus::PointF body[] = {{ox, oy + 5 * s},          {ox + 4 * s, oy + 5 * s}, {ox + 9 * s, oy + 0.5f * s},
                              {ox + 9 * s, oy + 15.5f * s}, {ox + 4 * s, oy + 11 * s}, {ox, oy + 11 * s}};
    g.FillPolygon(&brush, body, 6);
    Gdiplus::Pen pen(color, 1.8f * s);
    pen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
    if (on) {
        g.DrawArc(&pen, ox + 7 * s, oy + 4 * s, 8 * s, 8 * s, -50, 100);
        g.DrawArc(&pen, ox + 6 * s, oy + 0.5f * s, 15 * s, 15 * s, -50, 100);
    } else {
        g.DrawLine(&pen, ox + 12.5f * s, oy + 5 * s, ox + 18.5f * s, oy + 11 * s);
        g.DrawLine(&pen, ox + 12.5f * s, oy + 11 * s, ox + 18.5f * s, oy + 5 * s);
    }
}

void cross_icon(Gdiplus::Graphics &g, const Gdiplus::RectF &rect, float scale) {
    float cx = rect.X + rect.Width / 2, cy = rect.Y + rect.Height / 2, d = 5.5f * scale;
    Gdiplus::Pen pen(th::white(0.85f), 2.0f * scale);
    pen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
    g.DrawLine(&pen, cx - d, cy - d, cx + d, cy + d);
    g.DrawLine(&pen, cx - d, cy + d, cx + d, cy - d);
}

void fill_round(Gdiplus::Graphics &g, const Gdiplus::RectF &r, float radius, Gdiplus::Color c) {
    Gdiplus::GraphicsPath p;
    th::add_rounded_rect(p, r, radius);
    Gdiplus::SolidBrush b(c);
    g.FillPath(&b, &p);
}

} // namespace

bool Toolbar::create(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                            kClass, L"TinyHand toolbar", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, instance, this);
    current_ = prefs::get().pointerStyle;
    refresh();
    return hwnd_ != nullptr;
}

LRESULT CALLBACK Toolbar::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        reinterpret_cast<Toolbar *>(cs->lpCreateParams)->hwnd_ = hwnd;
    }
    auto *self = reinterpret_cast<Toolbar *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->proc(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Toolbar::proc(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_MOUSEMOVE: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int h = hit(p);
        if (h != hover_) {
            hover_ = h;
            render();
        }
        TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (hover_ != -1) {
            hover_ = -1;
            render();
        }
        return 0;
    case WM_LBUTTONDOWN:
        pressed_ = hit(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        return 0;
    case WM_LBUTTONUP: {
        int h = hit(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        int was = pressed_;
        pressed_ = -1;
        if (h >= 0 && h == was) {
            activate(h);
        }
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

void Toolbar::refresh() {
    std::vector<std::wstring> styles = prefs::get().pinned;
    if (std::find(styles.begin(), styles.end(), current_) == styles.end()) {
        styles.push_back(current_);
    }
    styles_ = styles;
    if (!prefs::get().showInkButton) {
        trayOpen_ = false;
    }
    layout();
    if (shown_) {
        render();
    }
}

void Toolbar::select(const std::wstring &style) {
    current_ = style;
    refresh();
}

void Toolbar::setSoundOn(bool on) {
    if (soundOn_ == on) {
        return;
    }
    soundOn_ = on;
    if (shown_) {
        render();
    }
}

void Toolbar::layout() {
    const float s = scale_;
    const float pad = 10 * s, padV = 8 * s, btn = 40 * s, gap = 6 * s;
    items_.clear();
    float x = pad;
    for (const auto &id : styles_) {
        items_.push_back({Kind::Style, id, Gdiplus::RectF(x, padV, btn, btn)});
        x += btn + gap;
    }
    items_.push_back({Kind::Divider, L"", Gdiplus::RectF(x, padV + (btn - 24 * s) / 2, 1 * s, 24 * s)});
    x += 1 * s + gap;
    if (prefs::get().showInkButton) {
        items_.push_back({Kind::Ink, L"", Gdiplus::RectF(x, padV, btn, btn)});
        x += btn + gap;
    }
    items_.push_back({Kind::Sound, L"", Gdiplus::RectF(x, padV, btn, btn)});
    x += btn + gap;
    items_.push_back({Kind::Exit, L"", Gdiplus::RectF(x, padV, btn, btn)});
    x += btn + pad;
    const float barW = x, barH = btn + 2 * padV;

    const float swatch = 30 * s, swatchGap = 2 * s;
    const float trayW = 12 * s + float(kInkChoices.size()) * swatch + float(kInkChoices.size() - 1) * swatchGap;
    const float trayH = swatch + 10 * s;

    float hintW = 0, hintH = 36 * s;
    if (!hintParts_.empty()) {
        auto family = font_family();
        Gdiplus::Font bold(family.get(), 12.5f * s, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font regular(family.get(), 12.5f * s, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Bitmap scratch(1, 1, PixelFormat32bppPARGB);
        Gdiplus::Graphics g(&scratch);
        hintW = 32 * s;
        for (size_t i = 0; i < hintParts_.size(); i++) {
            Gdiplus::RectF a, b;
            g.MeasureString(hintParts_[i].first.c_str(), -1, &bold, Gdiplus::PointF(0, 0), &a);
            std::wstring rest = L" " + hintParts_[i].second;
            g.MeasureString(rest.c_str(), -1, &regular, Gdiplus::PointF(0, 0), &b);
            hintW += a.Width + b.Width + (i ? 14 * s : 0);
        }
    }

    const float M = 24 * s;
    const float W = std::max(barW, hintW) + 2 * trayW + 2 * M;
    const float H = M + hintH + 12 * s + trayH + 8 * s + barH + M;
    bar_ = Gdiplus::RectF(std::round((W - barW) / 2), std::round(H - M - barH), barW, barH);
    for (auto &item : items_) {
        item.rect.X += bar_.X;
        item.rect.Y += bar_.Y;
    }
    float inkX = bar_.X + barW / 2;
    for (const auto &item : items_) {
        if (item.kind == Kind::Ink) {
            inkX = item.rect.X + item.rect.Width / 2;
        }
    }
    tray_ = Gdiplus::RectF(std::round(inkX - trayW / 2), bar_.Y - 8 * s - trayH, trayW, trayH);
    if (trayOpen_) {
        float sx = tray_.X + 6 * s;
        for (const auto &c : kInkChoices) {
            items_.push_back({Kind::Swatch, c.id, Gdiplus::RectF(sx, tray_.Y + 5 * s, swatch, swatch)});
            sx += swatch + swatchGap;
        }
    }
    float hintBottom = (trayOpen_ ? tray_.Y : bar_.Y) - 12 * s;
    hint_ = Gdiplus::RectF(std::round(bar_.X + barW / 2 - hintW / 2), hintBottom - hintH, hintW, hintH);

    size_ = SIZE{LONG(std::ceil(W)), LONG(std::ceil(H))};
    LONG centerX = (monitor_.left + monitor_.right) / 2;
    origin_ = POINT{centerX - LONG(std::lround(bar_.X + barW / 2)),
                    work_.bottom - LONG(std::lround(14 * s)) - LONG(std::lround(bar_.Y + barH))};
}

void Toolbar::drawPanel(Gdiplus::Graphics &g, const Gdiplus::RectF &r, float radius) const {
    for (int i = 6; i >= 1; i--) {
        float grow = float(i) * 1.6f * scale_;
        Gdiplus::RectF sr(r.X - grow, r.Y - grow + 3 * scale_, r.Width + 2 * grow, r.Height + 2 * grow);
        fill_round(g, sr, radius + grow, th::black(0.045f));
    }
    fill_round(g, r, radius, th::rgba(0x141416, 0.9f));
    Gdiplus::GraphicsPath border;
    Gdiplus::RectF br(r.X + 0.5f, r.Y + 0.5f, r.Width - 1, r.Height - 1);
    th::add_rounded_rect(border, br, radius);
    Gdiplus::Pen pen(th::white(0.12f), 1.0f);
    g.DrawPath(&pen, &border);
}

void Toolbar::drawHint(Gdiplus::Graphics &g) const {
    if (hintParts_.empty() || hintAlpha_ <= 0) {
        return;
    }
    const float s = scale_;
    Gdiplus::RectF r = hint_;
    fill_round(g, r, 12 * s, th::rgba(0x141416, 0.9f * hintAlpha_));
    auto family = font_family();
    Gdiplus::Font bold(family.get(), 12.5f * s, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font regular(family.get(), 12.5f * s, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush strong(th::white(hintAlpha_));
    Gdiplus::SolidBrush soft(th::white(0.7f * hintAlpha_));
    float x = r.X + 16 * s;
    for (size_t i = 0; i < hintParts_.size(); i++) {
        Gdiplus::RectF a, b;
        g.MeasureString(hintParts_[i].first.c_str(), -1, &bold, Gdiplus::PointF(0, 0), &a);
        std::wstring rest = L" " + hintParts_[i].second;
        g.MeasureString(rest.c_str(), -1, &regular, Gdiplus::PointF(0, 0), &b);
        float y = r.Y + (r.Height - a.Height) / 2;
        g.DrawString(hintParts_[i].first.c_str(), -1, &bold, Gdiplus::PointF(x, y), &strong);
        g.DrawString(rest.c_str(), -1, &regular, Gdiplus::PointF(x + a.Width - 4 * s, y + (a.Height - b.Height) / 2),
                     &soft);
        x += a.Width + b.Width + 14 * s;
    }
}

void Toolbar::drawItem(Gdiplus::Graphics &g, const Item &item, int index) const {
    const float s = scale_;
    const Gdiplus::RectF &r = item.rect;
    bool hovered = index == hover_;
    switch (item.kind) {
    case Kind::Divider:
        g.FillRectangle(std::make_unique<Gdiplus::SolidBrush>(th::white(0.18f)).get(), r);
        return;
    case Kind::Style: {
        if (item.id == current_) {
            fill_round(g, r, 10 * s, th::white(0.18f));
        } else if (hovered) {
            fill_round(g, r, 10 * s, th::white(0.08f));
        }
        if (style_is_laser(item.id)) {
            laser_icon(g, r);
        } else if (Gdiplus::Bitmap *thumb = hands::thumbnail(item.id)) {
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            Gdiplus::RectF inner(r.X + 2 * s, r.Y + 2 * s, r.Width - 4 * s, r.Height - 4 * s);
            g.DrawImage(thumb, inner);
        }
        return;
    }
    case Kind::Ink:
        if (trayOpen_) {
            fill_round(g, r, 10 * s, th::white(0.18f));
        } else if (hovered) {
            fill_round(g, r, 10 * s, th::white(0.08f));
        }
        ink_dot(g, r, ink_for(current_));
        return;
    case Kind::Sound:
        if (hovered) {
            fill_round(g, r, 10 * s, th::white(0.08f));
        }
        speaker_icon(g, r, soundOn_, s);
        return;
    case Kind::Exit:
        if (hovered) {
            fill_round(g, r, 10 * s, th::white(0.08f));
        }
        cross_icon(g, r, s);
        return;
    case Kind::Swatch: {
        if (item.id == prefs::get().inkChoice) {
            fill_round(g, r, r.Width / 2, th::white(0.2f));
        } else if (hovered) {
            fill_round(g, r, r.Width / 2, th::white(0.1f));
        }
        for (const auto &c : kInkChoices) {
            if (item.id == c.id) {
                if (c.match) {
                    rainbow_dot(g, r);
                } else {
                    ink_dot(g, r, c.color);
                }
            }
        }
        return;
    }
    }
}

void Toolbar::render() {
    if (!hwnd_ || !surface_.resize(size_.cx, size_.cy)) {
        return;
    }
    surface_.clear();
    {
        Gdiplus::Graphics g(surface_.bitmap.get());
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        drawHint(g);
        if (trayOpen_) {
            drawPanel(g, tray_, 14 * scale_);
        }
        drawPanel(g, bar_, 16 * scale_);
        for (size_t i = 0; i < items_.size(); i++) {
            drawItem(g, items_[i], int(i));
        }
    }
    present();
}

void Toolbar::present() {
    if (shown_) {
        surface_.present(hwnd_, origin_, BYTE(std::lround(std::clamp(alpha_, 0.0f, 1.0f) * 255)));
    }
}

int Toolbar::hit(POINT p) const {
    for (size_t i = 0; i < items_.size(); i++) {
        const Item &it = items_[i];
        if (it.kind != Kind::Divider && p.x >= it.rect.X && p.x < it.rect.X + it.rect.Width && p.y >= it.rect.Y &&
            p.y < it.rect.Y + it.rect.Height) {
            return int(i);
        }
    }
    return -1;
}

void Toolbar::activate(int index) {
    Item item = items_[size_t(index)];
    switch (item.kind) {
    case Kind::Style:
        if (onPick) {
            onPick(item.id);
        }
        break;
    case Kind::Ink:
        setTrayOpen(!trayOpen_);
        break;
    case Kind::Sound:
        if (onToggleSound) {
            onToggleSound();
        }
        break;
    case Kind::Exit:
        if (onExit) {
            onExit();
        }
        break;
    case Kind::Swatch:
        prefs::get().inkChoice = item.id;
        trayOpen_ = false;
        prefs::changed();
        break;
    case Kind::Divider:
        break;
    }
}

void Toolbar::setTrayOpen(bool open) {
    if (open == trayOpen_) {
        return;
    }
    trayOpen_ = open;
    hover_ = -1;
    layout();
    if (shown_) {
        render();
    }
}

void Toolbar::place(const RECT &monitor, const RECT &work, float scale) {
    monitor_ = monitor;
    work_ = work;
    scale_ = scale;
    trayOpen_ = false;
    layout();
    if (shown_) {
        render();
    }
}

void Toolbar::show() {
    shown_ = true;
    layout();
    render();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void Toolbar::hide() {
    trayOpen_ = false;
    hintParts_.clear();
    hintAlpha_ = hintTarget_ = 0;
    revealed_ = false;
    alpha_ = alphaTarget_ = 0;
    shown_ = false;
    hover_ = -1;
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, GetWindowLongPtrW(hwnd_, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
    ShowWindow(hwnd_, SW_HIDE);
    layout();
}

void Toolbar::setRevealed(bool value) {
    if (value == revealed_) {
        return;
    }
    revealed_ = value;
    if (!value) {
        setTrayOpen(false);
    }
    alphaTarget_ = value ? 1.0f : 0.0f;
    LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, value ? (ex & ~LONG_PTR(WS_EX_TRANSPARENT)) : (ex | WS_EX_TRANSPARENT));
}

void Toolbar::showHint(const std::vector<std::pair<std::wstring, std::wstring>> &parts, double now) {
    hintParts_ = parts;
    hintTarget_ = 1;
    lastAnimate_ = now;
    layout();
    if (shown_) {
        render();
    }
}

void Toolbar::hideHint(double) { hintTarget_ = 0; }

bool Toolbar::animating() const { return alpha_ != alphaTarget_ || hintAlpha_ != hintTarget_; }

void Toolbar::animate(double now) {
    double dt = std::clamp(now - lastAnimate_, 0.0, 0.1);
    lastAnimate_ = now;
    if (!shown_) {
        return;
    }
    auto step = [dt](float &v, float target, double in, double out) {
        if (v == target) {
            return false;
        }
        float d = float(dt / (target > v ? in : out));
        v = target > v ? std::min(target, v + d) : std::max(target, v - d);
        return true;
    };
    bool hintChanged = step(hintAlpha_, hintTarget_, 0.2, 0.3);
    bool alphaChanged = step(alpha_, alphaTarget_, 0.15, 0.35);
    if (hintChanged && hintAlpha_ <= 0 && hintTarget_ <= 0) {
        hintParts_.clear();
        layout();
    }
    if (hintChanged) {
        render();
    } else if (alphaChanged) {
        present();
    }
}

RECT Toolbar::barRect() const {
    return RECT{origin_.x + LONG(bar_.X), origin_.y + LONG(bar_.Y), origin_.x + LONG(bar_.X + bar_.Width),
                origin_.y + LONG(bar_.Y + bar_.Height)};
}

RECT Toolbar::frameRect() const {
    RECT r = barRect();
    if (trayOpen_) {
        r.left = std::min(r.left, origin_.x + LONG(tray_.X));
        r.right = std::max(r.right, origin_.x + LONG(tray_.X + tray_.Width));
        r.top = std::min(r.top, origin_.y + LONG(tray_.Y));
    }
    return r;
}

bool Toolbar::buttonCenter(const std::wstring &style, POINT *out) const {
    for (const auto &item : items_) {
        if (item.kind == Kind::Style && item.id == style) {
            out->x = origin_.x + LONG(item.rect.X + item.rect.Width / 2);
            out->y = origin_.y + LONG(item.rect.Y + item.rect.Height / 2);
            return true;
        }
    }
    return false;
}

bool Toolbar::renderPng(const std::wstring &path, bool withHint, bool withTray) {
    if (withHint) {
        hintParts_ = {{L"Click", L"to tap"}, {L"Drag", L"to draw"}, {L"Hold Alt", L"to click for real"},
                      {L"Esc", L"to exit"}};
        hintAlpha_ = hintTarget_ = 1;
    }
    trayOpen_ = withTray;
    layout();
    if (!surface_.resize(size_.cx, size_.cy)) {
        return false;
    }
    bool wasShown = shown_;
    shown_ = false;
    render();
    shown_ = wasShown;
    std::unique_ptr<Gdiplus::Bitmap> out(th::new_pargb(size_.cx, size_.cy));
    if (!out) {
        return false;
    }
    {
        Gdiplus::Graphics g(out.get());
        g.Clear(Gdiplus::Color(255, 41, 41, 41));
        g.DrawImage(surface_.bitmap.get(), 0, 0, size_.cx, size_.cy);
    }
    return th::save_png(out.get(), path);
}
