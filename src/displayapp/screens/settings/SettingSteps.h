#pragma once

#include <cstdint>
#include <lvgl/lvgl.h>
#include "components/motion/MotionController.h"
#include "components/settings/Settings.h"
#include "displayapp/screens/Screen.h"

namespace Pinetime {

  namespace Applications {
    namespace Screens {

      class SettingSteps : public Screen {
      public:
        SettingSteps(Pinetime::Controllers::Settings& settingsController, Pinetime::Controllers::MotionController& motionController);
        ~SettingSteps() override;

        void UpdateSelected(lv_obj_t* object, lv_event_t event);

      private:
        /// Puts the current state of step tracking on the toggle, including the case where the watch
        /// has no accelerometer and there is nothing to toggle.
        void UpdateTrackingLabel();

        Controllers::Settings& settingsController;
        Controllers::MotionController& motionController;

        lv_obj_t* stepValue;
        lv_obj_t* btnPlus;
        lv_obj_t* btnMinus;
        lv_obj_t* btnTracking;
        lv_obj_t* lblTracking;
      };
    }
  }
}
