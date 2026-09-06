#include "displayapp/screens/SensorDisplay.h"

#include <cstdio>

#include "components/heartrate/HeartRateController.h"
#include "components/motion/MotionController.h"
#include "components/settings/Settings.h"

namespace Pinetime {
  namespace Applications {
    namespace Screens {

      bool StepsShown(const Controllers::Settings& settingsController, const Controllers::MotionController& motionController) {
        return settingsController.GetStepsEnabled() &&
               motionController.DeviceType() != Controllers::MotionController::DeviceTypes::Unknown;
      }

      const char* HeartRateText(const Controllers::HeartRateController& heartRateController, char* buffer, size_t size) {
        const uint8_t heartRate = heartRateController.HeartRate();
        if (heartRateController.State() != Controllers::HeartRateController::States::Running || heartRate == 0) {
          snprintf(buffer, size, "?");
        } else {
          snprintf(buffer, size, "%d", heartRate);
        }
        return buffer;
      }
    }
  }
}
