#include "ink.h"

#include "prefs.h"

namespace {

constexpr double kFadeDuration = 0.6;
constexpr double kArrowheadWindow = 3.0;

RECT union_rect(const RECT &a, const RECT &b) {
    if (a.right <= a.left || a.bottom <= a.top) {
        return b;
    }
    if (b.right <= b.left || b.bottom <= b.top) {
        return a;
    }
    return RECT{std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right),
                std::max(a.bottom, b.bottom)};
}

} // namespace

void Ink::markDirty(const RECT &r) { dirty_ = union_rect(dirty_, r); }

RECT Ink::takeDirty() {
    RECT d = dirty_;
    dirty_ = RECT{0, 0, 0, 0};
    return d;
}

Ink::Stroke *Ink::find(int id) {
    for (auto &s : strokes_) {
        if (s->id == id) {
            return s.get();
        }
    }
    return nullptr;
}

void Ink::begin(Gdiplus::PointF p, float width, uint32_t color) {
    auto s = std::make_unique<Stroke>();
    s->id = nextId_++;
    s->raw.push_back(p);
    s->width = width;
    s->color = color;
    current_ = s.get();
    strokes_.push_back(std::move(s));
    rebuild(*current_);
}

void Ink::extend(Gdiplus::PointF p) {
    if (!current_) {
        return;
    }
    const Gdiplus::PointF &last = current_->raw.back();
    if (std::hypot(p.X - last.X, p.Y - last.Y) < 1.5f) {
        return;
    }
    current_->raw.push_back(p);
    rebuild(*current_);
}

th_shape_kind Ink::end(double now) {
    Stroke *s = current_;
    if (!s) {
        return TH_SHAPE_NONE;
    }
    current_ = nullptr;
    s->endTime = now;
    th_shape_kind kind = TH_SHAPE_NONE;
    if (prefs::get().snapShapes && s->raw.size() >= 2) {
        std::vector<th_point> pts;
        pts.reserve(s->raw.size());
        for (const auto &p : s->raw) {
            pts.push_back(th_point{p.X, p.Y});
        }
        Stroke *line = lastLineId_ ? find(lastLineId_) : nullptr;
        if (line && now - lastLineTime_ < kArrowheadWindow) {
            th_shape arrow;
            if (th_recognize_arrowhead(&line->shape, pts.data(), int(pts.size()), 1, &arrow) == TH_SHAPE_ARROW) {
                markDirty(s->bounds);
                strokes_.erase(std::find_if(strokes_.begin(), strokes_.end(),
                                            [s](const std::unique_ptr<Stroke> &x) { return x.get() == s; }));
                line->shape = arrow;
                line->endTime = now;
                rebuild(*line);
                lastLineId_ = 0;
                lastKind_ = TH_SHAPE_ARROW;
                th::log(L"snapped two strokes to an arrow");
                return TH_SHAPE_ARROW;
            }
        }
        th_shape shape;
        kind = th_recognize(pts.data(), int(pts.size()), 1, &shape);
        if (kind != TH_SHAPE_NONE) {
            s->shape = shape;
            s->snapped = true;
            rebuild(*s);
            th::log(L"snapped stroke of %d points to %hs", int(pts.size()), th_shape_name(kind));
        }
    }
    lastKind_ = kind;
    if (kind == TH_SHAPE_LINE) {
        lastLineId_ = s->id;
        lastLineTime_ = now;
    } else {
        lastLineId_ = 0;
    }
    return kind;
}

void Ink::clear() {
    for (auto &s : strokes_) {
        markDirty(s->bounds);
    }
    strokes_.clear();
    current_ = nullptr;
    lastLineId_ = 0;
}

bool Ink::fading(double now) const {
    double fade = prefs::get().fadeSeconds;
    if (fade <= 0) {
        return false;
    }
    double delay = std::max(0.5, fade);
    for (const auto &s : strokes_) {
        if (s->endTime >= 0 && now >= s->endTime + delay - 0.1) {
            return true;
        }
    }
    return false;
}

