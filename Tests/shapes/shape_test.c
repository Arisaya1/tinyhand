/*
 * Synthetic hand-drawn strokes for ShapeKit.
 *
 * Every shape is drawn many times with a seeded PRNG: random size, rotation,
 * aspect, start point and direction, wobble, jitter, rounded corners, gaps and
 * overshoot on closed shapes, uneven point spacing, sometimes whole-pixel
 * input. Half the strokes are fed in a y-up frame (macOS) and half y-down
 * (Windows). Exit code is non-zero when any check falls below its bar.
 */

#include "shapekit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846
#define DENSE 6000
#define MAXP 4000
#define SAMPLES 300

typedef struct {
    double x, y;
} P2;

typedef struct {
    P2 p[DENSE];
    double s[DENSE];
    int n;
    int closed;
    double len;
} Path;

typedef struct {
    double wob1, wob2, f1, f2, ph1, ph2;
    double jitter;
    double speed_var, speed_f, speed_ph;
    double cover;
    double drift;
    double start;
    int dir;
    int count;
    int pixels;
} Noise;

static unsigned long long rng_state = 0x9E3779B97F4A7C15ULL;

static double rnd(void)
{
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return (double)((rng_state * 2685821657736338717ULL) >> 11) * (1.0 / 9007199254740992.0);
}

static double ur(double a, double b) { return a + (b - a) * rnd(); }

static int ri(int a, int b)
{
    int v = a + (int)(rnd() * (b - a + 1));
    return v > b ? b : v;
}

static double gauss(void)
{
    double u1 = rnd(), u2 = rnd();
    if (u1 < 1e-12) {
        u1 = 1e-12;
    }
    return sqrt(-2 * log(u1)) * cos(2 * PI * u2);
}

static P2 p2(double x, double y)
{
    P2 r;
    r.x = x;
    r.y = y;
    return r;
}

static P2 rot(P2 a, double ang)
{
    double c = cos(ang), s = sin(ang);
    return p2(a.x * c - a.y * s, a.x * s + a.y * c);
}

static double rad(double d) { return d * PI / 180; }

/* ---------- ideal paths ---------- */

static void path_begin(Path *path, int closed)
{
    path->n = 0;
    path->closed = closed;
    path->len = 0;
}

static void path_add(Path *path, P2 q)
{
    if (path->n >= DENSE) {
        return;
    }
    if (path->n > 0) {
        P2 last = path->p[path->n - 1];
        double d = hypot(q.x - last.x, q.y - last.y);
        if (d < 1e-9) {
            return;
        }
        path->len += d;
    }
    path->s[path->n] = path->len;
    path->p[path->n++] = q;
}

static void path_end(Path *path)
{
    if (path->closed && path->n > 1) {
        P2 a = path->p[path->n - 1], b = path->p[0];
        path->len += hypot(a.x - b.x, a.y - b.y);
    }
}

static void add_segment(Path *path, P2 a, P2 b, int steps)
{
    int i;
    for (i = 0; i <= steps; i++) {
        double t = (double)i / steps;
        path_add(path, p2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t));
    }
}

/* Polyline through vertices, each corner rounded with a quadratic curve of radius r. */
static void path_polygon(Path *path, const P2 *v, int nv, int closed, double r)
{
    int j, first = closed ? 0 : 1, last = closed ? nv - 1 : nv - 2;
    P2 prev_end = v[0];
    path_begin(path, closed);
    if (!closed) {
        path_add(path, v[0]);
    }
    for (j = first; j <= last; j++) {
        P2 a = v[(j - 1 + nv) % nv], c = v[j], b = v[(j + 1) % nv];
        double la = hypot(c.x - a.x, c.y - a.y), lb = hypot(b.x - c.x, b.y - c.y);
        double rr = r;
        P2 s, e;
        int i;
        if (rr > 0.4 * la) rr = 0.4 * la;
        if (rr > 0.4 * lb) rr = 0.4 * lb;
        s = p2(c.x - (c.x - a.x) / la * rr, c.y - (c.y - a.y) / la * rr);
        e = p2(c.x + (b.x - c.x) / lb * rr, c.y + (b.y - c.y) / lb * rr);
        if (path->n > 0) {
            add_segment(path, prev_end, s, 60);
        }
        for (i = 0; i <= 12; i++) {
            double t = i / 12.0, u = 1 - t;
            path_add(path, p2(u * u * s.x + 2 * u * t * c.x + t * t * e.x, u * u * s.y + 2 * u * t * c.y + t * t * e.y));
        }
        prev_end = e;
    }
    if (closed) {
        add_segment(path, prev_end, path->p[0], 60);
    } else {
        add_segment(path, prev_end, v[nv - 1], 60);
    }
    path_end(path);
}

