#pragma once

#include "displayapp/screens/Screen.h"
#include "components/brightness/BrightnessController.h"
#include "systemtask/SystemTask.h"
#include "systemtask/WakeLock.h"
#include <cstdint>
#include <lvgl/lvgl.h>

namespace Pinetime {

  namespace Applications {
    namespace Screens {

      /**
       * Opens on six choices, three strengths in white and the same three in red, and lights the
       * whole screen the moment one is tapped. One tap from the watch face to the light wanted,
       * rather than turning it on and then adjusting it: red at the lowest strength is for the
       * middle of the night, and has to be what comes on, not something reached through a flash of
       * white.
       */
      class FlashLight : public Screen {
      public:
        FlashLight(System::SystemTask& systemTask, Controllers::BrightnessController& brightness);
        ~FlashLight() override;

        bool OnTouchEvent(Pinetime::Applications::TouchEvents event) override;

        void OnChoice(lv_obj_t* choice);
        void TurnOff();

      private:
        static constexpr uint8_t nTints = 2;
        static constexpr uint8_t nLevels = 3;

        void TurnOn();

        Pinetime::System::WakeLock wakeLock;
        Controllers::BrightnessController& brightnessController;
        Controllers::BrightnessController::Levels previousBrightnessLevel;

        // What is lit, as indices into the grid, so a swipe while lit can step the strength.
        uint8_t tint = 0;
        uint8_t level = nLevels - 1;

        lv_obj_t* choices[nTints][nLevels];
        // Covers the screen while lit and catches the tap that puts the choices back.
        lv_obj_t* backgroundAction;
        bool isOn = false;
      };
    }
  }
}
