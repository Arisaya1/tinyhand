#pragma once

#include "common.h"

// Strokes drawn with the hand, ported from Ink.swift, plus shape snapping:
// when a stroke ends it goes through the shared ShapeKit recognizer and, if
// it reads as a shape, is redrawn as that clean shape.
class Ink {
public:
    void begin(Gdiplus::PointF p, float width, uint32_t color);
    void extend(Gdiplus::PointF p);
    // Ends the current stroke. Returns the shape it snapped to, if any.
    th_shape_kind end(double now);
    void clear();

    bool drawing() const { return current_ != nullptr; }
    // True while a stroke is fading out (or about to), so frames must keep coming.
    bool fading(double now) const;
    int count() const { return int(strokes_.size()); }
    th_shape_kind lastKind() const { return lastKind_; }

    // Steps fades; returns true if anything on screen changed.
    bool update(double now);
    // Everything that changed since the last call (local pixels).
    RECT takeDirty();
    void draw(Gdiplus::Graphics &g) const;

private:
    struct Stroke {
        int id = 0;
        std::vector<Gdiplus::PointF> raw;
        th_shape shape{};
        bool snapped = false;
        uint32_t color = 0;
        float width = 4;
        double endTime = -1;
        float alpha = 1;
        RECT bounds{0, 0, 0, 0};
        std::unique_ptr<Gdiplus::GraphicsPath> path;
    };

    void rebuild(Stroke &s);
    void markDirty(const RECT &r);
    Stroke *find(int id);

    std::vector<std::unique_ptr<Stroke>> strokes_;
    Stroke *current_ = nullptr;
    int nextId_ = 1;
    int lastLineId_ = 0;
    double lastLineTime_ = 0;
    th_shape_kind lastKind_ = TH_SHAPE_NONE;
    RECT dirty_{0, 0, 0, 0};
};