static P2 path_at(const Path *path, double s)
{
    int lo = 0, hi = path->n - 1, mid;
    if (path->closed) {
        s = fmod(s, path->len);
        if (s < 0) {
            s += path->len;
        }
        if (s >= path->s[path->n - 1]) {
            P2 a = path->p[path->n - 1], b = path->p[0];
            double seg = path->len - path->s[path->n - 1];
            double t = seg > 1e-12 ? (s - path->s[path->n - 1]) / seg : 0;
            return p2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
        }
    } else {
        if (s <= 0) return path->p[0];
        if (s >= path->len) return path->p[path->n - 1];
    }
    while (hi - lo > 1) {
        mid = (lo + hi) / 2;
        if (path->s[mid] <= s) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    {
        double seg = path->s[hi] - path->s[lo];
        double t = seg > 1e-12 ? (s - path->s[lo]) / seg : 0;
        return p2(path->p[lo].x + (path->p[hi].x - path->p[lo].x) * t,
                  path->p[lo].y + (path->p[hi].y - path->p[lo].y) * t);
    }
}

/* ---------- drawing a path like a hand would ---------- */

static void noise_init(Noise *nz, double size, int sloppy, int closed)
{
    nz->wob1 = size * (sloppy ? ur(0.006, 0.014) : ur(0, 0.007));
    nz->wob2 = size * (sloppy ? ur(0.001, 0.004) : ur(0, 0.002));
    nz->f1 = ur(0.6, 2.5);
    nz->f2 = ur(4, 9);
    nz->ph1 = ur(0, 2 * PI);
    nz->ph2 = ur(0, 2 * PI);
    nz->jitter = size * (sloppy ? ur(0.001, 0.003) : ur(0, 0.0015));
    nz->speed_var = ur(0, 0.7);
    nz->speed_f = ur(0.5, 3);
    nz->speed_ph = ur(0, 2 * PI);
    nz->cover = closed ? (sloppy ? ur(0.93, 1.15) : ur(0.97, 1.06)) : 1;
    nz->drift = closed ? size * ur(-0.03, 0.03) : 0;
    nz->start = closed ? ur(0, 1) : 0;
    nz->dir = rnd() < 0.5 ? 1 : -1;
    nz->count = ri(30, 260);
    nz->pixels = rnd() < 0.5;
}

static int draw_path(const Path *path, const Noise *nz, th_point *out)
{
    double total = path->len * nz->cover, base = total / nz->count, t = 0;
    int n = 0, done = 0;
    while (!done && n < MAXP) {
        double s, off, eps = path->len * 1e-3;
        P2 q, a, b, nrm;
        double nl;
        if (t >= total) {
            t = total;
            done = 1;
        }
        s = path->closed ? nz->start * path->len + nz->dir * t : t;
        q = path_at(path, s);
        a = path_at(path, s - eps);
        b = path_at(path, s + eps);
        nrm = p2(-(b.y - a.y), b.x - a.x);
        nl = hypot(nrm.x, nrm.y);
        if (nl > 1e-12) {
            nrm.x /= nl;
            nrm.y /= nl;
        }
        off = nz->wob1 * sin(2 * PI * nz->f1 * t / total + nz->ph1) + nz->wob2 * sin(2 * PI * nz->f2 * t / total + nz->ph2);
        if (path->closed && t > path->len) {
            off += nz->drift * (t - path->len) / (0.15 * path->len);
        }
        q.x += nrm.x * off + gauss() * nz->jitter;
        q.y += nrm.y * off + gauss() * nz->jitter;
        if (nz->pixels) {
            q.x = floor(q.x + 0.5);
            q.y = floor(q.y + 0.5);
        }
        out[n].x = (float)q.x;
        out[n].y = (float)q.y;
        n++;
        {
            double step = base * (1 + nz->speed_var * sin(2 * PI * nz->speed_f * t / total + nz->speed_ph));
            t += step < 0.2 * base ? 0.2 * base : step;
        }
    }
    return n;
}

static void reverse_points(th_point *p, int n)
{
    int i;
    for (i = 0; i < n / 2; i++) {
        th_point t = p[i];
        p[i] = p[n - 1 - i];
        p[n - 1 - i] = t;
    }
}

static void flip_y(th_point *p, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        p[i].y = -p[i].y;
    }
}

/* ---------- shape generators (y-down: "up" is -y) ---------- */

enum {
    G_LINE, G_ARROW, G_CIRCLE, G_ELLIPSE, G_TRIANGLE, G_SQUARE, G_RECTANGLE, G_DIAMOND,
    G_PENTAGON, G_HEXAGON, G_STAR, G_PENTAGRAM, G_HEART, G_COUNT
};

static const char *gen_names[G_COUNT] = {
    "line", "arrow", "circle", "ellipse", "triangle", "square", "rectangle", "diamond",
    "pentagon", "hexagon", "star", "pentagram", "heart",
};

static const th_shape_kind gen_expect[G_COUNT] = {
    TH_SHAPE_LINE, TH_SHAPE_ARROW, TH_SHAPE_CIRCLE, TH_SHAPE_ELLIPSE, TH_SHAPE_TRIANGLE, TH_SHAPE_SQUARE,
    TH_SHAPE_RECTANGLE, TH_SHAPE_DIAMOND, TH_SHAPE_PENTAGON, TH_SHAPE_HEXAGON, TH_SHAPE_STAR, TH_SHAPE_STAR,
    TH_SHAPE_HEART,
};

enum {
    N_SCRIBBLE, N_ZIGZAG, N_CURSIVE, N_SPIRAL, N_SCURVE, N_FIGURE8, N_CARC, N_CHECK, N_LSHAPE, N_USHAPE, N_TWICE,
    N_TINY, N_COUNT
};

