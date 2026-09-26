#!/bin/sh
#
# Builds and runs the HeartRateController tests on the host. No hardware, no docker, no toolchain
# beyond a C++17 compiler.
#
#   tests/heartratecontroller/run.sh
#
# HeartRateController.cpp is compiled from src/ unchanged. The heart rate task, the BLE service and
# the system task it includes are replaced by the stubs next to this script, which come first on the
# include path so they win over the real headers.

set -e

here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."
out="${TMPDIR:-/tmp}/test_heart_rate_controller"

${CXX:-c++} -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined \
  -I "$here/stubs" \
  -I "$root/src" \
  "$here/test_heart_rate_controller.cpp" "$root/src/components/heartrate/HeartRateController.cpp" \
  -o "$out"

"$out"
