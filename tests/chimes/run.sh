#!/bin/sh
#
# Builds and runs the chime tests on the host. No hardware, no docker, no toolchain beyond a C++17
# compiler.
#
#   tests/chimes/run.sh
#
# Chimes.h is header only and free of the nRF headers SystemTask drags in, which is why the
# decision lives apart from the task that rings the chime.

set -e

here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."
out="${TMPDIR:-/tmp}/test_chimes"

${CXX:-c++} -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined \
  -I "$here/../stubs" -I "$root/src" \
  "$here/test_chimes.cpp" \
  -o "$out"

"$out"
