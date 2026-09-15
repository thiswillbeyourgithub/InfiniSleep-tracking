#!/bin/sh
#
# Builds and runs the pomodoro tests on the host. No hardware, no docker, no toolchain beyond a
# C++17 compiler.
#
#   tests/pomodoro/run.sh
#
# PomodoroController is compiled from src/ unchanged, against the stub headers in ../stubs/, which
# stand in for FreeRTOS, its software timers, the nRF logger, the filesystem and the motor. The
# timer stub runs off an explicit clock, so a queue that would take half an hour on the watch is
# stepped through here in microseconds.

set -e

here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."
out="${TMPDIR:-/tmp}/test_pomodoro"

${CXX:-c++} -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined \
  -I "$here/../stubs" -I "$root/src" \
  "$here/test_pomodoro.cpp" "$root/src/components/pomodoro/PomodoroController.cpp" \
  -o "$out"

"$out"
