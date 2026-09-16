// Host harness for StopWatchController, built against the stub headers in ../stubs so the real .cpp
// compiles unchanged. Exists because the whole point of the controller is that a run outlives the
// screen showing it: nothing here ever draws anything, and the elapsed time still has to be right
// after an hour of the fake clock has gone by with nobody looking.
#include <cstdio>

#include "components/stopwatch/StopWatchController.h"

using namespace Pinetime::Controllers;

static int failures = 0;

#define CHECK(cond)                                                                                                                        \
  do {                                                                                                                                     \
    if (!(cond)) {                                                                                                                         \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                                                             \
      failures++;                                                                                                                          \
    }                                                                                                                                      \
  } while (0)

namespace {
  constexpr TickType_t Seconds(uint32_t seconds) {
    return static_cast<TickType_t>(configTICK_RATE_HZ) * seconds;
  }

  void StartsCleared() {
    printf("starts cleared\n");
    TestTimers::Reset();
    StopWatchController stopWatch;

    CHECK(stopWatch.IsCleared());
    CHECK(!stopWatch.IsRunning());
    CHECK(!stopWatch.IsPaused());
    CHECK(stopWatch.GetElapsedTime() == 0);
    CHECK(stopWatch.GetMaxLapNumber() == 0);
    CHECK(!stopWatch.GetLapFromHistory(0).has_value());
  }

  // The reason the controller exists: while the wearer is in another app, or the screen is off, no
  // code of ours runs, so the elapsed time has to come out of the tick counter rather than out of
  // something counting up on every refresh.
  void CountsWhileNobodyLooks() {
    printf("counts while nobody looks\n");
    TestTimers::Reset();
    StopWatchController stopWatch;

    stopWatch.Start();
    CHECK(stopWatch.IsRunning());

    TestTimers::Advance(Seconds(90));
    CHECK(stopWatch.GetElapsedTime() == Seconds(90));

    TestTimers::Advance(Seconds(3510));
    CHECK(stopWatch.GetElapsedTime() == Seconds(3600));
    CHECK(stopWatch.IsRunning());
  }

  void PauseFreezesAndResumeSkipsThePause() {
    printf("pause freezes, resume skips the pause\n");
    TestTimers::Reset();
    StopWatchController stopWatch;

    stopWatch.Start();
    TestTimers::Advance(Seconds(30));
    stopWatch.Pause();
    CHECK(stopWatch.IsPaused());
    CHECK(stopWatch.GetElapsedTime() == Seconds(30));

    TestTimers::Advance(Seconds(600));
    CHECK(stopWatch.GetElapsedTime() == Seconds(30));

    stopWatch.Start();
    TestTimers::Advance(Seconds(15));
    CHECK(stopWatch.GetElapsedTime() == Seconds(45));
  }

  void ClearForgetsEverything() {
    printf("clear forgets everything\n");
    TestTimers::Reset();
    StopWatchController stopWatch;

    stopWatch.Start();
    TestTimers::Advance(Seconds(10));
    stopWatch.AddLapToHistory();
    stopWatch.Pause();
    stopWatch.Clear();

    CHECK(stopWatch.IsCleared());
    CHECK(stopWatch.GetElapsedTime() == 0);
    CHECK(stopWatch.GetMaxLapNumber() == 0);
    CHECK(!stopWatch.GetLapFromHistory(0).has_value());

    // And a second run starts from zero rather than from where the first one left off.
    stopWatch.Start();
    TestTimers::Advance(Seconds(5));
    CHECK(stopWatch.GetElapsedTime() == Seconds(5));
  }

  void LapsAreNewestFirst() {
    printf("laps are newest first\n");
    TestTimers::Reset();
    StopWatchController stopWatch;

    stopWatch.Start();
    TestTimers::Advance(Seconds(10));
    stopWatch.AddLapToHistory();
    TestTimers::Advance(Seconds(20));
    stopWatch.AddLapToHistory();

    CHECK(stopWatch.GetMaxLapNumber() == 2);

    const auto latest = stopWatch.GetLapFromHistory(0);
    CHECK(latest.has_value());
    CHECK(latest->number == 2);
    CHECK(latest->timeSinceStart == Seconds(30));

    const auto first = stopWatch.GetLapFromHistory(1);
    CHECK(first.has_value());
    CHECK(first->number == 1);
    CHECK(first->timeSinceStart == Seconds(10));

    CHECK(!stopWatch.GetLapFromHistory(2).has_value());
  }

  // A lap is timed from the start of the run, not from the wall clock, so time spent paused must
  // not land in it.
  void LapsExcludePauses() {
    printf("laps exclude pauses\n");
    TestTimers::Reset();
    StopWatchController stopWatch;

    stopWatch.Start();
    TestTimers::Advance(Seconds(10));
    stopWatch.Pause();
    TestTimers::Advance(Seconds(300));
    stopWatch.Start();
    TestTimers::Advance(Seconds(5));
    stopWatch.AddLapToHistory();

    const auto lap = stopWatch.GetLapFromHistory(0);
    CHECK(lap.has_value());
    CHECK(lap->timeSinceStart == Seconds(15));
  }

  // Only the last four laps are kept, and asking for a fifth says so rather than handing back a
  // stale one.
  void OnlyTheLastFourLapsAreKept() {
    printf("only the last four laps are kept\n");
    TestTimers::Reset();
    StopWatchController stopWatch;

    stopWatch.Start();
    for (uint8_t lap = 1; lap <= 6; lap++) {
      TestTimers::Advance(Seconds(10));
      stopWatch.AddLapToHistory();
    }

    CHECK(stopWatch.GetMaxLapNumber() == 6);
    for (uint8_t index = 0; index < 4; index++) {
      const auto lap = stopWatch.GetLapFromHistory(index);
      CHECK(lap.has_value());
      CHECK(lap->number == 6 - index);
      CHECK(lap->timeSinceStart == Seconds(10 * (6 - index)));
    }
    CHECK(!stopWatch.GetLapFromHistory(4).has_value());
  }

  // Left running for more than the 1000 hours the display can word, the elapsed time wraps round
  // to zero instead of overflowing the tick arithmetic.
  void WrapsAfterAThousandHours() {
    printf("wraps after a thousand hours\n");
    TestTimers::Reset();
    StopWatchController stopWatch;

    constexpr TickType_t thousandHours = Seconds(1000 * 60 * 60);
    stopWatch.Start();
    TestTimers::Advance(thousandHours - Seconds(1));
    CHECK(stopWatch.GetElapsedTime() == thousandHours - Seconds(1));

    TestTimers::Advance(Seconds(2));
    CHECK(stopWatch.GetElapsedTime() == Seconds(1));
  }
}

int main() {
  StartsCleared();
  CountsWhileNobodyLooks();
  PauseFreezesAndResumeSkipsThePause();
  ClearForgetsEverything();
  LapsAreNewestFirst();
  LapsExcludePauses();
  OnlyTheLastFourLapsAreKept();
  WrapsAfterAThousandHours();

  if (failures == 0) {
    printf("all stopwatch tests passed\n");
    return 0;
  }
  printf("%d check(s) failed\n", failures);
  return 1;
}
