#include "custom_hands.h"

#include <shlobj.h>
#include <shlwapi.h>

#include <cmath>
#include <cwctype>
#include <limits>
#include <string_view>
#include <unordered_map>

namespace custom_hands {
namespace {

std::wstring trim(const std::wstring &value) {
    size_t first = 0;
    while (first < value.size() && std::iswspace(value[first])) {
        ++first;
    }
    size_t last = value.size();
    while (last > first && std::iswspace(value[last - 1])) {
        --last;
    }
    return value.substr(first, last - first);
}

size_t character_count(const std::wstring &value) {
    size_t count = 0;
    for (size_t i = 0; i < value.size(); ++i, ++count) {
        if (value[i] >= 0xD800 && value[i] <= 0xDBFF && i + 1 < value.size() &&
            value[i + 1] >= 0xDC00 && value[i + 1] <= 0xDFFF) {
            ++i;
        }
    }
    return count;
}

std::wstring limit_name(const std::wstring &value) {
    size_t count = 0;
    size_t end = 0;
    while (end < value.size() && count < 16) {
        if (value[end] >= 0xD800 && value[end] <= 0xDBFF && end + 1 < value.size() &&
            value[end + 1] >= 0xDC00 && value[end + 1] <= 0xDFFF) {
            end += 2;
        } else {
            ++end;
        }
        ++count;
    }
    return value.substr(0, end);
}

std::wstring environment(const wchar_t *name) {
    wchar_t value[32768];
    DWORD n = GetEnvironmentVariableW(name, value, DWORD(std::size(value)));
    return n > 0 && n < DWORD(std::size(value)) ? std::wstring(value, n) : std::wstring();
}

std::wstring directory() {
    std::wstring overridden = environment(L"TH_HANDS_DIR");
    if (!overridden.empty()) {
        return overridden;
    }
    std::wstring base = environment(L"LOCALAPPDATA");
    if (base.empty()) {
        wchar_t *known = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &known)) && known) {
            base = known;
        }
        if (known) {
            CoTaskMemFree(known);
        }
    }
    if (base.empty()) {
        base = L".";
    }
    return base + L"\\TinyHand\\Hands";
}

void set_error(std::wstring *error, const wchar_t *message) {
    if (error) {
        *error = message;
    }
}

bool ensure_directory() {
    int result = SHCreateDirectoryExW(nullptr, directory().c_str(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS;
}

std::wstring new_id() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) {
        return {};
    }
    wchar_t hex[9]{};
    for (int i = 0; i < 4; ++i) {
        swprintf(hex + i * 2, 3, L"%02x", reinterpret_cast<const BYTE *>(&guid)[i]);
    }
    return L"custom-" + std::wstring(hex);
}

