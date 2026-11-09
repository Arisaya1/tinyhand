#include "app.h"

#include <shellapi.h>

namespace {

const wchar_t *kClass = L"TinyHandApp";
constexpr UINT kTrayId = 1;
constexpr int kToggleHotkey = 1;
constexpr int kClearHotkey = 2;

enum : UINT { kMenuToggle = 100, kMenuClear, kMenuSound, kMenuSnap, kMenuSettings, kMenuQuit };

bool light_taskbar() {
    DWORD v = 0, size = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size) == ERROR_SUCCESS) {
        return v != 0;
    }
    // No Windows 10/11 theme key (older Windows or Wine): the classic taskbar is light.
    return true;
}

HICON make_icon(int resource, bool dark) {
    std::unique_ptr<Gdiplus::Bitmap> src(th::load_png_resource(resource));
    if (!src) {
        return nullptr;
    }
    int size = GetSystemMetrics(SM_CXSMICON);
    Gdiplus::Bitmap dst(size, size, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&dst);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        float c = dark ? 0.0f : 1.0f;
        Gdiplus::ColorMatrix m = {{{0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 1, 0}, {c, c, c, 0, 1}}};
        Gdiplus::ImageAttributes attrs;
        attrs.SetColorMatrix(&m);
        g.DrawImage(src.get(), Gdiplus::Rect(0, 0, size, size), 0, 0, INT(src->GetWidth()), INT(src->GetHeight()),
                    Gdiplus::UnitPixel, &attrs);
    }
    HICON icon = nullptr;
    dst.GetHICON(&icon);
    return icon;
}

} // namespace

bool App::init(HINSTANCE instance, bool testing) {
    instance_ = instance;
    testing_ = testing;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kClass, L"TinyHand", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance,
                            this);
    if (!hwnd_) {
        return false;
    }
    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");

    if (!toolbar_.create(instance) || !overlay_.create(instance, &toolbar_)) {
        return false;
    }
    overlay_.onActiveChanged = [this] { updateTray(); };
    settings_.applyHotkey = [this](const Hotkey &h, bool isToggle) { return applyHotkey(h, isToggle); };
    settings_.onRecording = [this](bool recording) {
        recording_ = recording;
        if (recording) {
            unregisterHotkeys();
        } else {
            registerHotkeys();
        }
    };
    loadIcons();
    addTray();
    if (!registerHotkeys()) {
        th::log(L"could not register shortcuts %ls / %ls", prefs::get().toggleKey.display().c_str(),
                prefs::get().clearKey.display().c_str());
    }
    th::log(L"launch %ls", th::exe_path().c_str());

    welcome_.onStart = [this] { overlay_.activate(); };
    if (!testing_ && !prefs::get().welcomeShown) {
        prefs::get().welcomeShown = true;
        prefs::save();
        welcome_.show(instance_);
    }
    return true;
}

void App::shutdown() {
    overlay_.deactivate();
    unregisterHotkeys();
    if (trayAdded_) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd_;
        nid.uID = kTrayId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        trayAdded_ = false;
    }
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        reinterpret_cast<App *>(cs->lpCreateParams)->hwnd_ = hwnd;
    }
    auto *self = reinterpret_cast<App *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->proc(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::proc(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == taskbarCreated_ && taskbarCreated_) {
        trayAdded_ = false;
        addTray();
        return 0;
    }
    switch (msg) {
    case WM_TH_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONUP:
            overlay_.toggle();
            break;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            showMenu();
            break;
        default:
            break;
        }
        return 0;
    case WM_TH_SHOW_SETTINGS:
        openSettings();
        return 0;
    case WM_HOTKEY:
        if (wp == kToggleHotkey) {
            overlay_.toggle();
        } else if (wp == kClearHotkey) {
            overlay_.clearInk();
        }
        return 0;
    case WM_SETTINGCHANGE:
        if (lp && wcscmp(reinterpret_cast<const wchar_t *>(lp), L"ImmersiveColorSet") == 0) {
            loadIcons();
            updateTray();
        }
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wp) {
            overlay_.deactivate();
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

void App::loadIcons() {
    if (iconOn_) {
        DestroyIcon(iconOn_);
    }
    if (iconOff_) {
        DestroyIcon(iconOff_);
    }
    bool dark = light_taskbar();
    iconOn_ = make_icon(IDR_TRAY_ON, dark);
    iconOff_ = make_icon(IDR_TRAY_OFF, dark);
}

void App::addTray() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TH_TRAY;
    nid.hIcon = iconOff_ ? iconOff_ : LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP));
    wcscpy_s(nid.szTip, L"TinyHand: click to turn on, right-click for the menu");
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    if (!trayAdded_) {
        th::log(L"tray icon not added (error %lu)", GetLastError());
    }
}

