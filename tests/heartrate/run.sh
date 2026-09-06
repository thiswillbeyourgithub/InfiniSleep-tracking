#!/bin/sh
#
# Builds and runs the Ppg tests on the host. No hardware, no docker, no toolchain beyond a
# C++17 compiler.
#
#   tests/heartrate/run.sh
#
# Ppg.cpp is compiled from src/ unchanged and needs no stubs: arduinoFFT is a header and Ppg
# talks to nothing else.

set -e

here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."
out="${TMPDIR:-/tmp}/test_ppg"

${CXX:-c++} -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined \
  -I "$root/src" \
  "$here/test_ppg.cpp" "$root/src/components/heartrate/Ppg.cpp" \
  -o "$out"

"$out"
