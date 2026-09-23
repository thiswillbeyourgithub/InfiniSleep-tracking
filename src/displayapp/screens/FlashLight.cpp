#include "displayapp/screens/FlashLight.h"
#include "displayapp/DisplayApp.h"

using namespace Pinetime::Applications::Screens;

namespace {
  using Levels = Pinetime::Controllers::BrightnessController::Levels;

  // Rows of the grid. Red rather than any warmer white because it is the colour that leaves night
  // vision, and whoever is asleep beside the wearer, alone.
  constexpr lv_color_t tintColours[] = {LV_COLOR_WHITE, LV_COLOR_RED};
  // Written on each choice in the colour that reads on it: black on white, white on red.
  constexpr lv_color_t tintTextColours[] = {LV_COLOR_BLACK, LV_COLOR_WHITE};

  // Columns of the grid, weakest first so the red one at the left is the night light.
  constexpr Levels levels[] = {Levels::Low, Levels::Medium, Levels::High};
  constexpr const char* levelLabels[] = {"Low", "Mid", "High"};

  // Two rows of three on the 240 pixel screen, with an even gap all round.
  constexpr lv_coord_t gap = 6;
  constexpr lv_coord_t choiceWidth = (240 - 4 * gap) / 3;
  constexpr lv_coord_t choiceHeight = (240 - 3 * gap) / 2;

  void ChoiceHandler(lv_obj_t* obj, lv_event_t event) {
    if (event == LV_EVENT_CLICKED) {
      static_cast<FlashLight*>(obj->user_data)->OnChoice(obj);
    }
  }

  void BackgroundHandler(lv_obj_t* obj, lv_event_t event) {
    if (event == LV_EVENT_CLICKED) {
      static_cast<FlashLight*>(obj->user_data)->TurnOff();
    }
  }
}

FlashLight::FlashLight(System::SystemTask& systemTask, Controllers::BrightnessController& brightnessController)
  : wakeLock(systemTask), brightnessController {brightnessController} {

  previousBrightnessLevel = brightnessController.Level();
  // The choices are read at the lowest strength, so opening the app at night is not itself a
  // flash of light.
  brightnessController.Set(Levels::Low);
  lv_obj_set_style_local_bg_color(lv_scr_act(), LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);

  for (uint8_t t = 0; t < nTints; t++) {
    for (uint8_t l = 0; l < nLevels; l++) {
      lv_obj_t* choice = lv_btn_create(lv_scr_act(), nullptr);
      lv_obj_set_size(choice, choiceWidth, choiceHeight);
      lv_obj_set_pos(choice, gap + l * (choiceWidth + gap), gap + t * (choiceHeight + gap));
      lv_obj_set_style_local_bg_color(choice, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, tintColours[t]);
      lv_obj_set_style_local_text_color(choice, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, tintTextColours[t]);
      // The strength is also said by how opaque the choice is, so the grid reads at a glance
      // without the words.
      lv_obj_set_style_local_bg_opa(choice, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, static_cast<lv_opa_t>(LV_OPA_40 + l * LV_OPA_30));
      choice->user_data = this;
      lv_obj_set_event_cb(choice, ChoiceHandler);

      lv_obj_t* label = lv_label_create(choice, nullptr);
      lv_label_set_text_static(label, levelLabels[l]);

      choices[t][l] = choice;
    }
  }

  backgroundAction = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_long_mode(backgroundAction, LV_LABEL_LONG_CROP);
  lv_obj_set_size(backgroundAction, 240, 240);
  lv_obj_set_pos(backgroundAction, 0, 0);
  lv_label_set_text_static(backgroundAction, "");
  lv_obj_set_click(backgroundAction, true);
  backgroundAction->user_data = this;
  lv_obj_set_event_cb(backgroundAction, BackgroundHandler);
  lv_obj_set_hidden(backgroundAction, true);

  wakeLock.Lock();
}

FlashLight::~FlashLight() {
  lv_obj_clean(lv_scr_act());
  lv_obj_set_style_local_bg_color(lv_scr_act(), LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  brightnessController.Set(previousBrightnessLevel);
}

void FlashLight::OnChoice(lv_obj_t* choice) {
  for (uint8_t t = 0; t < nTints; t++) {
    for (uint8_t l = 0; l < nLevels; l++) {
      if (choices[t][l] == choice) {
        tint = t;
        level = l;
        TurnOn();
        return;
      }
    }
  }
}

void FlashLight::TurnOn() {
  isOn = true;
  for (auto& row : choices) {
    for (auto* choice : row) {
      lv_obj_set_hidden(choice, true);
    }
  }
  lv_obj_set_hidden(backgroundAction, false);
  lv_obj_set_style_local_bg_color(lv_scr_act(), LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, tintColours[tint]);
  brightnessController.Set(levels[level]);
}

void FlashLight::TurnOff() {
  isOn = false;
  // Back to the grid rather than to the watch face, so a wrong pick is one tap from the right one.
  brightnessController.Set(Levels::Low);
  lv_obj_set_style_local_bg_color(lv_scr_act(), LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_hidden(backgroundAction, true);
  for (auto& row : choices) {
    for (auto* choice : row) {
      lv_obj_set_hidden(choice, false);
    }
  }
}

bool FlashLight::OnTouchEvent(Pinetime::Applications::TouchEvents event) {
  // Only while lit: a swipe on the grid is left to DisplayApp, which is how a swipe left takes the
  // wearer back to the watch face they swiped right from.
  if (!isOn) {
    return false;
  }
  if (event == TouchEvents::SwipeLeft) {
    if (level > 0) {
      level--;
      brightnessController.Set(levels[level]);
    }
    return true;
  }
  if (event == TouchEvents::SwipeRight) {
    if (level < nLevels - 1) {
      level++;
      brightnessController.Set(levels[level]);
    }
    return true;
  }
  return false;
}
