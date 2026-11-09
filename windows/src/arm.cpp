#include "arm.h"

#include "prefs.h"

namespace {

constexpr float kMinRodFrac = 0.05f;
constexpr float kGripLeanFrac = 0.30f;
constexpr float kGripDepthFrac = 0.12f;
constexpr float kGripFollow = 0.08f;
constexpr float kLiftFrac = 0.06f;
constexpr float kShadowOpacity = 0.27f;

// The macOS view steps at 120 Hz. Scale per-step easing so the feel is the
// same whatever rate the Windows timer manages.
float ease(float perStep, double dt) {
    return 1.0f - float(std::pow(1.0 - perStep, std::clamp(dt, 0.0, 0.25) * 120.0));
}

void box_blur(std::vector<float> &a, int w, int h, int r) {
    std::vector<float> tmp(a.size());
    for (int pass = 0; pass < 3; pass++) {
        for (int y = 0; y < h; y++) {
            float sum = 0;
            const float *row = &a[size_t(y) * w];
            for (int x = -r; x <= r; x++) {
                sum += row[std::clamp(x, 0, w - 1)] * (x >= 0 && x < w);
            }
            for (int x = 0; x < w; x++) {
                tmp[size_t(y) * w + x] = sum / float(2 * r + 1);
                int add = x + r + 1, sub = x - r;
                sum += (add < w ? row[add] : 0) - (sub >= 0 ? row[sub] : 0);
            }
        }
        for (int x = 0; x < w; x++) {
            float sum = 0;
            for (int y = 0; y <= r && y < h; y++) {
                sum += tmp[size_t(y) * w + x];
            }
            for (int y = 0; y < h; y++) {
                a[size_t(y) * w + x] = sum / float(2 * r + 1);
                int add = y + r + 1, sub = y - r;
                sum += (add < h ? tmp[size_t(add) * w + x] : 0) - (sub >= 0 ? tmp[size_t(sub) * w + x] : 0);
            }
        }
    }
}

} // namespace

void Arm::setStyle(const std::wstring &style) {
    style_ = style;
    laser_ = style_is_laser(style);
    asset_.reset();
    display_.reset();
    shadow_.reset();
    displayHeight_ = 0;
    if (!laser_) {
        asset_ = hands::load(style);
        if (!asset_) {
            style_ = L"laser";
            laser_ = true;
        }
    }
    lastKey_.clear();
}

void Arm::setSize(int width, int height) {
    if (width == width_ && height == height_) {
        return;
    }
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    gripX_ = -1;
    lastKey_.clear();
}

float Arm::inkWidth() const { return std::max(3.0f, float(height_) * 0.0055f); }

bool Arm::settling() const {
    if (press_ > 0.002f) {
        return true;
    }
    return gripX_ >= 0 && gripTarget_ >= 0 && std::fabs(gripTarget_ - gripX_) > 0.5f;
}

void Arm::updateDisplayImage() {
    if (!asset_) {
        return;
    }
    float sizeFrac = float(prefs::get().pointerSize);
    int full = asset_->height;
    float restPts = sizeFrac * float(height_) / (asset_->wrist - asset_->handTop);
    const int step = 64;
    int needed = (int(std::ceil(restPts)) + step - 1) / step * step;
    int h = needed >= full * 9 / 10 ? full : std::max(step, needed);
    if (h == displayHeight_ && display_) {
        return;
    }
    displayHeight_ = h;
    display_.reset(hands::resized(asset_->image.get(), h));
    shadow_.reset();
    if (!display_) {
        return;
    }

    int dw = int(display_->GetWidth()), dh = int(display_->GetHeight());
    float radius = std::max(1.0f, kLiftFrac * float(height_) * 0.35f);
    int blur = std::max(1, int(std::lround(radius / 2)));
    shadowPad_ = blur * 3;
    int sw = dw + 2 * shadowPad_, sh = dh + 2 * shadowPad_;
    std::vector<float> alpha(size_t(sw) * sh, 0.0f);
    Gdiplus::BitmapData data{};
    Gdiplus::Rect all(0, 0, dw, dh);
    if (display_->LockBits(&all, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data) == Gdiplus::Ok) {
        for (int y = 0; y < dh; y++) {
            const uint8_t *row = static_cast<const uint8_t *>(data.Scan0) + ptrdiff_t(y) * data.Stride;
            for (int x = 0; x < dw; x++) {
                alpha[size_t(y + shadowPad_) * sw + x + shadowPad_] = row[x * 4 + 3] / 255.0f;
            }
        }
        display_->UnlockBits(&data);
    }
    box_blur(alpha, sw, sh, blur);
    shadow_.reset(th::new_pargb(sw, sh));
    if (!shadow_) {
        return;
    }
    Gdiplus::Rect sall(0, 0, sw, sh);
    if (shadow_->LockBits(&sall, Gdiplus::ImageLockModeWrite, PixelFormat32bppPARGB, &data) == Gdiplus::Ok) {
        for (int y = 0; y < sh; y++) {
            uint8_t *row = static_cast<uint8_t *>(data.Scan0) + ptrdiff_t(y) * data.Stride;
            for (int x = 0; x < sw; x++) {
                uint8_t a = uint8_t(std::clamp(alpha[size_t(y) * sw + x] * kShadowOpacity, 0.0f, 1.0f) * 255.0f);
                row[x * 4 + 0] = 0;
                row[x * 4 + 1] = 0;
                row[x * 4 + 2] = 0;
                row[x * 4 + 3] = a;
            }
        }
        shadow_->UnlockBits(&data);
    }
}

