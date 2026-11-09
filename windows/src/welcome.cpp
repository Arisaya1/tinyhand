#include "welcome.h"

#include "prefs.h"

namespace {

const wchar_t *kClass = L"TinyHandWelcome";
constexpr float kCardW = 340;
constexpr float kCardH = 300;
constexpr float kMargin = 24;
enum Part { None, Start, Close };

Gdiplus::RectF card_rect() { return Gdiplus::RectF(kMargin, kMargin, kCardW, kCardH); }
Gdiplus::RectF button_rect() { return Gdiplus::RectF(kMargin + (kCardW - 180) / 2, kMargin + 57 + 76 + 16 + 24 + 6 + 17 + 22, 180, 32); }
Gdiplus::RectF close_rect() { return Gdiplus::RectF(kMargin + kCardW - 14 - 18, kMargin + 12, 20, 20); }

std::unique_ptr<Gdiplus::FontFamily> family() {
    for (const wchar_t *name : {L"Segoe UI", L"Tahoma", L"Arial"}) {
        auto f = std::make_unique<Gdiplus::FontFamily>(name);
        if (f->IsAvailable()) {
            return f;
        }
    }
    return std::unique_ptr<Gdiplus::FontFamily>(Gdiplus::FontFamily::GenericSansSerif()->Clone());
}

void round_fill(Gdiplus::Graphics &g, const Gdiplus::RectF &r, float radius, Gdiplus::Color c) {
    Gdiplus::GraphicsPath p;
    th::add_rounded_rect(p, r, radius);
    Gdiplus::SolidBrush b(c);
    g.FillPath(&b, &p);
}

} // namespace

