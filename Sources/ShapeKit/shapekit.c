#include "shapekit.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * How it works, in short:
 *   1. Resample the stroke to evenly spaced points (so speed and input rate
 *      do not matter) in an internal frame where +y points down.
 *   2. Straight enough end to end: a line.
 *   3. Ends meet (small gap or an overshoot): a closed loop. Fit both a
 *      polygon (corner detection plus line fits per side) and an ellipse,
 *      match against a heart template, and pick what explains it best.
 *      Five or more self-crossings with five straight sides: a pentagram.
 *   4. Otherwise look for an arrow: a straight shaft plus a small head that
 *      folds back on both sides of the tip.
 *   5. Anything else stays freehand.
 */

#define SK_PI 3.14159265358979323846
#define SK_TAU (2.0 * SK_PI)
#define SK_OPEN_N 128
#define SK_LOOP_N 96
#define SK_TPL_N 64
#define SK_CURVE_N 96
#define SK_HEAD_N 32
#define SK_MAX_V 48
#define SK_MIN_SIZE 16.0

typedef struct {
    double x;
    double y;
} vec2;

typedef struct {
    double minx, miny, maxx, maxy;
} box2;

static vec2 v2(double x, double y)
{
    vec2 r;
    r.x = x;
    r.y = y;
    return r;
}

static vec2 vadd(vec2 a, vec2 b) { return v2(a.x + b.x, a.y + b.y); }
static vec2 vsub(vec2 a, vec2 b) { return v2(a.x - b.x, a.y - b.y); }
static vec2 vscale(vec2 a, double s) { return v2(a.x * s, a.y * s); }
static double vdot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
static double vcross(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }
static double vlen(vec2 a) { return sqrt(a.x * a.x + a.y * a.y); }
static double vdist(vec2 a, vec2 b) { return vlen(vsub(a, b)); }
static vec2 vpolar(double r, double ang) { return v2(r * cos(ang), r * sin(ang)); }

static vec2 vnorm(vec2 a)
{
    double l = vlen(a);
    return l > 1e-12 ? vscale(a, 1.0 / l) : v2(0, 0);
}

static vec2 vrot(vec2 a, double ang)
{
    double c = cos(ang), s = sin(ang);
    return v2(a.x * c - a.y * s, a.x * s + a.y * c);
}

static double rad(double deg) { return deg * SK_PI / 180.0; }

static double wrap(double a)
{
    a = fmod(a + SK_PI, SK_TAU);
    if (a < 0) {
        a += SK_TAU;
    }
    return a - SK_PI;
}