static const char *neg_names[N_COUNT] = {
    "scribble", "zigzag", "cursive loops", "spiral", "s-curve", "figure eight", "open arc", "check mark",
    "L shape", "U shape", "circle twice", "tiny mark",
};

static Path g_path;

static void transform(P2 *v, int n, double ang, P2 c)
{
    int i;
    for (i = 0; i < n; i++) {
        P2 q = rot(v[i], ang);
        v[i] = p2(q.x + c.x, q.y + c.y);
    }
}

static void path_curve(Path *path, int closed, P2 (*fn)(double, const double *), const double *args, double ang, P2 c)
{
    int i, steps = 2000;
    path_begin(path, closed);
    for (i = 0; i <= (closed ? steps - 1 : steps); i++) {
        P2 q = rot(fn((double)i / steps, args), ang);
        path_add(path, p2(q.x + c.x, q.y + c.y));
    }
    path_end(path);
}

static P2 f_ellipse(double t, const double *a)
{
    return p2(a[0] * cos(2 * PI * t), a[1] * sin(2 * PI * t));
}

static P2 f_heart(double t, const double *a)
{
    double u = 2 * PI * t, s = sin(u);
    return p2(a[0] * a[2] * 16 * s * s * s, -a[0] * (13 * cos(u) - 5 * cos(2 * u) - 2 * cos(3 * u) - cos(4 * u)));
}

static P2 f_spiral(double t, const double *a)
{
    double th = 2 * PI * a[1] * t, r = a[0] * (0.08 + t);
    return p2(r * cos(th), r * sin(th));
}

static P2 f_scurve(double t, const double *a)
{
    return p2(a[0] * t, a[1] * sin(2 * PI * a[2] * t));
}

static P2 f_cursive(double t, const double *a)
{
    double u = 2 * PI * a[2] * t;
    return p2(a[0] * (u - a[1] * sin(u)), -a[0] * a[1] * cos(u));
}

static P2 f_figure8(double t, const double *a)
{
    double u = 2 * PI * t;
    return p2(a[0] * sin(u), a[1] * sin(u) * cos(u));
}

static P2 f_twice(double t, const double *a)
{
    double u = 2 * PI * a[2] * t, grow = 1 + 0.08 * t;
    return p2(a[0] * grow * cos(u), a[1] * grow * sin(u));
}

static P2 f_arc(double t, const double *a)
{
    double u = a[1] * t;
    return p2(a[0] * cos(u), a[0] * sin(u));
}

static P2 catmull(P2 p0, P2 p1, P2 p2_, P2 p3, double t)
{
    double t2 = t * t, t3 = t2 * t;
    return p2(0.5 * (2 * p1.x + (-p0.x + p2_.x) * t + (2 * p0.x - 5 * p1.x + 4 * p2_.x - p3.x) * t2 + (-p0.x + 3 * p1.x - 3 * p2_.x + p3.x) * t3),
              0.5 * (2 * p1.y + (-p0.y + p2_.y) * t + (2 * p0.y - 5 * p1.y + 4 * p2_.y - p3.y) * t2 + (-p0.y + 3 * p1.y - 3 * p2_.y + p3.y) * t3));
}

