#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <lvgl/lvgl.h>

#include "components/settings/Settings.h"
#include "displayapp/screens/CheckboxList.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/screens/ScreenList.h"
#include "displayapp/widgets/Counter.h"
#include "displayapp/widgets/PageIndicator.h"

namespace Pinetime {

  namespace Applications {
    namespace Screens {

      /// The chimes, on two pages: how often on the first, and between which hours, and whether
      /// they ring through notifications turned off, on the second. What silences a chime regardless
      /// (a tracked night, sleep mode, a ringing alarm) is decided in Chimes.h.
      class SettingChimes : public Screen {
      public:
        SettingChimes(DisplayApp* app, Pinetime::Controllers::Settings& settingsController);
        ~SettingChimes() override;

        bool OnTouchEvent(TouchEvents event) override;

      private:
        auto CreateScreenList() const;
        std::unique_ptr<Screen> CreateScreen(unsigned int screenNum) const;

        Controllers::Settings& settingsController;

        static constexpr int nScreens = 2;

        ScreenList<nScreens> screens;
      };

      /// The second page of SettingChimes: the first and last hour that may chime, and the
      /// override for notifications turned off. Saved when the page is left.
      class SettingChimesHours : public Screen {
      public:
        SettingChimesHours(uint8_t screenID, uint8_t numScreens, Pinetime::Controllers::Settings& settingsController);
        ~SettingChimesHours() override;

        void OnHoursChanged();
        void OnIgnoreDndToggled();

      private:
        Controllers::Settings& settingsController;

        Widgets::Counter startCounter = Widgets::Counter(0, 23, jetbrains_mono_bold_20);
        Widgets::Counter endCounter = Widgets::Counter(0, 23, jetbrains_mono_bold_20);
        lv_obj_t* cbIgnoreDnd;

        Widgets::PageIndicator pageIndicator;
      };
    }
  }
}