static double clampd(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static double turn_between(vec2 a, vec2 b) { return atan2(vcross(a, b), vdot(a, b)); }

static box2 bounds(const vec2 *p, int n)
{
    box2 b;
    int i;
    b.minx = b.maxx = p[0].x;
    b.miny = b.maxy = p[0].y;
    for (i = 1; i < n; i++) {
        if (p[i].x < b.minx) b.minx = p[i].x;
        if (p[i].x > b.maxx) b.maxx = p[i].x;
        if (p[i].y < b.miny) b.miny = p[i].y;
        if (p[i].y > b.maxy) b.maxy = p[i].y;
    }
    return b;
}

static double box_diag(box2 b) { return hypot(b.maxx - b.minx, b.maxy - b.miny); }

static vec2 mean_of(const vec2 *p, int n)
{
    vec2 m = v2(0, 0);
    int i;
    for (i = 0; i < n; i++) {
        m = vadd(m, p[i]);
    }
    return vscale(m, 1.0 / n);
}

static double polyline_length(const vec2 *p, int n, int closed)
{
    double l = 0;
    int i;
    for (i = 1; i < n; i++) {
        l += vdist(p[i - 1], p[i]);
    }
    if (closed && n > 1) {
        l += vdist(p[n - 1], p[0]);
    }
    return l;
}

/* Principal axis through the mean: centre, unit direction, and the spread along / across it. */
static void principal_axis(const vec2 *p, int n, vec2 *center, vec2 *dir)
{
    vec2 m = mean_of(p, n);
    double sxx = 0, syy = 0, sxy = 0, ang;
    int i;
    for (i = 0; i < n; i++) {
        vec2 d = vsub(p[i], m);
        sxx += d.x * d.x;
        syy += d.y * d.y;
        sxy += d.x * d.y;
    }
    ang = 0.5 * atan2(2 * sxy, sxx - syy);
    *center = m;
    *dir = v2(cos(ang), sin(ang));
}

static vec2 raw_point(const th_point *p, int i, double sy)
{
    return v2((double)p[i].x, (double)p[i].y * sy);
}

/* Evenly spaced points along the raw stroke, y flipped into the internal y-down frame. */
static int resample_raw(const th_point *p, int n, double sy, vec2 *out, int m, double *length)
{
    double total = 0, step, acc = 0;
    vec2 prev;
    int i, k = 1;
    for (i = 1; i < n; i++) {
        total += vdist(raw_point(p, i - 1, sy), raw_point(p, i, sy));
    }
    *length = total;
    if (total <= 1e-9 || m < 2) {
        return 0;
    }
    step = total / (m - 1);
    prev = raw_point(p, 0, sy);
    out[0] = prev;
    for (i = 1; i < n && k < m; i++) {
        vec2 cur = raw_point(p, i, sy);
        double d = vdist(prev, cur);
        while (d > 0 && acc + d >= step && k < m) {
            double t = (step - acc) / d;
            vec2 q = vadd(prev, vscale(vsub(cur, prev), t));
            out[k++] = q;
            prev = q;
            d = vdist(prev, cur);
            acc = 0;
        }
        acc += d;
        prev = cur;
    }
    while (k < m) {
        out[k++] = raw_point(p, n - 1, sy);
    }
    return 1;
}

/* Evenly spaced points along a polyline. Closed: m points around the loop, starting at p[0]. */
static int resample(const vec2 *p, int n, int closed, vec2 *out, int m)
{
    double total = polyline_length(p, n, closed), step, acc = 0;
    vec2 prev;
    int i, k = 1, segs = closed ? n : n - 1;
    if (total <= 1e-9 || n < 2 || m < 2) {
        return 0;
    }
    step = closed ? total / m : total / (m - 1);
    prev = p[0];
    out[0] = prev;
    for (i = 1; i <= segs && k < m; i++) {
        vec2 cur = p[i % n];
        double d = vdist(prev, cur);
        while (d > 0 && acc + d >= step && k < m) {
            double t = (step - acc) / d;
            vec2 q = vadd(prev, vscale(vsub(cur, prev), t));
            out[k++] = q;
            prev = q;
            d = vdist(prev, cur);
            acc = 0;
        }
        acc += d;
        prev = cur;
    }
    while (k < m) {
        out[k++] = closed ? p[0] : p[n - 1];
    }
    return 1;
}

static void shape_clear(th_shape *s)
{
    memset(s, 0, sizeof *s);
    s->kind = TH_SHAPE_NONE;
}

static int shape_add(th_shape *s, double sy, const vec2 *p, int n, int closed)
{
    int i, start;
    if (n <= 0 || s->path_count >= TH_SHAPE_MAX_PATHS || s->point_count + n > TH_SHAPE_MAX_POINTS) {
        return 0;
    }
    start = s->point_count;
    for (i = 0; i < n; i++) {
        s->points[start + i].x = (float)p[i].x;
        s->points[start + i].y = (float)(p[i].y * sy);
    }
    s->path_start[s->path_count] = start;
    s->path_length[s->path_count] = n;
    s->path_closed[s->path_count] = closed ? 1 : 0;
    s->path_count++;
    s->point_count += n;
    return 1;
}

static th_shape_kind finish(th_shape *o, th_shape_kind kind)
{
    o->kind = kind;
    return kind;
}

/* Snap an angle to the nearest of base + m * step when within tol. */
static double snap_angle(double ang, double base, double step, double tol)
{
    double rel = ang - base;
    double m = floor(rel / step + 0.5);
    if (fabs(rel - m * step) < tol) {
        return base + m * step;
    }
    return ang;
}

/* Best-fit rotation of an n-fold symmetric set of vertex angles around c. */
static double nfold_rotation(const vec2 *v, int count, vec2 c, int fold)
{
    double sx = 0, sy = 0;
    int i;
    for (i = 0; i < count; i++) {
        double a = atan2(v[i].y - c.y, v[i].x - c.x);
        sx += cos(fold * a);
        sy += sin(fold * a);
    }
    return atan2(sy, sx) / fold;
}

static void regular_polygon(vec2 c, double r, int n, double rot, vec2 *out)
{
    int j;
    for (j = 0; j < n; j++) {
        out[j] = vadd(c, vpolar(r, rot + SK_TAU * j / n));
    }
}

static void star_points(vec2 c, double outer, double inner, double rot, vec2 *out)
{
    int j;
    for (j = 0; j < 10; j++) {
        out[j] = vadd(c, vpolar(j % 2 ? inner : outer, rot + SK_PI * j / 5));
    }
}

/* ---------- Lines and arrows ---------- */

static vec2 snap_segment_end(vec2 fixed, vec2 moving, double tol)
{
    vec2 d = vsub(moving, fixed);
    double ang = atan2(d.y, d.x);
    double snapped = snap_angle(ang, 0, SK_PI / 2, tol);
    return vadd(fixed, vpolar(vlen(d), snapped));
}

static th_shape_kind build_arrow(vec2 tail, vec2 tip, double barb, double sy, th_shape *o)
{
    vec2 shaft[2], head[3], back;
    tail = snap_segment_end(tip, tail, rad(6));
    back = vnorm(vsub(tail, tip));
    shaft[0] = tail;
    shaft[1] = tip;
    head[0] = vadd(tip, vscale(vrot(back, rad(30)), barb));
    head[1] = tip;
    head[2] = vadd(tip, vscale(vrot(back, -rad(30)), barb));
    shape_add(o, sy, shaft, 2, 0);
    shape_add(o, sy, head, 3, 0);
    return finish(o, TH_SHAPE_ARROW);
}

static th_shape_kind try_line(const vec2 *s, int n, double length, double sy, th_shape *o)
{
    vec2 c, d, p0, p1, mid, half;
    double chord = vdist(s[0], s[n - 1]), maxdev = 0, ang, snapped;
    int i;
    double backtrack = 0, coarse = 0;
    for (i = 4; i < n; i += 4) {
        coarse += vdist(s[i - 4], s[i]);
    }
    coarse += vdist(s[(n - 1) / 4 * 4], s[n - 1]);
    /* Length measured on every fourth point, so hand jitter does not count but a zigzag does. */
    if (chord < 0.92 * coarse || chord < 0.75 * length || chord < SK_MIN_SIZE) {
        return TH_SHAPE_NONE;
    }
    principal_axis(s, n, &c, &d);
    if (vdot(d, vsub(s[n - 1], s[0])) < 0) {
        d = vscale(d, -1);
    }
    for (i = 0; i < n; i++) {
        double dev = fabs(vcross(d, vsub(s[i], c)));
        if (dev > maxdev) maxdev = dev;
        if (i > 0) {
            double step = vdot(d, vsub(s[i], s[i - 1]));
            if (step < 0) backtrack -= step;
        }
    }
    /* Straight, and never doubling back (a scribbled-over line is not a line). */
    if (maxdev > 0.06 * chord || backtrack > 0.08 * chord) {
        return TH_SHAPE_NONE;
    }
    p0 = vadd(c, vscale(d, vdot(vsub(s[0], c), d)));
    p1 = vadd(c, vscale(d, vdot(vsub(s[n - 1], c), d)));
    mid = vscale(vadd(p0, p1), 0.5);
    half = vsub(p1, mid);
    ang = atan2(half.y, half.x);
    snapped = snap_angle(ang, 0, SK_PI / 2, rad(6));
    if (snapped != ang) {
        half = vpolar(vlen(half), snapped);
        p0 = vsub(mid, half);
        p1 = vadd(mid, half);
    }
    {
        vec2 seg[2];
        seg[0] = p0;
        seg[1] = p1;
        shape_add(o, sy, seg, 2, 0);
    }
    return finish(o, TH_SHAPE_LINE);
}

static th_shape_kind try_arrow(const vec2 *s, int n, double size, double sy, th_shape *o)
{
    vec2 p[SK_OPEN_N];
    int pass, i;
    for (pass = 0; pass < 2; pass++) {
        int tip = 0, ok = 1;
        double shaft = 0, maxdev = 0, reach = 0, side_pos = 0, side_neg = 0, fwd = -1e9, head_len = 0;
        vec2 d, nrm, tail, tp;
        for (i = 0; i < n; i++) {
            p[i] = pass ? s[n - 1 - i] : s[i];
        }
        for (i = 1; i < n; i++) {
            double dd = vdist(p[0], p[i]);
            if (dd > shaft) {
                shaft = dd;
            }
        }
        /* The head of a triangle-headed arrow returns to the tip: take the first visit. */
        for (i = 1; i < n; i++) {
            if (vdist(p[0], p[i]) >= 0.97 * shaft) {
                tip = i;
                break;
            }
        }
        for (i = tip + 1; i < n && i <= tip + 4; i++) {
            if (vdist(p[0], p[i]) > vdist(p[0], p[tip])) {
                tip = i;
            }
        }
        shaft = vdist(p[0], p[tip]);
        if (tip < 0.45 * (n - 1) || tip > 0.93 * (n - 1) || shaft < 0.5 * size) {
            continue;
        }
        tail = p[0];
        tp = p[tip];
        d = vnorm(vsub(tp, tail));
        nrm = v2(-d.y, d.x);
        for (i = 0; i <= tip; i++) {
            double dev = fabs(vcross(d, vsub(p[i], tail)));
            if (dev > maxdev) maxdev = dev;
        }
        if (maxdev > 0.07 * shaft) {
            continue;
        }
        for (i = tip + 1; i < n; i++) {
            vec2 w = vsub(p[i], tp);
            double u = vdot(w, d), v = vdot(w, nrm), r = vlen(w);
            if (r > reach) reach = r;
            if (v > side_pos) side_pos = v;
            if (v < side_neg) side_neg = v;
            if (u > fwd) fwd = u;
            head_len += vdist(p[i - 1], p[i]);
        }
        if (reach < 0.08 * shaft || reach > 0.6 * shaft || fwd > 0.25 * reach) {
            continue;
        }
        if (side_pos < 0.2 * reach || -side_neg < 0.2 * reach) {
            continue;
        }
        if (head_len < 1.5 * reach || head_len > 5.0 * reach) {
            continue;
        }
        for (i = tip + 1; i < n && ok; i++) {
            vec2 w = vsub(p[i], tp);
            double r = vlen(w);
            if (r > 0.35 * reach && vdot(w, d) / r > -0.2) {
                ok = 0;
            }
        }
        if (!ok) {
            continue;
        }
        return build_arrow(tail, tp, clampd(reach, 0.1 * shaft, 0.4 * shaft), sy, o);
    }
    return TH_SHAPE_NONE;
}

/* ---------- Closed loops ---------- */

/*
 * Finds where the stroke closes on itself and resamples that loop. One end of
 * the stroke has to come back to the other end's neighbourhood: either the two
 * ends nearly meet (a gap) or one end runs past the other (an overshoot).
 */
static int find_loop(const vec2 *s, int n, double size, double length, vec2 *loop)
{
    int win = (int)(0.2 * (n - 1)), i, bi = 0, bj = n - 1;
    double best = vdist(s[0], s[n - 1]), minor, lo = 1e300, hi = -1e300;
    vec2 c, d, nrm;
    for (i = n - 1 - win; i < n; i++) {
        double dd = vdist(s[0], s[i]);
        if (dd < best) {
            best = dd;
            bi = 0;
            bj = i;
        }
    }
    for (i = 0; i <= win; i++) {
        double dd = vdist(s[i], s[n - 1]);
        if (dd < best) {
            best = dd;
            bi = i;
            bj = n - 1;
        }
    }
    if (bj - bi < 0.6 * (n - 1)) {
        return 0;
    }
    principal_axis(s + bi, bj - bi + 1, &c, &d);
    nrm = v2(-d.y, d.x);
    for (i = bi; i <= bj; i++) {
        double v = vdot(vsub(s[i], c), nrm);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    minor = hi - lo;
    if ((best > 0.2 * size && best > 0.07 * length) || best > 0.6 * minor) {
        return 0;
    }
    return resample(s + bi, bj - bi + 1, 1, loop, SK_LOOP_N);
}

static int segments_cross(vec2 a, vec2 b, vec2 c, vec2 d)
{
    double d1 = vcross(vsub(b, a), vsub(c, a));
    double d2 = vcross(vsub(b, a), vsub(d, a));
    double d3 = vcross(vsub(d, c), vsub(a, c));
    double d4 = vcross(vsub(d, c), vsub(b, c));
    return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}

static int count_crossings(const vec2 *p, int m)
{
    int i, j, count = 0;
    for (i = 0; i < m; i++) {
        for (j = i + 2; j < m; j++) {
            if (i == 0 && j == m - 1) {
                continue;
            }
            if (segments_cross(p[i], p[(i + 1) % m], p[j], p[(j + 1) % m])) {
                count++;
            }
        }
    }
    return count;
}

/*
 * Cuts out small self-loops (an overshoot that curls back over the start, a
 * scribble at a heart's cusp) so they do not count as crossings or turns.
 */
static void drop_small_loops(vec2 *p, int m)
{
    vec2 tmp[SK_LOOP_N];
    int pass;
    for (pass = 0; pass < 3; pass++) {
        int i, j, t, keep = 0, found = 0;
        for (i = 0; i < m && !found; i++) {
            for (j = i + 2; j < m && !found; j++) {
                int inner = j - i;
                if (i == 0 && j == m - 1) {
                    continue;
                }
                if (!segments_cross(p[i], p[(i + 1) % m], p[j], p[(j + 1) % m])) {
                    continue;
                }
                if (inner <= 0.15 * m) {
                    for (t = 0; t <= i; t++) tmp[keep++] = p[t];
                    for (t = j + 1; t < m; t++) tmp[keep++] = p[t];
                    found = 1;
                } else if (m - inner <= 0.15 * m) {
                    for (t = i + 1; t <= j; t++) tmp[keep++] = p[t];
                    found = 1;
                }
            }
        }
        if (!found || keep < 8 || !resample(tmp, keep, 1, p, m)) {
            return;
        }
    }
}

static double total_turning(const vec2 *p, int m)
{
    double sum = 0;
    int i;
    for (i = 0; i < m; i++) {
        vec2 a = vsub(p[i], p[(i - 1 + m) % m]);
        vec2 b = vsub(p[(i + 1) % m], p[i]);
        sum += turn_between(a, b);
    }
    return sum;
}

static void dp_mark(const vec2 *p, int a, int b, double tol, unsigned char *keep)
{
    vec2 d;
    double len, best = -1;
    int i, at = -1;
    if (b <= a + 1) {
        return;
    }
    d = vsub(p[b], p[a]);
    len = vlen(d);
    for (i = a + 1; i < b; i++) {
        double dev = len > 1e-9 ? fabs(vcross(d, vsub(p[i], p[a]))) / len : vdist(p[i], p[a]);
        if (dev > best) {
            best = dev;
            at = i;
        }
    }
    if (best > tol) {
        keep[at] = 1;
        dp_mark(p, a, at, tol, keep);
        dp_mark(p, at, b, tol, keep);
    }
}

/* Douglas-Peucker around a closed loop; returns loop indices of the corners in order. */
static int closed_dp(const vec2 *p, int m, double tol, int *idx)
{
    vec2 r[SK_LOOP_N + 1], c = mean_of(p, m);
    unsigned char keep[SK_LOOP_N + 1];
    int i, a = 0, b = 0, bb, k = 0;
    double far = -1;
    for (i = 0; i < m; i++) {
        double dd = vdist(p[i], c);
        if (dd > far) {
            far = dd;
            a = i;
        }
    }
    far = -1;
    for (i = 0; i < m; i++) {
        double dd = vdist(p[i], p[a]);
        if (dd > far) {
            far = dd;
            b = i;
        }
    }
    for (i = 0; i <= m; i++) {
        r[i] = p[(a + i) % m];
        keep[i] = 0;
    }
    bb = (b - a + m) % m;
    if (bb == 0) {
        bb = m / 2;
    }
    keep[0] = keep[bb] = 1;
    dp_mark(r, 0, bb, tol, keep);
    dp_mark(r, bb, m, tol, keep);
    for (i = 0; i < m && k < SK_MAX_V; i++) {
        if (keep[i]) {
            idx[k++] = (a + i) % m;
        }
    }
    return k;
}

static double vertex_turn(const vec2 *p, const int *idx, int k, int j)
{
    vec2 a = p[idx[(j - 1 + k) % k]], b = p[idx[j]], c = p[idx[(j + 1) % k]];
    return turn_between(vsub(b, a), vsub(c, b));
}

static void remove_at(int *idx, int *k, int j)
{
    int i;
    for (i = j; i < *k - 1; i++) {
        idx[i] = idx[i + 1];
    }
    (*k)--;
}

/* Drops corners that barely turn and merges corners split in two by a rounded drawing. */
static int simplify_corners(const vec2 *p, int m, double perim, int *idx, int k)
{
    int guard;
    for (guard = 0; guard < 4 * SK_MAX_V && k > 3; guard++) {
        int j, jmin = -1, emin = -1;
        double tmin = 1e9, lmin = 1e300;
        for (j = 0; j < k; j++) {
            double t = fabs(vertex_turn(p, idx, k, j));
            if (t < tmin) {
                tmin = t;
                jmin = j;
            }
        }
        if (tmin < rad(22)) {
            remove_at(idx, &k, jmin);
            continue;
        }
        for (j = 0; j < k; j++) {
            double l = vdist(p[idx[j]], p[idx[(j + 1) % k]]);
            /* Only a corner split in two turns the same way at both ends (a star's tip and notch do not). */
            if (l < lmin && vertex_turn(p, idx, k, j) * vertex_turn(p, idx, k, (j + 1) % k) > 0) {
                lmin = l;
                emin = j;
            }
        }
        if (emin >= 0 && lmin < 0.07 * perim) {
            int j2 = (emin + 1) % k;
            int span = (idx[j2] - idx[emin] + m) % m;
            idx[emin] = (idx[emin] + span / 2) % m;
            remove_at(idx, &k, j2);
            continue;
        }
        break;
    }
    return k;
}

typedef struct {
    int k;
    int idx[SK_MAX_V];
    vec2 v[SK_MAX_V];
    double sharp_mean;
    double sharp_min;
    double straight_max;
} poly_fit;

static vec2 loop_tangent(const vec2 *p, int m, int i)
{
    int a = ((i - 2) % m + m) % m, b = ((i + 2) % m + m) % m;
    return vnorm(vsub(p[b], p[a]));
}

static double loop_turn(const vec2 *p, int m, int center, int half)
{
    return turn_between(loop_tangent(p, m, center - half), loop_tangent(p, m, center + half));
}

/* Samples skipped at each end of a side, so rounded corners do not count as bent sides. */
static int edge_trim(int span)
{
    int t = (int)(0.1 * span);
    return t < 2 ? 2 : t;
}

/*
 * Fits a straight line to the middle of each side and puts each corner where
 * neighbouring sides meet, which also restores corners a drawing rounded off.
 * Returns how far the most bent side strays from straight, relative to its length.
 */
static double fit_sides(const vec2 *p, int m, double size, const int *idx, int k, double reach, vec2 *v)
{
    vec2 lc[SK_MAX_V], ld[SK_MAX_V], tmp[SK_LOOP_N + 1];
    double straight = 0;
    int j;
    for (j = 0; j < k; j++) {
        int a = idx[j], b = idx[(j + 1) % k];
        int span = (b - a + m) % m, t0, t1, t, cnt = 0;
        vec2 pa = p[a], pb = p[b], chord = vsub(pb, pa);
        double clen = vlen(chord);
        if (span == 0) {
            span = m;
        }
        t0 = (int)floor(0.15 * span);
        t1 = span - t0;
        if (t1 - t0 < 2) {
            t0 = 0;
            t1 = span;
        }
        for (t = t0; t <= t1; t++) {
            tmp[cnt++] = p[(a + t) % m];
        }
        principal_axis(tmp, cnt, &lc[j], &ld[j]);
        for (t = edge_trim(span); t <= span - edge_trim(span); t++) {
            double dev = clen > 1e-9 ? fabs(vcross(chord, vsub(p[(a + t) % m], pa))) / clen : 0;
            if (clen > 1e-9 && dev / clen > straight) {
                straight = dev / clen;
            }
        }
    }
    for (j = 0; j < k; j++) {
        int pj = (j - 1 + k) % k;
        vec2 corner = p[idx[j]];
        double den = vcross(ld[pj], ld[j]);
        if (fabs(den) > sin(rad(10))) {
            double t = vcross(vsub(lc[j], lc[pj]), ld[j]) / den;
            vec2 x = vadd(lc[pj], vscale(ld[pj], t));
            if (vdist(x, corner) < reach * size) {
                corner = x;
            }
        }
        v[j] = corner;
    }
    return straight;
}

static int fit_polygon(const vec2 *p, int m, double size, double perim, poly_fit *f)
{
    int j, k;
    k = closed_dp(p, m, 0.04 * size, f->idx);
    k = simplify_corners(p, m, perim, f->idx, k);
    f->k = k;
    f->sharp_mean = 0;
    f->sharp_min = 1e9;
    f->straight_max = 0;
    if (k < 3) {
        return 0;
    }
    f->straight_max = fit_sides(p, m, size, f->idx, k, 0.2, f->v);
    for (j = 0; j < k; j++) {
        int i = f->idx[j];
        int sp = (f->idx[j] - f->idx[(j - 1 + k) % k] + m) % m;
        int sn = (f->idx[(j + 1) % k] - f->idx[j] + m) % m;
        int span = sp < sn ? sp : sn;
        int wide = span / 2, c;
        double narrow = 0, wturn, sharp;
        if (wide > 8) wide = 8;
        if (wide < 4) wide = 4;
        wturn = loop_turn(p, m, i, wide);
        for (c = -1; c <= 1; c++) {
            double t = loop_turn(p, m, i + c, 3);
            if (t * wturn > 0 && fabs(t) > fabs(narrow)) {
                narrow = t;
            }
        }
        sharp = fabs(wturn) > rad(10) ? fabs(narrow) / fabs(wturn) : 1.0;
        f->sharp_mean += sharp;
        if (sharp < f->sharp_min) {
            f->sharp_min = sharp;
        }
    }
    f->sharp_mean /= k;
    return 1;
}

typedef struct {
    vec2 c;
    double a, b, phi;
    double err_mean, err_max;
} ellipse_fit;

static void fit_ellipse(const vec2 *p, int m, ellipse_fit *e)
{
    vec2 c, u, v;
    double umin = 1e300, umax = -1e300, vmin = 1e300, vmax = -1e300;
    int i;
    principal_axis(p, m, &c, &u);
    v = v2(-u.y, u.x);
    for (i = 0; i < m; i++) {
        vec2 d = vsub(p[i], c);
        double pu = vdot(d, u), pv = vdot(d, v);
        if (pu < umin) umin = pu;
        if (pu > umax) umax = pu;
        if (pv < vmin) vmin = pv;
        if (pv > vmax) vmax = pv;
    }
    e->c = vadd(c, vadd(vscale(u, 0.5 * (umin + umax)), vscale(v, 0.5 * (vmin + vmax))));
    e->a = 0.5 * (umax - umin);
    e->b = 0.5 * (vmax - vmin);
    e->phi = atan2(u.y, u.x);
    e->err_mean = 0;
    e->err_max = 0;
    if (e->a < 1e-6 || e->b < 1e-6) {
        e->err_mean = e->err_max = 1e9;
        return;
    }
    for (i = 0; i < m; i++) {
        vec2 d = vsub(p[i], e->c);
        double r = hypot(vdot(d, u) / e->a, vdot(d, v) / e->b);
        double err = fabs(r - 1);
        e->err_mean += err;
        if (err > e->err_max) e->err_max = err;
    }
    e->err_mean /= m;
    if (e->a < e->b) {
        double t = e->a;
        e->a = e->b;
        e->b = t;
        e->phi += SK_PI / 2;
    }
}

static void unit_box(vec2 *p, int n)
{
    box2 b = bounds(p, n);
    double w = b.maxx - b.minx, h = b.maxy - b.miny;
    int i;
    if (w < 1e-9) w = 1;
    if (h < 1e-9) h = 1;
    for (i = 0; i < n; i++) {
        p[i] = v2((p[i].x - b.minx) / w, (p[i].y - b.miny) / h);
    }
}

/* Mean point distance between two closed curves, best over start point and direction. */
static double loop_distance(const vec2 *a, const vec2 *b, int n)
{
    double best = 1e300;
    int dir, s, i;
    for (dir = -1; dir <= 1; dir += 2) {
        for (s = 0; s < n; s++) {
            double sum = 0;
            for (i = 0; i < n && sum < best; i++) {
                int j = ((s + dir * i) % n + n) % n;
                sum += vdist(a[i], b[j]);
            }
            if (sum < best) {
                best = sum;
            }
        }
    }
    return best / n;
}

/* Classic heart curve, lobes up and point down in the y-down frame. */
static void heart_curve(vec2 *out, int n)
{
    vec2 dense[256];
    int i;
    for (i = 0; i < 256; i++) {
        double t = SK_TAU * i / 256, s = sin(t);
        dense[i] = v2(16 * s * s * s, -(13 * cos(t) - 5 * cos(2 * t) - 2 * cos(3 * t) - cos(4 * t)));
    }
    resample(dense, 256, 1, out, n);
}

static void circle_curve(vec2 *out, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        out[i] = vpolar(1, SK_TAU * i / n);
    }
}

static th_shape_kind make_ellipse(const ellipse_fit *e, double sy, th_shape *o)
{
    vec2 pts[SK_CURVE_N];
    int i;
    if (e->b / e->a >= 0.82) {
        double r = 0.5 * (e->a + e->b);
        for (i = 0; i < SK_CURVE_N; i++) {
            pts[i] = vadd(e->c, vpolar(r, SK_TAU * i / SK_CURVE_N));
        }
        shape_add(o, sy, pts, SK_CURVE_N, 1);
        return finish(o, TH_SHAPE_CIRCLE);
    }
    {
        double phi = snap_angle(e->phi, 0, SK_PI / 2, rad(10));
        for (i = 0; i < SK_CURVE_N; i++) {
            double t = SK_TAU * i / SK_CURVE_N;
            vec2 q = v2(e->a * cos(t), e->b * sin(t));
            pts[i] = vadd(e->c, vrot(q, phi));
        }
    }
    shape_add(o, sy, pts, SK_CURVE_N, 1);
    return finish(o, TH_SHAPE_ELLIPSE);
}

static th_shape_kind make_heart(const vec2 *loop, int m, double sy, th_shape *o)
{
    vec2 pts[SK_CURVE_N];
    box2 lb = bounds(loop, m), hb;
    double w = lb.maxx - lb.minx, h = lb.maxy - lb.miny;
    int i;
    heart_curve(pts, SK_CURVE_N);
    hb = bounds(pts, SK_CURVE_N);
    for (i = 0; i < SK_CURVE_N; i++) {
        pts[i] = v2(lb.minx + (pts[i].x - hb.minx) / (hb.maxx - hb.minx) * w,
                    lb.miny + (pts[i].y - hb.miny) / (hb.maxy - hb.miny) * h);
    }
    shape_add(o, sy, pts, SK_CURVE_N, 1);
    return finish(o, TH_SHAPE_HEART);
}

static double min_interior_angle(const vec2 *v, int k)
{
    double best = 1e9;
    int j;
    for (j = 0; j < k; j++) {
        vec2 a = vsub(v[(j - 1 + k) % k], v[j]), b = vsub(v[(j + 1) % k], v[j]);
        double ang = fabs(turn_between(a, b));
        if (ang < best) best = ang;
    }
    return best;
}

static th_shape_kind make_triangle(const vec2 *v, double sy, th_shape *o)
{
    vec2 out[3], c = mean_of(v, 3);
    double l[3], lmin, lmax;
    int j;
    if (min_interior_angle(v, 3) < rad(12)) {
        return TH_SHAPE_NONE;
    }
    for (j = 0; j < 3; j++) {
        l[j] = vdist(v[j], v[(j + 1) % 3]);
    }
    lmin = fmin(l[0], fmin(l[1], l[2]));
    lmax = fmax(l[0], fmax(l[1], l[2]));
    if (lmax / lmin < 1.2) {
        double r = (vdist(v[0], c) + vdist(v[1], c) + vdist(v[2], c)) / 3;
        double rot = snap_angle(nfold_rotation(v, 3, c, 3), -SK_PI / 2, SK_PI / 3, rad(12));
        regular_polygon(c, r, 3, rot, out);
    } else {
        double best = 1e9, turn = 0;
        for (j = 0; j < 3; j++) {
            vec2 e = vsub(v[(j + 1) % 3], v[j]);
            double ang = atan2(e.y, e.x);
            double off = ang - snap_angle(ang, 0, SK_PI / 2, SK_PI);
            if (fabs(off) < best) {
                best = fabs(off);
                turn = -off;
            }
        }
        for (j = 0; j < 3; j++) {
            out[j] = best < rad(8) ? vadd(c, vrot(vsub(v[j], c), turn)) : v[j];
        }
    }
    shape_add(o, sy, out, 3, 1);
    return finish(o, TH_SHAPE_TRIANGLE);
}

static int near_axis(vec2 d, double axis, double tol)
{
    double ang = atan2(d.y, d.x);
    double rel = wrap(2 * (ang - axis)) / 2;
    return fabs(rel) < tol;
}

static th_shape_kind make_quad(const vec2 *v, double sy, th_shape *o)
{
    vec2 out[4], c = mean_of(v, 4), d02 = vsub(v[2], v[0]), d13 = vsub(v[3], v[1]);
    double side[4], smin = 1e300, smax = 0, ang_dev = 0;
    int j, rhombus, diag_axis_tight, diag_axis_loose, is_rect;
    for (j = 0; j < 4; j++) {
        vec2 a = vsub(v[(j + 3) % 4], v[j]), b = vsub(v[(j + 1) % 4], v[j]);
        double interior = fabs(turn_between(a, b));
        double dev = fabs(interior - SK_PI / 2);
        if (dev > ang_dev) ang_dev = dev;
        side[j] = vdist(v[j], v[(j + 1) % 4]);
        if (side[j] < smin) smin = side[j];
        if (side[j] > smax) smax = side[j];
    }
    if (smin < 1e-6) {
        return TH_SHAPE_NONE;
    }
    is_rect = ang_dev < rad(22);
    rhombus = smax / smin < 1.3;
    diag_axis_tight = (near_axis(d02, 0, rad(12)) && near_axis(d13, SK_PI / 2, rad(12)))
        || (near_axis(d02, SK_PI / 2, rad(12)) && near_axis(d13, 0, rad(12)));
    diag_axis_loose = (near_axis(d02, 0, rad(18)) && near_axis(d13, SK_PI / 2, rad(18)))
        || (near_axis(d02, SK_PI / 2, rad(18)) && near_axis(d13, 0, rad(18)));
    if ((diag_axis_tight && rhombus) || (!is_rect && diag_axis_loose && smax / smin < 1.35)) {
        int d02_flat = fabs(d02.x) > fabs(d02.y);
        double hw = 0.5 * fabs(d02_flat ? d02.x : d13.x);
        double hh = 0.5 * fabs(d02_flat ? d13.y : d02.y);
        if (hw < 1e-6 || hh < 1e-6) {
            return TH_SHAPE_NONE;
        }
        out[0] = v2(c.x, c.y - hh);
        out[1] = v2(c.x + hw, c.y);
        out[2] = v2(c.x, c.y + hh);
        out[3] = v2(c.x - hw, c.y);
        shape_add(o, sy, out, 4, 1);
        return finish(o, TH_SHAPE_DIAMOND);
    }
    if (is_rect) {
        double sx = 0, syy = 0, theta, hu = 0, hv = 0;
        vec2 u, w;
        th_shape_kind kind = TH_SHAPE_RECTANGLE;
        for (j = 0; j < 4; j++) {
            vec2 e = vsub(v[(j + 1) % 4], v[j]);
            double a = atan2(e.y, e.x);
            double len = vlen(e);
            sx += len * cos(4 * a);
            syy += len * sin(4 * a);
        }
        theta = snap_angle(atan2(syy, sx) / 4, 0, SK_PI / 2, rad(10));
        u = vpolar(1, theta);
        w = v2(-u.y, u.x);
        for (j = 0; j < 4; j++) {
            vec2 d = vsub(v[j], c);
            hu += fabs(vdot(d, u));
            hv += fabs(vdot(d, w));
        }
        hu /= 4;
        hv /= 4;
        if (fmin(hu, hv) / fmax(hu, hv) >= 0.85) {
            hu = hv = 0.5 * (hu + hv);
            kind = TH_SHAPE_SQUARE;
        }
        out[0] = vadd(c, vadd(vscale(u, -hu), vscale(w, -hv)));
        out[1] = vadd(c, vadd(vscale(u, hu), vscale(w, -hv)));
        out[2] = vadd(c, vadd(vscale(u, hu), vscale(w, hv)));
        out[3] = vadd(c, vadd(vscale(u, -hu), vscale(w, hv)));
        shape_add(o, sy, out, 4, 1);
        return finish(o, kind);
    }
    return TH_SHAPE_NONE;
}

/* Pentagon or hexagon: regular when close to it, otherwise the straightened outline (a house, say). */
static th_shape_kind make_ngon(const vec2 *v, int k, double sy, th_shape *o)
{
    vec2 out[6], c = mean_of(v, k);
    double rmean = 0, rvar = 0, smin = 1e300, smax = 0;
    int j;
    for (j = 0; j < k; j++) {
        double s = vdist(v[j], v[(j + 1) % k]);
        rmean += vdist(v[j], c);
        if (s < smin) smin = s;
        if (s > smax) smax = s;
    }
    rmean /= k;
    for (j = 0; j < k; j++) {
        double d = vdist(v[j], c) - rmean;
        rvar += d * d;
    }
    if (sqrt(rvar / k) / rmean < 0.15 && smax / smin < 1.6) {
        double rot = nfold_rotation(v, k, c, k);
        rot = snap_angle(rot, -SK_PI / 2, SK_PI / k, k == 5 ? rad(10) : rad(8));
        regular_polygon(c, rmean, k, rot, out);
    } else {
        if (min_interior_angle(v, k) < rad(25)) {
            return TH_SHAPE_NONE;
        }
        for (j = 0; j < k; j++) {
            out[j] = v[j];
        }
    }
    shape_add(o, sy, out, k, 1);
    return finish(o, k == 5 ? TH_SHAPE_PENTAGON : TH_SHAPE_HEXAGON);
}

static th_shape_kind make_star(vec2 c, double outer, double inner, double rot, double sy, th_shape *o)
{
    vec2 out[10];
    rot = snap_angle(rot, -SK_PI / 2, SK_TAU / 5, rad(12));
    star_points(c, outer, inner, rot, out);
    shape_add(o, sy, out, 10, 1);
    return finish(o, TH_SHAPE_STAR);
}

static th_shape_kind make_star_outline(const vec2 *v, double sy, th_shape *o)
{
    vec2 c = mean_of(v, 10), outer[5];
    double r[10], even = 0, odd = 0, omin = 1e300, imax = 0, ro = 0, ri = 0;
    int j, first;
    for (j = 0; j < 10; j++) {
        r[j] = vdist(v[j], c);
        if (j % 2) {
            odd += r[j];
        } else {
            even += r[j];
        }
    }
    first = even > odd ? 0 : 1;
    for (j = 0; j < 10; j++) {
        if (j % 2 == first) {
            outer[j / 2] = v[j];
            ro += r[j];
            if (r[j] < omin) omin = r[j];
        } else {
            ri += r[j];
            if (r[j] > imax) imax = r[j];
        }
    }
    ro /= 5;
    ri /= 5;
    if (imax > 0.9 * omin || ri / ro > 0.75) {
        return TH_SHAPE_NONE;
    }
    c = mean_of(outer, 5);
    return make_star(c, ro, ro * clampd(ri / ro, 0.32, 0.55), nfold_rotation(outer, 5, c, 5), sy, o);
}

static th_shape_kind try_pentagram(const vec2 *p, int m, double size, double sy, th_shape *o)
{
    int idx[SK_MAX_V], k, j, sign = 0;
    vec2 v[5], c;
    double perim = polyline_length(p, m, 1), rmean = 0;
    k = closed_dp(p, m, 0.05 * size, idx);
    k = simplify_corners(p, m, perim, idx, k);
    if (k != 5) {
        return TH_SHAPE_NONE;
    }
    /* Five long crossing strokes: a gap at the start can clip a tip, so let the sides rebuild it. */
    if (fit_sides(p, m, size, idx, 5, 0.35, v) > 0.25) {
        return TH_SHAPE_NONE;
    }
    c = mean_of(v, 5);
    for (j = 0; j < 5; j++) {
        double a0 = atan2(v[j].y - c.y, v[j].x - c.x);
        double a1 = atan2(v[(j + 1) % 5].y - c.y, v[(j + 1) % 5].x - c.x);
        double step = wrap(a1 - a0);
        int s = step > 0 ? 1 : -1;
        if (fabs(step) < rad(108)) {
            return TH_SHAPE_NONE;
        }
        if (sign == 0) {
            sign = s;
        } else if (s != sign) {
            return TH_SHAPE_NONE;
        }
        rmean += vdist(v[j], c);
    }
    rmean /= 5;
    for (j = 0; j < 5; j++) {
        if (fabs(vdist(v[j], c) - rmean) > 0.3 * rmean) {
            return TH_SHAPE_NONE;
        }
    }
    return make_star(c, rmean, rmean * 0.382, nfold_rotation(v, 5, c, 5), sy, o);
}

static th_shape_kind classify_loop(vec2 *p, int m, double sy, th_shape *o)
{
    box2 b = bounds(p, m);
    double size = box_diag(b), perim = polyline_length(p, m, 1), turning;
    int crossings;
    poly_fit poly;
    ellipse_fit ell;
    vec2 cand[SK_TPL_N], heart[SK_TPL_N], circle[SK_TPL_N];
    double d_heart, d_circle;
    int poly_good, ell_good, simple;

    if (size < SK_MIN_SIZE) {
        return TH_SHAPE_NONE;
    }
    drop_small_loops(p, m);
    perim = polyline_length(p, m, 1);
    crossings = count_crossings(p, m);
    if (crossings >= 3) {
        return try_pentagram(p, m, size, sy, o);
    }
    /* One full turn for a simple outline. A heart's cusp can read as +-180, so it is checked apart. */
    turning = fabs(total_turning(p, m));
    simple = turning > rad(250) && turning < rad(470);
    fit_ellipse(p, m, &ell);
    if (ell.b / ell.a < 0.12) {
        return TH_SHAPE_NONE;
    }

    fit_polygon(p, m, size, perim, &poly);
    /* Corners decide it: a polygon turns all at once at each corner, a round shape turns everywhere. */
    poly_good = simple && (poly.k == 3 || poly.k == 4 || poly.k == 5 || poly.k == 6 || poly.k == 10)
        && poly.sharp_mean >= 0.82 && poly.sharp_min >= 0.5 && poly.straight_max < (poly.k == 10 ? 0.3 : 0.14)
        && !(ell.err_mean < 0.03 && poly.sharp_mean < 0.9);
    if (poly_good) {
        th_shape_kind kind = TH_SHAPE_NONE;
        switch (poly.k) {
        case 3: kind = make_triangle(poly.v, sy, o); break;
        case 4: kind = make_quad(poly.v, sy, o); break;
        case 5:
        case 6: kind = make_ngon(poly.v, poly.k, sy, o); break;
        case 10: kind = make_star_outline(poly.v, sy, o); break;
        default: break;
        }
        if (kind != TH_SHAPE_NONE) {
            return kind;
        }
        shape_clear(o);
    }

    ell_good = simple && ell.err_mean < 0.07 && ell.err_max < 0.25;

    resample(p, m, 1, cand, SK_TPL_N);
    unit_box(cand, SK_TPL_N);
    heart_curve(heart, SK_TPL_N);
    unit_box(heart, SK_TPL_N);
    circle_curve(circle, SK_TPL_N);
    unit_box(circle, SK_TPL_N);
    d_heart = loop_distance(cand, heart, SK_TPL_N);
    d_circle = loop_distance(cand, circle, SK_TPL_N);
    if (d_heart < 0.065 && d_heart < 0.6 * d_circle && crossings <= 2) {
        return make_heart(p, m, sy, o);
    }
    if (ell_good && crossings <= 2) {
        return make_ellipse(&ell, sy, o);
    }
    return TH_SHAPE_NONE;
}

th_shape_kind th_recognize(const th_point *points, int count, int y_down, th_shape *out)
{
    th_shape scratch;
    th_shape *o = out ? out : &scratch;
    double sy = y_down ? 1.0 : -1.0, raw_len = 0, length, size;
    vec2 s[SK_OPEN_N], loop[SK_LOOP_N];
    th_shape_kind kind;

    shape_clear(o);
    if (!points || count < 8) {
        return TH_SHAPE_NONE;
    }
    if (!resample_raw(points, count, sy, s, SK_OPEN_N, &raw_len)) {
        return TH_SHAPE_NONE;
    }
    size = box_diag(bounds(s, SK_OPEN_N));
    if (size < SK_MIN_SIZE) {
        return TH_SHAPE_NONE;
    }
    length = polyline_length(s, SK_OPEN_N, 0);
    kind = try_line(s, SK_OPEN_N, length, sy, o);
    if (kind == TH_SHAPE_NONE) {
        shape_clear(o);
        if (find_loop(s, SK_OPEN_N, size, length, loop)) {
            kind = classify_loop(loop, SK_LOOP_N, sy, o);
        } else {
            kind = try_arrow(s, SK_OPEN_N, size, sy, o);
        }
    }
    if (kind == TH_SHAPE_NONE) {
        shape_clear(o);
    }
    o->kind = kind;
    return kind;
}

th_shape_kind th_recognize_arrowhead(const th_shape *line, const th_point *points, int count,
                                     int y_down, th_shape *out)
{
    th_shape scratch;
    th_shape *o = out ? out : &scratch;
    double sy = y_down ? 1.0 : -1.0, raw_len, len, ssize, best = -1, arm1, arm2, maxdev = 0;
    vec2 s[SK_HEAD_N], a, b, apex, e, t, d, w1, w2, chord;
    int i, m = 0, n = SK_HEAD_N, first, last;

    shape_clear(o);
    if (!line || line->kind != TH_SHAPE_LINE || line->path_count < 1 || line->path_length[0] < 2
        || !points || count < 3) {
        return TH_SHAPE_NONE;
    }
    first = line->path_start[0];
    last = first + line->path_length[0] - 1;
    if (last >= line->point_count || last >= TH_SHAPE_MAX_POINTS) {
        return TH_SHAPE_NONE;
    }
    a = v2(line->points[first].x, line->points[first].y * sy);
    b = v2(line->points[last].x, line->points[last].y * sy);
    len = vdist(a, b);
    if (len < SK_MIN_SIZE || !resample_raw(points, count, sy, s, n, &raw_len)) {
        return TH_SHAPE_NONE;
    }
    ssize = box_diag(bounds(s, n));
    if (ssize < 6 || ssize < 0.04 * len || ssize > 0.7 * len) {
        return TH_SHAPE_NONE;
    }
    chord = vsub(s[n - 1], s[0]);
    for (i = 1; i < n - 1; i++) {
        double dev = vlen(chord) > 1e-9 ? fabs(vcross(chord, vsub(s[i], s[0]))) / vlen(chord) : vdist(s[i], s[0]);
        if (dev > best) {
            best = dev;
            m = i;
        }
    }
    if (m < 0.2 * (n - 1) || m > 0.8 * (n - 1)) {
        return TH_SHAPE_NONE;
    }
    apex = s[m];
    arm1 = vdist(s[0], apex);
    arm2 = vdist(s[n - 1], apex);
    if (arm1 < 1e-6 || arm2 < 1e-6 || arm1 / arm2 > 3 || arm2 / arm1 > 3) {
        return TH_SHAPE_NONE;
    }
    for (i = 0; i < n; i++) {
        vec2 from = i <= m ? s[0] : apex, to = i <= m ? apex : s[n - 1];
        vec2 seg = vsub(to, from);
        double sl = vlen(seg);
        double dev = sl > 1e-9 ? fabs(vcross(seg, vsub(s[i], from))) / sl : 0;
        double rel = dev / (i <= m ? arm1 : arm2);
        if (rel > maxdev) maxdev = rel;
    }
    if (maxdev > 0.18) {
        return TH_SHAPE_NONE;
    }
    w1 = vnorm(vsub(s[0], apex));
    w2 = vnorm(vsub(s[n - 1], apex));
    {
        double opening = fabs(turn_between(w1, w2));
        if (opening < rad(15) || opening > rad(140)) {
            return TH_SHAPE_NONE;
        }
    }
    if (vdist(apex, a) < vdist(apex, b)) {
        e = a;
        t = b;
    } else {
        e = b;
        t = a;
    }
    if (vdist(apex, e) > fmax(0.6 * fmax(arm1, arm2), 0.12 * len)) {
        return TH_SHAPE_NONE;
    }
    d = vnorm(vsub(e, t));
    if (vdot(w1, d) > -0.25 || vdot(w2, d) > -0.25) {
        return TH_SHAPE_NONE;
    }
    if (vcross(d, w1) * vcross(d, w2) >= 0) {
        return TH_SHAPE_NONE;
    }
    return build_arrow(t, e, clampd(0.5 * (arm1 + arm2), 0.1 * len, 0.4 * len), sy, o);
}

const char *th_shape_name(th_shape_kind kind)
{
    switch (kind) {
    case TH_SHAPE_LINE: return "Line";
    case TH_SHAPE_ARROW: return "Arrow";
    case TH_SHAPE_CIRCLE: return "Circle";
    case TH_SHAPE_ELLIPSE: return "Ellipse";
    case TH_SHAPE_TRIANGLE: return "Triangle";
    case TH_SHAPE_SQUARE: return "Square";
    case TH_SHAPE_RECTANGLE: return "Rectangle";
    case TH_SHAPE_DIAMOND: return "Diamond";
    case TH_SHAPE_PENTAGON: return "Pentagon";
    case TH_SHAPE_HEXAGON: return "Hexagon";
    case TH_SHAPE_STAR: return "Star";
    case TH_SHAPE_HEART: return "Heart";
    case TH_SHAPE_NONE:
    case TH_SHAPE_KIND_COUNT:
    default: return "None";
    }
}

int th_shape_path_count(const th_shape *shape)
{
    return shape ? shape->path_count : 0;
}

int th_shape_path_start(const th_shape *shape, int path)
{
    return shape && path >= 0 && path < shape->path_count ? shape->path_start[path] : 0;
}

int th_shape_path_length(const th_shape *shape, int path)
{
    return shape && path >= 0 && path < shape->path_count ? shape->path_length[path] : 0;
}

int th_shape_path_closed(const th_shape *shape, int path)
{
    return shape && path >= 0 && path < shape->path_count ? shape->path_closed[path] : 0;
}

th_point th_shape_point(const th_shape *shape, int index)
{
    th_point zero;
    zero.x = 0;
    zero.y = 0;
    if (!shape || index < 0 || index >= shape->point_count) {
        return zero;
    }
    return shape->points[index];
}