/* Builds one random stroke. Returns the point count; *closed says whether it was a closed shape. */
static int make_positive(int g, th_point *out, int sloppy)
{
    P2 v[16], c = p2(ur(300, 1500), ur(300, 1100));
    double r = ur(40, 380), size = 2 * r, ang = ur(0, 2 * PI), round_r = size * (sloppy ? ur(0.02, 0.06) : ur(0, 0.03));
    double args[4];
    int closed = 1, i, n, reverse = 0;
    Noise nz;
    switch (g) {
    case G_LINE:
        v[0] = p2(-r, 0);
        v[1] = p2(r, 0);
        transform(v, 2, ang, c);
        path_begin(&g_path, 0);
        add_segment(&g_path, v[0], v[1], 400);
        path_end(&g_path);
        closed = 0;
        break;
    case G_ARROW: {
        double barb = ur(0.15, 0.35) * size, spread = rad(ur(20, 45));
        P2 tip = p2(size, 0), back = p2(-1, 0);
        P2 b1 = p2(tip.x + barb * rot(back, spread).x, barb * rot(back, spread).y);
        P2 b2 = p2(tip.x + barb * rot(back, -spread).x, barb * rot(back, -spread).y);
        v[0] = p2(0, 0);
        v[1] = tip;
        if (rnd() < 0.5) {
            v[2] = b1;
            v[3] = tip;
            v[4] = b2;
        } else {
            v[2] = b1;
            v[3] = b2;
            v[4] = tip;
        }
        transform(v, 5, ang, c);
        path_polygon(&g_path, v, 5, 0, size * ur(0, 0.008));
        closed = 0;
        reverse = rnd() < 0.5;
        break;
    }
    case G_CIRCLE:
        args[0] = r;
        args[1] = r * ur(0.94, 1.0);
        path_curve(&g_path, 1, f_ellipse, args, ang, c);
        break;
    case G_ELLIPSE:
        args[0] = r;
        args[1] = r * ur(0.38, 0.7);
        path_curve(&g_path, 1, f_ellipse, args, ang, c);
        break;
    case G_TRIANGLE: {
        double minang = 0;
        do {
            double base = ur(0, 2 * PI);
            for (i = 0; i < 3; i++) {
                double a = base + i * 2 * PI / 3 + rad(ur(-28, 28)), rr = r * ur(0.8, 1.2);
                v[i] = p2(rr * cos(a), rr * sin(a));
            }
            minang = 1e9;
            for (i = 0; i < 3; i++) {
                P2 a = v[(i + 2) % 3], b = v[i], d = v[(i + 1) % 3];
                double t = acos(((a.x - b.x) * (d.x - b.x) + (a.y - b.y) * (d.y - b.y)) /
                                (hypot(a.x - b.x, a.y - b.y) * hypot(d.x - b.x, d.y - b.y)));
                if (t < minang) minang = t;
            }
        } while (minang < rad(30));
        transform(v, 3, 0, c);
        path_polygon(&g_path, v, 3, 1, round_r);
        break;
    }
    case G_SQUARE:
    case G_RECTANGLE: {
        double w = r, h = g == G_SQUARE ? r / ur(1.0, 1.08) : r / ur(1.4, 3.0);
        double a = rad(g == G_SQUARE ? ur(-25, 25) : ur(-40, 40));
        v[0] = p2(-w, -h);
        v[1] = p2(w, -h);
        v[2] = p2(w, h);
        v[3] = p2(-w, h);
        if (rnd() < 0.5 && g == G_RECTANGLE) {
            for (i = 0; i < 4; i++) {
                v[i] = p2(v[i].y, v[i].x);
            }
        }
        transform(v, 4, a, c);
        path_polygon(&g_path, v, 4, 1, round_r);
        break;
    }
    case G_DIAMOND: {
        double hw = r, hh = r * ur(0.65, 1.5);
        v[0] = p2(0, -hh);
        v[1] = p2(hw, 0);
        v[2] = p2(0, hh);
        v[3] = p2(-hw, 0);
        transform(v, 4, rad(ur(-6, 6)), c);
        path_polygon(&g_path, v, 4, 1, round_r);
        break;
    }
    case G_PENTAGON:
    case G_HEXAGON: {
        int k = g == G_PENTAGON ? 5 : 6;
        for (i = 0; i < k; i++) {
            double a = ang + i * 2 * PI / k + rad(ur(-3, 3)), rr = r * ur(0.96, 1.04);
            v[i] = p2(rr * cos(a), rr * sin(a));
        }
        transform(v, k, 0, c);
        path_polygon(&g_path, v, k, 1, round_r * 0.7);
        break;
    }
    case G_STAR: {
        double inner = ur(0.36, 0.5);
        for (i = 0; i < 10; i++) {
            double a = ang + i * PI / 5 + rad(ur(-3, 3)), rr = r * (i % 2 ? inner : 1) * ur(0.95, 1.05);
            v[i] = p2(rr * cos(a), rr * sin(a));
        }
        transform(v, 10, 0, c);
        path_polygon(&g_path, v, 10, 1, size * ur(0, 0.015));
        break;
    }
    case G_PENTAGRAM:
        for (i = 0; i < 5; i++) {
            double a = ang + (i * 2 % 5) * 2 * PI / 5 + rad(ur(-3, 3)), rr = r * ur(0.95, 1.05);
            v[i] = p2(rr * cos(a), rr * sin(a));
        }
        transform(v, 5, 0, c);
        path_polygon(&g_path, v, 5, 1, size * ur(0, 0.015));
        break;
    case G_HEART:
        args[0] = r / 16;
        args[2] = ur(0.85, 1.15);
        path_curve(&g_path, 1, f_heart, args, rad(ur(-8, 8)), c);
        break;
    default:
        return 0;
    }
    noise_init(&nz, size, sloppy, closed);
    if (g == G_HEART && rnd() < 0.5) {
        nz.start = 0;
    }
    if (g == G_ARROW) {
        nz.wob1 = size * ur(0, 0.006);
        nz.wob2 = size * ur(0, 0.002);
    }
    n = draw_path(&g_path, &nz, out);
    if (reverse) {
        reverse_points(out, n);
    }
    return n;
}