void App::updateTray() {
    if (!trayAdded_) {
        return;
    }
    bool on = overlay_.active();
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon = on ? iconOn_ : iconOff_;
    wcscpy_s(nid.szTip, on ? L"TinyHand is on: click to turn off, right-click for the menu"
                           : L"TinyHand: click to turn on, right-click for the menu");
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void App::showMenu() {
    HMENU menu = CreatePopupMenu();
    std::wstring toggle = std::wstring(overlay_.active() ? L"Turn Pointer Off" : L"Turn Pointer On") + L"\t" +
                          prefs::get().toggleKey.display();
    std::wstring clear = L"Clear Drawing\t" + prefs::get().clearKey.display();
    AppendMenuW(menu, MF_STRING, kMenuToggle, toggle.c_str());
    AppendMenuW(menu, MF_STRING, kMenuClear, clear.c_str());
    AppendMenuW(menu, MF_STRING | (overlay_.soundOn() ? MF_CHECKED : 0), kMenuSound, L"Tap Sound");
    AppendMenuW(menu, MF_STRING | (prefs::get().snapShapes ? MF_CHECKED : 0), kMenuSnap, L"Snap Shapes");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"Settings...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit TinyHand");

    POINT p;
    GetCursorPos(&p);
    overlay_.suspendInput(true);
    SetForegroundWindow(hwnd_);
    UINT cmd = UINT(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, p.x, p.y, 0, hwnd_,
                                   nullptr));
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    overlay_.suspendInput(false);
    DestroyMenu(menu);

    switch (cmd) {
    case kMenuToggle:
        overlay_.toggle();
        break;
    case kMenuClear:
        overlay_.clearInk();
        break;
    case kMenuSound:
        overlay_.setSoundOn(!overlay_.soundOn());
        break;
    case kMenuSnap:
        prefs::get().snapShapes = !prefs::get().snapShapes;
        prefs::changed();
        break;
    case kMenuSettings:
        openSettings();
        break;
    case kMenuQuit:
        shutdown();
        PostQuitMessage(0);
        break;
    default:
        break;
    }
}

void App::openSettings() {
    overlay_.exitPointer();
    settings_.show(instance_);
}

bool App::registerHotkeys() {
    if (toggleRegistered_ && clearRegistered_) {
        return true;
    }
    const Hotkey &t = prefs::get().toggleKey;
    const Hotkey &c = prefs::get().clearKey;
    if (!toggleRegistered_) {
        toggleRegistered_ = RegisterHotKey(hwnd_, kToggleHotkey, t.mods | MOD_NOREPEAT, t.vk) != FALSE;
    }
    if (!clearRegistered_) {
        clearRegistered_ = RegisterHotKey(hwnd_, kClearHotkey, c.mods | MOD_NOREPEAT, c.vk) != FALSE;
    }
    return toggleRegistered_ && clearRegistered_;
}

void App::unregisterHotkeys() {
    if (toggleRegistered_) {
        UnregisterHotKey(hwnd_, kToggleHotkey);
    }
    if (clearRegistered_) {
        UnregisterHotKey(hwnd_, kClearHotkey);
    }
    toggleRegistered_ = clearRegistered_ = false;
}

bool App::applyHotkey(const Hotkey &h, bool isToggle) {
    unregisterHotkeys();
    Hotkey &slot = isToggle ? prefs::get().toggleKey : prefs::get().clearKey;
    Hotkey old = slot;
    slot = h;
    if (registerHotkeys()) {
        if (recording_) {
            unregisterHotkeys();
        }
        prefs::changed();
        return true;
    }
    unregisterHotkeys();
    slot = old;
    if (!recording_) {
        registerHotkeys();
    }
    return false;
}
