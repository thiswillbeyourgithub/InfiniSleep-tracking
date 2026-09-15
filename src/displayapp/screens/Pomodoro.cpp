/*  Copyright (C) 2022 github user thiswillbeyourgithub

    This file is part of InfiniTime.

    InfiniTime is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published
    by the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    InfiniTime is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "displayapp/screens/Pomodoro.h"
#include "displayapp/screens/Symbols.h"
#include "displayapp/InfiniTimeTheme.h"
#include "systemtask/SystemTask.h"
#include <cstring>

using namespace Pinetime::Applications::Screens;
using Pinetime::Controllers::PomodoroController;

namespace {
  /* Two rows of digits then the three actions, mirroring the wasp-os keypad. "Then" appends the
   * ',' that chains one interval to the next, "Go" starts the queue. A single button matrix is
   * used rather than thirteen buttons because it is one object instead of twenty six. */
  constexpr const char* const buttonMap[] =
    {"1", "2", "3", "4", "5", "\n", "6", "7", "8", "9", "0", "\n", "Del", "Then", "Go", ""};

  constexpr uint8_t goButtonIndex = 12;

  void ButtonEventHandler(lv_obj_t* obj, lv_event_t event) {
    auto* screen = static_cast<Pomodoro*>(obj->user_data);
    screen->OnButtonEvent(obj, event);
  }
}

Pomodoro::Pomodoro(Controllers::PomodoroController& pomodoroController, System::SystemTask& systemTask)
  : pomodoroController {pomodoroController}, wakeLock(systemTask) {
  BuildCurrentView();
  taskRefresh = lv_task_create(RefreshTaskCallback, LV_DISP_DEF_REFR_PERIOD, LV_TASK_PRIO_MID, this);
}

Pomodoro::~Pomodoro() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
  /* The pomodoro itself is deliberately left running: leaving the app is not stopping it, the
   * controller keeps counting down and will pull this screen back up when the interval elapses. */
  pomodoroController.SaveSettings();
}

void Pomodoro::BuildCurrentView() {
  lv_obj_clean(lv_scr_act());
  buttonMatrix = nullptr;
  queueLabel = nullptr;
  vibrationsLabel = nullptr;
  positionLabel = nullptr;
  countdownLabel = nullptr;
  btnStop = nullptr;
  btnAddMinute = nullptr;

  displayedState = pomodoroController.GetState();
  switch (displayedState) {
    case PomodoroController::State::Stopped:
      BuildStoppedView();
      break;
    case PomodoroController::State::Running:
      BuildRunningView();
      break;
    case PomodoroController::State::Ringing:
      BuildRingingView();
      break;
  }

  // Only an alert needs the screen kept on; a countdown is fine to watch go dark.
  if (displayedState == PomodoroController::State::Ringing) {
    wakeLock.Lock();
  } else {
    wakeLock.Release();
  }
}

