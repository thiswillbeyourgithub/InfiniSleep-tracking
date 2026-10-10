// Host harness for the InfiniSleep snooze bookkeeping. Exists because the fault it guards against
// only shows a day later: a snooze left behind by one morning put that morning's alarm time back
// over the one set the next evening, and nothing on the watch said so until the alarm rang late.

#include <cstdio>

#include "components/infinisleep/WakeAlarmSnooze.h"

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
  /// The alarm's time as the controller holds it, in its settings struct.
  struct Alarm {
    uint8_t hours;
    uint8_t minutes;
  };

  void snoozesMoveTheAlarmAndTheRingsEndPutsTheSetTimeBack() {
    WakeAlarmSnooze snooze;
    Alarm alarm {8, 0};
    snooze.Snooze(alarm.hours, alarm.minutes, 8, 3);
    CHECK(snooze.IsSnoozing());
    CHECK(alarm.hours == 8 && alarm.minutes == 3);
    // The second snooze moves from 8:03, but the time worth going back to is still 8:00
    snooze.Snooze(alarm.hours, alarm.minutes, 8, 6);
    CHECK(alarm.hours == 8 && alarm.minutes == 6);
    CHECK(snooze.SetHours(alarm.hours) == 8 && snooze.SetMinutes(alarm.minutes) == 0);
    snooze.End(alarm.hours, alarm.minutes);
    CHECK(!snooze.IsSnoozing());
    CHECK(alarm.hours == 8 && alarm.minutes == 0);
  }

  void aTimeSetByHandOutlivesTheSnoozeBeforeIt() {
    // The morning: the 8:00 alarm is snoozed, and the night ends some way that never ends the
    // snooze, as stopping the tracker used to.
    WakeAlarmSnooze snooze;
    Alarm alarm {8, 0};
    snooze.Snooze(alarm.hours, alarm.minutes, 8, 3);

    // The evening: 10:00 is set by hand, then the alarm is switched on, which ends any snooze.
    snooze.SetByHand(alarm.hours, alarm.minutes, 10, 0);
    CHECK(snooze.SetHours(alarm.hours) == 10 && snooze.SetMinutes(alarm.minutes) == 0);
    snooze.End(alarm.hours, alarm.minutes);
    CHECK(alarm.hours == 10 && alarm.minutes == 0);
  }

  void endingWithoutASnoozeLeavesTheAlarmAlone() {
    WakeAlarmSnooze snooze;
    Alarm alarm {10, 0};
    snooze.End(alarm.hours, alarm.minutes);
    CHECK(alarm.hours == 10 && alarm.minutes == 0);
    CHECK(snooze.SetHours(alarm.hours) == 10 && snooze.SetMinutes(alarm.minutes) == 0);
  }
}

int main() {
  printf("wake alarm snooze\n");

  snoozesMoveTheAlarmAndTheRingsEndPutsTheSetTimeBack();
  aTimeSetByHandOutlivesTheSnoozeBeforeIt();
  endingWithoutASnoozeLeavesTheAlarmAlone();

  if (failures == 0) {
    printf("  all checks passed\n");
    return 0;
  }
  printf("  %d check(s) failed\n", failures);
  return 1;
}
