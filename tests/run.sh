#!/bin/sh
# Host test of LAX-32: the demo, every timbre, the rhythm part, the reverbs, bend and mod to build/render-out, then a fuzz (UBSan).
set -e
cd "$(dirname "$0")/.."
mkdir -p build/render-out
${CC:-cc} -O1 -g -fsanitize=undefined -fno-sanitize-recover -Wall -Wextra -Isrc -Iplatform \
  tests/render.c src/la.c src/demo.c src/seq.c -lm -o build/render
build/render build/render-out