bool Ink::update(double now) {
    double fade = prefs::get().fadeSeconds;
    if (fade <= 0) {
        return false;
    }
    double delay = std::max(0.5, fade);
    bool changed = false;
    for (size_t i = 0; i < strokes_.size();) {
        Stroke &s = *strokes_[i];
        if (s.endTime < 0) {
            i++;
            continue;
        }
        double t = (now - s.endTime - delay) / kFadeDuration;
        if (t <= 0) {
            i++;
            continue;
        }
        markDirty(s.bounds);
        changed = true;
        if (t >= 1) {
            if (s.id == lastLineId_) {
                lastLineId_ = 0;
            }
            strokes_.erase(strokes_.begin() + ptrdiff_t(i));
            continue;
        }
        s.alpha = float(1 - t * t);
        i++;
    }
    return changed;
}

void Ink::rebuild(Stroke &s) {
    markDirty(s.bounds);
    auto path = std::make_unique<Gdiplus::GraphicsPath>();
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    auto grow = [&](float x, float y) {
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    };
    if (s.snapped) {
        const th_shape &sh = s.shape;
        for (int i = 0; i < sh.path_count; i++) {
            std::vector<Gdiplus::PointF> pts;
            for (int k = 0; k < sh.path_length[i]; k++) {
                const th_point &p = sh.points[sh.path_start[i] + k];
                pts.emplace_back(p.x, p.y);
                grow(p.x, p.y);
            }
            if (pts.size() < 2) {
                continue;
            }
            path->StartFigure();
            path->AddLines(pts.data(), INT(pts.size()));
            if (sh.path_closed[i]) {
                path->CloseFigure();
            }
        }
    } else {
        const auto &p = s.raw;
        for (const auto &q : p) {
            grow(q.X, q.Y);
        }
        if (p.size() == 1) {
            path->AddLine(p[0], Gdiplus::PointF(p[0].X + 0.01f, p[0].Y));
        } else {
            Gdiplus::PointF cur = p[0];
            for (size_t i = 1; i < p.size(); i++) {
                const Gdiplus::PointF &ctrl = p[i - 1];
                Gdiplus::PointF mid((p[i - 1].X + p[i].X) / 2, (p[i - 1].Y + p[i].Y) / 2);
                Gdiplus::PointF c1(cur.X + 2.0f / 3 * (ctrl.X - cur.X), cur.Y + 2.0f / 3 * (ctrl.Y - cur.Y));
                Gdiplus::PointF c2(mid.X + 2.0f / 3 * (ctrl.X - mid.X), mid.Y + 2.0f / 3 * (ctrl.Y - mid.Y));
                path->AddBezier(cur, c1, c2, mid);
                cur = mid;
            }
            path->AddLine(cur, p.back());
        }
    }
    s.path = std::move(path);
    float pad = s.width / 2 + 6;
    s.bounds = RECT{LONG(std::floor(minX - pad)), LONG(std::floor(minY - pad)), LONG(std::ceil(maxX + pad)),
                    LONG(std::ceil(maxY + pad))};
    markDirty(s.bounds);
}

void Ink::draw(Gdiplus::Graphics &g) const {
    Gdiplus::RectF clip;
    g.GetClipBounds(&clip);
    for (const auto &s : strokes_) {
        if (!s->path || s->bounds.right < clip.X || s->bounds.left > clip.X + clip.Width ||
            s->bounds.bottom < clip.Y || s->bounds.top > clip.Y + clip.Height) {
            continue;
        }
        Gdiplus::Pen shadow(th::black(0.18f * s->alpha), s->width + 1.0f);
        shadow.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
        shadow.SetLineJoin(Gdiplus::LineJoinRound);
        Gdiplus::GraphicsState saved = g.Save();
        g.TranslateTransform(1.0f, 1.5f);
        g.DrawPath(&shadow, s->path.get());
        g.Restore(saved);

        Gdiplus::Pen pen(th::rgba(s->color, s->alpha), s->width);
        pen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        g.DrawPath(&pen, s->path.get());
    }
}
