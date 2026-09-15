#pragma once
// The pieces of the FreeRTOS core every stub here and every controller under test needs, plus the
// fake tick counter that task.h and timers.h both drive. Keeping the clock in one place is what
// lets a test advance time and have both the tick counter and the software timers agree on it.
#include <cstdint>

#define portMAX_DELAY 0xffffffffUL
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0

// Matches the watch, so that a pdMS_TO_TICKS conversion left out by mistake shows up as a wrong
// duration in a test rather than being hidden by a tick rate of exactly one per millisecond.
#define configTICK_RATE_HZ 1024

using TickType_t = unsigned long;

#define pdMS_TO_TICKS(ms) ((TickType_t) (((TickType_t) (ms) * configTICK_RATE_HZ) / 1000))

namespace TestClock {
  // Frozen at zero unless a test moves it, so harnesses that do not care about time see none pass.
  inline TickType_t& Ticks() {
    static TickType_t ticks = 0;
    return ticks;
  }
}
