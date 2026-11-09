#include "hands.h"

#include "prefs.h"

#include <map>

namespace hands {

static std::map<std::wstring, std::shared_ptr<HandAsset>> cache;
static std::vector<std::wstring> recent;
static std::map<std::wstring, std::unique_ptr<Gdiplus::Bitmap>> thumbs;
static SRWLOCK thumbsLock = SRWLOCK_INIT;

struct SharedLock {
    SharedLock() { AcquireSRWLockExclusive(&thumbsLock); }
    ~SharedLock() { ReleaseSRWLockExclusive(&thumbsLock); }
};

std::wstring asset_dir() {
    static std::wstring dir = [] {
        std::wstring exe = th::exe_dir();
        const wchar_t *candidates[] = {L"\\assets", L"\\..\\assets", L"\\..\\..\\assets"};
        for (const wchar_t *c : candidates) {
            std::wstring d = exe + c;
            if (th::file_exists(d + L"\\hand_lavender.png")) {
                return d;
            }
        }
        if (th::file_exists(L"assets\\hand_lavender.png")) {
            return std::wstring(L"assets");
        }
        return exe + L"\\assets";
    }();
    return dir;
}

static std::unique_ptr<HandAsset> measure(Gdiplus::Bitmap *image) {
    int w = int(image->GetWidth());
    int h = int(image->GetHeight());
    Gdiplus::BitmapData data{};
    Gdiplus::Rect all(0, 0, w, h);
    if (image->LockBits(&all, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        return nullptr;
    }
    std::vector<uint8_t> alpha(size_t(w) * size_t(h));
    for (int y = 0; y < h; y++) {
        const uint8_t *row = static_cast<const uint8_t *>(data.Scan0) + ptrdiff_t(y) * data.Stride;
        for (int x = 0; x < w; x++) {
            alpha[size_t(y) * w + x] = row[x * 4 + 3];
        }
    }
    image->UnlockBits(&data);

    std::vector<int> counts(h, 0);
    for (int y = 0; y < h; y++) {
        int n = 0;
        for (int x = 0; x < w; x++) {
            n += alpha[size_t(y) * w + x] > 128;
        }
        counts[y] = n;
    }
    auto center = [&](int y) {
        int lo = -1, hi = -1;
        for (int x = 0; x < w; x++) {
            if (alpha[size_t(y) * w + x] > 128) {
                if (lo < 0) {
                    lo = x;
                }
                hi = x;
            }
        }
        return lo < 0 ? 0.5f : float(lo + hi) / 2.0f / float(w);
    };

    int top = -1;
    for (int y = 0; y < h; y++) {
        if (counts[y] > 0) {
            top = y;
            break;
        }
    }
    if (top < 0) {
        return nullptr;
    }
    int searchEnd = std::min(h, top + int(0.45 * h));
    int maxW = 0;
    for (int y = top; y < searchEnd; y++) {
        maxW = std::max(maxW, counts[y]);
    }
    if (maxW <= 0) {
        return nullptr;
    }
    int fist = -1;
    for (int y = top; y < h; y++) {
        if (counts[y] > 0.6 * maxW) {
            fist = y;
            break;
        }
    }
    if (fist < 0) {
        return nullptr;
    }
    int wrist = -1;
    for (int y = fist; y < h; y++) {
        if (counts[y] < 0.35 * maxW) {
            wrist = y;
            break;
        }
    }
    if (wrist < 0) {
        return nullptr;
    }
    int rodW = counts[std::min(h - 1, wrist + h / 50)];
    int lastRow = h - 1;
    for (int y = h - 1; y >= 0; y--) {
        if (counts[y] > 0) {
            lastRow = y;
            break;
        }
    }
    int from = std::min(h - 1, wrist + h / 33);
    int rodEnd = lastRow;
    for (int y = from; y < h; y++) {
        if (counts[y] > 1.8 * rodW) {
            rodEnd = y;
            break;
        }
    }
    int minX = w, maxX = 0;
    for (int y = top; y < wrist; y++) {
        for (int x = 0; x < w; x++) {
            if (alpha[size_t(y) * w + x] > 128) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
            }
        }
    }

    auto a = std::make_unique<HandAsset>();
    float fh = float(h);
    a->width = w;
    a->height = h;
    a->aspect = float(w) / fh;
    a->handTop = std::max(0.0f, float(top) - 0.004f * fh) / fh;
    a->tip = float(top) / fh;
    a->wrist = float(wrist) / fh;
    a->rodEnd = float(rodEnd) / fh;
    a->tipX = center(std::min(h - 1, top + h / 200));
    a->rodX = center((wrist + rodEnd) / 2);
    a->handMinX = float(minX) / float(w);
    a->handMaxX = float(maxX) / float(w);
    return a;
}

static std::unique_ptr<HandAsset> read(const std::wstring &style) {
    if (style_is_laser(style)) {
        return nullptr;
    }
    std::wstring path = asset_dir() + L"\\hand_" + style + L".png";
    Gdiplus::Bitmap *image = th::load_png_file(path);
    if (!image) {
        th::log(L"missing hand picture %ls", path.c_str());
        return nullptr;
    }
    auto asset = measure(image);
    if (!asset) {
        delete image;
        th::log(L"could not measure %ls", path.c_str());
        return nullptr;
    }
    asset->image.reset(image);
    return asset;
}

std::shared_ptr<HandAsset> load(const std::wstring &style) {
    auto hit = cache.find(style);
    if (hit == cache.end()) {
        std::unique_ptr<HandAsset> fresh = read(style);
        if (!fresh) {
            return nullptr;
        }
        hit = cache.emplace(style, std::shared_ptr<HandAsset>(std::move(fresh))).first;
    }
    recent.erase(std::remove(recent.begin(), recent.end(), style), recent.end());
    recent.push_back(style);
    while (recent.size() > 2) {
        cache.erase(recent.front());
        recent.erase(recent.begin());
    }
    return hit->second;
}

Gdiplus::Bitmap *resized(Gdiplus::Bitmap *image, int height) {
    int w = std::max(1, int(std::lround(double(image->GetWidth()) * height / double(image->GetHeight()))));
    Gdiplus::Bitmap *out = th::new_pargb(w, height);
    if (!out) {
        return nullptr;
    }
    Gdiplus::Graphics g(out);
    g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::ImageAttributes attrs;
    attrs.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
    g.DrawImage(image, Gdiplus::Rect(0, 0, w, height), 0, 0, INT(image->GetWidth()), INT(image->GetHeight()),
                Gdiplus::UnitPixel, &attrs);
    return out;
}

static Gdiplus::Bitmap *make_thumbnail(const std::wstring &style) {
    std::unique_ptr<HandAsset> a = read(style);
    if (!a) {
        return nullptr;
    }
    float w = float(a->width), h = float(a->height);
    float handW = (a->handMaxX - a->handMinX) * w;
    float handH = (a->wrist - a->handTop) * h;
    float side = std::max(handW, handH) * 1.06f;
    float cx = (a->handMinX + a->handMaxX) / 2 * w;
    float cy = (a->handTop + a->wrist) / 2 * h;
    float x0 = std::max(0.0f, cx - side / 2), y0 = std::max(0.0f, cy - side / 2);
    float x1 = std::min(w, cx + side / 2), y1 = std::min(h, cy + side / 2);
    const int px = 128;
    Gdiplus::Bitmap *thumb = th::new_pargb(px, px);
    if (!thumb) {
        return nullptr;
    }
    Gdiplus::Graphics g(thumb);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.DrawImage(a->image.get(), Gdiplus::RectF(0, 0, px, px), x0, y0, x1 - x0, y1 - y0, Gdiplus::UnitPixel);
    return thumb;
}

Gdiplus::Bitmap *cached_thumbnail(const std::wstring &style) {
    SharedLock lock;
    auto hit = thumbs.find(style);
    return hit == thumbs.end() ? nullptr : hit->second.get();
}

Gdiplus::Bitmap *thumbnail(const std::wstring &style) {
    if (Gdiplus::Bitmap *hit = cached_thumbnail(style)) {
        return hit;
    }
    Gdiplus::Bitmap *made = make_thumbnail(style);
    if (!made) {
        return nullptr;
    }
    SharedLock lock;
    auto &slot = thumbs[style];
    if (!slot) {
        slot.reset(made);
    } else {
        delete made;
    }
    return slot.get();
}

struct PreloadJob {
    std::vector<std::wstring> styles;
    HWND notify;
    UINT msg;
};

static DWORD WINAPI preload_thread(LPVOID param) {
    std::unique_ptr<PreloadJob> job(static_cast<PreloadJob *>(param));
    for (size_t i = 0; i < job->styles.size(); i++) {
        thumbnail(job->styles[i]);
        if (job->notify && (i % 6 == 5 || i + 1 == job->styles.size())) {
            PostMessageW(job->notify, job->msg, 0, 0);
        }
    }
    return 0;
}

void preload_thumbnails(const std::vector<std::wstring> &styles, HWND notify, UINT msg) {
    auto job = std::make_unique<PreloadJob>();
    for (const auto &s : styles) {
        if (!style_is_laser(s) && !cached_thumbnail(s)) {
            job->styles.push_back(s);
        }
    }
    if (job->styles.empty()) {
        return;
    }
    job->notify = notify;
    job->msg = msg;
    HANDLE t = CreateThread(nullptr, 0, preload_thread, job.get(), 0, nullptr);
    if (t) {
        job.release();
        CloseHandle(t);
    }
}

} // namespace hands
