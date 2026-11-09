#include "common.h"

#include <cstdarg>
#include <cstdio>
#include <shlobj.h>
#include <shlwapi.h>

namespace th {

double now() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart) / double(freq.QuadPart);
}

std::wstring exe_path() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    return std::wstring(buf, n);
}

std::wstring exe_dir() {
    std::wstring p = exe_path();
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}

std::wstring data_dir() {
    wchar_t *base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)) && base) {
        dir = std::wstring(base) + L"\\TinyHand";
    } else {
        dir = exe_dir();
    }
    if (base) {
        CoTaskMemFree(base);
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

bool file_exists(const std::wstring &path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

void log(const wchar_t *fmt, ...) {
    static std::wstring path = data_dir() + L"\\TinyHand.log";
    wchar_t msg[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf(msg, std::size(msg) - 1, fmt, args);
    va_end(args);
    msg[std::size(msg) - 1] = 0;

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[1200];
    int n = _snwprintf(line, std::size(line) - 1, L"%04d-%02d-%02d %02d:%02d:%02d.%03d [%lu] %ls\r\n", st.wYear,
                       st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                       GetCurrentProcessId(), msg);
    if (n < 0) {
        return;
    }
    char utf8[2400];
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, n, utf8, sizeof(utf8), nullptr, nullptr);
    HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(f, utf8, DWORD(bytes), &written, nullptr);
        CloseHandle(f);
    }
}

bool apps_use_dark() {
    DWORD v = 1, size = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size) == ERROR_SUCCESS) {
        return v == 0;
    }
    return false;
}

float dpi_scale_at(POINT pt) {
    using GetDpiForMonitorFn = HRESULT(WINAPI *)(HMONITOR, int, UINT *, UINT *);
    static GetDpiForMonitorFn fn = [] {
        HMODULE shcore = LoadLibraryW(L"shcore.dll");
        return shcore ? reinterpret_cast<GetDpiForMonitorFn>(
                            reinterpret_cast<void *>(GetProcAddress(shcore, "GetDpiForMonitor")))
                      : nullptr;
    }();
    if (fn) {
        UINT x = 96, y = 96;
        if (SUCCEEDED(fn(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), 0, &x, &y)) && y > 0) {
            return float(y) / 96.0f;
        }
    }
    HDC dc = GetDC(nullptr);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    return dpi > 0 ? float(dpi) / 96.0f : 1.0f;
}

float dpi_scale_window(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI *)(HWND);
    static GetDpiForWindowFn fn = [] {
        HMODULE user = GetModuleHandleW(L"user32.dll");
        return user ? reinterpret_cast<GetDpiForWindowFn>(
                          reinterpret_cast<void *>(GetProcAddress(user, "GetDpiForWindow")))
                    : nullptr;
    }();
    if (fn && hwnd) {
        UINT dpi = fn(hwnd);
        if (dpi > 0) {
            return float(dpi) / 96.0f;
        }
    }
    POINT pt{0, 0};
    return dpi_scale_at(pt);
}

Gdiplus::Color rgba(uint32_t rgb, float alpha) {
    BYTE a = BYTE(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    return Gdiplus::Color(a, BYTE((rgb >> 16) & 0xFF), BYTE((rgb >> 8) & 0xFF), BYTE(rgb & 0xFF));
}

Gdiplus::Color white(float alpha) { return rgba(0xFFFFFF, alpha); }
Gdiplus::Color black(float alpha) { return rgba(0x000000, alpha); }

Gdiplus::Bitmap *new_pargb(int width, int height) {
    auto *b = new Gdiplus::Bitmap(std::max(1, width), std::max(1, height), PixelFormat32bppPARGB);
    if (b->GetLastStatus() != Gdiplus::Ok) {
        delete b;
        return nullptr;
    }
    return b;
}

static Gdiplus::Bitmap *decode(const void *data, DWORD size) {
    IStream *stream = SHCreateMemStream(static_cast<const BYTE *>(data), size);
    if (!stream) {
        return nullptr;
    }
    Gdiplus::Bitmap *decoded = Gdiplus::Bitmap::FromStream(stream);
    Gdiplus::Bitmap *copy = nullptr;
    if (decoded && decoded->GetLastStatus() == Gdiplus::Ok) {
        int w = int(decoded->GetWidth());
        int h = int(decoded->GetHeight());
        copy = new_pargb(w, h);
        if (copy) {
            Gdiplus::Graphics g(copy);
            g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
            g.DrawImage(decoded, Gdiplus::Rect(0, 0, w, h), 0, 0, w, h, Gdiplus::UnitPixel);
        }
    }
    delete decoded;
    stream->Release();
    return copy;
}

Gdiplus::Bitmap *load_png_file(const std::wstring &path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        return nullptr;
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    std::vector<uint8_t> data(size_t(size.QuadPart));
    DWORD read = 0;
    BOOL ok = ReadFile(f, data.data(), DWORD(data.size()), &read, nullptr);
    CloseHandle(f);
    if (!ok || read != data.size()) {
        return nullptr;
    }
    return decode(data.data(), read);
}

const void *resource_bytes(int id, DWORD *size) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));
    if (!r) {
        return nullptr;
    }
    HGLOBAL h = LoadResource(nullptr, r);
    if (!h) {
        return nullptr;
    }
    if (size) {
        *size = SizeofResource(nullptr, r);
    }
    return LockResource(h);
}

