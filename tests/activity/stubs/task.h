#pragma once

// The controller reads the tick counter to say how long ago a host last collected. Frozen here:
// no test asserts on it, and a stub that moved would make the tests depend on wall clock time.
using TickType_t = unsigned long;

inline TickType_t xTaskGetTickCount() {
  return 0;
}