bool replace_file(const std::wstring &temporary, const std::wstring &destination) {
    return MoveFileExW(temporary.c_str(), destination.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

std::wstring temporary_path(const wchar_t *extension) {
    std::wstring id = new_id();
    return id.empty() ? std::wstring() : directory() + L"\\." + id + extension;
}

void append_codepoint(std::wstring &out, uint32_t cp) {
    if (cp <= 0xFFFF) {
        out.push_back(wchar_t(cp));
    } else if (cp <= 0x10FFFF) {
        cp -= 0x10000;
        out.push_back(wchar_t(0xD800 + (cp >> 10)));
        out.push_back(wchar_t(0xDC00 + (cp & 0x3FF)));
    }
}

class JsonReader {
  public:
    explicit JsonReader(std::string_view text) : text_(text) {}

    bool array(std::vector<Hand> &hands) {
        whitespace();
        if (!take('[')) {
            return false;
        }
        whitespace();
        if (take(']')) {
            return finished();
        }
        while (true) {
            Hand hand{};
            bool valid = true;
            if (!object(hand, valid)) {
                return false;
            }
            if (valid && valid_id(hand.id)) {
                bool duplicate = false;
                for (const Hand &existing : hands) {
                    duplicate = duplicate || existing.id == hand.id;
                }
                if (!duplicate) {
                    hands.push_back(std::move(hand));
                }
            }
            whitespace();
            if (take(']')) {
                return finished();
            }
            if (!take(',')) {
                return false;
            }
            whitespace();
        }
        return false;
    }

  private:
    std::string_view text_;
    size_t at_ = 0;

    bool finished() {
        whitespace();
        return at_ == text_.size();
    }

    void whitespace() {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' ||
                                      text_[at_] == '\r' || text_[at_] == '\n')) {
            ++at_;
        }
    }

    bool take(char c) {
        if (at_ < text_.size() && text_[at_] == c) {
            ++at_;
            return true;
        }
        return false;
    }

    int hex(char c) const {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool hex4(uint32_t &value) {
        if (text_.size() - at_ < 4) return false;
        value = 0;
        for (int i = 0; i < 4; ++i) {
            int digit = hex(text_[at_++]);
            if (digit < 0) return false;
            value = value * 16 + uint32_t(digit);
        }
        return true;
    }

    bool string(std::wstring &out) {
        out.clear();
        if (!take('"')) return false;
        while (at_ < text_.size()) {
            unsigned char c = static_cast<unsigned char>(text_[at_++]);
            if (c == '"') return true;
            if (c < 0x20) return false;
            if (c == '\\') {
                if (at_ == text_.size()) return false;
                char escaped = text_[at_++];
                switch (escaped) {
                case '"': out.push_back(L'"'); break;
                case '\\': out.push_back(L'\\'); break;
                case '/': out.push_back(L'/'); break;
                case 'b': out.push_back(L'\b'); break;
                case 'f': out.push_back(L'\f'); break;
                case 'n': out.push_back(L'\n'); break;
                case 'r': out.push_back(L'\r'); break;
                case 't': out.push_back(L'\t'); break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (text_.size() - at_ < 6 || text_[at_] != '\\' || text_[at_ + 1] != 'u') {
                            return false;
                        }
                        at_ += 2;
                        uint32_t low = 0;
                        if (!hex4(low) || low < 0xDC00 || low > 0xDFFF) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return false;
                    }
                    append_codepoint(out, cp);
                    break;
                }
                default: return false;
                }
                continue;
            }
            if (c < 0x80) {
                out.push_back(wchar_t(c));
                continue;
            }
            int continuation = c >= 0xC2 && c <= 0xDF ? 1 : c >= 0xE0 && c <= 0xEF ? 2
                               : c >= 0xF0 && c <= 0xF4 ? 3 : -1;
            if (continuation < 0 || text_.size() - at_ < size_t(continuation)) return false;
            uint32_t cp = c & (0x7F >> (continuation + 1));
            for (int i = 0; i < continuation; ++i) {
                unsigned char next = static_cast<unsigned char>(text_[at_++]);
                if ((next & 0xC0) != 0x80) return false;
                cp = (cp << 6) | (next & 0x3F);
            }
            if ((continuation == 2 && cp < 0x800) || (continuation == 3 && cp < 0x10000) ||
                cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
                return false;
            }
            append_codepoint(out, cp);
        }
        return false;
    }

    bool number(uint32_t &value) {
        size_t start = at_;
        value = 0;
        while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') {
            uint32_t digit = uint32_t(text_[at_++] - '0');
            if (value > (std::numeric_limits<uint32_t>::max() - digit) / 10) return false;
            value = value * 10 + digit;
        }
        return at_ > start;
    }

    bool value(int depth = 0) {
        if (depth > 64) return false;
        whitespace();
        if (at_ == text_.size()) return false;
        if (text_[at_] == '"') {
            std::wstring ignored;
            return string(ignored);
        }
        if (take('[')) {
            whitespace();
            if (take(']')) return true;
            while (value(depth + 1)) {
                whitespace();
                if (take(']')) return true;
                if (!take(',')) return false;
            }
            return false;
        }
        if (take('{')) {
            whitespace();
            if (take('}')) return true;
            while (true) {
                std::wstring ignored;
                whitespace();
                if (!string(ignored)) return false;
                whitespace();
                if (!take(':') || !value(depth + 1)) return false;
                whitespace();
                if (take('}')) return true;
                if (!take(',')) return false;
            }
        }
        if (text_.substr(at_, 4) == "true") { at_ += 4; return true; }
        if (text_.substr(at_, 5) == "false") { at_ += 5; return true; }
        if (text_.substr(at_, 4) == "null") { at_ += 4; return true; }
        size_t start = at_;
        take('-');
        if (take('0')) {
            // A leading zero is a complete integer part.
        } else {
            size_t digits = at_;
            while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') ++at_;
            if (digits == at_) return false;
        }
        if (take('.')) {
            size_t digits = at_;
            while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') ++at_;
            if (digits == at_) return false;
        }
        if (at_ < text_.size() && (text_[at_] == 'e' || text_[at_] == 'E')) {
            ++at_;
            if (at_ < text_.size() && (text_[at_] == '+' || text_[at_] == '-')) ++at_;
            size_t digits = at_;
            while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') ++at_;
            if (digits == at_) return false;
        }
        return at_ > start;
    }

    bool object(Hand &hand, bool &valid) {
        if (!take('{')) return false;
        whitespace();
        if (take('}')) { valid = false; return true; }
        bool have_id = false, have_name = false, have_ink = false;
        while (true) {
            std::wstring key;
            whitespace();
            if (!string(key)) return false;
            whitespace();
            if (!take(':')) return false;
            whitespace();
            if (key == L"id" || key == L"name") {
                if (at_ == text_.size() || text_[at_] != '"') {
                    valid = false;
                    if (!value()) return false;
                } else {
                    std::wstring parsed;
                    if (!string(parsed)) return false;
                    std::wstring &target = key == L"id" ? hand.id : hand.name;
                    bool &present = key == L"id" ? have_id : have_name;
                    if (present) valid = false;
                    target = std::move(parsed);
                    present = true;
                }
            } else if (key == L"ink") {
                size_t start = at_;
                uint32_t parsed = 0;
                bool parsed_number = number(parsed);
                if (parsed_number) {
                    size_t end = at_;
                    whitespace();
                    parsed_number = at_ < text_.size() && (text_[at_] == ',' || text_[at_] == '}');
                    at_ = end;
                }
                if (parsed_number) {
                    if (have_ink) valid = false;
                    hand.ink = parsed;
                    have_ink = true;
                } else {
                    valid = false;
                    at_ = start;
                    if (!value()) return false;
                }
            } else if (!value()) {
                return false;
            }
            whitespace();
            if (take('}')) break;
            if (!take(',')) return false;
            whitespace();
        }
        valid = valid && have_id && have_name && have_ink;
        return true;
    }
};

