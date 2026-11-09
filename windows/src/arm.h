#pragma once

#include "hands.h"

// The hand on a stick (or the laser dot), ported from ArmView.swift.
// The pose math runs in a y-up frame exactly like the macOS view, then is
// drawn into a y-down GDI+ surface.
class Arm {
public:
    void setStyle(const std::wstring &style);
    const std::wstring &style() const { return style_; }
    bool isLaser() const { return laser_; }

    void setSize(int width, int height);
    void press() { press_ = 1; }
    bool held = false;
    bool hidden = false;

    // Advances the pose towards the cursor (local, y-down pixels).
    // Returns true when what is on screen changed.
    bool aim(float cursorX, float cursorY, double dt);
    bool settling() const;

    // Area the arm covers right now, including its shadow (local pixels).
    RECT bounds() const { return bounds_; }
    void draw(Gdiplus::Graphics &g) const;
    float inkWidth() const;

private:
    void updateDisplayImage();
    void drawHand(Gdiplus::Graphics &g) const;
    void drawLaser(Gdiplus::Graphics &g) const;
    RECT computeBounds() const;

    std::wstring style_ = L"lavender";
    bool laser_ = false;
    std::shared_ptr<HandAsset> asset_;
    std::unique_ptr<Gdiplus::Bitmap> display_;
    std::unique_ptr<Gdiplus::Bitmap> shadow_;
    int shadowPad_ = 0;
    int displayHeight_ = 0;
    float displaySizeFrac_ = 0;

    int width_ = 1280;
    int height_ = 800;
    float press_ = 0;
    float gripX_ = -1;
    float gripTarget_ = -1;

    struct Pose {
        float rotation = 0, rodLen = 0, handPts = 0, widthPts = 0, gripX = 0, gripY = 0, lift = 0, press = 0;
        float cursorX = 0, cursorY = 0, laserR = 0;
    } pose_;
    std::vector<long> lastKey_;
    RECT bounds_{0, 0, 0, 0};
};
