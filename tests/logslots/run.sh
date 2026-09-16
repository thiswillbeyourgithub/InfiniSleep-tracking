#!/bin/sh
#
# Builds and runs the slot table tests on the host. No hardware, no docker, no toolchain beyond a
# C++17 compiler.
#
#   tests/logslots/run.sh
#
# LogSlots is compiled from src/ unchanged, against the stub headers in ../stubs/, which stand in for
# the nRF logger and the filesystem. The filesystem stub keeps one file in memory, so a push
# followed by a reboot goes through the same bytes the watch writes.

set -e

here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."
out="${TMPDIR:-/tmp}/test_log_slots"

${CXX:-c++} -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined \
  -I "$here/../stubs" -I "$root/src" \
  "$here/test_log_slots.cpp" "$root/src/components/log/LogSlots.cpp" \
  -o "$out"

"$out"
