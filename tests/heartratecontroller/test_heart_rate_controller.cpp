// Host harness for HeartRateController, the one place that sees every reading the heart rate task
// publishes.
//
// Exists for LatestSettledReading(). An activity epoch used to read HeartRate() once, at the end of its
// settle window, and a single bad window just before that instant makes the task publish Running
// with 0, which cost the epoch its whole reading. What the epoch needs is the last good value of
// the window, and whether it gets one is a matter of the order updates arrive in, which a wrist
// cannot reproduce on demand and a test can.

#include <cstdio>
#include "components/heartrate/HeartRateController.h"
#include "components/ble/HeartRateService.h"
#include "heartratetask/HeartRateTask.h"

using namespace Pinetime::Controllers;
using States = HeartRateController::States;

static int failures = 0;

#define CHECK(cond)                                                                                                                        \
  do {                                                                                                                                     \
    if (!(cond)) {                                                                                                                         \
      printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                              \
      failures++;                                                                                                                          \
    }                                                                                                                                      \
  } while (0)

namespace {
  struct Fixture {
    Pinetime::Applications::HeartRateTask task;
    HeartRateService service;
    HeartRateController controller;

    Fixture() {
      controller.SetHeartRateTask(&task);
      controller.SetService(&service);
    }
  };
}

int main() {
  printf("A bad window at the end of the epoch keeps the good reading before it\n");
  {
    Fixture f;
    f.controller.ClearLatestSettledReading();
    f.controller.Start();
    f.controller.Update(States::NotEnoughData, 0, 0);
    f.controller.Update(States::Running, 64, 10);
    f.controller.Update(States::Running, 62, 5);
    // What HeartRateTask publishes when Ppg asks for a reset after a good window.
    f.controller.Update(States::Running, 0, 0);
    // The snapshot the epoch used to take: nothing to record.
    CHECK(f.controller.HeartRate() == 0);
    CHECK(f.controller.LatestSettledReading() == 62);
  }

  printf("A coarse estimate is not a settled reading\n");
  {
    Fixture f;
    f.controller.ClearLatestSettledReading();
    f.controller.Start();
    // The half window estimate: Ppg reports it with the 10 bpm resolution of half a window.
    f.controller.Update(States::Running, 64, 10);
    CHECK(f.controller.HeartRate() == 64);
    CHECK(f.controller.LatestSettledReading() == 0);
    // Windows that disagree by more than the bound are not settled either.
    f.controller.Update(States::Running, 66, 12);
    CHECK(f.controller.LatestSettledReading() == 0);
    f.controller.Update(States::Running, 63, 5);
    CHECK(f.controller.LatestSettledReading() == 63);
  }

  printf("Nothing converged means no reading\n");
  {
    Fixture f;
    f.controller.ClearLatestSettledReading();
    f.controller.Start();
    f.controller.Update(States::NotEnoughData, 0, 0);
    f.controller.Update(States::NotEnoughData, 0, 0);
    CHECK(f.controller.LatestSettledReading() == 0);
  }

  printf("A reading from before the epoch is not this epoch's\n");
  {
    Fixture f;
    f.controller.Start();
    f.controller.Update(States::Running, 70, 5);
    f.controller.Stop();
    f.controller.ClearLatestSettledReading();
    f.controller.Start();
    f.controller.Update(States::NotEnoughData, 0, 0);
    CHECK(f.controller.LatestSettledReading() == 0);
  }

  printf("An update after Stop is ignored, reading included\n");
  {
    Fixture f;
    f.controller.ClearLatestSettledReading();
    f.controller.Start();
    f.controller.Stop();
    f.controller.Update(States::Running, 80, 5);
    CHECK(f.controller.LatestSettledReading() == 0);
  }

  printf("%s\n", failures == 0 ? "PASS" : "FAILED");
  return failures == 0 ? 0 : 1;
}
