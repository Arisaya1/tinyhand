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
#include <objidl.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Gdiplus {
using std::max;
using std::min;
}
#include <gdiplus.h>

namespace custom_hands {

struct Hand {
    std::wstring id;
    std::wstring name;
    uint32_t ink = 0;
};

struct MadeHand {
    std::unique_ptr<Gdiplus::Bitmap> image;
    uint32_t ink = 0;
};

// TH_HANDS_DIR overrides the storage directory (primarily for isolated tests).
// Otherwise files live in %LOCALAPPDATA%\TinyHand\Hands. Metadata is hands.json.
std::vector<Hand> all();

// On failure these return false and, when supplied, set a user-facing error.
// Output parameters are reset before work starts. GDI+ is initialized on demand.
bool make_from_file(const std::wstring &path, MadeHand &out, std::wstring *error = nullptr);
bool add(Gdiplus::Bitmap *image, uint32_t ink, const std::wstring &name, Hand &out,
         std::wstring *error = nullptr);
bool remove(const std::wstring &id, std::wstring *error = nullptr);

std::wstring prompt(const std::wstring &idea = L"");
std::wstring suggested_name(const std::wstring &source_path);

// Empty for an invalid id. Valid ids are exactly custom-[0-9a-f]{8}.
std::wstring asset_path(const std::wstring &id);
bool valid_id(const std::wstring &id);

} // namespace custom_hands