bool Arm::aim(float cursorX, float cursorY, double dt) {
    const float W = float(width_), H = float(height_);
    const float cx = cursorX, cy = H - cursorY;
    float targetX = std::clamp(cx + kGripLeanFrac * W, W * 0.15f, W * 1.05f);
    float gx = gripX_ < 0 ? targetX : gripX_ + (targetX - gripX_) * ease(kGripFollow, dt);
    gripX_ = gx;
    gripTarget_ = targetX;
    float gy = -kGripDepthFrac * H;
    if (!held) {
        press_ += (0 - press_) * ease(0.18f, dt);
    }
    float sizeFrac = float(prefs::get().pointerSize);
    bool shadow = prefs::get().showShadow;

    std::vector<long> key;
    if (laser_) {
        float r = std::max(4.0f, H * 0.0038f * sizeFrac / 0.13f) * (1 + 0.2f * press_);
        pose_.laserR = r;
        pose_.cursorX = cursorX;
        pose_.cursorY = cursorY;
        pose_.press = press_;
        key = {1, std::lround(r * 100), std::lround(cursorX * 100), std::lround(cursorY * 100), hidden};
    } else {
        if (!asset_) {
            return false;
        }
        updateDisplayImage();
        const HandAsset &a = *asset_;
        float dx = cx - gx, dy = cy - gy;
        float reach = std::hypot(dx, dy);
        float aimAngle = std::atan2(dy, dx);
        float handPts = sizeFrac * H * (1 - 0.06f * press_);
        float s = handPts / (a.wrist - a.handTop);
        float widthPts = s * a.aspect;
        float handTipPts = (a.wrist - a.tip) * s;
        float tipOffX = (a.tipX - a.rodX) * widthPts;
        float along = std::sqrt(std::max(0.0f, reach * reach - tipOffX * tipOffX));
        float rodLen = std::max(kMinRodFrac * H, along - handTipPts);
        float tipAngle = std::atan2(rodLen + handTipPts, tipOffX);
        pose_.rotation = aimAngle - tipAngle;
        pose_.rodLen = rodLen;
        pose_.handPts = handPts;
        pose_.widthPts = widthPts;
        pose_.gripX = gx;
        pose_.gripY = H - gy;
        pose_.lift = kLiftFrac * H * (1 - 0.8f * press_);
        pose_.press = press_;
        key = {0,
               std::lround(pose_.rotation * 100),
               std::lround(rodLen * 100),
               std::lround(handPts * 100),
               std::lround(widthPts * 100),
               std::lround(gx * 100),
               std::lround(pose_.lift * 100),
               std::lround(press_ * 100),
               shadow,
               hidden};
    }
    if (key == lastKey_) {
        return false;
    }
    lastKey_ = key;
    bounds_ = computeBounds();
    return true;
}

RECT Arm::computeBounds() const {
    RECT r{0, 0, 0, 0};
    if (hidden) {
        return r;
    }
    if (laser_) {
        float glow = pose_.laserR + pose_.laserR * (2.2f + pose_.press) * 1.2f + 3;
        r.left = LONG(std::floor(pose_.cursorX - glow));
        r.top = LONG(std::floor(pose_.cursorY - glow));
        r.right = LONG(std::ceil(pose_.cursorX + glow));
        r.bottom = LONG(std::ceil(pose_.cursorY + glow));
        return r;
    }
    if (!asset_) {
        return r;
    }
    float pad = float(shadowPad_) * (display_ ? pose_.widthPts / float(display_->GetWidth()) : 1.0f) + 4;
    float x0 = -asset_->rodX * pose_.widthPts - pad;
    float x1 = (1 - asset_->rodX) * pose_.widthPts + pad;
    float y0 = -(pose_.rodLen + pose_.handPts) - pad;
    float y1 = pad;
    float angle = -pose_.rotation;
    float c = std::cos(angle), s = std::sin(angle);
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    float shadowX = prefs::get().showShadow ? pose_.lift * 0.6f : 0;
    float shadowY = prefs::get().showShadow ? pose_.lift : 0;
    const float xs[2] = {x0, x1}, ys[2] = {y0, y1};
    for (float lx : xs) {
        for (float ly : ys) {
            float px = pose_.gripX + lx * c - ly * s;
            float py = pose_.gripY + lx * s + ly * c;
            for (int k = 0; k < 2; k++) {
                float ox = k ? shadowX : 0, oy = k ? shadowY : 0;
                minX = std::min(minX, px + ox);
                maxX = std::max(maxX, px + ox);
                minY = std::min(minY, py + oy);
                maxY = std::max(maxY, py + oy);
            }
        }
    }
    r.left = LONG(std::floor(minX));
    r.top = LONG(std::floor(minY));
    r.right = LONG(std::ceil(maxX));
    r.bottom = LONG(std::ceil(maxY));
    return r;
}

