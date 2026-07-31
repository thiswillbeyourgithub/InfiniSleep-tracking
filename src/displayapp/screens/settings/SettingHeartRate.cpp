#include "displayapp/screens/settings/SettingHeartRate.h"
#include <lvgl/lvgl.h>
#include <functional>
#include "displayapp/DisplayApp.h"
#include "displayapp/screens/Screen.h"
#include "components/settings/Settings.h"

using namespace Pinetime::Applications::Screens;

constexpr const char* SettingHeartRate::title;
constexpr const char* SettingHeartRate::symbol;

namespace {
  struct Option {
    /// Minutes between measurements, 0 for off.
    uint8_t interval;
    const char* name;
  };

  constexpr std::array<Option, 5> options = {{
    {0, "Off"},
    {5, "5 min"},
    {15, "15 min"},
    {30, "30 min"},
    {60, "60 min"},
  }};

  uint32_t IndexOf(uint8_t interval) {
    for (size_t i = 0; i < options.size(); i++) {
      if (options[i].interval == interval) {
        return i;
      }
    }
    // A setting written by another build, or a file that survived a version it should not have.
    // Off is the answer that cannot surprise anyone.
    return 0;
  }

  uint8_t IntervalAt(size_t index) {
    if (index >= options.size()) {
      return options[0].interval;
    }
    return options[index].interval;
  }
}

auto SettingHeartRate::CreateScreenList() const {
  std::array<std::function<std::unique_ptr<Screen>()>, nScreens> screens;
  for (size_t i = 0; i < screens.size(); i++) {
    screens[i] = [this, i]() -> std::unique_ptr<Screen> {
      return CreateScreen(i);
    };
  }
  return screens;
}

SettingHeartRate::SettingHeartRate(Pinetime::Applications::DisplayApp* app, Pinetime::Controllers::Settings& settingsController)
  : settingsController {settingsController}, screens {app, 0, CreateScreenList(), Screens::ScreenListModes::UpDown} {
}

SettingHeartRate::~SettingHeartRate() {
  lv_obj_clean(lv_scr_act());
}

bool SettingHeartRate::OnTouchEvent(Pinetime::Applications::TouchEvents event) {
  return screens.OnTouchEvent(event);
}

std::unique_ptr<Screen> SettingHeartRate::CreateScreen(unsigned int screenNum) const {
  std::array<Screens::CheckboxList::Item, settingsPerScreen> optionsOnThisScreen;
  for (int i = 0; i < settingsPerScreen; i++) {
    const size_t index = i + screenNum * settingsPerScreen;
    if (index >= options.size()) {
      optionsOnThisScreen[i] = {"", false};
    } else {
      optionsOnThisScreen[i] = Screens::CheckboxList::Item {options[index].name, true};
    }
  }

  return std::make_unique<Screens::CheckboxList>(
    screenNum,
    nScreens,
    title,
    symbol,
    IndexOf(settingsController.GetHeartRatePollInterval()),
    [&settings = settingsController](uint32_t index) {
      settings.SetHeartRatePollInterval(IntervalAt(index));
      settings.SaveSettings();
    },
    optionsOnThisScreen);
}
