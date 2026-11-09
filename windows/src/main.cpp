#include "app.h"
#include "selftest.h"

#include <commctrl.h>
#include <shellapi.h>

namespace {

LONG WINAPI on_crash(EXCEPTION_POINTERS *) {
    cursor::show();
    return EXCEPTION_CONTINUE_SEARCH;
}

void enable_per_monitor_dpi() {
    using SetContext = BOOL(WINAPI *)(HANDLE);
    auto fn = reinterpret_cast<SetContext>(
        reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext")));
    if (fn) {
        fn(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));
    }
}

int run_app(HINSTANCE instance) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\TinyHand.Instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(L"TinyHandApp", nullptr)) {
            PostMessageW(other, WM_TH_SHOW_SETTINGS, 0, 0);
        }
        return 0;
    }
    SetUnhandledExceptionFilter(on_crash);
    cursor::repair_if_needed();

    App app;
    if (!app.init(instance)) {
        th::log(L"startup failed");
        return 1;
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    app.shutdown();
    if (mutex) {
        CloseHandle(mutex);
    }
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    enable_per_monitor_dpi();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartup(&token, &input, nullptr);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_HOTKEY_CLASS | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    prefs::load();

    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    auto arg = [&](int i, const wchar_t *fallback) { return std::wstring(i < argc ? argv[i] : fallback); };
    int code = -1;
    for (int i = 1; i < argc && code < 0; i++) {
        std::wstring a = argv[i];
        if (a == L"--render-test") {
            code = selftest::render(arg(i + 1, L"tinyhand-render.png"));
        } else if (a == L"--toolbar-test") {
            code = selftest::toolbar(arg(i + 1, L"tinyhand-toolbar.png"));
        } else if (a == L"--settings-test") {
            code = selftest::settings(instance, arg(i + 1, L"tinyhand-settings.png"));
        } else if (a == L"--settings-ui-test") {
            code = selftest::settings_ui(instance);
        } else if (a == L"--welcome-test") {
            code = selftest::welcome(arg(i + 1, L"tinyhand-welcome.png"));
        } else if (a == L"--e2e-test") {
            code = selftest::e2e(instance);
        } else if (a == L"--version") {
            selftest::print("TinyHand for Windows");
            code = 0;
        }
    }
    if (code < 0) {
        code = run_app(instance);
    }
    LocalFree(argv);
    Gdiplus::GdiplusShutdown(token);
    CoUninitialize();
    return code;
}
