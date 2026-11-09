#pragma once

#include "arm.h"
#include "ink.h"
#include "sound.h"
#include "toolbar.h"

namespace cursor {
void hide();
void show();
// Puts the user's cursors back if a previous run died while they were hidden.
void repair_if_needed();
} // namespace cursor

// The full-screen, click-through layer the hand and ink are drawn on, plus
// the low-level mouse hook that turns clicks into taps (OverlayController.swift
// and InputTap.swift).
class Overlay {
public:
    bool create(HINSTANCE instance, Toolbar *toolbar);

    void toggle();
    bool activate();
    void deactivate();
    void exitPointer();
    void clearInk();
    bool active() const { return active_; }
    bool hookInstalled() const;

    void pick(const std::wstring &style);
    bool soundOn() const;
    void setSoundOn(bool on);
    void suspendInput(bool suspended);

    std::function<void()> onActiveChanged;
    std::function<void()> onSoundChanged;

    // For --e2e-test.
    int strokeCount() const { return ink_.count(); }
    th_shape_kind lastShape() const { return ink_.lastKind(); }
    int presses() const { return presses_; }
    bool drawing() const { return ink_.drawing(); }
    void tick();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT proc(UINT msg, WPARAM wp, LPARAM lp);
    void syncPrefs();
    void syncSound();
    void updateZones(POINT mouse, double now);
    void render(bool armChanged, bool full);
    void drainPoints();
    void release();
    void runTimer(UINT ms);
    void wake();
    void updatePace(bool busy, double now);
    void setEscEnabled(bool enabled);
    void scanShell(double now);
    Gdiplus::PointF local(POINT p) const;

    HWND hwnd_ = nullptr;
    Toolbar *toolbar_ = nullptr;
    th::Surface surface_;
    Arm arm_;
    Ink ink_;
    TapSound sound_;

    bool active_ = false;
    HMONITOR monitor_ = nullptr;
    RECT monRect_{0, 0, 0, 0};
    RECT workRect_{0, 0, 0, 0};
    float scale_ = 1;
    RECT armBounds_{0, 0, 0, 0};

    bool down_ = false;
    Gdiplus::PointF downPoint_{0, 0};
    int presses_ = 0;

    UINT timerMs_ = 0;
    double lastTick_ = 0;
    double stillSince_ = 0;
    POINT lastMouse_{-1, -1};
    double lastButtonEvent_ = -10;
    double revealUntil_ = 0;
    double hintHideAt_ = 0;
    double menuOpeningUntil_ = 0;
    double lastScan_ = -10;
    std::vector<RECT> menuRects_;
    std::vector<RECT> shellRects_;
    bool normalCursor_ = false;
    bool ownMenuOpen_ = false;
    bool escRegistered_ = false;
    bool periodRaised_ = false;
};