static int make_negative(int k, th_point *out)
{
    P2 c = p2(ur(300, 1500), ur(300, 1100)), v[32];
    double r = ur(60, 380), size = 2 * r, ang = ur(0, 2 * PI), args[4];
    int closed = 0, i, n;
    Noise nz;
    switch (k) {
    case N_SCRIBBLE: {
        int cnt = ri(8, 14);
        for (i = 0; i < cnt; i++) {
            v[i] = p2(c.x + ur(-r, r), c.y + ur(-r, r));
        }
        path_begin(&g_path, 0);
        for (i = 0; i < cnt - 1; i++) {
            int s;
            P2 p0 = v[i > 0 ? i - 1 : 0], p3 = v[i + 2 < cnt ? i + 2 : cnt - 1];
            for (s = 0; s < 80; s++) {
                path_add(&g_path, catmull(p0, v[i], v[i + 1], p3, s / 80.0));
            }
        }
        path_end(&g_path);
        break;
    }
    case N_ZIGZAG: {
        int teeth = ri(4, 9);
        double w = size / teeth, h = w * ur(0.4, 1.2);
        for (i = 0; i <= teeth; i++) {
            v[i] = p2(i * w - size / 2, i % 2 ? h : -h);
        }
        transform(v, teeth + 1, ang, c);
        path_polygon(&g_path, v, teeth + 1, 0, w * 0.05);
        break;
    }
    case N_CURSIVE:
        args[0] = r / 12;
        args[1] = ur(1.4, 2.4);
        args[2] = ri(3, 6);
        path_curve(&g_path, 0, f_cursive, args, rad(ur(-15, 15)), c);
        break;
    case N_SPIRAL:
        args[0] = r;
        args[1] = ur(2, 4);
        path_curve(&g_path, 0, f_spiral, args, ang, c);
        break;
    case N_SCURVE:
        args[0] = size;
        args[1] = size * ur(0.15, 0.35);
        args[2] = ur(0.8, 1.5);
        path_curve(&g_path, 0, f_scurve, args, ang, c);
        break;
    case N_FIGURE8:
        args[0] = r;
        args[1] = r * ur(0.6, 1.2);
        path_curve(&g_path, 1, f_figure8, args, ang, c);
        closed = 1;
        break;
    case N_CARC:
        args[0] = r;
        args[1] = rad(ur(160, 260));
        path_curve(&g_path, 0, f_arc, args, ang, c);
        break;
    case N_CHECK:
        v[0] = p2(0, 0);
        v[1] = p2(0.3 * size, 0.35 * size);
        v[2] = p2(size, -0.45 * size);
        transform(v, 3, rad(ur(-15, 15)), c);
        path_polygon(&g_path, v, 3, 0, size * 0.02);
        break;
    case N_LSHAPE:
        v[0] = p2(0, -size * ur(0.5, 1));
        v[1] = p2(0, 0);
        v[2] = p2(size * ur(0.5, 1), 0);
        transform(v, 3, ang, c);
        path_polygon(&g_path, v, 3, 0, size * 0.02);
        break;
    case N_USHAPE: {
        double w = size * ur(0.3, 0.7), h = size * ur(0.5, 1);
        v[0] = p2(-w / 2, -h);
        v[1] = p2(-w / 2, 0);
        v[2] = p2(w / 2, 0);
        v[3] = p2(w / 2, -h);
        transform(v, 4, ang, c);
        path_polygon(&g_path, v, 4, 0, w * ur(0.1, 0.5));
        break;
    }
    case N_TWICE:
        args[0] = r;
        args[1] = r * ur(0.9, 1.0);
        args[2] = ur(1.7, 2.1);
        path_curve(&g_path, 0, f_twice, args, ang, c);
        break;
    case N_TINY: {
        int g = ri(0, G_COUNT - 1);
        th_point tmp[MAXP];
        int m = make_positive(g, tmp, 0);
        double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9, scale;
        for (i = 0; i < m; i++) {
            if (tmp[i].x < minx) minx = tmp[i].x;
            if (tmp[i].y < miny) miny = tmp[i].y;
            if (tmp[i].x > maxx) maxx = tmp[i].x;
            if (tmp[i].y > maxy) maxy = tmp[i].y;
        }
        scale = ur(4, 10) / hypot(maxx - minx, maxy - miny);
        for (i = 0; i < m; i++) {
            out[i].x = (float)((tmp[i].x - minx) * scale + 500);
            out[i].y = (float)((tmp[i].y - miny) * scale + 500);
        }
        return m;
    }
    default:
        return 0;
    }
    noise_init(&nz, size, rnd() < 0.5, closed);
    n = draw_path(&g_path, &nz, out);
    if (rnd() < 0.5) {
        reverse_points(out, n);
    }
    return n;
}

/* ---------- checks ---------- */

static int failures = 0;

static void check(int ok, const char *what)
{
    if (!ok) {
        failures++;
        printf("  FAIL: %s\n", what);
    }
}

static int layout_ok(const th_shape *s)
{
    int expect_pts = 0;
    switch (s->kind) {
    case TH_SHAPE_LINE:
        return s->path_count == 1 && s->path_length[0] == 2 && !s->path_closed[0];
    case TH_SHAPE_ARROW:
        return s->path_count == 2 && s->path_length[0] == 2 && s->path_length[1] == 3 && !s->path_closed[0] && !s->path_closed[1];
    case TH_SHAPE_CIRCLE:
    case TH_SHAPE_ELLIPSE:
    case TH_SHAPE_HEART: expect_pts = -1; break;
    case TH_SHAPE_TRIANGLE: expect_pts = 3; break;
    case TH_SHAPE_SQUARE:
    case TH_SHAPE_RECTANGLE:
    case TH_SHAPE_DIAMOND: expect_pts = 4; break;
    case TH_SHAPE_PENTAGON: expect_pts = 5; break;
    case TH_SHAPE_HEXAGON: expect_pts = 6; break;
    case TH_SHAPE_STAR: expect_pts = 10; break;
    default: return 0;
    }
    if (s->path_count != 1 || !s->path_closed[0]) {
        return 0;
    }
    return expect_pts < 0 ? s->path_length[0] >= 32 && s->path_length[0] <= 128 : s->path_length[0] == expect_pts;
}