Gdiplus::Bitmap *load_png_resource(int id) {
    DWORD size = 0;
    const void *data = resource_bytes(id, &size);
    return data ? decode(data, size) : nullptr;
}

bool save_png(Gdiplus::Bitmap *bitmap, const std::wstring &path) {
    UINT count = 0, bytes = 0;
    Gdiplus::GetImageEncodersSize(&count, &bytes);
    if (!bytes) {
        return false;
    }
    std::vector<uint8_t> buf(bytes);
    auto *codecs = reinterpret_cast<Gdiplus::ImageCodecInfo *>(buf.data());
    Gdiplus::GetImageEncoders(count, bytes, codecs);
    for (UINT i = 0; i < count; i++) {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0) {
            return bitmap->Save(path.c_str(), &codecs[i].Clsid, nullptr) == Gdiplus::Ok;
        }
    }
    return false;
}

void add_rounded_rect(Gdiplus::GraphicsPath &path, const Gdiplus::RectF &r, float radius) {
    float d = std::min(radius * 2, std::min(r.Width, r.Height));
    path.StartFigure();
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();
}

Surface::~Surface() {
    bitmap.reset();
    if (dc) {
        SelectObject(dc, old);
        DeleteDC(dc);
    }
    if (dib) {
        DeleteObject(dib);
    }
}

bool Surface::resize(int w, int h) {
    if (w == width && h == height && dib) {
        return true;
    }
    bitmap.reset();
    if (dc) {
        SelectObject(dc, old);
        DeleteDC(dc);
        dc = nullptr;
    }
    if (dib) {
        DeleteObject(dib);
        dib = nullptr;
    }
    width = std::max(1, w);
    height = std::max(1, h);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *raw = nullptr;
    dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &raw, nullptr, 0);
    if (!dib) {
        width = height = 0;
        return false;
    }
    bits = static_cast<uint8_t *>(raw);
    dc = CreateCompatibleDC(nullptr);
    old = SelectObject(dc, dib);
    bitmap = std::make_unique<Gdiplus::Bitmap>(width, height, width * 4, PixelFormat32bppPARGB, bits);
    clear();
    return true;
}

void Surface::clear() {
    if (bits) {
        memset(bits, 0, size_t(width) * size_t(height) * 4);
    }
}

void Surface::clear(const RECT &r) {
    int x0 = std::max(0, int(r.left));
    int x1 = std::min(width, int(r.right));
    int y0 = std::max(0, int(r.top));
    int y1 = std::min(height, int(r.bottom));
    if (!bits || x1 <= x0 || y1 <= y0) {
        return;
    }
    for (int y = y0; y < y1; y++) {
        memset(bits + (size_t(y) * width + x0) * 4, 0, size_t(x1 - x0) * 4);
    }
}

bool Surface::present(HWND hwnd, POINT origin, BYTE alpha, const RECT *dirty) {
    if (!dc) {
        return false;
    }
    GdiFlush();
    SIZE size{width, height};
    POINT src{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
    if (dirty) {
        RECT clipped = *dirty;
        clipped.left = std::max(0L, clipped.left);
        clipped.top = std::max(0L, clipped.top);
        clipped.right = std::min(LONG(width), clipped.right);
        clipped.bottom = std::min(LONG(height), clipped.bottom);
        if (clipped.right <= clipped.left || clipped.bottom <= clipped.top) {
            return true;
        }
        UPDATELAYEREDWINDOWINFO info{};
        info.cbSize = sizeof(info);
        info.pptDst = &origin;
        info.psize = &size;
        info.hdcSrc = dc;
        info.pptSrc = &src;
        info.pblend = &blend;
        info.dwFlags = ULW_ALPHA;
        info.prcDirty = &clipped;
        if (UpdateLayeredWindowIndirect(hwnd, &info)) {
            return true;
        }
    }
    return UpdateLayeredWindow(hwnd, nullptr, &origin, &size, dc, &src, 0, &blend, ULW_ALPHA) != FALSE;
}

} // namespace th
