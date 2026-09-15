// Host harness for PomodoroController, built against the stub headers in ../stubs so the real .cpp
// compiles unchanged. Exists because the state machine only shows itself over tens of minutes of
// wall clock time on the watch: here a full queue, alert included, runs in microseconds.
#include <cstdio>
#include <cstring>

#include "components/pomodoro/PomodoroController.h"
#include "components/motor/MotorController.h"

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
  unsigned alertCount = 0;

  void CountAlert(void*) {
    alertCount++;
  }

  // Everything one test needs, torn down and rebuilt between tests so no timer outlives its owner.
  struct Fixture {
    FS fs;
    MotorController motor;
    PomodoroController pomodoro {fs, motor, nullptr, CountAlert};

    Fixture() {
      pomodoro.Init();
    }

    ~Fixture() {
      TestTimers::Reset();
    }
  };

  void Reset() {
    TestTimers::Reset();
    alertCount = 0;
  }

  // Types a queue into the controller the way the keypad would, one button at a time.
  void Type(PomodoroController& pomodoro, const char* text) {
    while (std::strlen(pomodoro.GetQueue()) > 0) {
      pomodoro.DeleteLastCharacter();
    }
    for (const char* c = text; *c != '\0'; c++) {
      if (*c == ',') {
        pomodoro.AppendSeparator();
      } else {
        pomodoro.AppendDigit(*c);
      }
    }
  }

  TickType_t Minutes(unsigned n) {
    return pdMS_TO_TICKS(n * 60 * 1000);
  }

  /* How long an alert lasts. The first vibration goes off the instant the interval elapses rather
   * than a second later, so an alert of n vibrations is over n-1 seconds after it began. */
  TickType_t Alert(const PomodoroController& pomodoro) {
    return pdMS_TO_TICKS((pomodoro.GetVibrationsPerAlarm() - 1) * 1000);
  }
}

