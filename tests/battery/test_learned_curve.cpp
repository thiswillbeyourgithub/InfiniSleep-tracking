// Host harness for the curve the watch learns from its own discharges.
//
// The watch is simulated discharging a cell whose voltage does not follow the reference curve, read
// through an ADC that steps by 4.7 mV, with a little noise and the settling a cell does off the
// charger. If learning works, after a few discharges the percentage falls at a steady rate in time
// on that cell, which the reference curve alone does not manage.
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "components/battery/LearnedCurve.h"

using namespace Pinetime::Controllers;

static int failures = 0;

#define CHECK(cond)                                                                                                            \
  do {                                                                                                                         \
    if (!(cond)) {                                                                                                             \
      printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                  \
      failures++;                                                                                                              \
    }                                                                                                                          \
  } while (0)

namespace {
  /// The simulated watch's cell: resting voltage against the share of the discharge time left.
  /// Deliberately not the reference curve: this is the shape upstream InfiniTime measured over time
  /// on three PineTimes, stretched to the 3350 to 4150 mV scale, and it holds much more of the
  /// discharge under 3.75 V than the reference says.
  struct CellPoint {
    double timeLeft;
    double millivolts;
  };

  constexpr CellPoint cell[] = {{0, 3350}, {0.03, 3500}, {0.06, 3616}, {0.24, 3723}, {0.49, 3776}, {0.80, 3979}, {1.0, 4150}};
  constexpr int cellSize = sizeof(cell) / sizeof(cell[0]);

  double CellMillivolts(double timeLeft) {
    for (int i = 1; i < cellSize; i++) {
      if (timeLeft <= cell[i].timeLeft) {
        return cell[i - 1].millivolts +
               (timeLeft - cell[i - 1].timeLeft) * (cell[i].millivolts - cell[i - 1].millivolts) / (cell[i].timeLeft - cell[i - 1].timeLeft);
      }
    }
    return cell[cellSize - 1].millivolts;
  }

  /// What the watch's ADC makes of a voltage: 4800/1024 mV steps and a couple of millivolts of
  /// noise, deterministic so a failure can be reproduced.
  uint16_t Read(double millivolts) {
    static uint32_t seed = 12345;
    seed = seed * 1103515245 + 12345;
    const double noise = static_cast<double>((seed >> 16) % 7) - 3.0;
    const double step = 4800.0 / 1024.0;
    return static_cast<uint16_t>(std::floor((millivolts + noise) / step) * step);
  }

  constexpr uint32_t samplePeriod = 600;
  constexpr double dischargeHours = 7 * 24;

  /// One discharge from `from` to `to` (shares of the discharge time left), sampled every ten
  /// minutes. A cell just off the charger reads 40 mV high, fading over the first hour.
  ///
  /// @param dayAndNight when true the drain swings between half and one and a half times its
  ///        average over a day, as it does between a night asleep and a day of use
  void Discharge(LearnedCurve& curve, double from, double to, bool dayAndNight = false) {
    curve.StartDischarge();
    double timeLeft = from;
    double seconds = 0;
    while (timeLeft > to) {
      const double settling = 40.0 * std::exp(-seconds / 1200.0);
      curve.OnSample(Read(CellMillivolts(timeLeft) + settling), samplePeriod);
      double rate = 1.0;
      if (dayAndNight) {
        rate = 1.0 + 0.5 * std::sin(2 * M_PI * seconds / 86400.0);
      }
      timeLeft -= rate * samplePeriod / (dischargeHours * 3600);
      seconds += samplePeriod;
    }
    curve.EndDischarge();
  }

  /// The worst gap, in points of the scale, between what the curve shows and the share of the
  /// discharge time actually left.
  int WorstError(const LearnedCurve& curve) {
    int worst = 0;
    for (int left = 0; left <= 100; left++) {
      const auto millivolts = static_cast<uint16_t>(std::lround(CellMillivolts(left / 100.0)));
      worst = std::max(worst, std::abs(curve.Percent(millivolts) - left));
    }
    return worst;
  }

  void anUntrainedWatchReadsTheReferenceCurve() {
    const LearnedCurve curve;
    for (uint16_t millivolts = 3000; millivolts <= 4400; millivolts++) {
      // Within a point: the learned curve interpolates on a 10 mV grid, the reference between its
      // own points, and a slice holding one of those points rounds a little differently.
      CHECK(std::abs(curve.Percent(millivolts) - BatteryCurve::PercentFromVoltage(millivolts)) <= 1);
    }
  }

  void aFewFullDischargesMakeTheNumberFallSteadily() {
    LearnedCurve curve;
    const int before = WorstError(curve);
    for (int i = 0; i < 6; i++) {
      Discharge(curve, 1.0, 0.0);
    }
    const int after = WorstError(curve);
    printf("  steady drain: worst gap %d points untrained, %d after six discharges\n", before, after);
    // The reference curve is the wrong shape for this cell, so learning has something to fix.
    CHECK(before > 10);
    CHECK(after <= 3);
  }

