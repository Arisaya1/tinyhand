#include "prefs.h"

const std::vector<StyleInfo> kStyles = {
    {L"lavender", L"Lavender toy", kInkPurple},
    {L"glove", L"Cursor glove", kInkPurple},
    {L"coral", L"Coral vinyl", kInkRed},
    {L"plush", L"Plush", 0x2F86F0},
    {L"robot", L"Robot", 0x16A87A},
    {L"pink", L"Bubblegum", 0xEE4F98},
    {L"blue", L"Sky", 0x2F86F0},
    {L"mint", L"Mint", 0x16A87A},
    {L"lemon", L"Lemon", 0xF2B705},
    {L"black", L"Midnight", 0x1D1D21},
    {L"chrome", L"Chrome", 0x6B7380},
    {L"holo", L"Holo", kInkPurple},
    {L"fuzzy", L"Fuzzy", kInkPurple},
    {L"wood", L"Wood", 0x9A5E2C},
    {L"marble", L"Marble", 0x6B7380},
    {L"terrazzo", L"Terrazzo", 0xF0561D},
    {L"glass", L"Glass", 0x1597E5},
    {L"jelly", L"Jelly", 0xA33FE6},
    {L"pastel", L"Pastel", 0xEE4F98},
    {L"pixel", L"Pixel", 0x1D1D21},
    {L"lowpoly", L"Low poly", kInkPurple},
    {L"knit", L"Knit", kInkPurple},
    {L"skeleton", L"Skeleton", 0x9A5E2C},
    {L"astro", L"Astronaut", 0x2F86F0},
    {L"space", L"Space", kInkRed},
    {L"monster", L"Monster", 0x3E9E2E},
    {L"water", L"Water", 0x1597E5},
    {L"lava", L"Lava", 0xF0561D},
    {L"grass", L"Grass", 0x3E9E2E},
    {L"cactus", L"Cactus", 0x3E9E2E},
    {L"choco", L"Chocolate", 0x9A5E2C},
    {L"cloud", L"Cloud", 0x2F86F0},
    {L"mech", L"Mech", 0xF0561D},
    {L"neon", L"Neon", 0xE0309A},
    {L"leaf", L"Leaf", 0x3E9E2E},
    {L"cookie", L"Gingerbread", 0x9A5E2C},
    {L"laser", L"Laser dot", kInkRed},
};

const std::vector<InkChoiceInfo> kInkChoices = {
    {L"match", L"Match the pointer", 0, true},
    {L"red", L"Red", kInkRed, false},
    {L"orange", L"Orange", 0xFF8A00, false},
    {L"yellow", L"Yellow", 0xFFCC00, false},
    {L"green", L"Green", 0x30C25A, false},
    {L"blue", L"Blue", 0x0A84FF, false},
    {L"purple", L"Purple", 0x8754F2, false},
    {L"white", L"White", 0xFFFFFF, false},
    {L"black", L"Black", 0x1D1D21, false},
};

const std::vector<TapSoundInfo> kTapSounds = {
    {L"thock", L"Thock", IDR_SOUND_THOCK}, {L"click", L"Click", IDR_SOUND_CLICK},
    {L"pop", L"Pop", IDR_SOUND_POP},       {L"wood", L"Wood block", IDR_SOUND_WOOD},
    {L"boop", L"Boop", IDR_SOUND_BOOP},    {L"squeak", L"Squeak", IDR_SOUND_SQUEAK},
};

bool style_is_laser(const std::wstring &id) { return id == L"laser"; }

const StyleInfo *style_info(const std::wstring &id) {
    for (const auto &s : kStyles) {
        if (id == s.id) {
            return &s;
        }
    }
    return nullptr;
}

std::wstring style_title(const std::wstring &id) {
    const StyleInfo *s = style_info(id);
    return s ? s->title : id;
}

uint32_t style_ink(const std::wstring &id) {
    const StyleInfo *s = style_info(id);
    return s ? s->ink : kInkPurple;
}

uint32_t ink_for(const std::wstring &style) {
    for (const auto &c : kInkChoices) {
        if (prefs::get().inkChoice == c.id && !c.match) {
            return c.color;
        }
    }
    return style_ink(style);
}

const wchar_t *interact_key_name(InteractKey key) {
    switch (key) {
    case InteractKey::Alt:
        return L"Alt";
    case InteractKey::Ctrl:
        return L"Ctrl";
    case InteractKey::Shift:
        return L"Shift";
    case InteractKey::Off:
        break;
    }
    return L"";
}

