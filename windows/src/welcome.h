#pragma once

#include "common.h"

// The first-run card from WelcomeWindow.swift. Windows needs no permission,
// so it is always the "You're all set" card with a Start button.
class WelcomeWindow {
public:
    std::function<void()> onStart;

    void show(HINSTANCE instance);
    void close();
    bool snapshot(const std::wstring &path, bool dark);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT proc(UINT msg, WPARAM wp, LPARAM lp);
    void render();
    void paint(Gdiplus::Graphics &g, bool dark);
    int hit(POINT client) const;

    HWND hwnd_ = nullptr;
    th::Surface surface_;
    float scale_ = 1;
    int hover_ = 0;
    int pressed_ = 0;
};