bool load_metadata(std::vector<Hand> &hands) {
    hands.clear();
    std::wstring path = directory() + L"\\hands.json";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return true;
    LARGE_INTEGER size{};
    bool okay = GetFileSizeEx(file, &size) && size.QuadPart >= 0 && size.QuadPart <= 4 * 1024 * 1024;
    std::string bytes;
    if (okay) {
        try {
            bytes.resize(size_t(size.QuadPart));
        } catch (const std::bad_alloc &) {
            okay = false;
        }
    }
    DWORD read = 0;
    if (okay && !bytes.empty() && (!ReadFile(file, bytes.data(), DWORD(bytes.size()), &read, nullptr) ||
                                   read != bytes.size())) {
        okay = false;
    }
    CloseHandle(file);
    if (!okay) return false;
    if (bytes.empty()) return true;
    JsonReader reader(bytes);
    return reader.array(hands);
}

void append_json_string(std::string &out, const std::wstring &value) {
    out.push_back('"');
    for (size_t i = 0; i < value.size(); ++i) {
        wchar_t wc = value[i];
        switch (wc) {
        case L'"': out += "\\\""; break;
        case L'\\': out += "\\\\"; break;
        case L'\b': out += "\\b"; break;
        case L'\f': out += "\\f"; break;
        case L'\n': out += "\\n"; break;
        case L'\r': out += "\\r"; break;
        case L'\t': out += "\\t"; break;
        default:
            if (wc < 0x20) {
                constexpr char digits[] = "0123456789abcdef";
                out += "\\u00";
                out.push_back(digits[(wc >> 4) & 0xF]);
                out.push_back(digits[wc & 0xF]);
            } else {
                int units = 1;
                if (wc >= 0xD800 && wc <= 0xDBFF && i + 1 < value.size() &&
                    value[i + 1] >= 0xDC00 && value[i + 1] <= 0xDFFF) {
                    units = 2;
                } else if (wc >= 0xD800 && wc <= 0xDFFF) {
                    out += "\\ufffd";
                    break;
                }
                const wchar_t *start = value.data() + i;
                int count = WideCharToMultiByte(CP_UTF8, 0, start, units, nullptr, 0, nullptr, nullptr);
                if (count > 0) {
                    char encoded[4]{};
                    WideCharToMultiByte(CP_UTF8, 0, start, units, encoded, count, nullptr, nullptr);
                    out.append(encoded, size_t(count));
                    if (units == 2) ++i;
                }
            }
        }
    }
    out.push_back('"');
}

bool save_metadata(const std::vector<Hand> &hands) {
    std::string json = "[";
    for (size_t i = 0; i < hands.size(); ++i) {
        if (i) json.push_back(',');
        json += "{\"id\":";
        append_json_string(json, hands[i].id);
        json += ",\"name\":";
        append_json_string(json, hands[i].name);
        json += ",\"ink\":" + std::to_string(hands[i].ink) + "}";
    }
    json += "]\n";
    std::wstring temporary = temporary_path(L".json.tmp");
    if (temporary.empty()) return false;
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool okay = json.empty() || (WriteFile(file, json.data(), DWORD(json.size()), &written, nullptr) &&
                                 written == json.size());
    okay = okay && FlushFileBuffers(file);
    okay = CloseHandle(file) && okay;
    if (okay) okay = replace_file(temporary, directory() + L"\\hands.json");
    if (!okay) DeleteFileW(temporary.c_str());
    return okay;
}

bool ensure_gdiplus() {
    static ULONG_PTR token = [] {
        ULONG_PTR value = 0;
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&value, &input, nullptr);
        return value;
    }();
    return token != 0;
}

