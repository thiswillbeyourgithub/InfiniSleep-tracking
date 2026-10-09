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
      /// Both ends are chosen rather than measured. Zero sits at 3500 mV, where the watch already
      /// called it empty and comfortably above the brown-out. A hundred sits at 4140 rather than at
      /// the 4200 mV the charger stops at, because a cell taken off a charger reads 4.20 V and
      /// settles towards 4.15 over the next few hours with very little charge actually gone:
      /// counting that settling as capacity is what makes a fresh charge appear to collapse.
      /// Everything from 4140 up reads full, which is what those five points of scale buy. Widen
      /// that gap if a fresh charge still falls away too fast, narrow it if the watch sits on 100%
      /// for too long.
      constexpr uint16_t emptyMillivolts = 3500;
      constexpr uint16_t fullMillivolts = 4140;

      inline uint8_t PercentFromVoltage(uint16_t millivolts) {
        static const Utility::LinearApproximation<uint16_t, uint8_t, 21> approx {{{{emptyMillivolts, 0},
                                                                                  {3647, 5},
                                                                                  {3696, 10},
                                                                                  {3715, 15},
                                                                                  {3733, 20},
                                                                                  {3752, 25},
                                                                                  {3770, 30},
                                                                                  {3784, 35},
                                                                                  {3798, 40},
                                                                                  {3816, 45},
                                                                                  {3835, 50},
                                                                                  {3850, 55},
                                                                                  {3867, 60},
                                                                                  {3893, 65},
                                                                                  {3927, 70},
                                                                                  {3963, 75},
                                                                                  {3995, 80},
                                                                                  {4031, 85},
                                                                                  {4075, 90},
                                                                                  {4108, 95},
                                                                                  {fullMillivolts, 100}}}};
        return approx.GetValue(millivolts);
      }
    }
  }
}
