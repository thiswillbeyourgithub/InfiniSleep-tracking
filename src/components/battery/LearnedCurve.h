#pragma once

#include <array>
#include <cstdint>

#include "components/battery/BatteryCurve.h"

namespace Pinetime {
  namespace Controllers {
    /// A voltage-to-percent curve the watch learns from its own discharges, so that the number falls
    /// at a steady rate in time on this watch with this cell and this wearer, rather than on the
    /// reference cell BatteryCurve describes.
    ///
    /// The scale from BatteryCurve::emptyMillivolts to fullMillivolts is cut into 10 mV slices. While
    /// unplugged, the watch notes how long its voltage takes to cross each slice; on plug-in each
    /// slice crossed from end to end moves its stored duration a quarter of the way to what was just
    /// measured. The percentage is then the time stored below the current voltage over the time
    /// stored in all slices, which is exactly "share of the discharge still ahead" if the drain is
    /// steady. A slice never crossed yet borrows its share from the reference curve, scaled to the
    /// slices that were, so an untrained watch reads exactly as BatteryCurve does and a partly
    /// trained one blends the two.
    ///
    /// Each slice learns on its own, so a discharge does not have to run from full to empty to teach
    /// anything: a day off the charger teaches the slices that day crossed.
    ///
    /// Free of the nRF and FreeRTOS headers, like BatteryCurve, so it can be run on a host. Not safe
    /// to call from two contexts at once: Battery serialises it.
    class LearnedCurve {
    public:
      static constexpr uint16_t sliceMillivolts = 10;
      static constexpr int sliceCount = (BatteryCurve::fullMillivolts - BatteryCurve::emptyMillivolts) / sliceMillivolts;
      static_assert((BatteryCurve::fullMillivolts - BatteryCurve::emptyMillivolts) % sliceMillivolts == 0);

      /// Minutes a slice took to cross, smoothed over discharges, or zero for a slice never crossed.
      /// This is all of the state worth keeping across a reboot.
      using Minutes = std::array<uint16_t, sliceCount>;

      /// How long after unplugging nothing is timed. A cell coming off the charger reads high and
      /// settles over the first hour with hardly any charge gone, so timing that would teach the top
      /// slices that they hold almost nothing.
      static constexpr uint32_t settleSeconds = 60 * 60;

      /// One discharge moves a slice a quarter of the way to what it measured, which averages out a
      /// night of sleep tracking against a day of screen use over about five discharges.
      static constexpr int adaptShift = 2;

      const Minutes& Learned() const {
        return learned;
      }

      void Restore(const Minutes& minutes) {
        learned = minutes;
      }

      /// The charger was unplugged: start timing afresh.
      void StartDischarge() {
        crossed.fill(0);
        discharging = true;
        sinceUnplug = 0;
        sinceLastCrossing = 0;
        timing = false;
        smoothedx16 = 0;
        recent = 0;
        previous = {0, 0};
        lowestSlice = sliceCount;
      }

      /// A voltage read while unplugged.
      ///
      /// @param millivolts the calibrated voltage
      /// @param elapsedSeconds time since the previous sample, ignored on the first one
      void OnSample(uint16_t millivolts, uint32_t elapsedSeconds) {
        if (!discharging) {
          return;
        }
        // A screen lit or the motor running pulls one reading down, and a single reading 60 mV low
        // would carry the average below through a slice and a half. The median of the last three
        // drops it outright.
        previous[1] = previous[0];
        previous[0] = recent;
        recent = millivolts;
        const uint16_t median = MedianOfThree(recent, previous[0] == 0 ? recent : previous[0], previous[1] == 0 ? recent : previous[1]);
        if (smoothedx16 == 0) {
          // The first sample seeds the average; time before it belongs to no sample.
          smoothedx16 = static_cast<int32_t>(median) * 16;
          elapsedSeconds = 0;
        } else {
          // The ADC steps by about 5 mV, half a slice. Averaging over about four samples, forty
          // minutes, smooths that without lagging a slice that takes hours to cross.
          smoothedx16 += (static_cast<int32_t>(median) * 16 - smoothedx16) / 4;
        }
        sinceUnplug += elapsedSeconds;
        sinceLastCrossing += elapsedSeconds;

        // Only the lowest point reached counts, so a reading that recovers after a load does not
        // cross the same slice twice.
        const int slice = SliceOf(static_cast<uint16_t>(smoothedx16 / 16));
        if (slice >= lowestSlice) {
          return;
        }
        if (sinceUnplug < settleSeconds) {
          lowestSlice = slice;
          return;
        }
        if (timing) {
          // Every slice from the one being timed down to the one just entered was crossed in the
          // time since the last crossing. More than one when the voltage fell fast, on the knee.
          const int count = lowestSlice - slice;
          const uint32_t minutes = (sinceLastCrossing + 30) / 60 / static_cast<uint32_t>(count);
          for (int i = slice + 1; i <= lowestSlice && i < sliceCount; i++) {
            crossed[i] = static_cast<uint16_t>(minutes == 0 ? 1 : (minutes > 0xFFFF ? 0xFFFF : minutes));
          }
        }
        // The first crossing after settling starts the clock: the slice before it was entered at an
        // unknown time.
        timing = true;
        sinceLastCrossing = 0;
        lowestSlice = slice;
      }