int main() {
  {
    printf("the keypad refuses anything that would not parse\n");
    Reset();
    Fixture f;

    Type(f.pomodoro, "2,10");
    CHECK(std::strcmp(f.pomodoro.GetQueue(), "2,10") == 0);
    CHECK(f.pomodoro.Start());
    CHECK(f.pomodoro.GetIntervalCount() == 2);

    // A leading separator would make an empty first interval, so the keypad drops it outright.
    Type(f.pomodoro, ",5");
    CHECK(std::strcmp(f.pomodoro.GetQueue(), "5") == 0);

    // A doubled separator goes the same way, leaving the queue parseable at every keystroke.
    Type(f.pomodoro, "5");
    f.pomodoro.AppendSeparator();
    f.pomodoro.AppendSeparator();
    CHECK(std::strcmp(f.pomodoro.GetQueue(), "5,") == 0);
    // "5," is still refused by Go, which is the only place a trailing separator can be caught.
    CHECK(!f.pomodoro.Start());

    Type(f.pomodoro, "");
    CHECK(!f.pomodoro.Start());

    // Thirteen characters is the cap, and seven intervals is what that allows at one digit each.
    Type(f.pomodoro, "1,2,3,4,5,6,7");
    CHECK(std::strcmp(f.pomodoro.GetQueue(), "1,2,3,4,5,6,7") == 0);
    CHECK(f.pomodoro.Start());
    CHECK(f.pomodoro.GetIntervalCount() == 7);

    // Past the cap the extra digits are simply not taken.
    Type(f.pomodoro, "1,2,3,4,5,6,78");
    CHECK(std::strcmp(f.pomodoro.GetQueue(), "1,2,3,4,5,6,7") == 0);
  }

  {
    printf("the vibration count wraps in both directions\n");
    Reset();
    Fixture f;

    for (uint8_t i = f.pomodoro.GetVibrationsPerAlarm(); i < 15; i++) {
      f.pomodoro.IncreaseVibrations();
    }
    CHECK(f.pomodoro.GetVibrationsPerAlarm() == 15);
    f.pomodoro.IncreaseVibrations();
    CHECK(f.pomodoro.GetVibrationsPerAlarm() == 1);
    f.pomodoro.DecreaseVibrations();
    CHECK(f.pomodoro.GetVibrationsPerAlarm() == 15);
  }

  {
    printf("presets cycle round in both directions\n");
    Reset();
    Fixture f;

    f.pomodoro.LoadNextPreset();
    const char* second = f.pomodoro.GetQueue();
    char remembered[16];
    std::strcpy(remembered, second);
    f.pomodoro.LoadPreviousPreset();
    f.pomodoro.LoadNextPreset();
    CHECK(std::strcmp(f.pomodoro.GetQueue(), remembered) == 0);

    // Three presets, so three steps come back to where it started.
    f.pomodoro.LoadNextPreset();
    f.pomodoro.LoadNextPreset();
    f.pomodoro.LoadNextPreset();
    CHECK(std::strcmp(f.pomodoro.GetQueue(), remembered) == 0);
  }

  {
    printf("a queue runs its intervals in order and chains back to the start\n");
    Reset();
    Fixture f;

    Type(f.pomodoro, "2,10");
    CHECK(f.pomodoro.Start());
    CHECK(f.pomodoro.GetState() == PomodoroController::State::Running);
    CHECK(f.pomodoro.GetCurrentInterval() == 0);
    CHECK(f.pomodoro.SecondsRemaining() == 120);

    // One second short of the first interval, nothing has happened yet.
    TestTimers::Advance(Minutes(2) - pdMS_TO_TICKS(1000));
    CHECK(f.pomodoro.GetState() == PomodoroController::State::Running);
    CHECK(alertCount == 0);

    TestTimers::Advance(pdMS_TO_TICKS(1000));
    CHECK(f.pomodoro.GetState() == PomodoroController::State::Ringing);
    CHECK(alertCount == 1);
    CHECK(!f.motor.pulses.empty());

    // One vibration short of the end, the alert is still going.
    TestTimers::Advance(Alert(f.pomodoro) - pdMS_TO_TICKS(1000));
    CHECK(f.pomodoro.GetState() == PomodoroController::State::Ringing);

    // The last vibration hands over to the next interval by itself.
    TestTimers::Advance(pdMS_TO_TICKS(1000));
    CHECK(f.pomodoro.GetState() == PomodoroController::State::Running);
    CHECK(f.pomodoro.GetCurrentInterval() == 1);
    CHECK(f.pomodoro.SecondsRemaining() == 600);
    CHECK(f.pomodoro.GetCompletedCycles() == 0);

    // Finishing the second interval and its alert closes one full walk of the queue.
    TestTimers::Advance(Minutes(10) + Alert(f.pomodoro));
    CHECK(f.pomodoro.GetCurrentInterval() == 0);
    CHECK(f.pomodoro.GetCompletedCycles() == 1);
    CHECK(alertCount == 2);
  }

  {
    printf("an alert vibrates exactly as many times as the count says\n");
    Reset();
    Fixture f;

    // Five is above the threshold where the pattern turns random, so this also covers the bursts.
    while (f.pomodoro.GetVibrationsPerAlarm() != 5) {
      f.pomodoro.IncreaseVibrations();
    }
    Type(f.pomodoro, "1");
    CHECK(f.pomodoro.Start());

    TestTimers::Advance(Minutes(1));
    CHECK(f.pomodoro.GetState() == PomodoroController::State::Ringing);
    const TickType_t alertStart = xTaskGetTickCount();

    TestTimers::Advance(Alert(f.pomodoro));
    CHECK(f.pomodoro.GetState() == PomodoroController::State::Running);

    /* Every pulse of the alert has to land inside its own second: the burst schedule is what keeps
     * a randomised pattern from bleeding into the following one and drifting out of step. */
    unsigned pulsesInAlert = 0;
    for (const auto& pulse : f.motor.pulses) {
      if (pulse.tick < alertStart) {
        continue;
      }
      pulsesInAlert++;
      const TickType_t offsetInSecond = (pulse.tick - alertStart) % pdMS_TO_TICKS(1000);
      CHECK(offsetInSecond + pdMS_TO_TICKS(pulse.durationMs) <= pdMS_TO_TICKS(1000));
    }
    // At least one pulse per second of the alert, and more than that when a burst was drawn.
    CHECK(pulsesInAlert >= 5);
  }

  {
    printf("a short alert is one long buzz rather than a burst\n");
    Reset();
    Fixture f;

    while (f.pomodoro.GetVibrationsPerAlarm() != 3) {
      f.pomodoro.DecreaseVibrations();
    }
    Type(f.pomodoro, "1");
    CHECK(f.pomodoro.Start());

    TestTimers::Advance(Minutes(1));
    const size_t before = f.motor.pulses.size();
    TestTimers::Advance(Alert(f.pomodoro));
    // One pulse per vibration and no burst, so two more after the one the alert opened with.
    CHECK(f.motor.pulses.size() - before == 2);
    for (size_t i = before; i < f.motor.pulses.size(); i++) {
      CHECK(f.motor.pulses[i].durationMs == 650);
    }
  }

  {
    printf("+1 pushes the current interval exactly one minute further out\n");
    Reset();
    Fixture f;

    Type(f.pomodoro, "5");
    CHECK(f.pomodoro.Start());
    TestTimers::Advance(Minutes(1));
    CHECK(f.pomodoro.SecondsRemaining() == 240);

    f.pomodoro.AddMinute();
    CHECK(f.pomodoro.SecondsRemaining() == 300);

    // And it is only the one interval that moved: the queue itself is untouched.
    CHECK(std::strcmp(f.pomodoro.GetQueue(), "5") == 0);
  }

  {
    printf("the button skips an alert without losing count of the cycle\n");
    Reset();
    Fixture f;

    Type(f.pomodoro, "1,1");
    CHECK(f.pomodoro.Start());
    TestTimers::Advance(Minutes(1));
    CHECK(f.pomodoro.GetState() == PomodoroController::State::Ringing);

    f.pomodoro.StopAlerting();
    CHECK(f.pomodoro.GetState() == PomodoroController::State::Running);
    CHECK(f.pomodoro.GetCurrentInterval() == 1);
    CHECK(f.pomodoro.GetCompletedCycles() == 0);

    // Skipping the second alert too completes the walk, exactly as waiting it out would have.
    TestTimers::Advance(Minutes(1));
    f.pomodoro.StopAlerting();
    CHECK(f.pomodoro.GetCurrentInterval() == 0);
    CHECK(f.pomodoro.GetCompletedCycles() == 1);
  }

  {
    printf("stop clears the countdown and the cycle counter\n");
    Reset();
    Fixture f;

    Type(f.pomodoro, "2,10");
    CHECK(f.pomodoro.Start());
    TestTimers::Advance(Minutes(2) + Alert(f.pomodoro));
    CHECK(f.pomodoro.GetCurrentInterval() == 1);

    f.pomodoro.Stop();
    CHECK(f.pomodoro.GetState() == PomodoroController::State::Stopped);
    CHECK(f.pomodoro.SecondsRemaining() == 0);
    CHECK(f.pomodoro.GetCurrentInterval() == 0);
    CHECK(f.pomodoro.GetCompletedCycles() == 0);

    // Nothing left armed, so no stray vibration after the user walked away.
    const size_t pulses = f.motor.pulses.size();
    TestTimers::Advance(Minutes(60));
    CHECK(f.motor.pulses.size() == pulses);
    CHECK(alertCount == 1);
  }

  {
    printf("a forgotten pomodoro gives up rather than running for days\n");
    Reset();
    Fixture f;

    // One vibration per alert and zero minute intervals, so 99 cycles pass in a simulated minute.
    while (f.pomodoro.GetVibrationsPerAlarm() != 1) {
      f.pomodoro.DecreaseVibrations();
    }
    Type(f.pomodoro, "0");
    CHECK(f.pomodoro.Start());
    // A zero minute interval still has to advance, so it is held at one second.
    CHECK(f.pomodoro.SecondsRemaining() == 1);

    TestTimers::Advance(Minutes(10));
    CHECK(f.pomodoro.GetState() == PomodoroController::State::Stopped);
    CHECK(alertCount == 99);
  }

  {
    printf("the queue and the vibration count survive a reboot\n");
    Reset();
    FS fs;
    MotorController motor;

    {
      PomodoroController pomodoro {fs, motor, nullptr, CountAlert};
      pomodoro.Init();
      Type(pomodoro, "45,15");
      pomodoro.IncreaseVibrations();
      const uint8_t saved = pomodoro.GetVibrationsPerAlarm();
      CHECK(pomodoro.Start());  // Start is what commits the settings to the file
      CHECK(saved == 11);
    }

    PomodoroController reloaded {fs, motor, nullptr, CountAlert};
    reloaded.Init();
    CHECK(std::strcmp(reloaded.GetQueue(), "45,15") == 0);
    CHECK(reloaded.GetVibrationsPerAlarm() == 11);
    // A reboot does not resume a pomodoro, only the settings come back.
    CHECK(reloaded.GetState() == PomodoroController::State::Stopped);
    TestTimers::Reset();
  }

  {
    printf("a corrupt settings file is discarded rather than trusted\n");
    Reset();
    FS fs;
    MotorController motor;
    fs.exists = true;
    fs.contents.assign(32, 0xff);  // wrong version byte, and a vibration count out of range

    PomodoroController pomodoro {fs, motor, nullptr, CountAlert};
    pomodoro.Init();
    CHECK(std::strcmp(pomodoro.GetQueue(), "2,10") == 0);
    CHECK(pomodoro.GetVibrationsPerAlarm() == 10);
    TestTimers::Reset();
  }

  if (failures == 0) {
    printf("\nall pomodoro tests passed\n");
    return 0;
  }
  printf("\n%d check(s) failed\n", failures);
  return 1;
}
