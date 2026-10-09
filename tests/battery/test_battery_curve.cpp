// Host harness for the battery curve. Exists because the number the wearer watches all day is the
// one thing about the battery they can see, and a curve that spends a quarter of its scale on a
// fiftieth of the discharge is not something to discover by wearing the watch for a week.
//
// The reference discharge below is the physical claim the curve rests on. The point of pinning it
// here is that anyone moving a point in the table has to keep it consistent with a stated cell
// rather than with a hunch, and that the fault the curve was written to fix cannot come back
// quietly: theOldSixPointTableWouldFail proves the checks have teeth by running them against what
// was there before.
#include <cstdio>
#include <cmath>

#include "components/battery/BatteryCurve.h"
#include "utility/LinearApproximation.h"

using namespace Pinetime::Controllers;

static int failures = 0;

#define CHECK(cond)                                                                                                            \
  do {                                                                                                                         \
    if (!(cond)) {                                                                                                             \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                                                 \
      failures++;                                                                                                              \
    }                                                                                                                          \
  } while (0)

namespace {
  /// Resting terminal voltage of a small single-cell LiCoO2 pack at about C/100, which is the load
  /// a PineTime puts on it. Charge left, as a share of the cell's own capacity, against millivolts.
  struct ReferencePoint {
    double soc;
    double millivolts;
  };

  constexpr ReferencePoint reference[] = {{0, 3400},  {5, 3620},  {10, 3690}, {15, 3710}, {20, 3730}, {25, 3750}, {30, 3770},
                                          {35, 3785}, {40, 3800}, {45, 3820}, {50, 3840}, {55, 3855}, {60, 3875}, {65, 3910},
                                          {70, 3950}, {75, 3985}, {80, 4020}, {85, 4070}, {90, 4105}, {95, 4140}, {100, 4170}};
  constexpr int referenceSize = sizeof(reference) / sizeof(reference[0]);

  double Interpolate(double x, bool fromSoc) {
    auto key = [&](int i) {
      return fromSoc ? reference[i].soc : reference[i].millivolts;
    };
    auto value = [&](int i) {
      return fromSoc ? reference[i].millivolts : reference[i].soc;
    };
    if (x <= key(0)) {
      return value(0);
    }
    for (int i = 1; i < referenceSize; i++) {
      if (x < key(i)) {
        return value(i - 1) + (x - key(i - 1)) * (value(i) - value(i - 1)) / (key(i) - key(i - 1));
      }
    }
    return value(referenceSize - 1);
  }

  double MillivoltsAtSoc(double soc) {
    return Interpolate(soc, true);
  }

  double SocAtMillivolts(double millivolts) {
    return Interpolate(millivolts, false);
  }

  /// The voltage the cell rests at when the given share of the window the watch actually uses is
  /// left. The window runs from the voltage the curve calls empty to the one it calls full, which
  /// is narrower than the cell's own range at both ends on purpose.
  double MillivoltsAtUsable(double usablePercent) {
    const double socEmpty = SocAtMillivolts(BatteryCurve::emptyMillivolts);
    const double socFull = SocAtMillivolts(BatteryCurve::fullMillivolts);
    return MillivoltsAtSoc(socEmpty + (socFull - socEmpty) * usablePercent / 100.0);
  }

  /// How much of the scale a table spends on each tenth of the discharge. One means the number on
  /// screen falls at the same rate all the way down, which is the whole point.
  ///
  /// @param shown what the table under test reports for a voltage
  double WorstBandError(uint8_t (*shown)(uint16_t)) {
    double worst = 0;
    for (int low = 0; low < 100; low += 10) {
      const double from = shown(static_cast<uint16_t>(std::lround(MillivoltsAtUsable(low))));
      const double to = shown(static_cast<uint16_t>(std::lround(MillivoltsAtUsable(low + 10))));
      worst = std::fmax(worst, std::fabs((to - from) / 10.0 - 1.0));
    }
    return worst;
  }

  uint8_t OldSixPointTable(uint16_t millivolts) {
    static const Pinetime::Utility::LinearApproximation<uint16_t, uint8_t, 6> approx {
      {{{3500, 0}, {3616, 3}, {3723, 22}, {3776, 48}, {3979, 79}, {4180, 100}}}};
    return approx.GetValue(millivolts);
  }

  void theScaleRunsFromEmptyToFullAndStopsThere() {
    CHECK(BatteryCurve::PercentFromVoltage(BatteryCurve::emptyMillivolts) == 0);
    CHECK(BatteryCurve::PercentFromVoltage(BatteryCurve::fullMillivolts) == 100);
    // A flat cell and a cell still on the charger both sit outside the table, and neither may wrap
    // round to the other end of the scale.
    CHECK(BatteryCurve::PercentFromVoltage(3000) == 0);
    CHECK(BatteryCurve::PercentFromVoltage(BatteryCurve::emptyMillivolts - 1) == 0);
    CHECK(BatteryCurve::PercentFromVoltage(4200) == 100);
    CHECK(BatteryCurve::PercentFromVoltage(5000) == 100);
  }