bool save_png_atomic(Gdiplus::Bitmap *image, const std::wstring &path) {
    if (!ensure_gdiplus()) return false;
    UINT count = 0, bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || !bytes) return false;
    try {
        std::vector<uint8_t> codecs(bytes);
        auto *info = reinterpret_cast<Gdiplus::ImageCodecInfo *>(codecs.data());
        if (Gdiplus::GetImageEncoders(count, bytes, info) != Gdiplus::Ok) return false;
        for (UINT i = 0; i < count; ++i) {
            if (wcscmp(info[i].MimeType, L"image/png") != 0) continue;
            std::wstring temporary = temporary_path(L".png.tmp");
            if (temporary.empty()) return false;
            bool okay = image->Save(temporary.c_str(), &info[i].Clsid, nullptr) == Gdiplus::Ok;
            if (okay) okay = replace_file(temporary, path);
            if (!okay) DeleteFileW(temporary.c_str());
            return okay;
        }
    } catch (const std::bad_alloc &) {
        return false;
    }
    return false;
}

struct StreamCloser {
    void operator()(IStream *stream) const {
        if (stream) stream->Release();
    }
};
using StreamPtr = std::unique_ptr<IStream, StreamCloser>;

struct Pixels {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> bgra;

    uint8_t alpha(int x, int y) const {
        return bgra[(size_t(y) * size_t(width) + size_t(x)) * 4 + 3];
    }
};

struct Bounds {
    int min_x = 0;
    int min_y = 0;
    int max_x = -1;
    int max_y = -1;
};

Bounds find_bounds(const Pixels &pixels) {
    Bounds box{pixels.width, pixels.height, -1, -1};
    for (int y = 0; y < pixels.height; ++y) {
        for (int x = 0; x < pixels.width; ++x) {
            if (pixels.alpha(x, y) <= 128) continue;
            box.min_x = std::min(box.min_x, x);
            box.max_x = std::max(box.max_x, x);
            box.min_y = std::min(box.min_y, y);
            box.max_y = std::max(box.max_y, y);
        }
    }
    return box;
}

struct Span {
    int width = 0;
    double center = 0;
};

Span bottom_span(const Pixels &pixels, const Bounds &box) {
    int height = box.max_y - box.min_y + 1;
    int low = pixels.width;
    int high = -1;
    int start = std::max(box.min_y, box.max_y - std::max(2, height * 4 / 100));
    for (int y = start; y <= box.max_y; ++y) {
        for (int x = box.min_x; x <= box.max_x; ++x) {
            if (pixels.alpha(x, y) <= 128) continue;
            low = std::min(low, x);
            high = std::max(high, x);
        }
    }
    return high < 0 ? Span{0, double(box.min_x + box.max_x) / 2}
                    : Span{high - low + 1, double(low + high) / 2};
}

const wchar_t *check_pixels(const Pixels &pixels) {
    const int w = pixels.width;
    const int h = pixels.height;
    int edge = 0;
    int edge_solid = 0;
    for (int x = 0; x < w; ++x) {
        edge += 2;
        edge_solid += (pixels.alpha(x, 0) > 32 ? 1 : 0) + (pixels.alpha(x, h - 1) > 32 ? 1 : 0);
    }
    for (int y = 0; y < h; ++y) {
        edge += 2;
        edge_solid += (pixels.alpha(0, y) > 32 ? 1 : 0) + (pixels.alpha(w - 1, y) > 32 ? 1 : 0);
    }
    if (double(edge_solid) / double(edge) > 0.02) {
        return L"The background isn't transparent, or the hand touches the edge of the image.";
    }

    int step = std::max(1, std::min(w, h) / 256);
    int gw = w / step;
    int gh = h / step;
    std::vector<bool> grid(size_t(gw) * size_t(gh), false);
    for (int gy = 0; gy < gh; ++gy) {
        for (int gx = 0; gx < gw; ++gx) {
            if (pixels.alpha(gx * step, gy * step) > 128) grid[size_t(gy) * gw + gx] = true;
        }
    }
    std::vector<bool> seen(grid.size(), false);
    std::vector<int> sizes;
    std::vector<int> stack;
    for (int start = 0; start < int(grid.size()); ++start) {
        if (!grid[start] || seen[start]) continue;
        stack.assign(1, start);
        seen[start] = true;
        int count = 0;
        while (!stack.empty()) {
            int index = stack.back();
            stack.pop_back();
            ++count;
            int x = index % gw;
            int y = index / gw;
            const int nx[4] = {x + 1, x - 1, x, x};
            const int ny[4] = {y, y, y + 1, y - 1};
            for (int n = 0; n < 4; ++n) {
                if (nx[n] < 0 || ny[n] < 0 || nx[n] >= gw || ny[n] >= gh) continue;
                int next = ny[n] * gw + nx[n];
                if (grid[next] && !seen[next]) {
                    seen[next] = true;
                    stack.push_back(next);
                }
            }
        }
        sizes.push_back(count);
    }
    if (sizes.empty()) return L"The image is empty.";
    int biggest = 0;
    int total = 0;
    for (int size : sizes) {
        biggest = std::max(biggest, size);
        total += size;
    }
    if (double(biggest) / double(total) < 0.97) {
        return L"The image has more than one thing in it. Use a single hand.";
    }

    Bounds box = find_bounds(pixels);
    if (box.max_x < 0) return L"The image is empty.";
    int bw = box.max_x - box.min_x + 1;
    int bh = box.max_y - box.min_y + 1;
    double aspect = double(bh) / double(bw);
    if (aspect < 1.05 || aspect > 2.8) {
        return L"The hand should stand upright, taller than it is wide.";
    }
    if (double(bh) / double(h) < 0.5) {
        return L"The hand is too small in the image. Crop it closer.";
    }

    int top_band = std::max(2, bh * 8 / 100);
    int finger_min = w;
    int finger_max = 0;
    for (int y = box.min_y; y < box.min_y + top_band; ++y) {
        for (int x = box.min_x; x <= box.max_x; ++x) {
            if (pixels.alpha(x, y) <= 128) continue;
            finger_min = std::min(finger_min, x);
            finger_max = std::max(finger_max, x);
        }
    }
    if (double(finger_max - finger_min + 1) / double(bw) > 0.4) {
        return L"The top of the image should be a single finger pointing up.";
    }
    if (double(bottom_span(pixels, box).width) / double(bw) < 0.2) {
        return L"The hand should end in a flat wrist or cuff at the bottom.";
    }

    int soft = 0;
    int any = 0;
    for (int y = 0; y < h; y += 2) {
        for (int x = 0; x < w; x += 2) {
            uint8_t alpha = pixels.alpha(x, y);
            if (alpha > 20) {
                ++any;
                if (alpha < 200) ++soft;
            }
        }
    }
    if (double(soft) / double(std::max(1, any)) > 0.08) {
        return L"The hand has a soft shadow or glow around it. Ask for no shadow.";
    }
    return nullptr;
}

