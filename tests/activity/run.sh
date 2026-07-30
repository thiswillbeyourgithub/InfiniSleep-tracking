#!/bin/sh
#
# Builds and runs the activity log tests on the host. No hardware, no docker, no toolchain
# beyond a C++17 compiler.
#
#   tests/activity/run.sh
#
# ActivityLogController is compiled from src/ unchanged, against the stub headers in stubs/,
# which stand in for FreeRTOS, the nRF logger and the filesystem. The filesystem stub keeps one
# file in memory, so a save followed by a load goes through the same bytes the watch writes.

set -e

here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."
out="${TMPDIR:-/tmp}/test_activity_log"

${CXX:-c++} -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined \
  -I "$here/stubs" -I "$root/src" \
  "$here/test_activity_log.cpp" "$root/src/components/activity/ActivityLogController.cpp" \
  -o "$out"

"$out"
