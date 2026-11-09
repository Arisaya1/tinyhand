#!/bin/bash
# Builds and runs the ShapeKit synthetic stroke tests natively. Pass a number to change the seed.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/.build/shape-test"
mkdir -p "$ROOT/.build"
cc -std=c99 -O2 -Wall -Wextra -Werror -I"$ROOT/Sources/ShapeKit/include" \
    "$ROOT/Sources/ShapeKit/shapekit.c" "$ROOT/Tests/shapes/shape_test.c" -lm -o "$OUT"
"$OUT" "$@"
