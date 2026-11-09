#pragma once

#include "overlay.h"
#include "settings.h"
#include "toolbar.h"
#include "welcome.h"

// The tray icon, its menu and the global shortcuts (AppDelegate.swift).
class App {
public:
    bool init(HINSTANCE instance, bool testing = false);
    void shutdown();
    Overlay &overlay() { return overlay_; }
    SettingsWindow &settings() { return settings_; }
    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT proc(UINT msg, WPARAM wp, LPARAM lp);
    void addTray();
    void updateTray();
    void loadIcons();
    void showMenu();
    void openSettings();
    bool registerHotkeys();
    void unregisterHotkeys();
    bool applyHotkey(const Hotkey &h, bool isToggle);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    UINT taskbarCreated_ = 0;
    bool trayAdded_ = false;
    HICON iconOn_ = nullptr;
    HICON iconOff_ = nullptr;
    bool toggleRegistered_ = false;
    bool clearRegistered_ = false;
    bool testing_ = false;
    bool recording_ = false;

    Toolbar toolbar_;
    Overlay overlay_;
    SettingsWindow settings_;
    WelcomeWindow welcome_;
};