void Pomodoro::BuildStoppedView() {
  vibrationsLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_align(vibrationsLabel, lv_scr_act(), LV_ALIGN_IN_TOP_LEFT, 5, 8);

  queueLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_align(queueLabel, lv_scr_act(), LV_ALIGN_IN_TOP_RIGHT, -5, 8);

  UpdateQueueLabels();

  buttonMatrix = lv_btnmatrix_create(lv_scr_act(), nullptr);
  buttonMatrix->user_data = this;
  lv_obj_set_event_cb(buttonMatrix, ButtonEventHandler);
  lv_btnmatrix_set_map(buttonMatrix, const_cast<const char**>(buttonMap));
  lv_obj_set_size(buttonMatrix, LV_HOR_RES, 200);
  lv_obj_align(buttonMatrix, lv_scr_act(), LV_ALIGN_IN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_local_pad_all(buttonMatrix, LV_BTNMATRIX_PART_BG, LV_STATE_DEFAULT, 0);
  lv_obj_set_style_local_pad_inner(buttonMatrix, LV_BTNMATRIX_PART_BG, LV_STATE_DEFAULT, 1);
  lv_obj_set_style_local_bg_opa(buttonMatrix, LV_BTNMATRIX_PART_BG, LV_STATE_DEFAULT, LV_OPA_TRANSP);
  // "Go" is the only destructive-feeling action here, so it gets the same red as every other start.
  lv_btnmatrix_set_btn_ctrl(buttonMatrix, goButtonIndex, LV_BTNMATRIX_CTRL_CHECK_STATE);
  lv_obj_set_style_local_bg_color(buttonMatrix, LV_BTNMATRIX_PART_BTN, LV_STATE_CHECKED, Colors::highlight);
}

void Pomodoro::BuildRunningView() {
  positionLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_fmt(positionLabel,
                        "Timer #%u/%u  (%lu)",
                        static_cast<unsigned>(pomodoroController.GetCurrentInterval() + 1),
                        static_cast<unsigned>(pomodoroController.GetIntervalCount()),
                        static_cast<unsigned long>(pomodoroController.GetCompletedCycles()));
  lv_obj_align(positionLabel, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 0, 20);

  countdownLabel = lv_label_create(lv_scr_act(), nullptr);
  /* Rendered unconditionally here rather than through UpdateCountdown, whose dirty check would
   * leave the label empty until the displayed second happened to change. */
  RenderCountdown(pomodoroController.SecondsRemaining());

  btnStop = lv_btn_create(lv_scr_act(), nullptr);
  btnStop->user_data = this;
  lv_obj_set_event_cb(btnStop, ButtonEventHandler);
  lv_obj_set_size(btnStop, 158, 50);
  lv_obj_align(btnStop, lv_scr_act(), LV_ALIGN_IN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_local_bg_color(btnStop, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
  lv_label_set_text_static(lv_label_create(btnStop, nullptr), "STOP");

  btnAddMinute = lv_btn_create(lv_scr_act(), nullptr);
  btnAddMinute->user_data = this;
  lv_obj_set_event_cb(btnAddMinute, ButtonEventHandler);
  lv_obj_set_size(btnAddMinute, 80, 50);
  lv_obj_align(btnAddMinute, lv_scr_act(), LV_ALIGN_IN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_local_bg_color(btnAddMinute, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Colors::bgAlt);
  lv_label_set_text_static(lv_label_create(btnAddMinute, nullptr), "+1");
}

void Pomodoro::BuildRingingView() {
  /* Drawn at the default font size on purpose: the FontAwesome glyphs are only merged into
   * jetbrains_mono_bold_20, so there is no larger version of this symbol to scale up to. */
  lv_obj_t* symbol = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(symbol, Symbols::clock);
  lv_obj_align(symbol, lv_scr_act(), LV_ALIGN_CENTER, 0, -40);

  lv_obj_t* nameLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_fmt(nameLabel,
                        "Timer #%u/%u done",
                        static_cast<unsigned>(pomodoroController.GetCurrentInterval() + 1),
                        static_cast<unsigned>(pomodoroController.GetIntervalCount()));
  lv_obj_align(nameLabel, lv_scr_act(), LV_ALIGN_CENTER, 0, 30);

  /* The wasp-os app had no way out of an alert other than waiting it out. The button is wired to
   * skip it instead, which is what every other alerting app on InfiniTime does. */
  lv_obj_t* hintLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(hintLabel, "button to skip");
  lv_obj_set_style_local_text_color(hintLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
  lv_obj_align(hintLabel, lv_scr_act(), LV_ALIGN_IN_BOTTOM_MID, 0, -10);
}

void Pomodoro::UpdateQueueLabels() {
  lv_label_set_text_fmt(vibrationsLabel, "V%u", static_cast<unsigned>(pomodoroController.GetVibrationsPerAlarm()));
  lv_obj_align(vibrationsLabel, lv_scr_act(), LV_ALIGN_IN_TOP_LEFT, 5, 8);

  lv_label_set_text_fmt(queueLabel, "%s", pomodoroController.GetQueue());
  lv_obj_align(queueLabel, lv_scr_act(), LV_ALIGN_IN_TOP_RIGHT, -5, 8);
}

void Pomodoro::UpdateCountdown() {
  displaySeconds = pomodoroController.SecondsRemaining();
  if (displaySeconds.IsUpdated()) {
    RenderCountdown(displaySeconds.Get());
  }
}

void Pomodoro::RenderCountdown(uint32_t secondsRemaining) {
  uint32_t minutes = secondsRemaining / 60;
  uint32_t seconds = secondsRemaining % 60;

  /* Past 99 minutes the string no longer fits at 76pt. Queue entries are clamped to 9999 minutes
   * when parsed, so the 42pt fallback is always wide enough and no truncation is needed. */
  const lv_font_t* font = minutes > 99 ? &jetbrains_mono_42 : &jetbrains_mono_76;
  lv_obj_set_style_local_text_font(countdownLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, font);
  lv_label_set_text_fmt(countdownLabel,
                        "%02lu:%02lu",
                        static_cast<unsigned long>(minutes),
                        static_cast<unsigned long>(seconds));
  lv_obj_align(countdownLabel, lv_scr_act(), LV_ALIGN_CENTER, 0, -10);
}

void Pomodoro::Refresh() {
  if (displayedState != pomodoroController.GetState()) {
    BuildCurrentView();
    return;
  }
  if (displayedState == PomodoroController::State::Running) {
    UpdateCountdown();
  }
}

void Pomodoro::HandleKeypadInput() {
  const char* text = lv_btnmatrix_get_active_btn_text(buttonMatrix);
  if (text == nullptr) {
    return;
  }

  if (std::strcmp(text, "Del") == 0) {
    pomodoroController.DeleteLastCharacter();
  } else if (std::strcmp(text, "Then") == 0) {
    pomodoroController.AppendSeparator();
  } else if (std::strcmp(text, "Go") == 0) {
    /* A malformed queue such as "25," is simply refused: Start leaves everything alone and the
     * next refresh finds the state unchanged, so the keypad stays up for the user to fix it. */
    pomodoroController.Start();
    return;
  } else {
    pomodoroController.AppendDigit(text[0]);
  }
  UpdateQueueLabels();
}

void Pomodoro::OnButtonEvent(lv_obj_t* obj, lv_event_t event) {
  if (obj == buttonMatrix && event == LV_EVENT_PRESSED) {
    HandleKeypadInput();
    return;
  }
  if (event != LV_EVENT_CLICKED) {
    return;
  }
  if (obj == btnStop) {
    pomodoroController.Stop();
  } else if (obj == btnAddMinute) {
    pomodoroController.AddMinute();
    UpdateCountdown();
  }
}

bool Pomodoro::OnButtonPushed() {
  if (pomodoroController.GetState() == PomodoroController::State::Ringing) {
    pomodoroController.StopAlerting();
    return true;
  }
  // Anywhere else the button does its usual job of leaving the app, pomodoro still running or not.
  return false;
}

bool Pomodoro::OnTouchEvent(Pinetime::Applications::TouchEvents event) {
  /* Swipes only edit the queue, so they are only claimed on the keypad. While a pomodoro runs the
   * swipes are left alone, which is what lets the user swipe down and walk away from it. */
  if (pomodoroController.GetState() != PomodoroController::State::Stopped) {
    return false;
  }

  switch (event) {
    case TouchEvents::SwipeUp:
      pomodoroController.IncreaseVibrations();
      break;
    case TouchEvents::SwipeDown:
      /* Claiming the downward swipe costs the usual way out of the app, which is why the count can
       * be changed in both directions here and why the physical button is the way back. */
      pomodoroController.DecreaseVibrations();
      break;
    case TouchEvents::SwipeRight:
      pomodoroController.LoadNextPreset();
      break;
    case TouchEvents::SwipeLeft:
      pomodoroController.LoadPreviousPreset();
      break;
    default:
      return false;
  }

  UpdateQueueLabels();
  return true;
}