  void aDrainThatSwingsBetweenDayAndNightStillAveragesOut() {
    LearnedCurve curve;
    for (int i = 0; i < 8; i++) {
      Discharge(curve, 1.0, 0.0, true);
    }
    const int after = WorstError(curve);
    printf("  day and night drain: worst gap %d points after eight discharges\n", after);
    CHECK(after <= 6);
  }

  void partialDischargesTeachTheSlicesTheyCross() {
    // Charged every evening from about two thirds: never full, never near empty.
    LearnedCurve curve;
    for (int i = 0; i < 10; i++) {
      Discharge(curve, 1.0, 0.6);
    }
    int learned = 0;
    int learnedBelow3700 = 0;
    for (int i = 0; i < LearnedCurve::sliceCount; i++) {
      if (curve.Learned()[i] != 0) {
        learned++;
        if (BatteryCurve::emptyMillivolts + i * LearnedCurve::sliceMillivolts < 3700) {
          learnedBelow3700++;
        }
      }
    }
    CHECK(learned > 10);
    CHECK(learnedBelow3700 == 0);
    // Still a sane curve everywhere, the bottom borrowed from the reference.
    uint8_t previous = 0;
    for (uint16_t millivolts = 3300; millivolts <= 4200; millivolts++) {
      const uint8_t percent = curve.Percent(millivolts);
      CHECK(percent >= previous);
      CHECK(percent <= 100);
      previous = percent;
    }
  }

  void theHourOffTheChargerTeachesNothing() {
    // Unplugged for less than the settling time: the voltage falls through the top slices with
    // hardly any charge gone, and none of it may be learned.
    LearnedCurve curve;
    curve.StartDischarge();
    for (uint32_t seconds = 0; seconds < LearnedCurve::settleSeconds; seconds += samplePeriod) {
      curve.OnSample(static_cast<uint16_t>(4200 - seconds / 20), samplePeriod);
    }
    CHECK(!curve.EndDischarge());
  }

  void pluggingInWithoutAnUnplugChangesNothing() {
    LearnedCurve curve;
    CHECK(!curve.EndDischarge());
    curve.OnSample(3800, samplePeriod);
    CHECK(!curve.EndDischarge());
  }

  /// A slow fall through a few slices after settling, optionally with one reading pulled 60 mV
  /// down by the motor halfway.
  LearnedCurve FallWithDip(bool dip) {
    LearnedCurve curve;
    curve.StartDischarge();
    uint32_t seconds = 0;
    for (; seconds < LearnedCurve::settleSeconds; seconds += samplePeriod) {
      curve.OnSample(3900, samplePeriod);
    }
    for (int step = 0; step < 60; step++) {
      // 1 mV every ten minutes, a slice every hour and forty minutes.
      const auto millivolts = static_cast<uint16_t>(3900 - step);
      curve.OnSample(dip && step == 30 ? static_cast<uint16_t>(millivolts - 60) : millivolts, samplePeriod);
    }
    curve.EndDischarge();
    return curve;
  }

  void aMomentaryDipIsNotACrossing() {
    const LearnedCurve steady = FallWithDip(false);
    const LearnedCurve dipped = FallWithDip(true);
    // The dip itself is dropped, but it does take the place of a real reading in the median, which
    // can move one crossing by a sample: ten minutes from one slice to its neighbour, none lost.
    int learned = 0;
    int steadyTotal = 0;
    int dippedTotal = 0;
    for (int i = 0; i < LearnedCurve::sliceCount; i++) {
      learned += steady.Learned()[i] != 0;
      steadyTotal += steady.Learned()[i];
      dippedTotal += dipped.Learned()[i];
      CHECK(std::abs(dipped.Learned()[i] - steady.Learned()[i]) <= static_cast<int>(samplePeriod / 60));
    }
    CHECK(dippedTotal == steadyTotal);
    // And the fall did teach something, or the comparison above proves nothing.
    CHECK(learned >= 3);
  }

  void aMedianIsTakenNotAnAverage() {
    // Two dips in a row are a real fall, not noise: they must get through.
    LearnedCurve curve;
    curve.StartDischarge();
    for (uint32_t seconds = 0; seconds < 2 * LearnedCurve::settleSeconds; seconds += samplePeriod) {
      curve.OnSample(3900, samplePeriod);
    }
    for (int i = 0; i < 40; i++) {
      curve.OnSample(3800, samplePeriod);
    }
    CHECK(curve.EndDischarge());
  }
}

int main() {
  printf("learned battery curve\n");
  anUntrainedWatchReadsTheReferenceCurve();
  aFewFullDischargesMakeTheNumberFallSteadily();
  aDrainThatSwingsBetweenDayAndNightStillAveragesOut();
  partialDischargesTeachTheSlicesTheyCross();
  theHourOffTheChargerTeachesNothing();
  pluggingInWithoutAnUnplugChangesNothing();
  aMomentaryDipIsNotACrossing();
  aMedianIsTakenNotAnAverage();

  if (failures == 0) {
    printf("  all checks passed\n");
    return 0;
  }
  printf("  %d check(s) failed\n", failures);
  return 1;
}
