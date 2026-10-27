#include "displayapp/screens/settings/SettingChimes.h"
#include <lvgl/lvgl.h>
#include <functional>
#include "displayapp/DisplayApp.h"
#include "displayapp/screens/Symbols.h"

using namespace Pinetime::Applications::Screens;

namespace {
  struct Option {
    /// Minutes between chimes, 0 for off.
    uint8_t interval;
    const char* name;
  };

  // Exactly the four a checkbox list holds, so the intervals fit on the first page alone. Every
  // interval divides the hour, which is what lets Chimes.h ask only whether the minute is a
  // multiple of it.
  constexpr std::array<Option, CheckboxList::MaxItems> options = {{
    {0, "Off"},
    {60, "Every hour"},
    {30, "Every 30 mins"},
    {15, "Every 15 mins"},
  }};

  uint32_t IndexOf(uint8_t interval) {
    for (size_t i = 0; i < options.size(); i++) {
      if (options[i].interval == interval) {
        return i;
      }
    }
    // A value no option matches can only come from a settings file written by another build.
    // Off is the answer that cannot wake anyone.
    return 0;
  }

  void HoursChangedHandler(void* userData) {
    static_cast<SettingChimesHours*>(userData)->OnHoursChanged();
  }

  void IgnoreDndHandler(lv_obj_t* obj, lv_event_t event) {
    if (event == LV_EVENT_VALUE_CHANGED) {
      static_cast<SettingChimesHours*>(obj->user_data)->OnIgnoreDndToggled();
    }
  }
}

auto SettingChimes::CreateScreenList() const {
  std::array<std::function<std::unique_ptr<Screen>()>, nScreens> screens;
  for (size_t i = 0; i < screens.size(); i++) {
    screens[i] = [this, i]() -> std::unique_ptr<Screen> {
      return CreateScreen(i);
    };
  }
  return screens;
}

SettingChimes::SettingChimes(Pinetime::Applications::DisplayApp* app, Pinetime::Controllers::Settings& settingsController)
  : settingsController {settingsController}, screens {app, 0, CreateScreenList(), Screens::ScreenListModes::UpDown} {
}

SettingChimes::~SettingChimes() {
  lv_obj_clean(lv_scr_act());
}

bool SettingChimes::OnTouchEvent(Pinetime::Applications::TouchEvents event) {
  return screens.OnTouchEvent(event);
}

std::unique_ptr<Screen> SettingChimes::CreateScreen(unsigned int screenNum) const {
  if (screenNum == 1) {
    return std::make_unique<SettingChimesHours>(screenNum, nScreens, settingsController);
  }

  std::array<CheckboxList::Item, CheckboxList::MaxItems> items;
  for (size_t i = 0; i < options.size(); i++) {
    items[i] = {options[i].name, true};
  }
  return std::make_unique<CheckboxList>(
    screenNum,
    nScreens,
    "Chimes",
    Symbols::clock,
    IndexOf(settingsController.GetChimesInterval()),
    [&settings = settingsController](uint32_t index) {
      settings.SetChimesInterval(options[index].interval);
      settings.SaveSettings();
    },
    items);
}

SettingChimesHours::SettingChimesHours(uint8_t screenID, uint8_t numScreens, Pinetime::Controllers::Settings& settingsController)
  : settingsController {settingsController}, pageIndicator(screenID, numScreens) {

  lv_obj_t* title = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(title, "Chime hours");
  lv_label_set_align(title, LV_LABEL_ALIGN_CENTER);
  lv_obj_align(title, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 15, 15);

  lv_obj_t* icon = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(icon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_ORANGE);
  lv_label_set_text_static(icon, Symbols::clock);
  lv_label_set_align(icon, LV_LABEL_ALIGN_CENTER);
  lv_obj_align(icon, title, LV_ALIGN_OUT_LEFT_MID, -10, 0);

  // Whole hours only: a chime falls on a quarter at the finest, so minutes on the bounds would
  // only ever move them to the next quarter, and two counters fit where four would not.
  startCounter.Create();
  endCounter.Create();
  if (settingsController.GetClockType() == Controllers::Settings::ClockType::H12) {
    startCounter.EnableTwelveHourMode();
    endCounter.EnableTwelveHourMode();
  }
  startCounter.SetValue(settingsController.GetChimesStartHour());
  endCounter.SetValue(settingsController.GetChimesEndHour());
  lv_obj_align(startCounter.GetObject(), nullptr, LV_ALIGN_CENTER, -55, -5);
  lv_obj_align(endCounter.GetObject(), nullptr, LV_ALIGN_CENTER, 55, -5);
  startCounter.SetValueChangedEventCallback(this, HoursChangedHandler);
  endCounter.SetValueChangedEventCallback(this, HoursChangedHandler);

  lv_obj_t* to = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(to, "to");
  lv_obj_align(to, nullptr, LV_ALIGN_CENTER, 0, -5);

  cbIgnoreDnd = lv_checkbox_create(lv_scr_act(), nullptr);
  lv_checkbox_set_text_static(cbIgnoreDnd, "Ignore DND");
  lv_checkbox_set_checked(cbIgnoreDnd, settingsController.GetChimesIgnoreDnd());
  cbIgnoreDnd->user_data = this;
  lv_obj_set_event_cb(cbIgnoreDnd, IgnoreDndHandler);
  lv_obj_align(cbIgnoreDnd, lv_scr_act(), LV_ALIGN_IN_BOTTOM_MID, 0, -5);

  pageIndicator.Create();
}

SettingChimesHours::~SettingChimesHours() {
  lv_obj_clean(lv_scr_act());
  // Saved once on the way out rather than on every tap of a counter, each of which would
  // otherwise be a flash write. Leaving the page, either way, always passes through here.
  settingsController.SaveSettings();
}

void SettingChimesHours::OnHoursChanged() {
  settingsController.SetChimesStartHour(static_cast<uint8_t>(startCounter.GetValue()));
  settingsController.SetChimesEndHour(static_cast<uint8_t>(endCounter.GetValue()));
}

void SettingChimesHours::OnIgnoreDndToggled() {
  settingsController.SetChimesIgnoreDnd(lv_checkbox_is_checked(cbIgnoreDnd));
}