bool read_image(const std::wstring &path, std::unique_ptr<Gdiplus::Bitmap> &image, Pixels &pixels,
                std::wstring *error) {
    if (!ensure_gdiplus()) {
        set_error(error, L"TinyHand couldn't start its image reader.");
        return false;
    }
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        set_error(error, L"That file isn't an image TinyHand can read.");
        return false;
    }
    LARGE_INTEGER file_size{};
    bool okay = GetFileSizeEx(file, &file_size) && file_size.QuadPart > 0 &&
                file_size.QuadPart <= 32ll * 1024 * 1024;
    std::vector<uint8_t> data;
    uint8_t header[24]{};
    DWORD header_read = 0;
    if (okay && (!ReadFile(file, header, DWORD(std::size(header)), &header_read, nullptr))) {
        okay = false;
    }
    const uint8_t png_signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    bool is_png = header_read >= 8 && std::equal(std::begin(png_signature), std::end(png_signature), header);
    if (okay && is_png && header_read < 24) {
        okay = false;
    }
    if (okay && is_png) {
        auto be32 = [&](size_t offset) {
            return (uint64_t(header[offset]) << 24) | (uint64_t(header[offset + 1]) << 16) |
                   (uint64_t(header[offset + 2]) << 8) | uint64_t(header[offset + 3]);
        };
        uint64_t width = be32(16);
        uint64_t height = be32(20);
        if (width < 64 || height < 64) {
            CloseHandle(file);
            set_error(error, L"The image is too small. Use one at least 512 pixels tall.");
            return false;
        }
        if (width > 4096 || height > 4096 || width * height > 16ull * 1024 * 1024) {
            CloseHandle(file);
            set_error(error, L"The image is too large. Use at most 4096 by 4096 pixels and 16 million pixels.");
            return false;
        }
    }
    if (okay && SetFilePointer(file, 0, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER &&
                   GetLastError() != NO_ERROR) {
        okay = false;
    }
    if (okay) {
        try {
            data.resize(size_t(file_size.QuadPart));
        } catch (const std::bad_alloc &) {
            okay = false;
        }
    }
    DWORD count = 0;
    if (okay && (!ReadFile(file, data.data(), DWORD(data.size()), &count, nullptr) || count != data.size())) {
        okay = false;
    }
    CloseHandle(file);
    if (!okay) {
        set_error(error, file_size.QuadPart > 32ll * 1024 * 1024
                             ? L"The image file is too large. Use a file no bigger than 32 MiB."
                             : L"That file isn't an image TinyHand can read.");
        return false;
    }
    StreamPtr stream(SHCreateMemStream(data.data(), DWORD(data.size())));
    if (!stream) {
        set_error(error, L"That file isn't an image TinyHand can read.");
        return false;
    }
    std::unique_ptr<Gdiplus::Bitmap> decoded(Gdiplus::Bitmap::FromStream(stream.get(), TRUE));
    if (!decoded || decoded->GetLastStatus() != Gdiplus::Ok) {
        set_error(error, L"That file isn't an image TinyHand can read.");
        return false;
    }
    Gdiplus::PixelFormat format = decoded->GetPixelFormat();
    if ((format & (PixelFormatAlpha | PixelFormatPAlpha)) == 0) {
        set_error(error, L"The image needs a transparent background. Save it as a PNG with transparency.");
        return false;
    }
    uint64_t uw = decoded->GetWidth();
    uint64_t uh = decoded->GetHeight();
    if (uw < 64 || uh < 64) {
        set_error(error, L"The image is too small. Use one at least 512 pixels tall.");
        return false;
    }
    if (uw > 4096 || uh > 4096 || uw * uh > 16ull * 1024 * 1024) {
        set_error(error, L"The image is too large. Use at most 4096 by 4096 pixels and 16 million pixels.");
        return false;
    }
    int w = int(uw);
    int h = int(uh);
    auto normalized = std::make_unique<Gdiplus::Bitmap>(w, h, PixelFormat32bppPARGB);
    if (!normalized || normalized->GetLastStatus() != Gdiplus::Ok) {
        set_error(error, L"That file isn't an image TinyHand can read.");
        return false;
    }
    {
        Gdiplus::Graphics graphics(normalized.get());
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
        if (graphics.DrawImage(decoded.get(), Gdiplus::Rect(0, 0, w, h), 0, 0, w, h,
                               Gdiplus::UnitPixel) != Gdiplus::Ok) {
            set_error(error, L"That file isn't an image TinyHand can read.");
            return false;
        }
    }
    decoded.reset();
    Gdiplus::BitmapData locked{};
    Gdiplus::Rect area(0, 0, w, h);
    if (normalized->LockBits(&area, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &locked) != Gdiplus::Ok ||
        !locked.Scan0 || std::abs(locked.Stride) < w * 4) {
        set_error(error, L"That file isn't an image TinyHand can read.");
        return false;
    }
    try {
        pixels.width = w;
        pixels.height = h;
        pixels.bgra.resize(size_t(w) * size_t(h) * 4);
    } catch (const std::bad_alloc &) {
        normalized->UnlockBits(&locked);
        set_error(error, L"There isn't enough memory to read that image.");
        return false;
    }
    for (int y = 0; y < h; ++y) {
        const auto *row = static_cast<const uint8_t *>(locked.Scan0) + ptrdiff_t(y) * locked.Stride;
        std::copy(row, row + w * 4, pixels.bgra.begin() + ptrdiff_t(y) * w * 4);
    }
    normalized->UnlockBits(&locked);
    image = std::move(normalized);
    return true;
}

