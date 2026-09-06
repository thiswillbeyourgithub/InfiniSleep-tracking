#pragma once

#include <cstddef>

namespace Pinetime {
  namespace Controllers {
    class HeartRateController;
    class MotionController;
    class Settings;
  }

  namespace Applications {
    namespace Screens {

      /// Whether a step count is a figure the watch can stand behind.
      ///
      /// Two reasons it is not: the wearer turned step tracking off, and no accelerometer answered at
      /// boot, which is not a theoretical case (see SystemTask::MotionWantedBySleepTracker, which
      /// checks the same thing before waking the sensor all night). Either way the counter reads a
      /// permanent zero, and a screen showing zero steps is claiming the wearer has not moved.
      bool StepsShown(const Controllers::Settings& settingsController, const Controllers::MotionController& motionController);

      /// Bytes a HeartRateText() buffer needs: three digits and a terminator.
      constexpr size_t heartRateTextSize = 4;

      /// The text for a heart rate reading, written into buffer, which is returned for convenience.
      ///
      /// "?" whenever there is no reading rather than the zero the sensor reports: the value is zero
      /// while the photoplethysmograph has not converged and while it sees no skin, and the
      /// controller holds its last value indefinitely, so a bare zero on a screen means "measuring"
      /// or "that measurement failed" and never a heart rate.
      ///
      /// Says nothing about whether anything is measuring at all, which is the caller's business:
      /// a watch face draws no heart rate row when the sensor is stopped.
      const char* HeartRateText(const Controllers::HeartRateController& heartRateController, char* buffer, size_t size);
    }
  }
}
