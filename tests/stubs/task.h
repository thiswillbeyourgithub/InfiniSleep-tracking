#pragma once
// The tick counter, read straight from the shared fake clock in FreeRTOS.h. Tests that never call
// TestTimers::Advance see it stay at zero, which is what the activity harness relies on.
#include "FreeRTOS.h"

inline TickType_t xTaskGetTickCount() {
  return TestClock::Ticks();
}