void rounded_rectangle(Gdiplus::GraphicsPath &path, const Gdiplus::RectF &rect) {
    float diameter = std::min(rect.Width, rect.Height);
    path.StartFigure();
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270, 90);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter, diameter, diameter, 0, 90);
    path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90, 90);
    path.CloseFigure();
}

std::unique_ptr<Gdiplus::Bitmap> compose(Gdiplus::Bitmap &image, const Pixels &pixels) {
    Bounds box = find_bounds(pixels);
    if (box.max_x < 0) return nullptr;
    constexpr int canvas_w = 1024;
    constexpr int canvas_h = 1536;
    constexpr float hand_top = 150;
    constexpr float hand_height = 560;
    float bw = float(box.max_x - box.min_x + 1);
    float bh = float(box.max_y - box.min_y + 1);
    float scale = std::min(hand_height / bh, 700.0f / bw);
    float draw_w = float(pixels.width) * scale;
    float draw_h = float(pixels.height) * scale;
    float wrist_x = float((bottom_span(pixels, box).center + 0.5) * scale);
    float origin_x = float(canvas_w) / 2 - wrist_x;
    float origin_top = hand_top - float(box.min_y) * scale;
    float hand_bottom = hand_top + bh * scale;

    auto canvas = std::make_unique<Gdiplus::Bitmap>(canvas_w, canvas_h, PixelFormat32bppPARGB);
    if (!canvas || canvas->GetLastStatus() != Gdiplus::Ok) return nullptr;
    Gdiplus::Graphics graphics(canvas.get());
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    float rod_w = std::min(76.0f, std::max(54.0f, bw * scale * 0.18f));
    float rod_top = hand_bottom - 30;
    constexpr float rod_bottom = 1462;
    Gdiplus::RectF rod(float(canvas_w) / 2 - rod_w / 2, rod_top, rod_w, rod_bottom - rod_top);
    Gdiplus::GraphicsPath clip;
    rounded_rectangle(clip, rod);
    Gdiplus::GraphicsState state = graphics.Save();
    graphics.SetClip(&clip);
    Gdiplus::LinearGradientBrush gradient(rod, Gdiplus::Color(255, 0, 0, 0),
                                          Gdiplus::Color(255, 0, 0, 0),
                                         Gdiplus::LinearGradientModeHorizontal);
    const Gdiplus::Color colors[5] = {
        Gdiplus::Color(255, 0x00, 0x00, 0x00), Gdiplus::Color(255, 0x12, 0x12, 0x14),
        Gdiplus::Color(255, 0x3A, 0x3A, 0x3E), Gdiplus::Color(255, 0x12, 0x12, 0x14),
        Gdiplus::Color(255, 0x00, 0x00, 0x00)};
    const Gdiplus::REAL positions[5] = {0.0f, 0.22f, 0.45f, 0.75f, 1.0f};
    gradient.SetInterpolationColors(colors, positions, 5);
    graphics.FillRectangle(&gradient, rod);
    graphics.Restore(state);
    if (graphics.DrawImage(&image, Gdiplus::RectF(origin_x, origin_top, draw_w, draw_h), 0, 0,
                           float(pixels.width), float(pixels.height), Gdiplus::UnitPixel) != Gdiplus::Ok) {
        return nullptr;
    }
    return canvas;
}

