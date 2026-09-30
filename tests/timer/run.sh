#!/bin/sh
#
# Builds and runs the countdown timer tests on the host. No hardware, no docker, no toolchain beyond a
# C++17 compiler.
#
#   tests/timer/run.sh
#
# The Timer controller is compiled from src/ unchanged, against the stub headers in ../stubs/, which
# stand in for FreeRTOS and its software timers. The clock is explicit here, so a pause of any length
# is a single step.

set -e

here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."
out="${TMPDIR:-/tmp}/test_timer"

${CXX:-c++} -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined \
  -I "$here/../stubs" -I "$root/src" \
  "$here/test_timer.cpp" "$root/src/components/timer/Timer.cpp" \
  -o "$out"

"$out"
