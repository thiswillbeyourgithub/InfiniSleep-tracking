#!/bin/sh
#
# Builds and runs the InfiniSleep snooze tests on the host. No hardware, no docker, no toolchain
# beyond a C++17 compiler.
#
#   tests/infinisleep/run.sh
#
# WakeAlarmSnooze.h is header only and free of the nRF headers InfiniSleepController drags in,
# which is why the snooze bookkeeping lives apart from the controller that rings the alarm.

set -e

here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."
out="${TMPDIR:-/tmp}/test_wake_alarm_snooze"

${CXX:-c++} -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined \
  -I "$here/../stubs" -I "$root/src" \
  "$here/test_wake_alarm_snooze.cpp" \
  -o "$out"

"$out"