int interact_vk(InteractKey key) {
    switch (key) {
    case InteractKey::Alt:
        return VK_MENU;
    case InteractKey::Ctrl:
        return VK_CONTROL;
    case InteractKey::Shift:
        return VK_SHIFT;
    case InteractKey::Off:
        break;
    }
    return 0;
}

std::wstring Hotkey::display() const {
    std::wstring s;
    if (mods & MOD_CONTROL) {
        s += L"Ctrl+";
    }
    if (mods & MOD_ALT) {
        s += L"Alt+";
    }
    if (mods & MOD_SHIFT) {
        s += L"Shift+";
    }
    if (mods & MOD_WIN) {
        s += L"Win+";
    }
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
        s += wchar_t(vk);
        return s;
    }
    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG lparam = LONG(scan << 16);
    if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN || vk == VK_PRIOR || vk == VK_NEXT ||
        vk == VK_HOME || vk == VK_END || vk == VK_INSERT || vk == VK_DELETE) {
        lparam |= 1 << 24;
    }
    wchar_t name[64] = {};
    if (GetKeyNameTextW(lparam, name, int(std::size(name))) > 0) {
        s += name;
    } else {
        wchar_t buf[16];
        _snwprintf(buf, std::size(buf), L"0x%02X", vk);
        s += buf;
    }
    return s;
}

namespace prefs {

static const wchar_t *kKey = L"Software\\TinyHand";
static const wchar_t *kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static Prefs g;
static bool persist = true;

void set_persist(bool on) { persist = on; }
static std::vector<std::function<void()>> listeners;

Prefs &get() { return g; }

std::vector<std::wstring> default_pinned() { return {L"lavender", L"glove", L"coral", L"plush", L"robot", L"laser"}; }

static DWORD read_dword(HKEY k, const wchar_t *name, DWORD fallback) {
    DWORD v = 0, size = sizeof(v), type = 0;
    if (RegQueryValueExW(k, name, nullptr, &type, reinterpret_cast<BYTE *>(&v), &size) == ERROR_SUCCESS &&
        type == REG_DWORD) {
        return v;
    }
    return fallback;
}

static std::wstring read_string(HKEY k, const wchar_t *name, const std::wstring &fallback) {
    wchar_t buf[1024];
    DWORD size = sizeof(buf) - sizeof(wchar_t), type = 0;
    if (RegQueryValueExW(k, name, nullptr, &type, reinterpret_cast<BYTE *>(buf), &size) == ERROR_SUCCESS &&
        type == REG_SZ) {
        buf[size / sizeof(wchar_t)] = 0;
        return buf;
    }
    return fallback;
}

static void write_dword(HKEY k, const wchar_t *name, DWORD v) {
    RegSetValueExW(k, name, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&v), sizeof(v));
}

static void write_string(HKEY k, const wchar_t *name, const std::wstring &v) {
    RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(v.c_str()),
                   DWORD((v.size() + 1) * sizeof(wchar_t)));
}

static std::vector<std::wstring> split(const std::wstring &s) {
    std::vector<std::wstring> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t comma = s.find(L',', start);
        std::wstring part = s.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
        if (!part.empty() && style_info(part) && std::find(out.begin(), out.end(), part) == out.end()) {
            out.push_back(part);
        }
        if (comma == std::wstring::npos) {
            break;
        }
        start = comma + 1;
    }
    return out;
}

static InteractKey parse_key(const std::wstring &s) {
    if (s == L"ctrl") {
        return InteractKey::Ctrl;
    }
    if (s == L"shift") {
        return InteractKey::Shift;
    }
    if (s == L"off") {
        return InteractKey::Off;
    }
    return InteractKey::Alt;
}

static const wchar_t *key_id(InteractKey k) {
    switch (k) {
    case InteractKey::Ctrl:
        return L"ctrl";
    case InteractKey::Shift:
        return L"shift";
    case InteractKey::Off:
        return L"off";
    case InteractKey::Alt:
        break;
    }
    return L"alt";
}