static double overlap(const th_point *in, int n, const th_shape *s)
{
    double a0 = 1e9, b0 = 1e9, a1 = -1e9, b1 = -1e9, c0 = 1e9, d0 = 1e9, c1 = -1e9, d1 = -1e9;
    double ix, iy, inter, uni;
    int i;
    for (i = 0; i < n; i++) {
        if (in[i].x < a0) a0 = in[i].x;
        if (in[i].x > a1) a1 = in[i].x;
        if (in[i].y < b0) b0 = in[i].y;
        if (in[i].y > b1) b1 = in[i].y;
    }
    for (i = 0; i < s->point_count; i++) {
        if (s->points[i].x < c0) c0 = s->points[i].x;
        if (s->points[i].x > c1) c1 = s->points[i].x;
        if (s->points[i].y < d0) d0 = s->points[i].y;
        if (s->points[i].y > d1) d1 = s->points[i].y;
    }
    ix = fmin(a1, c1) - fmax(a0, c0);
    iy = fmin(b1, d1) - fmax(b0, d0);
    if (ix <= 0 || iy <= 0) {
        return 0;
    }
    inter = ix * iy;
    uni = (a1 - a0) * (b1 - b0) + (c1 - c0) * (d1 - d0) - inter;
    return uni > 0 ? inter / uni : 0;
}

static th_point pts[MAXP];

static const char *only = NULL;
static int verbose = 0;

static void run_positives(void)
{
    int g, i;
    printf("Positive shapes (%d each, half y-up, 40%% sloppy)\n", SAMPLES);
    printf("  %-10s %7s  %s\n", "shape", "correct", "misses");
    for (g = 0; g < G_COUNT; g++) {
        int correct = 0, miss[TH_SHAPE_KIND_COUNT] = {0}, bad_layout = 0, bad_place = 0, k;
        if (only && strcmp(only, gen_names[g]) != 0) {
            continue;
        }
        char line[256];
        int off = 0;
        for (i = 0; i < SAMPLES; i++) {
            th_shape shape;
            int up = i % 2, n = make_positive(g, pts, rnd() < 0.4);
            th_shape_kind kind;
            if (up) {
                flip_y(pts, n);
            }
            if (verbose) {
                fprintf(stderr, "---\n");
            }
            kind = th_recognize(pts, n, up ? 0 : 1, &shape);
            if (verbose && kind != gen_expect[g]) {
                fprintf(stderr, "MISS %s -> %s\n", gen_names[g], th_shape_name(kind));
            }
            if (kind == gen_expect[g]) {
                correct++;
                if (!layout_ok(&shape)) {
                    bad_layout++;
                }
                if (g != G_LINE && g != G_ARROW && overlap(pts, n, &shape) < 0.6) {
                    bad_place++;
                }
            } else {
                miss[kind]++;
            }
            if (shape.kind != kind) {
                bad_layout++;
            }
        }
        line[0] = 0;
        for (k = 0; k < TH_SHAPE_KIND_COUNT; k++) {
            if (miss[k]) {
                off += snprintf(line + off, sizeof line - off, "%s %d  ", th_shape_name((th_shape_kind)k), miss[k]);
            }
        }
        printf("  %-10s %6.1f%%  %s\n", gen_names[g], 100.0 * correct / SAMPLES, line);
        check(correct >= 0.9 * SAMPLES, gen_names[g]);
        check(bad_layout == 0, "output path layout");
        check(bad_place == 0, "output sits on the drawn stroke");
    }
}

static void run_negatives(void)
{
    int k, i, total_fp = 0, total = 0;
    printf("\nStrokes that must stay freehand (%d each)\n", SAMPLES);
    for (k = 0; k < N_COUNT; k++) {
        int fp = 0, hits[TH_SHAPE_KIND_COUNT] = {0}, j, off = 0;
        char line[256];
        for (i = 0; i < SAMPLES; i++) {
            int up = i % 2, n = make_negative(k, pts);
            th_shape_kind kind;
            if (up) {
                flip_y(pts, n);
            }
            kind = th_recognize(pts, n, up ? 0 : 1, NULL);
            if (kind != TH_SHAPE_NONE) {
                fp++;
                hits[kind]++;
            }
        }
        line[0] = 0;
        for (j = 0; j < TH_SHAPE_KIND_COUNT; j++) {
            if (hits[j]) {
                off += snprintf(line + off, sizeof line - off, "%s %d  ", th_shape_name((th_shape_kind)j), hits[j]);
            }
        }
        printf("  %-14s %5.1f%% snapped  %s\n", neg_names[k], 100.0 * fp / SAMPLES, line);
        check(k == N_TINY ? fp == 0 : fp <= 0.1 * SAMPLES, neg_names[k]);
        total_fp += fp;
        total += SAMPLES;
    }
    printf("  overall false positives: %.1f%%\n", 100.0 * total_fp / total);
    check(total_fp <= 0.05 * total, "overall false positive rate");
}