uint32_t dominant_ink(const Pixels &pixels) {
    struct Bucket {
        int count = 0;
        double red = 0;
        double green = 0;
        double blue = 0;
    };
    double best_score = -1;
    double red = 0.53, green = 0.33, blue = 0.95;
    std::unordered_map<int, Bucket> buckets;
    for (int y = 0; y < pixels.height; y += 3) {
        for (int x = 0; x < pixels.width; x += 3) {
            size_t index = (size_t(y) * pixels.width + x) * 4;
            double alpha = pixels.bgra[index + 3];
            if (alpha <= 200) continue;
            double b = pixels.bgra[index] / alpha;
            double g = pixels.bgra[index + 1] / alpha;
            double r = pixels.bgra[index + 2] / alpha;
            int key = int(r * 5) * 36 + int(g * 5) * 6 + int(b * 5);
            Bucket &bucket = buckets[key];
            ++bucket.count;
            bucket.red += r;
            bucket.green += g;
            bucket.blue += b;
        }
    }
    for (const auto &entry : buckets) {
        const Bucket &bucket = entry.second;
        double r = bucket.red / bucket.count;
        double g = bucket.green / bucket.count;
        double b = bucket.blue / bucket.count;
        double high = std::max({r, g, b});
        double low = std::min({r, g, b});
        double saturation = high == 0 ? 0 : (high - low) / high;
        double luminance = 0.3 * r + 0.59 * g + 0.11 * b;
        double score = bucket.count * (0.25 + saturation) *
                       (luminance > 0.12 && luminance < 0.85 ? 1 : 0.15);
        if (score > best_score) {
            best_score = score;
            red = r;
            green = g;
            blue = b;
        }
    }
    double high = std::max({red, green, blue});
    double low = std::min({red, green, blue});
    if (high - low < 0.08) {
        red = 0.42;
        green = 0.45;
        blue = 0.5;
    } else if (high > 0.82) {
        red *= 0.82 / high;
        green *= 0.82 / high;
        blue *= 0.82 / high;
    }
    auto byte = [](double value) {
        return uint32_t(std::min(255.0, value * 255));
    };
    return byte(red) << 16 | byte(green) << 8 | byte(blue);
}

} // namespace

bool valid_id(const std::wstring &id) {
    if (id.size() != 15 || id.compare(0, 7, L"custom-") != 0) {
        return false;
    }
    for (size_t i = 7; i < id.size(); ++i) {
        wchar_t c = id[i];
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) {
            return false;
        }
    }
    return true;
}

bool make_from_file(const std::wstring &path, MadeHand &out, std::wstring *error) {
    out = MadeHand{};
    if (error) error->clear();
    try {
        std::unique_ptr<Gdiplus::Bitmap> source;
        Pixels pixels;
        if (!read_image(path, source, pixels, error)) return false;
        if (const wchar_t *problem = check_pixels(pixels)) {
            set_error(error, problem);
            return false;
        }
        out.image = compose(*source, pixels);
        if (!out.image) {
            out = MadeHand{};
            set_error(error, L"TinyHand couldn't put that hand on a stick.");
            return false;
        }
        out.ink = dominant_ink(pixels);
        return true;
    } catch (const std::bad_alloc &) {
        out = MadeHand{};
        set_error(error, L"There isn't enough memory to make that hand.");
        return false;
    } catch (...) {
        out = MadeHand{};
        set_error(error, L"TinyHand couldn't make that hand.");
        return false;
    }
}

std::vector<Hand> all() {
    std::vector<Hand> hands;
    if (!load_metadata(hands)) {
        hands.clear();
    }
    return hands;
}

