#!/bin/sh
#
# Builds and runs the event log tests on the host. No hardware, no docker, no toolchain beyond a
# C++17 compiler.
#
#   tests/eventlog/run.sh
#
# EventLogController and the ring it is built on are compiled from src/ unchanged, against the stub
# headers in ../stubs/, which stand in for FreeRTOS, the nRF logger and the filesystem. The
# filesystem stub keeps one file in memory, so a save followed by a load goes through the same bytes
# the watch writes.

set -e

here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."
out="${TMPDIR:-/tmp}/test_event_log"

${CXX:-c++} -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined \
  -I "$here/../stubs" -I "$root/src" \
  "$here/test_event_log.cpp" "$root/src/components/log/EventLogController.cpp" \
  "$root/src/components/log/CollectableLog.cpp" \
  -o "$out"

"$out"
