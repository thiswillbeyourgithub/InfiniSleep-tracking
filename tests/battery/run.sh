#!/bin/sh
#
# Builds and runs the battery curve tests on the host. No hardware, no docker, no toolchain beyond
# a C++17 compiler.
#
#   tests/battery/run.sh
#
# BatteryCurve.h is header only and free of the nRF headers BatteryController drags in, which is
# why the curve lives apart from the driver that reads the ADC: the reading is hardware, the shape
# of the number is not.

set -e

here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."
out="${TMPDIR:-/tmp}/test_battery_curve"

${CXX:-c++} -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined \
  -I "$here/../stubs" -I "$root/src" \
  "$here/test_battery_curve.cpp" \
  -o "$out"

"$out"
