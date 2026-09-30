// Host harness for the countdown Timer controller, built against the stub headers in ../stubs so the
// real .cpp compiles unchanged. What it guards is the pause: resuming has to carry on from the time
// left, while resetting afterwards has to go back to the length the timer was first set to, which is
// easy to lose if resuming is done by starting a fresh countdown from what remained.
#include <cstdio>

#include "components/timer/Timer.h"

using namespace Pinetime::Controllers;
using namespace std::chrono_literals;

static int failures = 0;

#define CHECK(cond)                                                                                                                        \
  do {                                                                                                                                     \
    if (!(cond)) {                                                                                                                         \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                                                             \
      failures++;                                                                                                                          \
    }                                                                                                                                      \
  } while (0)

namespace {
  int expired = 0;

  void OnExpired(TimerHandle_t /*timer*/) {
    expired++;
  }

  constexpr TickType_t Seconds(uint32_t seconds) {
    return static_cast<TickType_t>(configTICK_RATE_HZ) * seconds;
  }

  void PauseKeepsTheTimeLeft() {
    printf("pause keeps the time left\n");
    TestTimers::Reset();
    expired = 0;
    Timer timer(nullptr, OnExpired);
    timer.StartTimer(10min);
    TestTimers::Advance(Seconds(60));
    timer.PauseTimer();
    CHECK(timer.IsPaused());
    CHECK(!timer.IsRunning());
    CHECK(timer.GetTimeRemaining() == 9min);
    // Nothing counts down while paused, however long it lasts.
    TestTimers::Advance(Seconds(3600));
    CHECK(timer.GetTimeRemaining() == 9min);
    CHECK(expired == 0);
  }

  void ResumeCarriesOnFromThePause() {
    printf("resume carries on from the pause\n");
    TestTimers::Reset();
    expired = 0;
    Timer timer(nullptr, OnExpired);
    timer.StartTimer(10min);
    TestTimers::Advance(Seconds(60));
    timer.PauseTimer();
    TestTimers::Advance(Seconds(3600));
    timer.ResumeTimer();
    CHECK(timer.IsRunning());
    CHECK(!timer.IsPaused());
    TestTimers::Advance(Seconds(9 * 60) - 1);
    CHECK(expired == 0);
    TestTimers::Advance(1);
    CHECK(expired == 1);
  }

  void ResumingKeepsTheLengthFirstSet() {
    printf("resuming keeps the length first set\n");
    TestTimers::Reset();
    Timer timer(nullptr, OnExpired);
    timer.StartTimer(10min);
    TestTimers::Advance(Seconds(60));
    timer.PauseTimer();
    timer.ResumeTimer();
    // A reset after this goes back to ten minutes, not to the nine that were left.
    CHECK(timer.GetLastDuration() == 10min);
  }

  void StopDropsThePause() {
    printf("stop drops the pause\n");
    TestTimers::Reset();
    Timer timer(nullptr, OnExpired);
    timer.StartTimer(10min);
    timer.PauseTimer();
    timer.StopTimer();
    CHECK(!timer.IsPaused());
    CHECK(!timer.IsRunning());
    CHECK(timer.GetTimeRemaining() == 0ms);
  }

  void StartDropsThePause() {
    printf("start drops the pause\n");
    TestTimers::Reset();
    Timer timer(nullptr, OnExpired);
    timer.StartTimer(10min);
    timer.PauseTimer();
    timer.StartTimer(5min);
    CHECK(!timer.IsPaused());
    CHECK(timer.GetLastDuration() == 5min);
  }
}

int main() {
  PauseKeepsTheTimeLeft();
  ResumeCarriesOnFromThePause();
  ResumingKeepsTheLengthFirstSet();
  StopDropsThePause();
  StartDropsThePause();
  // Frees the last test's timer, which nothing else would, so the leak checker stays quiet.
  TestTimers::Reset();

  if (failures == 0) {
    printf("all timer tests passed\n");
    return 0;
  }
  printf("%d check(s) failed\n", failures);
  return 1;
}
