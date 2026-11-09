#pragma once

#include "common.h"

// One hand-on-a-stick picture plus the landmarks the arm needs, measured from
// its alpha channel the same way the macOS app does (HandAsset.measure).
// All landmarks are fractions of the picture: y from the top, x from the left.
struct HandAsset {
    std::unique_ptr<Gdiplus::Bitmap> image;
    int width = 0;
    int height = 0;
    float aspect = 0;
    float handTop = 0;
    float tip = 0;
    float wrist = 0;
    float rodEnd = 0;
    float tipX = 0;
    float rodX = 0;
    float handMinX = 0;
    float handMaxX = 0;
};

namespace hands {

std::shared_ptr<HandAsset> load(const std::wstring &style);
// 128 px square crop of the hand, cached for the toolbar and settings.
Gdiplus::Bitmap *thumbnail(const std::wstring &style);
// The thumbnail if it is ready, without decoding anything.
Gdiplus::Bitmap *cached_thumbnail(const std::wstring &style);
// Builds missing thumbnails on a worker thread, then posts msg to notify.
void preload_thumbnails(const std::vector<std::wstring> &styles, HWND notify, UINT msg);
Gdiplus::Bitmap *resized(Gdiplus::Bitmap *image, int height);
std::wstring asset_dir();

} // namespace hands