void Arm::draw(Gdiplus::Graphics &g) const {
    if (hidden) {
        return;
    }
    if (laser_) {
        drawLaser(g);
    } else {
        drawHand(g);
    }
}

void Arm::drawHand(Gdiplus::Graphics &g) const {
    if (!asset_ || !display_) {
        return;
    }
    const HandAsset &a = *asset_;
    const float dw = float(display_->GetWidth()), dh = float(display_->GetHeight());
    const float wPts = pose_.widthPts, rodLen = pose_.rodLen, handPts = pose_.handPts;
    const float x0 = -a.rodX * wPts;
    const float degrees = -pose_.rotation * 180.0f / float(th::kPi);

    Gdiplus::GraphicsState outer = g.Save();
    g.SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    if (prefs::get().showShadow && shadow_) {
        const float P = float(shadowPad_);
        const float sx = wPts / dw;
        const float handScale = handPts / ((a.wrist - a.handTop) * dh);
        const float rodScale = rodLen / std::max(1.0f, (a.rodEnd - a.wrist) * dh);
        Gdiplus::GraphicsState beforeShadow = g.Save();
        g.TranslateTransform(pose_.gripX + pose_.lift * 0.6f, pose_.gripY + pose_.lift);
        g.RotateTransform(degrees);
        g.DrawImage(shadow_.get(),
                    Gdiplus::RectF(x0 - P * sx, -(rodLen + handPts) - P * handScale, wPts + 2 * P * sx,
                                   handPts + P * handScale),
                    0, a.handTop * dh, dw + 2 * P, (a.wrist - a.handTop) * dh + P, Gdiplus::UnitPixel);
        g.DrawImage(shadow_.get(), Gdiplus::RectF(x0 - P * sx, -rodLen, wPts + 2 * P * sx, rodLen + P * rodScale), 0,
                    a.wrist * dh + P, dw + 2 * P, (a.rodEnd - a.wrist) * dh + P, Gdiplus::UnitPixel);
        g.Restore(beforeShadow);
    }

    g.TranslateTransform(pose_.gripX, pose_.gripY);
    g.RotateTransform(degrees);
    g.DrawImage(display_.get(), Gdiplus::RectF(x0, -rodLen, wPts, rodLen), 0, a.wrist * dh, dw,
                (a.rodEnd - a.wrist) * dh, Gdiplus::UnitPixel);
    g.DrawImage(display_.get(), Gdiplus::RectF(x0, -(rodLen + handPts), wPts, handPts), 0, a.handTop * dh, dw,
                (a.wrist - a.handTop) * dh, Gdiplus::UnitPixel);
    g.Restore(outer);
}

void Arm::drawLaser(Gdiplus::Graphics &g) const {
    const float r = pose_.laserR;
    const float x = pose_.cursorX, y = pose_.cursorY;
    const float glow = r + r * (2.2f + pose_.press) * 1.2f;
    Gdiplus::GraphicsPath halo;
    halo.AddEllipse(x - glow, y - glow, 2 * glow, 2 * glow);
    Gdiplus::PathGradientBrush brush(&halo);
    brush.SetCenterPoint(Gdiplus::PointF(x, y));
    brush.SetCenterColor(th::rgba(kInkRed, 0.85f));
    Gdiplus::Color edge = th::rgba(kInkRed, 0.0f);
    INT count = 1;
    brush.SetSurroundColors(&edge, &count);
    brush.SetFocusScales(r / glow, r / glow);
    g.FillPath(&brush, &halo);
    Gdiplus::SolidBrush red(th::rgba(kInkRed));
    g.FillEllipse(&red, x - r, y - r, 2 * r, 2 * r);
    float core = r * 0.45f;
    Gdiplus::SolidBrush light(Gdiplus::Color(242, 255, 219, 209));
    g.FillEllipse(&light, x - core, y - core, 2 * core, 2 * core);
}
