#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}
#include <gdiplus.h>

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "shapekit.h"
#include "resource.h"

enum : UINT {
    WM_TH_MOUSEDOWN = WM_APP + 1,
    WM_TH_MOUSEUP,
    WM_TH_DRAG,
    WM_TH_INTERACT_DOWN,
    WM_TH_INTERACT_UP,
    WM_TH_PASSTHROUGH,
    WM_TH_WAKE,
    WM_TH_TRAY,
    WM_TH_SHOW_SETTINGS,
    WM_TH_THUMBS,
};

namespace th {

constexpr double kPi = 3.14159265358979323846;

double now();
void log(const wchar_t *fmt, ...);
std::wstring exe_path();
std::wstring exe_dir();
std::wstring data_dir();
bool file_exists(const std::wstring &path);

bool apps_use_dark();
float dpi_scale_at(POINT pt);
float dpi_scale_window(HWND hwnd);

Gdiplus::Color rgba(uint32_t rgb, float alpha = 1.0f);
Gdiplus::Color white(float alpha);
Gdiplus::Color black(float alpha);

Gdiplus::Bitmap *load_png_file(const std::wstring &path);
Gdiplus::Bitmap *load_png_resource(int id);
Gdiplus::Bitmap *new_pargb(int width, int height);
bool save_png(Gdiplus::Bitmap *bitmap, const std::wstring &path);
const void *resource_bytes(int id, DWORD *size);

void add_rounded_rect(Gdiplus::GraphicsPath &path, const Gdiplus::RectF &r, float radius);

// A 32-bit premultiplied DIB that GDI+ draws into and UpdateLayeredWindow
// reads from, so layered windows need no extra copies.
struct Surface {
    HDC dc = nullptr;
    HBITMAP dib = nullptr;
    HGDIOBJ old = nullptr;
    uint8_t *bits = nullptr;
    int width = 0;
    int height = 0;
    std::unique_ptr<Gdiplus::Bitmap> bitmap;

    ~Surface();
    bool resize(int w, int h);
    void clear();
    void clear(const RECT &r);
    bool present(HWND hwnd, POINT origin, BYTE alpha, const RECT *dirty = nullptr);
};

} // namespace th