      /// The charger was plugged in: fold what this discharge measured into the learned slices.
      ///
      /// @return whether anything changed, which is when the caller has something to save
      bool EndDischarge() {
        if (!discharging) {
          return false;
        }
        discharging = false;
        bool changed = false;
        for (int i = 0; i < sliceCount; i++) {
          if (crossed[i] == 0) {
            continue;
          }
          const uint16_t before = learned[i];
          if (before == 0) {
            learned[i] = crossed[i];
          } else {
            const int32_t moved = before + ((static_cast<int32_t>(crossed[i]) - before) >> adaptShift);
            learned[i] = static_cast<uint16_t>(moved < 1 ? 1 : moved);
          }
          changed = changed || learned[i] != before;
        }
        return changed;
      }

      /// The share of the discharge left at this calibrated voltage, from 0 to 100.
      uint8_t Percent(uint16_t millivolts) const {
        if (millivolts >= BatteryCurve::fullMillivolts) {
          return 100;
        }
        if (millivolts <= BatteryCurve::emptyMillivolts) {
          return 0;
        }

        // What the reference curve gives each slice, and how many minutes one of its units is
        // worth on this watch, judged by the slices both know. Recomputed rather than kept in an
        // array, since this runs in the ADC interrupt, on a stack that has no room to spare.
        auto reference = [](int i) -> uint64_t {
          return static_cast<uint64_t>(BatteryCurve::HundredthsFromVoltage(SliceBottom(i + 1)) -
                                       BatteryCurve::HundredthsFromVoltage(SliceBottom(i)));
        };
        uint64_t learnedMinutes = 0;
        uint64_t referenceOfLearned = 0;
        for (int i = 0; i < sliceCount; i++) {
          if (learned[i] != 0) {
            learnedMinutes += learned[i];
            referenceOfLearned += reference(i);
          }
        }
        // Weights in sixteenths of a minute, so a borrowed slice of a fraction of a minute still
        // counts. With nothing learned the reference stands on its own.
        auto weight = [&](int i) -> uint64_t {
          if (learned[i] != 0) {
            return static_cast<uint64_t>(learned[i]) * 16;
          }
          if (referenceOfLearned == 0) {
            return reference(i);
          }
          return reference(i) * learnedMinutes * 16 / referenceOfLearned;
        };

        const int slice = SliceOf(millivolts);
        uint64_t below = 0;
        uint64_t total = 0;
        for (int i = 0; i < sliceCount; i++) {
          const uint64_t w = weight(i);
          total += w;
          if (i < slice) {
            below += w;
          } else if (i == slice) {
            below += w * (millivolts - SliceBottom(i)) / sliceMillivolts;
          }
        }
        if (total == 0) {
          return BatteryCurve::PercentFromVoltage(millivolts);
        }
        return static_cast<uint8_t>(below * 100 / total);
      }

    private:
      static uint16_t MedianOfThree(uint16_t a, uint16_t b, uint16_t c) {
        if (a > b) {
          const uint16_t t = a;
          a = b;
          b = t;
        }
        // Now a <= b; the median is b capped by c, floored by a.
        return c >= b ? b : (c <= a ? a : c);
      }

      static constexpr uint16_t SliceBottom(int slice) {
        return static_cast<uint16_t>(BatteryCurve::emptyMillivolts + slice * sliceMillivolts);
      }

      /// The slice a voltage sits in: -1 below empty, sliceCount at or above full.
      static int SliceOf(uint16_t millivolts) {
        if (millivolts < BatteryCurve::emptyMillivolts) {
          return -1;
        }
        const int slice = (millivolts - BatteryCurve::emptyMillivolts) / sliceMillivolts;
        return slice > sliceCount ? sliceCount : slice;
      }

      Minutes learned {};

      // What the discharge under way has measured so far, folded in on plug-in.
      Minutes crossed {};
      bool discharging = false;
      bool timing = false;
      uint32_t sinceUnplug = 0;
      uint32_t sinceLastCrossing = 0;
      int32_t smoothedx16 = 0;
      uint16_t recent = 0;
      std::array<uint16_t, 2> previous {};
      int lowestSlice = sliceCount;
    };
  }
}
