#ifndef TINYHAND_SHAPEKIT_H
#define TINYHAND_SHAPEKIT_H

/*
 * ShapeKit: turns a rough freehand stroke into a clean shape.
 *
 * Plain C99 with no dependencies, so the macOS app (through SwiftPM) and the
 * Windows port (through the Makefile) share one recognizer and behave the same.
 *
 * Coordinates are in whatever unit the caller draws in (points on macOS,
 * pixels on Windows). Pass y_down = 1 when +y points down the screen (Windows),
 * 0 when +y points up (macOS). It only matters for shapes that have an
 * "upright" pose: a triangle, pentagon or star pointing up, a heart with its
 * point at the bottom.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float x;
    float y;
} th_point;

typedef enum {
    TH_SHAPE_NONE = 0,
    TH_SHAPE_LINE,
    TH_SHAPE_ARROW,
    TH_SHAPE_CIRCLE,
    TH_SHAPE_ELLIPSE,
    TH_SHAPE_TRIANGLE,
    TH_SHAPE_SQUARE,
    TH_SHAPE_RECTANGLE,
    TH_SHAPE_DIAMOND,
    TH_SHAPE_PENTAGON,
    TH_SHAPE_HEXAGON,
    TH_SHAPE_STAR,
    TH_SHAPE_HEART,
    TH_SHAPE_KIND_COUNT
} th_shape_kind;

#define TH_SHAPE_MAX_POINTS 256
#define TH_SHAPE_MAX_PATHS 4

/*
 * The clean shape as one or more polylines. Path i uses
 * points[path_start[i] .. path_start[i] + path_length[i] - 1] and is closed
 * when path_closed[i] is non-zero. Curves (circle, ellipse, heart) come back
 * finely sampled, so callers only ever draw straight segments.
 */
typedef struct {
    th_shape_kind kind;
    int path_count;
    int path_start[TH_SHAPE_MAX_PATHS];
    int path_length[TH_SHAPE_MAX_PATHS];
    int path_closed[TH_SHAPE_MAX_PATHS];
    int point_count;
    th_point points[TH_SHAPE_MAX_POINTS];
} th_shape;

/*
 * Recognise one stroke, points in drawing order. Returns the kind found and
 * fills *out, or returns TH_SHAPE_NONE (and sets out->kind to it) when the
 * stroke should stay freehand: scribbles, handwriting, tiny marks.
 */
th_shape_kind th_recognize(const th_point *points, int count, int y_down, th_shape *out);

/*
 * Two-stroke arrows: call this for a new stroke when the previous stroke was
 * just snapped to a line. If the new stroke is a small "V" or "<" drawn at
 * either end of that line, *out becomes the finished arrow (pointing at that
 * end) and TH_SHAPE_ARROW is returned; the caller replaces the line with it
 * and drops the new stroke. Otherwise returns TH_SHAPE_NONE.
 */
th_shape_kind th_recognize_arrowhead(const th_shape *line, const th_point *points, int count,
                                     int y_down, th_shape *out);

/* "Line", "Arrow", "Circle", ... ; "None" for TH_SHAPE_NONE. */
const char *th_shape_name(th_shape_kind kind);

/* Small accessors so Swift does not have to walk C fixed-size arrays. */
int th_shape_path_count(const th_shape *shape);
int th_shape_path_start(const th_shape *shape, int path);
int th_shape_path_length(const th_shape *shape, int path);
int th_shape_path_closed(const th_shape *shape, int path);
th_point th_shape_point(const th_shape *shape, int index);

#ifdef __cplusplus
}
#endif

#endif