  void aHigherVoltageIsNeverALowerPercentage() {
    uint8_t previous = 0;
    for (uint16_t millivolts = 3000; millivolts <= 4400; millivolts++) {
      const uint8_t percent = BatteryCurve::PercentFromVoltage(millivolts);
      CHECK(percent >= previous);
      CHECK(percent <= 100);
      previous = percent;
    }
  }

  void theNumberShownIsTheChargeLeft() {
    for (int usable = 0; usable <= 100; usable++) {
      const auto millivolts = static_cast<uint16_t>(std::lround(MillivoltsAtUsable(usable)));
      const int shown = BatteryCurve::PercentFromVoltage(millivolts);
      // Three points of slack for the table being written in whole millivolts and read back with
      // integer arithmetic.
      CHECK(std::abs(shown - usable) <= 3);
    }
  }

  void everyTenthOfTheDischargeCostsTheSameSliceOfTheScale() {
    // This is the fault the curve was rewritten for: not that the number was wrong at any one
    // moment, but that it fell at wildly different speeds depending on where it was.
    const double worst = WorstBandError(BatteryCurve::PercentFromVoltage);
    if (worst > 0.15) {
      printf("  FAIL worst band is off by %.0f%%, budget is 15%%\n", worst * 100);
      failures++;
    }
  }

  void theOldSixPointTableWouldFail() {
    // Keeps the check above honest. The six points this replaced burned nearly twice the scale it
    // should have in one band and about half in another, which is the sticking and lurching the
    // wearer reported, and it has to be a failure here or the budget above is measuring nothing.
    const double worst = WorstBandError(OldSixPointTable);
    CHECK(worst > 0.15);
  }

  void aWatchThatMeasuresLowIsPutBackOnTheScale() {
    // This watch read 4116 mV when its charger stopped, so it reads about 2% low, and every later
    // reading of its has to be scaled back up before the curve sees it.
    constexpr uint16_t observed = 4116;
    CHECK(BatteryCurve::Calibrated(observed, observed) == BatteryCurve::nominalTermination);
    CHECK(BatteryCurve::Calibrated(3800, observed) > 3800);
    CHECK(BatteryCurve::Calibrated(3800, observed) < 3900);
    // A watch whose reading needs no correcting gets none.
    CHECK(BatteryCurve::Calibrated(3800, BatteryCurve::nominalTermination) == 3800);
  }

  void aWatchLearnsItsErrorFromItsFirstChargeWhicheverWayItIsOff() {
    // Before any charge has finished there is nothing to correct by.
    CHECK(BatteryCurve::Calibrated(3800, BatteryCurve::noTerminationSeen) == 3800);

    // A watch that reads low has to be corrected as surely as one that reads high. Starting from
    // nominalTermination and keeping the highest reading only ever corrected the high one.
    uint16_t low = BatteryCurve::KeptTermination(BatteryCurve::noTerminationSeen, 4116);
    CHECK(low == 4116);
    CHECK(BatteryCurve::Calibrated(3800, low) > 3800);
    const uint16_t high = BatteryCurve::KeptTermination(BatteryCurve::noTerminationSeen, 4250);
    CHECK(high == 4250);
    CHECK(BatteryCurve::Calibrated(3800, high) < 3800);

    // A charge interrupted early terminates lower and must not drag the correction down, and a
    // reading outside the credible window must not move it at all.
    low = BatteryCurve::KeptTermination(low, 4050);
    CHECK(low == 4116);
    low = BatteryCurve::KeptTermination(low, 3700);
    CHECK(low == 4116);
    CHECK(BatteryCurve::KeptTermination(BatteryCurve::noTerminationSeen, 3700) == BatteryCurve::noTerminationSeen);
  }

  void anImplausibleTerminationIsIgnored() {
    // A supply too weak to charge from leaves the watch looking at the cell's own voltage with
    // charging finished, which is not a termination voltage at all. Believing it would scale every
    // later reading up by twenty percent and show a flat battery as half full.
    CHECK(!BatteryCurve::IsCredibleTermination(3700));
    CHECK(!BatteryCurve::IsCredibleTermination(4500));
    CHECK(BatteryCurve::IsCredibleTermination(BatteryCurve::nominalTermination));
    // And a reading that was never believed leaves the measurement alone.
    CHECK(BatteryCurve::Calibrated(3800, 3700) == 3800);
  }
}

int main() {
  printf("battery curve\n");
  theScaleRunsFromEmptyToFullAndStopsThere();
  aHigherVoltageIsNeverALowerPercentage();
  theNumberShownIsTheChargeLeft();
  everyTenthOfTheDischargeCostsTheSameSliceOfTheScale();
  theOldSixPointTableWouldFail();
  aWatchThatMeasuresLowIsPutBackOnTheScale();
  aWatchLearnsItsErrorFromItsFirstChargeWhicheverWayItIsOff();
  anImplausibleTerminationIsIgnored();

  if (failures == 0) {
    printf("  all checks passed\n");
    return 0;
  }
  printf("  %d check(s) failed\n", failures);
  return 1;
}
