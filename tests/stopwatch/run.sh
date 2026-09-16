#!/bin/sh
#
# Builds and runs the stopwatch tests on the host. No hardware, no docker, no toolchain beyond a
# C++17 compiler.
#
#   tests/stopwatch/run.sh
#
# StopWatchController is compiled from src/ unchanged, against the stub headers in ../stubs/, which
# stand in for FreeRTOS and its tick counter. The clock is explicit here, so the thousand hours it
# takes for the elapsed time to wrap round pass in microseconds.

set -e

here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."
out="${TMPDIR:-/tmp}/test_stopwatch"

${CXX:-c++} -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined \
  -I "$here/../stubs" -I "$root/src" \
  "$here/test_stopwatch.cpp" "$root/src/components/stopwatch/StopWatchController.cpp" \
  -o "$out"

"$out"
