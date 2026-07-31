#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "components/settings/Settings.h"
#include "displayapp/screens/CheckboxList.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/screens/ScreenList.h"
#include "displayapp/screens/Symbols.h"

namespace Pinetime {

  namespace Applications {
    namespace Screens {

      /// How often the watch measures heart rate on its own while the screen is off.
      ///
      /// Two pages rather than one because a checkbox list holds four options and there are five
      /// to offer, off included.
      class SettingHeartRate : public Screen {
      public:
        SettingHeartRate(DisplayApp* app, Pinetime::Controllers::Settings& settingsController);
        ~SettingHeartRate() override;

        bool OnTouchEvent(TouchEvents event) override;

      private:
        auto CreateScreenList() const;
        std::unique_ptr<Screen> CreateScreen(unsigned int screenNum) const;

        Controllers::Settings& settingsController;

        static constexpr const char* title = "Heart Rate";
        static constexpr const char* symbol = Symbols::heartBeat;

        static constexpr int settingsPerScreen = CheckboxList::MaxItems;
        static constexpr int optionCount = 5;
        static constexpr int nScreens = (optionCount - 1) / settingsPerScreen + 1;

        ScreenList<nScreens> screens;
      };
    }
  }
}
