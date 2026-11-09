#pragma once

#include "common.h"

// The floating bar at the bottom of the screen (Toolbar.swift): pinned hands,
// ink colour tray, tap sound and exit, plus the first-run hint.
class Toolbar {
public:
    bool create(HINSTANCE instance);

    std::function<void(const std::wstring &)> onPick;
    std::function<void()> onToggleSound;
    std::function<void()> onExit;

    void select(const std::wstring &style);
    void setSoundOn(bool on);
    void refresh();
    void place(const RECT &monitor, const RECT &work, float scale);
    void show();
    void hide();
    void setRevealed(bool revealed);
    bool revealed() const { return revealed_; }
    void showHint(const std::vector<std::pair<std::wstring, std::wstring>> &parts, double now);
    void hideHint(double now);
    void animate(double now);
    bool animating() const;

    RECT barRect() const;
    RECT frameRect() const;
    bool buttonCenter(const std::wstring &style, POINT *out) const;
    HWND hwnd() const { return hwnd_; }

    bool renderPng(const std::wstring &path, bool withHint, bool withTray);

private:
    enum class Kind { Style, Ink, Sound, Exit, Swatch, Divider };
    struct Item {
        Kind kind;
        std::wstring id;
        Gdiplus::RectF rect;
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT proc(UINT msg, WPARAM wp, LPARAM lp);
    void layout();
    void render();
    void present();
    void setTrayOpen(bool open);
    int hit(POINT p) const;
    void activate(int index);
    void drawItem(Gdiplus::Graphics &g, const Item &item, int index) const;
    void drawPanel(Gdiplus::Graphics &g, const Gdiplus::RectF &r, float radius) const;
    void drawHint(Gdiplus::Graphics &g) const;

    HWND hwnd_ = nullptr;
    th::Surface surface_;
    float scale_ = 1;
    RECT monitor_{0, 0, 1280, 800};
    RECT work_{0, 0, 1280, 760};
    POINT origin_{0, 0};
    SIZE size_{0, 0};

    std::vector<std::wstring> styles_;
    std::wstring current_ = L"lavender";
    bool soundOn_ = true;
    bool trayOpen_ = false;
    bool shown_ = false;
    bool revealed_ = false;
    int hover_ = -1;
    int pressed_ = -1;

    std::vector<Item> items_;
    Gdiplus::RectF bar_, tray_, hint_;

    std::vector<std::pair<std::wstring, std::wstring>> hintParts_;
    float hintAlpha_ = 0;
    float hintTarget_ = 0;
    float alpha_ = 0;
    float alphaTarget_ = 0;
    double lastAnimate_ = 0;
};
