#pragma once

#include <cstdint>

#include "utility/LinearApproximation.h"

namespace Pinetime {
  namespace Controllers {
    /// Turning a battery voltage into the percentage the watch shows, and correcting the voltage
    /// for the watch that measured it.
    ///
    /// Its own header, free of the nRF headers BatteryController drags in, so that the curve can be
    /// compiled and checked on a host. It is the part worth checking: the reading is a divider and
    /// an ADC, but what the wearer complains about is the shape of the number.
    namespace BatteryCurve {

      /// What a Li-ion charger holds a cell at, and therefore what a reading taken the moment
      /// charging stops really is.
      constexpr uint16_t nominalTermination = 4200;

      /// The window a reading has to fall in to be believed as a termination voltage. Outside it
      /// the watch is looking at something else, usually a supply too weak to charge from, and a
      /// correction built on that would be worse than no correction at all.
      constexpr uint16_t lowestCredibleTermination = 4000;
      constexpr uint16_t highestCredibleTermination = 4300;

      inline bool IsCredibleTermination(uint16_t millivolts) {
        return millivolts >= lowestCredibleTermination && millivolts <= highestCredibleTermination;
      }

      /// A measured voltage put back on the scale the curve is drawn on.
      ///
      /// The divider and the ADC reference each carry a few percent of tolerance, so two watches
      /// report different voltages for the same cell, and the error is a factor rather than an
      /// offset: it grows with the reading. One known voltage is enough to remove it, and the watch
      /// is handed one every time it finishes charging, since a charger stops at a voltage that is
      /// the same on every charger to within about a percent.
      ///
      /// @param measured what this watch's ADC reported, in millivolts
      /// @param observedTermination what this watch reported the last time charging finished, or
      ///        noTerminationSeen while no full charge has been seen, which leaves the reading
      ///        untouched
      inline uint16_t Calibrated(uint16_t measured, uint16_t observedTermination) {
        if (!IsCredibleTermination(observedTermination)) {
          return measured;
        }
        return static_cast<uint16_t>(static_cast<uint32_t>(measured) * nominalTermination / observedTermination);
      }

      /// What a watch keeps as its termination reading before it has seen a charge finish.
      ///
      /// Below the credible window, so Calibrated leaves readings alone, and below every credible
      /// reading, so the first one replaces it whichever side of nominal it falls. Starting from
      /// nominalTermination instead would let only a watch that reads high ever be corrected: one
      /// that reads 4150 at termination would lose to the 4200 it started from, every time.
      constexpr uint16_t noTerminationSeen = 0;

      /// The termination reading to keep once charging has finished and the watch reads `reading`.
      ///
      /// The highest one seen is kept rather than the latest, because a charge interrupted early
      /// terminates low and would otherwise drag the correction with it; a full charge puts it back.
      ///
      /// @param kept what was kept so far, noTerminationSeen at first
      /// @param reading what the watch reads now, with charging finished and the charger attached
      inline uint16_t KeptTermination(uint16_t kept, uint16_t reading) {
        if (!IsCredibleTermination(reading)) {
          return kept;
        }
        return reading > kept ? reading : kept;
      }

      /// Resting terminal voltage in millivolts against the share of usable charge left, and the
      /// reason the number on screen falls at a steady rate rather than sticking and then lurching.
      ///
      /// A lithium cell's voltage is not linear in its charge: it falls quickly off a full charge,
      /// then sits on a long plateau between roughly 3.75 and 3.90 V where most of the charge is,
      /// then drops off a knee at the bottom. Reading a percentage off that needs the curve. The
      /// six points this replaced spent 26 points of the scale on the 53 mV between 3723 and 3776,
      /// half the plateau's width, against 31 points on the 203 mV above it, so crossing 3776 cost
      /// about three times as much of the scale per millivolt as the segment above did. That
      /// crossing is the sudden fall out of the high forties.
      ///
      /// The points describe a small single-cell LiCoO2 pack at about C/100, which is where a
      /// PineTime sits: a couple of milliamps out of around 180 mAh, so the internal resistance
      /// costs well under a millivolt and what the ADC reads is the resting voltage. Twenty-one of
      /// them, five points of scale apart, hold the shape to within two percent everywhere.
      ///
      /// Both ends are chosen rather than measured. Zero sits at 3350 mV, low on the knee, so the
      /// little charge left below 3.5 V is counted instead of the watch reading 0% while it still
      /// runs. A hundred sits at 4150 rather than at the 4200 mV the charger stops at, because a
      /// cell taken off a charger reads 4.20 V and settles towards 4.15 over the next few hours
      /// with very little charge actually gone: counting that settling as capacity is what makes a
      /// fresh charge appear to collapse. Everything from 4150 up reads full. Widen that gap if a
      /// fresh charge still falls away too fast, narrow it if the watch sits on 100% for too long.
      ///
      /// The points between are not a straight line from one end to the other: each is the voltage
      /// at which the reference cell has another twentieth of the window's charge left, so at a
      /// steady drain the number falls at a steady rate. They are regenerated from the reference
      /// discharge in tests/battery/test_battery_curve.cpp whenever an end moves.
      constexpr uint16_t emptyMillivolts = 3350;
      constexpr uint16_t fullMillivolts = 4150;

      /// The reference curve in hundredths of a percent, fine enough for LearnedCurve to take the
      /// share of the charge a 10 mV slice holds off it.
      inline uint16_t HundredthsFromVoltage(uint16_t millivolts) {
        static const Utility::LinearApproximation<uint16_t, uint16_t, 21> approx {{{{emptyMillivolts, 0},
                                                                                      {3624, 500},
                                                                                      {3690, 1000},
                                                                                      {3710, 1500},
                                                                                      {3729, 2000},
                                                                                      {3748, 2500},
                                                                                      {3767, 3000},
                                                                                      {3782, 3500},
                                                                                      {3797, 4000},
                                                                                      {3815, 4500},
                                                                                      {3834, 5000},
                                                                                      {3850, 5500},
                                                                                      {3868, 6000},
                                                                                      {3896, 6500},
                                                                                      {3933, 7000},
                                                                                      {3968, 7500},
                                                                                      {4002, 8000},
                                                                                      {4042, 8500},
                                                                                      {4084, 9000},
                                                                                      {4118, 9500},
                                                                                      {fullMillivolts, 10000}}}};
        return approx.GetValue(millivolts);
      }

      inline uint8_t PercentFromVoltage(uint16_t millivolts) {
        return static_cast<uint8_t>(HundredthsFromVoltage(millivolts) / 100);
      }
    }
  }
}