LRESULT CALLBACK WelcomeWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        reinterpret_cast<WelcomeWindow *>(cs->lpCreateParams)->hwnd_ = hwnd;
    }
    auto *self = reinterpret_cast<WelcomeWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->proc(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

int WelcomeWindow::hit(POINT client) const {
    float x = float(client.x) / scale_, y = float(client.y) / scale_;
    if (button_rect().Contains(x, y)) {
        return Start;
    }
    if (close_rect().Contains(x, y)) {
        return Close;
    }
    return None;
}

LRESULT WelcomeWindow::proc(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd_, &p);
        if (hit(p) != None) {
            return HTCLIENT;
        }
        return card_rect().Contains(float(p.x) / scale_, float(p.y) / scale_) ? HTCAPTION : HTNOWHERE;
    }
    case WM_MOUSEMOVE: {
        int h = hit(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (h != hover_) {
            hover_ = h;
            render();
        }
        TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE:
        hover_ = None;
        render();
        return 0;
    case WM_LBUTTONDOWN:
        pressed_ = hit(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        return 0;
    case WM_LBUTTONUP: {
        int h = hit(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (h != None && h == pressed_) {
            close();
            if (h == Start && onStart) {
                onStart();
            }
        }
        pressed_ = None;
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_RETURN) {
            close();
            if (onStart) {
                onStart();
            }
            return 0;
        }
        if (wp == VK_ESCAPE) {
            close();
            return 0;
        }
        break;
    case WM_SETTINGCHANGE:
        render();
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

void WelcomeWindow::paint(Gdiplus::Graphics &g, bool dark) {
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    Gdiplus::RectF card = card_rect();
    for (int i = 8; i >= 1; i--) {
        float grow = float(i) * 2.2f;
        round_fill(g, Gdiplus::RectF(card.X - grow, card.Y - grow + 6, card.Width + 2 * grow, card.Height + 2 * grow),
                   20 + grow, th::black(0.028f));
    }
    round_fill(g, card, 20, th::rgba(dark ? 0x2C2C2E : 0xF4F4F6));
    Gdiplus::GraphicsPath border;
    th::add_rounded_rect(border, Gdiplus::RectF(card.X + 0.5f, card.Y + 0.5f, card.Width - 1, card.Height - 1), 20);
    Gdiplus::Pen edge(dark ? th::white(0.1f) : th::black(0.08f), 1);
    g.DrawPath(&edge, &border);

    static std::unique_ptr<Gdiplus::Bitmap> icon(th::load_png_resource(IDR_APP_PNG));
    float y = card.Y + 57;
    if (icon) {
        g.DrawImage(icon.get(), Gdiplus::RectF(card.X + (kCardW - 76) / 2, y, 76, 76));
    }
    y += 76 + 16;
    auto fam = family();
    auto semi = std::make_unique<Gdiplus::FontFamily>(L"Segoe UI Semibold");
    Gdiplus::Font title(semi->IsAvailable() ? semi.get() : fam.get(), 19,
                        semi->IsAvailable() ? Gdiplus::FontStyleRegular : Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font body(fam.get(), 13, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat center;
    center.SetAlignment(Gdiplus::StringAlignmentCenter);
    center.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::SolidBrush primary(th::rgba(dark ? 0xF2F2F7 : 0x1D1D1F));
    Gdiplus::SolidBrush secondary(th::rgba(dark ? 0x98989F : 0x6E6E73));
    g.DrawString(L"You're all set", -1, &title, Gdiplus::RectF(card.X, y, kCardW, 24), &center, &primary);
    y += 24 + 6;
    std::wstring hint = L"Press " + prefs::get().toggleKey.display() + L" any time to start.";
    g.DrawString(hint.c_str(), -1, &body, Gdiplus::RectF(card.X, y, kCardW, 17), &center, &secondary);

    Gdiplus::RectF b = button_rect();
    uint32_t accent = dark ? 0x0A84FF : 0x0A7AFF;
    round_fill(g, b, 8, th::rgba(accent, hover_ == Start ? 0.88f : 1.0f));
    Gdiplus::SolidBrush white(th::white(1));
    g.DrawString(L"Start", -1, &body, b, &center, &white);

    Gdiplus::RectF c = close_rect();
    if (hover_ == Close) {
        Gdiplus::SolidBrush bg(dark ? th::white(0.1f) : th::black(0.06f));
        g.FillEllipse(&bg, c);
    }
    Gdiplus::Pen x(dark ? th::white(0.4f) : th::black(0.3f), 1.6f);
    x.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
    float cx = c.X + c.Width / 2, cy = c.Y + c.Height / 2, d = 4.5f;
    g.DrawLine(&x, cx - d, cy - d, cx + d, cy + d);
    g.DrawLine(&x, cx - d, cy + d, cx + d, cy - d);
}

void WelcomeWindow::render() {
    if (!hwnd_) {
        return;
    }
    int w = int(std::lround((kCardW + 2 * kMargin) * scale_));
    int h = int(std::lround((kCardH + 2 * kMargin) * scale_));
    if (!surface_.resize(w, h)) {
        return;
    }
    surface_.clear();
    {
        Gdiplus::Graphics g(surface_.bitmap.get());
        g.ScaleTransform(scale_, scale_);
        paint(g, th::apps_use_dark());
    }
    RECT r;
    GetWindowRect(hwnd_, &r);
    surface_.present(hwnd_, POINT{r.left, r.top}, 255);
}

void WelcomeWindow::show(HINSTANCE instance) {
    if (!hwnd_) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, kClass, L"Welcome to TinyHand", WS_POPUP, 0, 0, 10, 10,
                        nullptr, nullptr, instance, this);
    }
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    POINT cursor;
    GetCursorPos(&cursor);
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &mi);
    scale_ = th::dpi_scale_at(cursor);
    int w = int(std::lround((kCardW + 2 * kMargin) * scale_));
    int h = int(std::lround((kCardH + 2 * kMargin) * scale_));
    SetWindowPos(hwnd_, nullptr, (mi.rcWork.left + mi.rcWork.right - w) / 2, (mi.rcWork.top + mi.rcWork.bottom - h) / 2,
                 w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    hover_ = pressed_ = None;
    render();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void WelcomeWindow::close() {
    if (hwnd_) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

bool WelcomeWindow::snapshot(const std::wstring &path, bool dark) {
    int w = int(kCardW + 2 * kMargin), h = int(kCardH + 2 * kMargin);
    std::unique_ptr<Gdiplus::Bitmap> canvas(th::new_pargb(w, h));
    if (!canvas) {
        return false;
    }
    {
        Gdiplus::Graphics g(canvas.get());
        g.Clear(dark ? Gdiplus::Color(255, 60, 60, 64) : Gdiplus::Color(255, 200, 204, 210));
        paint(g, dark);
    }
    return th::save_png(canvas.get(), path);
}
