// Host harness for the chime decision. Exists because a chime that rings when it should not is
// only found at night, by being woken by it: outside the hours set, during a tracked night, or
// through notifications turned off.

#include <cstdio>

#include "components/settings/Chimes.h"

using namespace Pinetime::Controllers;

static int failures = 0;

#define CHECK(cond)                                                                                                                        \
  do {                                                                                                                                     \
    if (!(cond)) {                                                                                                                         \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                                                           \
      failures++;                                                                                                                          \
    }                                                                                                                                      \
  } while (0)

namespace {
  /// The defaults the settings ship with, every half hour.
  constexpr Chimes::State Defaults(uint8_t intervalMinutes = 30) {
    return {intervalMinutes, 10, 21, false, false, false, false, false};
  }

  /// How many times the chime goes off over a whole day, asked once a minute.
  int ChimesInADay(const Chimes::State& state) {
    int count = 0;
    for (uint8_t hour = 0; hour < 24; hour++) {
      for (uint8_t minute = 0; minute < 60; minute++) {
        count += Chimes::ShouldChime(state, hour, minute) ? 1 : 0;
      }
    }
    return count;
  }

  void eachIntervalChimesOnItsMinutesOnly() {
    CHECK(ChimesInADay(Defaults(0)) == 0);
    // 10:00 to 21:00 inclusive: 12 full hours, 23 half hours, 45 quarters
    CHECK(ChimesInADay(Defaults(60)) == 12);
    CHECK(ChimesInADay(Defaults(30)) == 23);
    CHECK(ChimesInADay(Defaults(15)) == 45);
    CHECK(Chimes::ShouldChime(Defaults(15), 12, 45));
    CHECK(!Chimes::ShouldChime(Defaults(30), 12, 45));
    CHECK(!Chimes::ShouldChime(Defaults(60), 12, 30));
  }

  void nothingOutsideTheWindow() {
    const auto state = Defaults(15);
    CHECK(Chimes::ShouldChime(state, 10, 0));
    CHECK(Chimes::ShouldChime(state, 21, 0));
    // The end hour chimes at its full hour and stops there
    CHECK(!Chimes::ShouldChime(state, 21, 15));
    CHECK(!Chimes::ShouldChime(state, 9, 45));
    CHECK(!Chimes::ShouldChime(state, 3, 0));
  }

  void aWindowAcrossMidnight() {
    auto state = Defaults(60);
    state.startHour = 22;
    state.endHour = 6;
    CHECK(Chimes::ShouldChime(state, 23, 0));
    CHECK(Chimes::ShouldChime(state, 0, 0));
    CHECK(Chimes::ShouldChime(state, 6, 0));
    CHECK(!Chimes::ShouldChime(state, 12, 0));
    // 22, 23, 0 through 6
    CHECK(ChimesInADay(state) == 9);
  }

  void neverWhileAsleepOrRinging() {
    auto state = Defaults(15);
    state.sleepTracking = true;
    CHECK(ChimesInADay(state) == 0);

    state = Defaults(15);
    state.sleepMode = true;
    state.ignoreDoNotDisturb = true;
    CHECK(ChimesInADay(state) == 0);

    state = Defaults(15);
    state.alarmRinging = true;
    CHECK(ChimesInADay(state) == 0);
  }

  void notificationsOffSilenceUnlessOverridden() {
    auto state = Defaults(60);
    state.notificationsOff = true;
    CHECK(ChimesInADay(state) == 0);
    state.ignoreDoNotDisturb = true;
    CHECK(ChimesInADay(state) == 12);
    // The override does not reach past the window
    CHECK(!Chimes::ShouldChime(state, 22, 0));
  }
}

int main() {
  eachIntervalChimesOnItsMinutesOnly();
  nothingOutsideTheWindow();
  aWindowAcrossMidnight();
  neverWhileAsleepOrRinging();
  notificationsOffSilenceUnlessOverridden();

  if (failures != 0) {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("chimes: all checks passed\n");
  return 0;
}