static th_shape make_line_shape(double x0, double y0, double x1, double y1)
{
    th_shape s;
    memset(&s, 0, sizeof s);
    s.kind = TH_SHAPE_LINE;
    s.path_count = 1;
    s.path_start[0] = 0;
    s.path_length[0] = 2;
    s.point_count = 2;
    s.points[0].x = (float)x0;
    s.points[0].y = (float)y0;
    s.points[1].x = (float)x1;
    s.points[1].y = (float)y1;
    return s;
}

static int draw_v(P2 apex, P2 dir, double arm, double spread, th_point *out)
{
    P2 v[3];
    Noise nz;
    P2 back = p2(-dir.x, -dir.y);
    v[0] = p2(apex.x + arm * rot(back, spread).x, apex.y + arm * rot(back, spread).y);
    v[1] = apex;
    v[2] = p2(apex.x + arm * ur(0.8, 1.2) * rot(back, -spread).x, apex.y + arm * ur(0.8, 1.2) * rot(back, -spread).y);
    path_polygon(&g_path, v, 3, 0, arm * 0.03);
    noise_init(&nz, arm * 2, 0, 0);
    nz.count = ri(8, 40);
    return draw_path(&g_path, &nz, out);
}

static void run_arrowheads(void)
{
    int i, ok = 0, rejected = 0;
    printf("\nTwo-stroke arrows (line, then a V at one end)\n");
    for (i = 0; i < SAMPLES; i++) {
        double ang = ur(0, 2 * PI), L = ur(120, 900);
        P2 a = p2(ur(200, 800), ur(200, 800)), b = p2(a.x + L * cos(ang), a.y + L * sin(ang));
        int at_b = rnd() < 0.5, up = i % 2, n, j;
        P2 end = at_b ? b : a, other = at_b ? a : b;
        P2 dir = p2((end.x - other.x) / L, (end.y - other.y) / L);
        P2 apex = p2(end.x + ur(-0.04, 0.04) * L, end.y + ur(-0.04, 0.04) * L);
        th_shape line = make_line_shape(a.x, a.y, b.x, b.y), out;
        th_shape_kind kind;
        n = draw_v(apex, dir, L * ur(0.08, 0.3), rad(ur(22, 45)), pts);
        if (up) {
            flip_y(pts, n);
            line.points[0].y = -line.points[0].y;
            line.points[1].y = -line.points[1].y;
        }
        kind = th_recognize_arrowhead(&line, pts, n, up ? 0 : 1, &out);
        if (kind == TH_SHAPE_ARROW && layout_ok(&out)) {
            th_point tip = out.points[1];
            double ey = up ? -end.y : end.y;
            if (hypot(tip.x - end.x, tip.y - ey) < 0.02 * L) {
                ok++;
            }
        }
        for (j = 0; j < 4; j++) {
            th_shape_kind bad = TH_SHAPE_NONE;
            P2 mid = p2((a.x + b.x) / 2, (a.y + b.y) / 2);
            th_shape l2 = make_line_shape(a.x, a.y, b.x, b.y);
            int m = 0;
            if (j == 0) {
                m = draw_v(mid, dir, L * 0.15, rad(30), pts);
            } else if (j == 1) {
                m = draw_v(end, p2(-dir.x, -dir.y), L * 0.15, rad(30), pts);
                {
                    int q;
                    for (q = 0; q < m; q++) {
                        pts[q].x += (float)(dir.x * L * 0.15);
                        pts[q].y += (float)(dir.y * L * 0.15);
                    }
                }
            } else if (j == 2) {
                m = draw_v(p2(end.x + dir.x * L * 0.5, end.y + dir.y * L * 0.5), dir, L * 0.15, rad(30), pts);
            } else {
                Path *gp = &g_path;
                double args2[2];
                Noise nz;
                args2[0] = L * 0.1;
                args2[1] = L * 0.1;
                path_curve(gp, 1, f_ellipse, args2, 0, end);
                noise_init(&nz, L * 0.2, 0, 1);
                m = draw_path(gp, &nz, pts);
            }
            bad = th_recognize_arrowhead(&l2, pts, m, 1, NULL);
            if (bad == TH_SHAPE_NONE) {
                rejected++;
            }
        }
    }
    printf("  V at an end -> arrow:      %5.1f%%\n", 100.0 * ok / SAMPLES);
    printf("  V elsewhere / wrong way:   %5.1f%% rejected\n", 100.0 * rejected / (4 * SAMPLES));
    check(ok >= 0.9 * SAMPLES, "arrowhead accepted");
    check(rejected >= 0.95 * 4 * SAMPLES, "arrowhead rejected when wrong");
}