void load() {
    g = Prefs();
    g.pinned = default_pinned();
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return;
    }
    std::wstring style = read_string(k, L"pointerStyle", g.pointerStyle);
    g.pointerStyle = style_info(style) ? style : L"lavender";
    g.pointerSize = std::clamp(read_dword(k, L"pointerSize", 130) / 1000.0, kMinSize, kMaxSize);
    g.showShadow = read_dword(k, L"showShadow", 1) != 0;
    g.fadeSeconds = read_dword(k, L"fadeSeconds", 3);
    g.handSound = read_dword(k, L"handSound", 1) != 0;
    g.laserSound = read_dword(k, L"laserSound", 0) != 0;
    g.tapSound = read_string(k, L"tapSound", g.tapSound);
    if (std::none_of(kTapSounds.begin(), kTapSounds.end(), [](const TapSoundInfo &t) { return g.tapSound == t.id; })) {
        g.tapSound = L"thock";
    }
    g.interactKey = parse_key(read_string(k, L"interactKey", L"alt"));
    g.interactKeepsHand = read_dword(k, L"interactKeepsHand", 0) != 0;
    g.inkChoice = read_string(k, L"inkChoice", g.inkChoice);
    g.showInkButton = read_dword(k, L"showInkButton", 1) != 0;
    g.snapShapes = read_dword(k, L"snapShapes", 1) != 0;
    std::vector<std::wstring> pinned = split(read_string(k, L"pinnedStyles", L""));
    if (!pinned.empty()) {
        if (pinned.size() > kMaxPinned) {
            pinned.resize(kMaxPinned);
        }
        g.pinned = pinned;
    }
    g.toggleKey = Hotkey::unpack(read_dword(k, L"toggleHotkey", g.toggleKey.pack()));
    g.clearKey = Hotkey::unpack(read_dword(k, L"clearHotkey", g.clearKey.pack()));
    g.hintShown = read_dword(k, L"hintShown", 0) != 0;
    g.welcomeShown = read_dword(k, L"welcomeShown", 0) != 0;
    RegCloseKey(k);
}

void save() {
    if (!persist) {
        return;
    }
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) {
        return;
    }
    write_string(k, L"pointerStyle", g.pointerStyle);
    write_dword(k, L"pointerSize", DWORD(std::lround(g.pointerSize * 1000)));
    write_dword(k, L"showShadow", g.showShadow);
    write_dword(k, L"fadeSeconds", DWORD(std::lround(g.fadeSeconds)));
    write_dword(k, L"handSound", g.handSound);
    write_dword(k, L"laserSound", g.laserSound);
    write_string(k, L"tapSound", g.tapSound);
    write_string(k, L"interactKey", key_id(g.interactKey));
    write_dword(k, L"interactKeepsHand", g.interactKeepsHand);
    write_string(k, L"inkChoice", g.inkChoice);
    write_dword(k, L"showInkButton", g.showInkButton);
    write_dword(k, L"snapShapes", g.snapShapes);
    std::wstring pinned;
    for (const auto &p : g.pinned) {
        pinned += (pinned.empty() ? L"" : L",") + p;
    }
    write_string(k, L"pinnedStyles", pinned);
    write_dword(k, L"toggleHotkey", g.toggleKey.pack());
    write_dword(k, L"clearHotkey", g.clearKey.pack());
    write_dword(k, L"hintShown", g.hintShown);
    write_dword(k, L"welcomeShown", g.welcomeShown);
    RegCloseKey(k);
}

void reset() {
    bool hint = g.hintShown;
    bool welcome = g.welcomeShown;
    g = Prefs();
    g.pinned = default_pinned();
    g.hintShown = hint;
    g.welcomeShown = welcome;
    changed();
}

void changed() {
    save();
    auto copy = listeners;
    for (auto &fn : copy) {
        fn();
    }
}

void listen(std::function<void()> fn) { listeners.push_back(std::move(fn)); }

bool launch_at_login() {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return false;
    }
    std::wstring v = read_string(k, L"TinyHand", L"");
    RegCloseKey(k);
    return !v.empty();
}

void set_launch_at_login(bool on) {
    if (!persist) {
        return;
    }
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) {
        th::log(L"launch at login change failed: cannot open Run key");
        return;
    }
    if (on) {
        write_string(k, L"TinyHand", L"\"" + th::exe_path() + L"\"");
    } else {
        RegDeleteValueW(k, L"TinyHand");
    }
    RegCloseKey(k);
}

void set_cursor_hidden_flag(bool hidden) {
    if (!persist) {
        return;
    }
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        write_dword(k, L"cursorHidden", hidden);
        RegCloseKey(k);
    }
}

bool cursor_hidden_flag() {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return false;
    }
    bool v = read_dword(k, L"cursorHidden", 0) != 0;
    RegCloseKey(k);
    return v;
}

} // namespace prefs
