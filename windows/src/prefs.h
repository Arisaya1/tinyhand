#pragma once

#include "common.h"

enum class InteractKey { Alt, Ctrl, Shift, Off };

struct Hotkey {
    UINT mods = 0;
    UINT vk = 0;
    std::wstring display() const;
    DWORD pack() const { return (DWORD(mods) << 16) | (vk & 0xFFFF); }
    static Hotkey unpack(DWORD v) { return Hotkey{UINT(v >> 16), UINT(v & 0xFFFF)}; }
    bool operator==(const Hotkey &o) const { return mods == o.mods && vk == o.vk; }
};

struct StyleInfo {
    const wchar_t *id;
    const wchar_t *title;
    uint32_t ink;
};

struct InkChoiceInfo {
    const wchar_t *id;
    const wchar_t *title;
    uint32_t color;
    bool match;
};

struct TapSoundInfo {
    const wchar_t *id;
    const wchar_t *title;
    int resource;
};

constexpr uint32_t kInkPurple = 0x8754F2;
constexpr uint32_t kInkRed = 0xFF291F;

extern const std::vector<StyleInfo> kStyles;
extern const std::vector<InkChoiceInfo> kInkChoices;
extern const std::vector<TapSoundInfo> kTapSounds;

bool style_is_laser(const std::wstring &id);
const StyleInfo *style_info(const std::wstring &id);
std::wstring style_title(const std::wstring &id);
uint32_t style_ink(const std::wstring &id);
uint32_t ink_for(const std::wstring &style);
const wchar_t *interact_key_name(InteractKey key);
int interact_vk(InteractKey key);

struct Prefs {
    std::wstring pointerStyle = L"lavender";
    double pointerSize = 0.13;
    bool showShadow = true;
    double fadeSeconds = 3;
    bool handSound = true;
    bool laserSound = false;
    std::wstring tapSound = L"thock";
    InteractKey interactKey = InteractKey::Alt;
    bool interactKeepsHand = false;
    std::wstring inkChoice = L"match";
    bool showInkButton = true;
    bool snapShapes = true;
    std::vector<std::wstring> pinned;
    Hotkey toggleKey{MOD_CONTROL | MOD_ALT, 'H'};
    Hotkey clearKey{MOD_CONTROL | MOD_ALT, 'X'};
    bool hintShown = false;
    bool welcomeShown = false;
};

namespace prefs {

constexpr double kMinSize = 0.08;
constexpr double kMaxSize = 0.20;
constexpr size_t kMaxPinned = 8;

Prefs &get();
void load();
void save();
void reset();
// Saves and tells every listener (overlay, toolbar, tray menu) to refresh.
void changed();
void listen(std::function<void()> fn);
// Self-tests run with persistence off so they never touch the user's settings.
void set_persist(bool on);

std::vector<std::wstring> default_pinned();
bool launch_at_login();
void set_launch_at_login(bool on);

// Set while the system cursor is swapped for a blank one, so a crash can
// be repaired on the next start.
void set_cursor_hidden_flag(bool hidden);
bool cursor_hidden_flag();

} // namespace prefs