/* Upright shapes drawn in a y-up frame must come back upright for y_down = 0. */
static void run_orientation(void)
{
    int i, tri_ok = 0, star_ok = 0, heart_up = 0, heart_wrong = 0;
    printf("\nOrientation\n");
    for (i = 0; i < SAMPLES; i++) {
        P2 v[10], c = p2(800, 600);
        double r = ur(60, 300);
        Noise nz;
        th_shape s;
        int n, j, top = 0;
        double ymax = -1e9;
        for (j = 0; j < 3; j++) {
            double a = PI / 2 + j * 2 * PI / 3 + rad(ur(-4, 4));
            v[j] = p2(c.x + r * cos(a), c.y + r * sin(a));
        }
        path_polygon(&g_path, v, 3, 1, r * 0.03);
        noise_init(&nz, 2 * r, 0, 1);
        n = draw_path(&g_path, &nz, pts);
        if (th_recognize(pts, n, 0, &s) == TH_SHAPE_TRIANGLE) {
            for (j = 0; j < 3; j++) {
                if (s.points[j].y > ymax) {
                    ymax = s.points[j].y;
                    top = j;
                }
            }
            {
                int b0 = (top + 1) % 3, b1 = (top + 2) % 3;
                if (fabs(s.points[b0].y - s.points[b1].y) < 0.01 * r && fabs(s.points[top].x - c.x) < 0.1 * r) {
                    tri_ok++;
                }
            }
        }
        for (j = 0; j < 10; j++) {
            double a = PI / 2 + j * PI / 5 + rad(ur(-3, 3));
            double rr = r * (j % 2 ? 0.42 : 1);
            v[j] = p2(c.x + rr * cos(a), c.y + rr * sin(a));
        }
        path_polygon(&g_path, v, 10, 1, r * 0.01);
        noise_init(&nz, 2 * r, 0, 1);
        n = draw_path(&g_path, &nz, pts);
        if (th_recognize(pts, n, 0, &s) == TH_SHAPE_STAR) {
            ymax = -1e9;
            for (j = 0; j < 10; j++) {
                if (s.points[j].y > ymax) {
                    ymax = s.points[j].y;
                    top = j;
                }
            }
            {
                double mx = 0;
                for (j = 0; j < 10; j++) {
                    mx += s.points[j].x / 10;
                }
                if (fabs(s.points[top].x - mx) < 0.01 * r) {
                    star_ok++;
                }
            }
        }
        {
            double args[3];
            args[0] = r / 16;
            args[2] = ur(0.9, 1.1);
            path_curve(&g_path, 1, f_heart, args, 0, p2(0, 0));
            noise_init(&nz, 2 * r, 0, 1);
            n = draw_path(&g_path, &nz, pts);
            flip_y(pts, n);
            if (th_recognize(pts, n, 0, NULL) == TH_SHAPE_HEART) {
                heart_up++;
            }
            if (th_recognize(pts, n, 1, NULL) == TH_SHAPE_HEART) {
                heart_wrong++;
            }
        }
    }
    printf("  upright triangle stays point-up (y-up input): %5.1f%%\n", 100.0 * tri_ok / SAMPLES);
    printf("  upright star stays point-up (y-up input):     %5.1f%%\n", 100.0 * star_ok / SAMPLES);
    printf("  heart in y-up input read with y_down=0:       %5.1f%%\n", 100.0 * heart_up / SAMPLES);
    printf("  same heart read upside down (y_down=1):       %5.1f%%\n", 100.0 * heart_wrong / SAMPLES);
    check(tri_ok >= 0.9 * SAMPLES, "triangle orientation");
    check(star_ok >= 0.9 * SAMPLES, "star orientation");
    check(heart_up >= 0.9 * SAMPLES, "heart y-up");
    check(heart_wrong <= 0.1 * SAMPLES, "upside-down heart rejected");
}

static void run_edges(void)
{
    th_point few[7];
    th_shape s;
    int i;
    printf("\nEdge cases\n");
    for (i = 0; i < 7; i++) {
        few[i].x = (float)(i * 40);
        few[i].y = 0;
    }
    check(th_recognize(few, 7, 1, &s) == TH_SHAPE_NONE && s.kind == TH_SHAPE_NONE, "fewer than 8 points");
    check(th_recognize(NULL, 0, 1, &s) == TH_SHAPE_NONE, "null input");
    for (i = 0; i < 7; i++) {
        few[i].x = 10;
        few[i].y = 10;
    }
    check(th_recognize(few, 7, 1, NULL) == TH_SHAPE_NONE, "all points equal");
    for (i = 0; i < 2000; i++) {
        pts[i].x = (float)(100 + i * 0.4);
        pts[i].y = (float)(300 + 0.2 * sin(i * 0.3));
    }
    check(th_recognize(pts, 2000, 1, &s) == TH_SHAPE_LINE && fabs(s.points[0].y - s.points[1].y) < 1e-3, "long dense horizontal line snaps level");
    check(strcmp(th_shape_name(TH_SHAPE_HEART), "Heart") == 0 && strcmp(th_shape_name(TH_SHAPE_NONE), "None") == 0, "names");
    check(th_shape_path_count(&s) == 1 && th_shape_path_length(&s, 0) == 2 && th_shape_path_closed(&s, 0) == 0
          && th_shape_path_length(&s, 5) == 0 && th_shape_point(&s, 999).x == 0, "accessors");
    printf("  done\n");
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        rng_state = strtoull(argv[1], NULL, 10) | 1;
    }
    verbose = getenv("SHAPE_VERBOSE") != NULL;
    if (argc > 2) {
        only = argv[2];
        run_positives();
        return failures ? 1 : 0;
    }
    run_positives();
    run_negatives();
    run_arrowheads();
    run_orientation();
    run_edges();
    if (failures) {
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    printf("\nAll shape checks passed\n");
    return 0;
}
