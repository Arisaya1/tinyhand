#include "custom_hand_ui.h"
#include "custom_hands.h"
#include "hands.h"
#include "prefs.h"
#include <shellapi.h>
#include <commdlg.h>

namespace custom_hand_ui {
namespace {
std::wstring url_encode(const std::wstring &value) {
    std::wstring out;
    wchar_t buf[7];
    for (wchar_t c : value) {
        if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') ||
            c == L'-' || c == L'_' || c == L'.' || c == L'~') {
            out += c;
        } else if (c == L' ') {
            out += L'+';
        } else if (c < 128) {
            _snwprintf(buf, std::size(buf), L"%%%02X", unsigned(c));
            out += buf;
        } else {
            wchar_t pair[2] = {c, 0};
            char utf8[8] = {};
            int n = WideCharToMultiByte(CP_UTF8, 0, pair, 1, utf8, sizeof(utf8), nullptr, nullptr);
            for (int i = 0; i < n; i++) {
                _snwprintf(buf, std::size(buf), L"%%%02X", unsigned(uint8_t(utf8[i])));
                out += buf;
            }
        }
    }
    return out;
}

void show_error(HWND owner, const std::wstring &message) {
    MessageBoxW(owner, message.c_str(), L"TinyHand", MB_OK | MB_ICONWARNING);
}
}

bool import_file(HWND owner, const std::wstring &path) {
    custom_hands::MadeHand made;
    std::wstring error;
    if (!custom_hands::make_from_file(path, made, &error)) {
        show_error(owner, error.empty() ? L"TinyHand couldn't read that hand image." : error);
        return false;
    }
    custom_hands::Hand created;
    if (!custom_hands::add(made.image.get(), made.ink, custom_hands::suggested_name(path), created, &error)) {
        show_error(owner, error.empty() ? L"TinyHand couldn't save that hand." : error);
        return false;
    }
    auto &p = prefs::get();
    if (p.pinned.size() < prefs::kMaxPinned &&
        std::find(p.pinned.begin(), p.pinned.end(), created.id) == p.pinned.end()) {
        p.pinned.push_back(created.id);
    }
    p.pointerStyle = created.id;
    hands::forget(created.id);
    hands::thumbnail(created.id);
    prefs::changed();
    return true;
}

void choose_file(HWND owner) {
    wchar_t path[32768] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"PNG images (*.png)\0*.png\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = DWORD(std::size(path));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = L"Choose a transparent hand image";
    if (GetOpenFileNameW(&dialog)) {
        import_file(owner, path);
    }
}

void copy_prompt() {
    std::wstring text = custom_hands::prompt();
    if (!OpenClipboard(nullptr)) {
        return;
    }
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void *target = GlobalLock(memory);
        memcpy(target, text.c_str(), bytes);
        GlobalUnlock(memory);
        SetClipboardData(CF_UNICODETEXT, memory);
    }
    CloseClipboard();
}

void open_chatgpt() {
    std::wstring url = L"https://chatgpt.com/?q=" + url_encode(custom_hands::prompt());
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void remove_current(HWND owner) {
    auto &p = prefs::get();
    if (!custom_hands::valid_id(p.pointerStyle)) {
        return;
    }
    if (MessageBoxW(owner, L"Remove this custom hand and its saved image?", L"TinyHand",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    std::wstring id = p.pointerStyle;
    std::wstring error;
    if (!custom_hands::remove(id, &error)) {
        show_error(owner, error);
        return;
    }
    p.pinned.erase(std::remove(p.pinned.begin(), p.pinned.end(), id), p.pinned.end());
    if (p.pinned.empty()) {
        p.pinned.push_back(L"lavender");
    }
    p.pointerStyle = L"lavender";
    hands::forget(id);
    prefs::changed();
}

void install_drag_drop(HWND hwnd) {
    DragAcceptFiles(hwnd, TRUE);
}

void handle_drop_files(HWND owner, HDROP drop) {
    wchar_t path[32768] = {};
    if (DragQueryFileW(drop, 0, path, UINT(std::size(path)))) {
        import_file(owner, path);
    }
    DragFinish(drop);
}
}