std::wstring prompt(const std::wstring &idea) {
    std::wstring style = trim(idea);
    if (style.empty()) {
        style = L"a soft vinyl toy in a bright colour";
    }
    return L"Make a pointer image for TinyHand, a Mac app that shows a toy hand on screen.\n\n"
           L"Draw one cartoon right hand in this style: " + style + L"\n\n"
           L"Rules:\n"
           L"- We see the BACK of the hand. The palm faces away from us. The knuckles and the smooth back of the hand face the viewer.\n"
           L"- The index finger points straight up. The other three fingers are folded down, so we only see their knuckles, never their fingertips or fingernails.\n"
           L"- The raised index finger sits at the far left of the knuckle row, right next to the thumb. Three folded knuckles sit to its right. It must never look like a middle finger.\n"
           L"- The thumb is tucked against the left side of the hand. Nothing crosses the front of the fist: no thumb, no fingertips, no fingernails, no palm.\n"
           L"- The fingertip is the highest point of the image. Nothing above or beside it.\n"
           L"- The hand ends at the bottom in a wrist cuff or a clean flat wrist, facing straight down, centred.\n"
           L"- Upright, not tilted. Whole hand visible, nothing cropped.\n"
           L"- Transparent background. No shadow, no floor, no outline glow, no text, no stick, no arm.\n"
           L"- Soft 3D toy look, bright and readable at small sizes.\n"
           L"- Portrait image, hand filling most of the height with a little empty space around it.\n";
}

std::wstring suggested_name(const std::wstring &source_path) {
    size_t slash = source_path.find_last_of(L"\\/");
    std::wstring base = slash == std::wstring::npos ? source_path : source_path.substr(slash + 1);
    size_t dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        base.resize(dot);
    }
    for (wchar_t &c : base) {
        if (c == L'_' || c == L'-') {
            c = L' ';
        }
    }
    std::vector<std::wstring> words;
    size_t at = 0;
    while (at < base.size() && words.size() < 2) {
        while (at < base.size() && std::iswspace(base[at])) {
            ++at;
        }
        size_t begin = at;
        while (at < base.size() && !std::iswspace(base[at])) {
            ++at;
        }
        if (at > begin) {
            words.push_back(base.substr(begin, at - begin));
        }
    }
    std::wstring result;
    for (size_t i = 0; i < words.size(); ++i) {
        if (i) {
            result += L' ';
        }
        result += words[i];
    }
    std::wstring lower = result;
    std::transform(lower.begin(), lower.end(), lower.begin(), std::towlower);
    if (result.empty() || character_count(result) > 16 || lower.compare(0, 13, L"chatgpt image") == 0) {
        return L"My hand " + std::to_wstring(all().size() + 1);
    }
    result[0] = wchar_t(std::towupper(result[0]));
    return result;
}

bool add(Gdiplus::Bitmap *image, uint32_t ink, const std::wstring &name, Hand &out, std::wstring *error) {
    out = Hand{};
    if (error) error->clear();
    if (!image || image->GetLastStatus() != Gdiplus::Ok || !image->GetWidth() || !image->GetHeight() ||
        image->GetWidth() > 4096 || image->GetHeight() > 4096) {
        set_error(error, L"TinyHand couldn't save that hand.");
        return false;
    }
    if (!ensure_directory()) {
        set_error(error, L"TinyHand couldn't create the custom hand folder.");
        return false;
    }
    std::vector<Hand> hands;
    if (!load_metadata(hands)) {
        set_error(error, L"TinyHand couldn't read the saved custom hands.");
        return false;
    }
    std::wstring id;
    std::wstring path;
    for (int attempt = 0; attempt < 32; ++attempt) {
        id = new_id();
        path = asset_path(id);
        if (!path.empty() && GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) break;
        id.clear();
    }
    if (id.empty() || !save_png_atomic(image, path)) {
        set_error(error, L"TinyHand couldn't save the hand image.");
        return false;
    }
    Hand added{id, trim(name), ink};
    if (added.name.empty()) {
        added.name = L"My hand " + std::to_wstring(hands.size() + 1);
    }
    if (character_count(added.name) > 16) {
        added.name = limit_name(added.name);
    }
    hands.push_back(added);
    if (!save_metadata(hands)) {
        DeleteFileW(path.c_str());
        set_error(error, L"TinyHand couldn't save the custom hand list.");
        return false;
    }
    out = std::move(added);
    return true;
}

bool remove(const std::wstring &id, std::wstring *error) {
    if (error) error->clear();
    if (!valid_id(id)) {
        set_error(error, L"That isn't a valid custom hand.");
        return false;
    }
    std::vector<Hand> hands;
    if (!load_metadata(hands)) {
        set_error(error, L"TinyHand couldn't read the saved custom hands.");
        return false;
    }
    size_t old_size = hands.size();
    hands.erase(std::remove_if(hands.begin(), hands.end(), [&](const Hand &hand) { return hand.id == id; }), hands.end());
    if (hands.size() != old_size && !save_metadata(hands)) {
        set_error(error, L"TinyHand couldn't save the custom hand list.");
        return false;
    }
    std::wstring path = asset_path(id);
    if (!DeleteFileW(path.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
        set_error(error, L"TinyHand couldn't delete the hand image.");
        return false;
    }
    return true;
}

std::wstring asset_path(const std::wstring &id) {
    return valid_id(id) ? directory() + L"\\" + id + L".png" : std::wstring();
}

} // namespace custom_hands
