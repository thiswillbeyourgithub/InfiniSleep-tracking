#pragma once
// A software timer service driven by an explicit clock instead of a scheduler, so that a state
// machine built out of FreeRTOS timers can be stepped through a whole hour in a few microseconds
// and asserted on at every step. Behaviour that the controllers under test depend on:
//   - xTimerChangePeriod also starts the timer, as it does on the watch
//   - a periodic timer's next expiry is computed before its callback runs, so a callback is free
//     to stop or reschedule its own timer and have that win
//   - callbacks may create, start and stop any timer, including the one being fired
#include "FreeRTOS.h"

#include <algorithm>
#include <vector>

struct StubTimer;
using TimerHandle_t = StubTimer*;
using TimerCallbackFunction_t = void (*)(TimerHandle_t);

struct StubTimer {
  const char* name;
  TickType_t period;
  bool autoReload;
  void* timerId;
  TimerCallbackFunction_t callback;
  bool active = false;
  TickType_t expiry = 0;
};

namespace TestTimers {
  inline std::vector<StubTimer*>& All() {
    static std::vector<StubTimer*> timers;
    return timers;
  }

  /* Moves the clock forward, firing every timer that comes due on the way in expiry order. The due
   * timer is looked up afresh each time round rather than collected into a list up front, because
   * a callback may stop a timer that was about to fire or arm one that was not. */
  inline void Advance(TickType_t ticks) {
    const TickType_t target = TestClock::Ticks() + ticks;
    while (true) {
      StubTimer* next = nullptr;
      for (StubTimer* timer : All()) {
        if (timer->active && timer->expiry <= target && (next == nullptr || timer->expiry < next->expiry)) {
          next = timer;
        }
      }
      if (next == nullptr) {
        break;
      }
      TestClock::Ticks() = next->expiry;
      if (next->autoReload) {
        next->expiry += next->period;
      } else {
        next->active = false;
      }
      next->callback(next);
    }
    TestClock::Ticks() = target;
  }

  // Forgets every timer, so one test's leftovers cannot fire during the next.
  inline void Reset() {
    for (StubTimer* timer : All()) {
      delete timer;
    }
    All().clear();
    TestClock::Ticks() = 0;
  }
}

inline TimerHandle_t
xTimerCreate(const char* name, TickType_t period, int autoReload, void* timerId, TimerCallbackFunction_t callback) {
  auto* timer = new StubTimer {name, std::max<TickType_t>(period, 1), autoReload != pdFALSE, timerId, callback};
  TestTimers::All().push_back(timer);
  return timer;
}

inline void* pvTimerGetTimerID(TimerHandle_t timer) {
  return timer->timerId;
}

inline int xTimerStart(TimerHandle_t timer, TickType_t) {
  timer->active = true;
  timer->expiry = TestClock::Ticks() + timer->period;
  return pdPASS;
}

inline int xTimerStop(TimerHandle_t timer, TickType_t) {
  timer->active = false;
  return pdPASS;
}

inline int xTimerChangePeriod(TimerHandle_t timer, TickType_t period, TickType_t) {
  // FreeRTOS rejects a period of zero; clamping matches how the real call would never arm at "now".
  timer->period = std::max<TickType_t>(period, 1);
  timer->active = true;
  timer->expiry = TestClock::Ticks() + timer->period;
  return pdPASS;
}

inline int xTimerIsTimerActive(TimerHandle_t timer) {
  return timer->active ? pdTRUE : pdFALSE;
}

inline TickType_t xTimerGetExpiryTime(TimerHandle_t timer) {
  return timer->expiry;
}
