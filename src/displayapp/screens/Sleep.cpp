#include "displayapp/screens/Sleep.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/screens/Symbols.h"
#include "displayapp/InfiniTimeTheme.h"
#include "components/settings/Settings.h"
#include "components/infinisleep/InfiniSleepController.h"
#include "components/motor/MotorController.h"
#include "systemtask/SystemTask.h"

#include <libraries/log/nrf_log.h>
#include <lvgl/lvgl.h>
#include <cstddef>

using namespace Pinetime::Applications::Screens;

namespace {
  void ValueChangedHandler(void* userData) {
    auto* screen = static_cast<Sleep*>(userData);
    screen->OnValueChanged();
  }

  void btnEventHandler(lv_obj_t* obj, lv_event_t event) {
    auto* screen = static_cast<Sleep*>(obj->user_data);
    screen->OnButtonEvent(obj, event);
  }

  void SnoozeAlarmTaskCallback(lv_task_t* task) {
    lv_task_set_prio(task, LV_TASK_PRIO_OFF);
    auto* screen = static_cast<Sleep*>(task->user_data);
    screen->ignoreButtonPush = true;
    screen->OnButtonEvent(screen->btnSnooze, LV_EVENT_CLICKED);
    screen->ignoreButtonPush = false;
  }

  const char* WakeModeName(const Pinetime::Controllers::InfiniSleepController::InfiniSleepSettings& settings) {
    if (settings.graddualWake && settings.naturalWake) {
      return "Both";
    }
    if (settings.graddualWake) {
      return "Pre.";
    }
    if (settings.naturalWake) {
      return "Nat.";
    }
    return "Norm.";
  }

  // The values the sensor page cycles through, rather than stepping one by one through a
  // range nobody wants to tap sixty times.
  constexpr uint8_t trackerIntervalChoices[] = {1, 2, 5, 10, 15, 20, 30, 45, 60};
  constexpr uint8_t motionIntervalChoices[] = {1, 2, 5, 10, 20, 50};

  // Next entry after current, wrapping. A value that is in no list, which is what a settings
  // file written by another version can hold, snaps to the first entry.
  template <size_t N>
  uint8_t NextChoice(uint8_t current, const uint8_t (&choices)[N]) {
    for (size_t i = 0; i < N; i++) {
      if (choices[i] == current) {
        return choices[(i + 1) % N];
      }
    }
    return choices[0];
  }

  void PressesToStopAlarmTimeoutCallback(lv_task_t* task) {
    auto* screen = static_cast<Sleep*>(task->user_data);
    screen->infiniSleepController.pushesLeftToStopWakeAlarm = screen->infiniSleepController.infiniSleepSettings.pushesToStopAlarm;
    screen->UpdateDisplay();
  }
}

Sleep::Sleep(Controllers::InfiniSleepController& infiniSleepController,
             Controllers::Settings::ClockType clockType,
             System::SystemTask& systemTask,
             Controllers::MotorController& motorController,
             DisplayApp& displayApp)
  : infiniSleepController {infiniSleepController},
    wakeLock(systemTask),
    motorController {motorController},
    clockType {clockType},
    displayApp {displayApp} {

  // heartRateTracking used to be forced off here, from when nothing measured the heart rate
  // overnight and the setting only promised something the firmware did not do. It is a real
  // setting now, so it is left to whatever the user chose.
  infiniSleepController.infiniSleepSettings.sleepCycleDuration = 90;
  infiniSleepController.SetSettingsChanged();

  // Created before the first draw, UpdateDisplay resets the refresh task
  taskRefresh = lv_task_create(RefreshTaskCallback, 2000, LV_TASK_PRIO_MID, this);
  taskPressesToStopAlarmTimeout =
    lv_task_create(PressesToStopAlarmTimeoutCallback, PUSHES_TO_STOP_ALARM_TIMEOUT * 1000, LV_TASK_PRIO_MID, this);

  // The tracking page is the only page reachable while the tracker runs, so that is where
  // the app has to open when it is reopened during the night
  if (infiniSleepController.IsTrackerEnabled()) {
    displayState = SleepDisplayState::Info;
  }

  UpdateDisplay();

  if (!infiniSleepController.IsEnabled()) {
    infiniSleepController.prevBrightnessLevel = infiniSleepController.GetBrightnessController().Level();
  }
  infiniSleepController.GetBrightnessController().Set(Controllers::BrightnessController::Levels::Low);
}

Sleep::~Sleep() {
  if (infiniSleepController.IsAlerting()) {
    StopAlerting();
  }
  lv_task_del(taskRefresh);
  lv_task_del(taskPressesToStopAlarmTimeout);
  if (taskSnoozeWakeAlarm != nullptr) {
    lv_task_del(taskSnoozeWakeAlarm);
  }
  lv_obj_clean(lv_scr_act());
  infiniSleepController.SaveWakeAlarm();
  infiniSleepController.SaveInfiniSleepSettings();
  if (!infiniSleepController.IsEnabled()) {
    infiniSleepController.GetBrightnessController().Set(infiniSleepController.prevBrightnessLevel);
  }
}

void Sleep::DisableWakeAlarm() {
  if (infiniSleepController.GetWakeAlarm().isEnabled) {
    infiniSleepController.DisableWakeAlarm();
    lv_switch_off(enableSwitch, LV_ANIM_ON);
  }
}

void Sleep::Refresh() {
  UpdateDisplay();
}

void Sleep::UpdateDisplay() {
  // The alarm page holds the time counters, redrawing it on every refresh would fight with the user
  if (screenDrawn && infiniSleepController.IsAlerting() != true && displayState == SleepDisplayState::Alarm &&
      drawnState == SleepDisplayState::Alarm) {
    return;
  }

  lv_task_reset(taskRefresh);

  // These point into the page about to be freed, and OnButtonEvent identifies which setting was
  // tapped by comparing against them. Left dangling, one could compare equal to a button freshly
  // allocated at the same address on the next page, which is enough to change the wrong setting.
  btnWakeMode = btnCycles = btnTestMotorGradual = btnMotorStrength = btnPushesToStop = nullptr;
  btnHeartRateTracking = btnBodyTracking = btnTrackerInterval = btnMotionInterval = nullptr;

  // Clear the screen
  lv_obj_clean(lv_scr_act());
  if (infiniSleepController.IsAlerting()) {
    displayState = SleepDisplayState::Alarm;
  }
  // Draw the screen
  switch (displayState) {
    case SleepDisplayState::Alarm:
      DrawAlarmScreen();
      pageIndicatorAlarm.Create();
      break;
    case SleepDisplayState::Info:
      DrawInfoScreen();
      pageIndicatorInfo.Create();
      break;
    case SleepDisplayState::Settings:
      DrawSettingsScreen();
      pageIndicatorSettings.Create();
      break;
    case SleepDisplayState::Sensors:
      DrawSensorsScreen();
      pageIndicatorSensors.Create();
      break;
  }
  drawnState = displayState;
  screenDrawn = true;

  if (alreadyAlerting) {
    RedrawSetAlerting();
    return;
  }

  if (infiniSleepController.IsAlerting()) {
    SetAlerting();
  } else if (displayState == SleepDisplayState::Alarm) {
    SetSwitchState(LV_ANIM_OFF);
  }
}

void Sleep::DrawAlarmScreen() {
  hourCounter.Create();
  lv_obj_align(hourCounter.GetObject(), nullptr, LV_ALIGN_IN_TOP_LEFT, 0, 0);
  if (clockType == Controllers::Settings::ClockType::H12) {
    hourCounter.EnableTwelveHourMode();

    lblampm = lv_label_create(lv_scr_act(), nullptr);
    lv_obj_set_style_local_text_font(lblampm, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
    lv_label_set_text_static(lblampm, "AM");
    lv_label_set_align(lblampm, LV_LABEL_ALIGN_CENTER);
    lv_obj_align(lblampm, lv_scr_act(), LV_ALIGN_CENTER, 0, 30);
  }
  hourCounter.SetValue(infiniSleepController.GetWakeAlarm().hours);
  hourCounter.SetValueChangedEventCallback(this, ValueChangedHandler);

  minuteCounter.Create();
  lv_obj_align(minuteCounter.GetObject(), nullptr, LV_ALIGN_IN_TOP_RIGHT, 0, 0);
  minuteCounter.SetValue(infiniSleepController.GetWakeAlarm().minutes);
  minuteCounter.SetValueChangedEventCallback(this, ValueChangedHandler);

  lv_obj_t* colonLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(colonLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_76);
  lv_label_set_text_static(colonLabel, ":");
  lv_obj_align(colonLabel, lv_scr_act(), LV_ALIGN_CENTER, 0, -29);

  if (infiniSleepController.IsAlerting()) {
    lv_obj_set_hidden(hourCounter.GetObject(), true);
    lv_obj_set_hidden(minuteCounter.GetObject(), true);
    lv_obj_set_hidden(colonLabel, true);

    lv_obj_t* lblTime = lv_label_create(lv_scr_act(), nullptr);
    if (clockType == Controllers::Settings::ClockType::H24) {
      lv_label_set_text_fmt(lblTime, "%02d:%02d", infiniSleepController.GetCurrentHour(), infiniSleepController.GetCurrentMinute());
    } else {
      lv_label_set_text_fmt(lblTime,
                            "%02d:%02d",
                            (infiniSleepController.GetCurrentHour() % 12 == 0) ? 12 : infiniSleepController.GetCurrentHour() % 12,
                            infiniSleepController.GetCurrentMinute());
    }
    lv_obj_align(lblTime, lv_scr_act(), LV_ALIGN_CENTER, -87, -100);
    lv_obj_set_style_local_text_color(lblTime, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_label_set_align(lblTime, LV_LABEL_ALIGN_CENTER);
    lv_obj_set_style_local_text_font(lblTime, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_76);

    lv_obj_t* lblWaketxt = lv_label_create(lv_scr_act(), nullptr);
    lv_label_set_text_static(lblWaketxt, "Wake Up!");
    lv_obj_align(lblWaketxt, lv_scr_act(), LV_ALIGN_CENTER, 0, -22);
    lv_label_set_align(lblWaketxt, LV_LABEL_ALIGN_CENTER);
    lv_obj_set_style_local_text_font(lblWaketxt, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
    lv_obj_set_style_local_text_color(lblWaketxt, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  }

  btnSnooze = lv_btn_create(lv_scr_act(), nullptr);
  btnSnooze->user_data = this;
  lv_obj_set_event_cb(btnSnooze, btnEventHandler);
  lv_obj_set_size(btnSnooze, 200, 63);
  lv_obj_align(btnSnooze, lv_scr_act(), LV_ALIGN_CENTER, 0, 28);
  lv_obj_set_style_local_bg_color(btnSnooze, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_ORANGE);
  txtSnooze = lv_label_create(btnSnooze, nullptr);
  lv_label_set_text_static(txtSnooze, "Snooze");
  lv_obj_set_hidden(btnSnooze, true);

  btnStop = lv_btn_create(lv_scr_act(), nullptr);
  btnStop->user_data = this;
  lv_obj_set_event_cb(btnStop, btnEventHandler);
  lv_obj_set_size(btnStop, 130, 50);
  lv_obj_align(btnStop, lv_scr_act(), LV_ALIGN_IN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_local_bg_color(btnStop, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
  txtStop = lv_label_create(btnStop, nullptr);
  lv_label_set_text_fmt(txtStop,
                        "Stop: %d/%d",
                        infiniSleepController.infiniSleepSettings.pushesToStopAlarm - infiniSleepController.pushesLeftToStopWakeAlarm,
                        infiniSleepController.infiniSleepSettings.pushesToStopAlarm);
  lv_obj_set_hidden(btnStop, true);

  static constexpr lv_color_t bgColor = Colors::bgAlt;

  btnSuggestedAlarm = lv_btn_create(lv_scr_act(), nullptr);
  btnSuggestedAlarm->user_data = this;
  lv_obj_set_event_cb(btnSuggestedAlarm, btnEventHandler);
  lv_obj_set_size(btnSuggestedAlarm, 115, 50);
  lv_obj_align(btnSuggestedAlarm, lv_scr_act(), LV_ALIGN_IN_BOTTOM_RIGHT, 0, 0);
  // txtSuggestedAlarm = lv_label_create(btnSuggestedAlarm, nullptr);
  // lv_label_set_text_static(txtSuggestedAlarm, "Use Sugg.\nAlarmTime");

  txtSuggestedAlarm = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_align(txtSuggestedAlarm, lv_scr_act(), LV_ALIGN_IN_BOTTOM_RIGHT, -15, -13);
  lv_label_set_text_static(txtSuggestedAlarm, "Auto");

  iconSuggestedAlarm = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(iconSuggestedAlarm, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_obj_align(iconSuggestedAlarm, lv_scr_act(), LV_ALIGN_IN_BOTTOM_RIGHT, -50, -13);
  lv_label_set_text_static(iconSuggestedAlarm, Symbols::sun);

  enableSwitch = lv_switch_create(lv_scr_act(), nullptr);
  enableSwitch->user_data = this;
  lv_obj_set_event_cb(enableSwitch, btnEventHandler);
  lv_obj_set_size(enableSwitch, 100, 50);
  // Align to the center of 115px from edge
  lv_obj_align(enableSwitch, lv_scr_act(), LV_ALIGN_IN_BOTTOM_LEFT, 7, 0);
  lv_obj_set_style_local_bg_color(enableSwitch, LV_SWITCH_PART_BG, LV_STATE_DEFAULT, bgColor);

  UpdateWakeAlarmTime();
}

void Sleep::DrawInfoScreen() {
  lv_obj_t* lblTime = lv_label_create(lv_scr_act(), nullptr);
  if (clockType == Controllers::Settings::ClockType::H24) {
    lv_label_set_text_fmt(lblTime, "%02d:%02d", infiniSleepController.GetCurrentHour(), infiniSleepController.GetCurrentMinute());
  } else {
    lv_label_set_text_fmt(lblTime,
                          "%02d:%02d",
                          (infiniSleepController.GetCurrentHour() % 12 == 0) ? 12 : infiniSleepController.GetCurrentHour() % 12,
                          infiniSleepController.GetCurrentMinute());
  }
  lv_obj_align(lblTime, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 0, 5);
  lv_obj_set_style_local_text_color(lblTime, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);

  // Total sleep time
  label_total_sleep = lv_label_create(lv_scr_act(), nullptr);

  const uint16_t totalMinutes = infiniSleepController.GetTotalSleep();

  lv_label_set_text_fmt(label_total_sleep, "Time Asleep: %dh%dm", totalMinutes / 60, totalMinutes % 60);
  lv_obj_align(label_total_sleep, lv_scr_act(), LV_ALIGN_CENTER, 0, -60);
  lv_obj_set_style_local_text_color(label_total_sleep,
                                    LV_LABEL_PART_MAIN,
                                    LV_STATE_DEFAULT,
                                    infiniSleepController.IsEnabled() ? LV_COLOR_RED : LV_COLOR_WHITE);

  // Sleep Cycles Info
  label_sleep_cycles = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_fmt(label_sleep_cycles,
                        "Sleep Cycles: %d.%02d",
                        infiniSleepController.GetSleepCycles() / 100,
                        infiniSleepController.GetSleepCycles() % 100);
  lv_obj_align(label_sleep_cycles, lv_scr_act(), LV_ALIGN_CENTER, 0, -40);
  lv_obj_set_style_local_text_color(label_sleep_cycles,
                                    LV_LABEL_PART_MAIN,
                                    LV_STATE_DEFAULT,
                                    infiniSleepController.IsEnabled() ? LV_COLOR_RED : LV_COLOR_WHITE);

  // Start time
  if (infiniSleepController.IsEnabled()) {
    label_start_time = lv_label_create(lv_scr_act(), nullptr);
    if (clockType == Controllers::Settings::ClockType::H24) {
      lv_label_set_text_fmt(label_start_time,
                            "Began at: %02d:%02d",
                            infiniSleepController.prevSessionData.startTimeHours,
                            infiniSleepController.prevSessionData.startTimeMinutes);
    } else {
      lv_label_set_text_fmt(
        label_start_time,
        "Began at: %02d:%02d",
        (infiniSleepController.prevSessionData.startTimeHours % 12 == 0) ? 12 : infiniSleepController.prevSessionData.startTimeHours % 12,
        infiniSleepController.prevSessionData.startTimeMinutes);
    }
    lv_obj_align(label_start_time, lv_scr_act(), LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_local_text_color(label_start_time, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
  }

  // The alarm info
  label_alarm_time = lv_label_create(lv_scr_act(), nullptr);
  if (infiniSleepController.GetWakeAlarm().isEnabled) {
    if (clockType == Controllers::Settings::ClockType::H24) {
      lv_label_set_text_fmt(label_alarm_time,
                            "Alarm at: %02d:%02d",
                            infiniSleepController.GetWakeAlarm().hours,
                            infiniSleepController.GetWakeAlarm().minutes);
    } else {
      lv_label_set_text_fmt(label_alarm_time,
                            "Alarm at: %02d:%02d",
                            (infiniSleepController.GetWakeAlarm().hours % 12 == 0) ? 12 : infiniSleepController.GetWakeAlarm().hours % 12,
                            infiniSleepController.GetWakeAlarm().minutes);
    }
  } else {
    lv_label_set_text_static(label_alarm_time, "Alarm is not set.");
  }
  lv_obj_align(label_alarm_time, lv_scr_act(), LV_ALIGN_CENTER, 0, 20);
  lv_obj_set_style_local_text_color(label_alarm_time,
                                    LV_LABEL_PART_MAIN,
                                    LV_STATE_DEFAULT,
                                    infiniSleepController.IsEnabled() ? LV_COLOR_RED : LV_COLOR_WHITE);

  // Wake Mode info
  if (infiniSleepController.GetWakeAlarm().isEnabled) {
    label_gradual_wake = lv_label_create(lv_scr_act(), nullptr);
    if (infiniSleepController.infiniSleepSettings.graddualWake && infiniSleepController.infiniSleepSettings.naturalWake) {
      lv_label_set_text_static(label_gradual_wake, "Wake Mode: Both");
    } else if (infiniSleepController.infiniSleepSettings.graddualWake) {
      lv_label_set_text_static(label_gradual_wake, "Wake Mode: PreWake");
    } else if (infiniSleepController.infiniSleepSettings.naturalWake) {
      lv_label_set_text_static(label_gradual_wake, "Wake Mode: Natural");
    } else {
      lv_label_set_text_static(label_gradual_wake, "Wake Mode: Normal");
    }
    lv_obj_align(label_gradual_wake, lv_scr_act(), LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_local_text_color(label_gradual_wake,
                                      LV_LABEL_PART_MAIN,
                                      LV_STATE_DEFAULT,
                                      infiniSleepController.IsEnabled() ? LV_COLOR_RED : LV_COLOR_WHITE);
  }

  // Start/Stop button
  trackerToggleBtn = lv_btn_create(lv_scr_act(), nullptr);
  trackerToggleBtn->user_data = this;
  // Same size as the stop button of the ringing alarm, which holds the same "Stop: x/y" text
  lv_obj_set_size(trackerToggleBtn, 130, 50);
  lv_obj_align(trackerToggleBtn, nullptr, LV_ALIGN_IN_BOTTOM_MID, 0, 0);

  // Tracker toggle button
  trackerToggleLabel = lv_label_create(trackerToggleBtn, nullptr);
  if (infiniSleepController.IsTrackerEnabled()) {
    // Stopping the tracker takes as many pushes as stopping the ringing alarm does
    lv_label_set_text_fmt(trackerToggleLabel,
                          "Stop: %d/%d",
                          infiniSleepController.infiniSleepSettings.pushesToStopAlarm - infiniSleepController.pushesLeftToStopWakeAlarm,
                          infiniSleepController.infiniSleepSettings.pushesToStopAlarm);
    lv_obj_set_style_local_bg_color(trackerToggleBtn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
  } else {
    lv_label_set_text_static(trackerToggleLabel, "Start");
    lv_obj_set_style_local_bg_color(trackerToggleBtn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_GREEN);
  }
  lv_obj_set_event_cb(trackerToggleBtn, btnEventHandler);
}

lv_obj_t* Sleep::CreateSettingRow(const char* name, int16_t yOffset) {
  lv_obj_t* label = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(label, name);
  lv_obj_align(label, lv_scr_act(), LV_ALIGN_IN_TOP_LEFT, 10, yOffset);

  lv_obj_t* button = lv_btn_create(lv_scr_act(), nullptr);
  lv_obj_set_size(button, 100, 50);
  lv_obj_align(button, lv_scr_act(), LV_ALIGN_IN_TOP_LEFT, 130, yOffset);
  button->user_data = this;
  lv_obj_set_event_cb(button, btnEventHandler);

  lv_obj_t* value = lv_label_create(button, nullptr);
  lv_obj_align(value, nullptr, LV_ALIGN_CENTER, 0, 0);
  return button;
}

void Sleep::DrawSettingsScreen() {
  // lv_obj_t* lblSettings = lv_label_create(lv_scr_act(), nullptr);
  // lv_label_set_text_static(lblSettings, "Settings");
  // lv_obj_align(lblSettings, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 0, 10);

  if (infiniSleepController.wakeAlarm.isEnabled) {
    lv_obj_t* lblWarning = lv_label_create(lv_scr_act(), nullptr);
    lv_label_set_text_static(lblWarning, "Disable alarm to\nchange settings.");
    lv_obj_align(lblWarning, lv_scr_act(), LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_local_text_color(lblWarning, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
    return;
  }

  int16_t y_offset = 10;

  btnWakeMode = CreateSettingRow("Wake\nMode", y_offset);
  lv_label_set_text_static(lv_obj_get_child(btnWakeMode, nullptr), WakeModeName(infiniSleepController.infiniSleepSettings));

  y_offset += 60; // Adjust the offset for the next UI element

  btnCycles = CreateSettingRow("Desired\nCycles", y_offset);
  lv_label_set_text_fmt(lv_obj_get_child(btnCycles, nullptr), "%d", infiniSleepController.infiniSleepSettings.desiredCycles);

  infiniSleepController.infiniSleepSettings.sleepCycleDuration = 90;
  infiniSleepController.SetSettingsChanged();

  y_offset += 60; // Adjust the offset for the next UI element

  btnTestMotorGradual = lv_btn_create(lv_scr_act(), nullptr);
  lv_obj_set_size(btnTestMotorGradual, 110, 50);
  lv_obj_align(btnTestMotorGradual, lv_scr_act(), LV_ALIGN_IN_TOP_LEFT, 10, y_offset);
  btnTestMotorGradual->user_data = this;
  lv_obj_set_event_cb(btnTestMotorGradual, btnEventHandler);

  lblMotorStrength = lv_label_create(btnTestMotorGradual, nullptr);
  lv_label_set_text_static(lblMotorStrength, "Motor\nPower");
  lv_obj_align(lblMotorStrength, lv_scr_act(), LV_ALIGN_IN_TOP_LEFT, 0, 0);

  // The label of this row lives inside its own button, which buzzes the motor to preview the
  // strength, so it does not go through CreateSettingRow
  btnMotorStrength = lv_btn_create(lv_scr_act(), nullptr);
  lv_obj_set_size(btnMotorStrength, 100, 50);
  lv_obj_align(btnMotorStrength, lv_scr_act(), LV_ALIGN_IN_TOP_LEFT, 130, y_offset);
  btnMotorStrength->user_data = this;
  lv_obj_set_event_cb(btnMotorStrength, btnEventHandler);

  lv_obj_t* lblMotorStrengthValue = lv_label_create(btnMotorStrength, nullptr);
  lv_label_set_text_fmt(lblMotorStrengthValue, "%d", infiniSleepController.infiniSleepSettings.motorStrength);
  motorController.infiniSleepMotorStrength = infiniSleepController.infiniSleepSettings.motorStrength;
  lv_obj_align(lblMotorStrengthValue, nullptr, LV_ALIGN_CENTER, 0, 0);

  y_offset += 60; // Adjust the offset for the next UI element

  btnPushesToStop = CreateSettingRow("Pushes\nto Stop", y_offset);
  lv_label_set_text_fmt(lv_obj_get_child(btnPushesToStop, nullptr), "%d", infiniSleepController.infiniSleepSettings.pushesToStopAlarm);
}

void Sleep::DrawSensorsScreen() {
  int16_t y_offset = 10;

  btnHeartRateTracking = CreateSettingRow("Heart\nRate", y_offset);
  lv_label_set_text_static(lv_obj_get_child(btnHeartRateTracking, nullptr),
                           infiniSleepController.infiniSleepSettings.heartRateTracking ? "On" : "Off");

  y_offset += 60;

  btnBodyTracking = CreateSettingRow("Body\nMotion", y_offset);
  lv_label_set_text_static(lv_obj_get_child(btnBodyTracking, nullptr),
                           infiniSleepController.infiniSleepSettings.bodyTracking ? "On" : "Off");

  y_offset += 60;

  btnTrackerInterval = CreateSettingRow("Log\nEvery", y_offset);
  lv_label_set_text_fmt(lv_obj_get_child(btnTrackerInterval, nullptr), "%dm", infiniSleepController.GetTrackerIntervalMinutes());

  y_offset += 60;

  btnMotionInterval = CreateSettingRow("Motion\nEvery", y_offset);
  const uint8_t motionDs = infiniSleepController.GetMotionSampleIntervalDs();
  lv_label_set_text_fmt(lv_obj_get_child(btnMotionInterval, nullptr), "%d.%ds", motionDs / 10, motionDs % 10);
}

void Sleep::OnButtonEvent(lv_obj_t* obj, lv_event_t event) {
  if (event == LV_EVENT_CLICKED) {
    if (obj == btnSnooze) {
      StopAlerting();
      UpdateDisplay();
      SnoozeWakeAlarm();
      displayState = SleepDisplayState::Info;
      UpdateDisplay();
      return;
    }
    if (obj == btnStop) {
      StopAlarmPush();
      return;
    }
    if (obj == enableSwitch) {
      const bool alarmEnabled = lv_switch_get_state(enableSwitch);
      if (alarmEnabled) {
        infiniSleepController.ScheduleWakeAlarm();
      } else {
        infiniSleepController.DisableWakeAlarm();
      }
      if (infiniSleepController.isSnoozing) {
        infiniSleepController.RestorePreSnoozeTime();
      }
      infiniSleepController.isSnoozing = false;
      if (alarmEnabled) {
        // The alarm is set, so the next thing to do is to start the tracker
        displayState = SleepDisplayState::Info;
        UpdateDisplay();
      }
      return;
    }
    if (obj == trackerToggleBtn) {
      // Stopping the tracker asks for the same pushes as stopping the ringing alarm, so that it
      // can't be turned off by a half asleep touch during the night
      if (infiniSleepController.IsTrackerEnabled() && !StopPushConfirmed()) {
        return;
      }
      infiniSleepController.ToggleTracker();
      UpdateDisplay();
      return;
    }
    if (obj == btnSuggestedAlarm) {
      // Set the suggested time: current time + the desired number of sleep cycles
      const uint16_t alarmTotalMinutes = infiniSleepController.GetTimeOfDayInMinutesFromNow(infiniSleepController.GetSuggestedSleepTime());

      const uint8_t alarmHour = alarmTotalMinutes / 60;
      const uint8_t alarmMinute = alarmTotalMinutes % 60;

      NRF_LOG_INFO("Suggested wake alarm: %02d:%02d + %d minutes -> %02d:%02d",
                   infiniSleepController.GetCurrentHour(),
                   infiniSleepController.GetCurrentMinute(),
                   infiniSleepController.GetSuggestedSleepTime(),
                   alarmHour,
                   alarmMinute);

      hourCounter.SetValue(alarmHour);
      minuteCounter.SetValue(alarmMinute);

      // Exactly what turning the counters by hand does, and nothing more: the button fills the
      // wake up time in, it does not decide that the alarm is on and that the night has started.
      OnValueChanged();
      return;
    }
    if (obj == btnWakeMode) {
      if (infiniSleepController.infiniSleepSettings.graddualWake && infiniSleepController.infiniSleepSettings.naturalWake) {
        infiniSleepController.infiniSleepSettings.graddualWake = false;
        infiniSleepController.infiniSleepSettings.naturalWake = false;
      } else if (infiniSleepController.infiniSleepSettings.graddualWake) {
        infiniSleepController.infiniSleepSettings.graddualWake = false;
        infiniSleepController.infiniSleepSettings.naturalWake = true;
      } else if (infiniSleepController.infiniSleepSettings.naturalWake) {
        infiniSleepController.infiniSleepSettings.naturalWake = true;
        infiniSleepController.infiniSleepSettings.graddualWake = true;
      } else if (!infiniSleepController.infiniSleepSettings.graddualWake && !infiniSleepController.infiniSleepSettings.naturalWake) {
        infiniSleepController.infiniSleepSettings.graddualWake = true;
        infiniSleepController.infiniSleepSettings.naturalWake = false;
      }
      infiniSleepController.SetSettingsChanged();
      lv_label_set_text_static(lv_obj_get_child(obj, nullptr), WakeModeName(infiniSleepController.infiniSleepSettings));
      return;
    }
    if (obj == btnCycles) {
      uint8_t value = infiniSleepController.infiniSleepSettings.desiredCycles;
      value = (value % 10) + 1; // Cycle through values 1 to 10
      infiniSleepController.infiniSleepSettings.desiredCycles = value;
      infiniSleepController.SetSettingsChanged();
      lv_label_set_text_fmt(lv_obj_get_child(obj, nullptr), "%d", value);
      return;
    }
    if (obj == btnTestMotorGradual) {
      motorController.GradualWakeBuzz();
      return;
    }
    if (obj == btnMotorStrength) {
      uint8_t value = infiniSleepController.infiniSleepSettings.motorStrength;
      value += 25;
      if (value > 200) {
        value = 100;
      }
      infiniSleepController.infiniSleepSettings.motorStrength = value;
      infiniSleepController.SetSettingsChanged();
      lv_label_set_text_fmt(lv_obj_get_child(obj, nullptr), "%d", value);
      motorController.infiniSleepMotorStrength = value;
      motorController.GradualWakeBuzz();
      return;
    }
    if (obj == btnPushesToStop) {
      uint8_t value = infiniSleepController.infiniSleepSettings.pushesToStopAlarm;
      value = (value % 10) + 1; // Cycle through values 1 to 10
      infiniSleepController.infiniSleepSettings.pushesToStopAlarm = value;
      infiniSleepController.SetSettingsChanged();
      lv_label_set_text_fmt(lv_obj_get_child(obj, nullptr), "%d", value);
      return;
    }
    if (obj == btnHeartRateTracking) {
      const bool enabled = !infiniSleepController.infiniSleepSettings.heartRateTracking;
      infiniSleepController.infiniSleepSettings.heartRateTracking = enabled;
      infiniSleepController.SetSettingsChanged();
      lv_label_set_text_static(lv_obj_get_child(obj, nullptr), enabled ? "On" : "Off");
      return;
    }
    if (obj == btnBodyTracking) {
      const bool enabled = !infiniSleepController.infiniSleepSettings.bodyTracking;
      infiniSleepController.infiniSleepSettings.bodyTracking = enabled;
      infiniSleepController.SetSettingsChanged();
      lv_label_set_text_static(lv_obj_get_child(obj, nullptr), enabled ? "On" : "Off");
      return;
    }
    if (obj == btnTrackerInterval) {
      const uint8_t value = NextChoice(infiniSleepController.GetTrackerIntervalMinutes(), trackerIntervalChoices);
      infiniSleepController.infiniSleepSettings.trackerIntervalMinutes = value;
      infiniSleepController.SetSettingsChanged();
      lv_label_set_text_fmt(lv_obj_get_child(obj, nullptr), "%dm", value);
      return;
    }
    if (obj == btnMotionInterval) {
      const uint8_t value = NextChoice(infiniSleepController.GetMotionSampleIntervalDs(), motionIntervalChoices);
      infiniSleepController.infiniSleepSettings.motionSampleIntervalDs = value;
      infiniSleepController.SetSettingsChanged();
      lv_label_set_text_fmt(lv_obj_get_child(obj, nullptr), "%d.%ds", value / 10, value % 10);
      return;
    }
  }
}

bool Sleep::OnButtonPushed() {
  if (ignoreButtonPush) {
    return true;
  }
  // Side button to snooze
  if (infiniSleepController.IsAlerting() && displayState == SleepDisplayState::Alarm) {
    OnButtonEvent(btnSnooze, LV_EVENT_CLICKED);
    return true;
  }
  // Back to the wake up time setter, which is the home page of the app. While the tracker runs
  // the pages are locked, so the button closes the app instead
  if (!infiniSleepController.IsTrackerEnabled() && displayState != SleepDisplayState::Alarm) {
    displayState = SleepDisplayState::Alarm;
    UpdateDisplay();
    return true;
  }
  return false;
}

bool Sleep::StopPushConfirmed() {
  if (infiniSleepController.pushesLeftToStopWakeAlarm > 1) {
    lv_task_reset(taskPressesToStopAlarmTimeout);
    infiniSleepController.pushesLeftToStopWakeAlarm--;
    UpdateDisplay();
    return false;
  }
  infiniSleepController.pushesLeftToStopWakeAlarm = infiniSleepController.infiniSleepSettings.pushesToStopAlarm;
  return true;
}

bool Sleep::StopAlarmPush() {
  if (!StopPushConfirmed()) {
    return true;
  }

  if (infiniSleepController.isSnoozing) {
    infiniSleepController.RestorePreSnoozeTime();
  }
  infiniSleepController.isSnoozing = false;
  StopAlerting();
  if (infiniSleepController.IsTrackerEnabled()) {
    displayState = SleepDisplayState::Info;
    UpdateDisplay();
    infiniSleepController.ToggleTracker();
    UpdateDisplay();
    return true;
  }
  displayState = SleepDisplayState::Info;
  UpdateDisplay();
  return true;
}

bool Sleep::OnTouchEvent(Pinetime::Applications::TouchEvents event) {

  // Swiping should be ignored when in alerting state
  if (infiniSleepController.IsAlerting() && (event == TouchEvents::SwipeDown || event == TouchEvents::SwipeUp ||
                                             event == TouchEvents::SwipeLeft || event == TouchEvents::SwipeRight)) {
    return true;
  }

  // While the tracker runs the app stays on the tracking page, so neither the wake up time
  // nor the settings can be changed during the night
  if (infiniSleepController.IsTrackerEnabled() && (event == TouchEvents::SwipeDown || event == TouchEvents::SwipeUp)) {
    return true;
  }

  // The cases for swiping to change page on app
  switch (event) {
    case TouchEvents::SwipeDown:
      if (displayState == firstPage) {
        return false;
      }
      displayApp.SetFullRefresh(Pinetime::Applications::DisplayApp::FullRefreshDirections::Down);
      displayState = static_cast<SleepDisplayState>(static_cast<uint8_t>(displayState) - 1);
      UpdateDisplay();
      NRF_LOG_INFO("SwipeDown: %d", static_cast<uint8_t>(displayState));
      return true;
    case TouchEvents::SwipeUp:
      if (displayState != lastPage) {
        displayApp.SetFullRefresh(Pinetime::Applications::DisplayApp::FullRefreshDirections::Up);
        displayState = static_cast<SleepDisplayState>(static_cast<uint8_t>(displayState) + 1);
        UpdateDisplay();
      }
      NRF_LOG_INFO("SwipeUp: %d", static_cast<uint8_t>(displayState));
      return true;
    default:
      break;
  }

  // Don't allow closing the screen by swiping while the alarm is alerting
  return infiniSleepController.IsAlerting() && event == TouchEvents::SwipeDown;
}

void Sleep::OnValueChanged() {
  DisableWakeAlarm();
  UpdateWakeAlarmTime();
}

// Currently snoozes baeed on define statement in InfiniSleepController.h
void Sleep::SnoozeWakeAlarm() {
  if (taskSnoozeWakeAlarm != nullptr) {
    lv_task_del(taskSnoozeWakeAlarm);
    taskSnoozeWakeAlarm = nullptr;
  }

  NRF_LOG_INFO("Snoozing alarm for %d minutes", SNOOZE_MINUTES);

  // Wraps over midnight, so snoozing at 23:58 gives 00:01 and not 24:01
  const uint16_t newSnoozeMinutes = infiniSleepController.GetTimeOfDayInMinutesFromNow(SNOOZE_MINUTES);

  if (infiniSleepController.isSnoozing != true) {
    infiniSleepController.SetPreSnoozeTime();
  }
  infiniSleepController.isSnoozing = true;

  infiniSleepController.SetWakeAlarmTime(newSnoozeMinutes / 60, newSnoozeMinutes % 60);

  hourCounter.SetValue(newSnoozeMinutes / 60);
  minuteCounter.SetValue(newSnoozeMinutes % 60);

  infiniSleepController.ScheduleWakeAlarm();
}

void Sleep::UpdateWakeAlarmTime() {
  if (lblampm != nullptr) {
    if (hourCounter.GetValue() >= 12) {
      lv_label_set_text_static(lblampm, "PM");
    } else {
      lv_label_set_text_static(lblampm, "AM");
    }
  }
  infiniSleepController.SetWakeAlarmTime(hourCounter.GetValue(), minuteCounter.GetValue());
  SetSwitchState(LV_ANIM_OFF);
}

void Sleep::SetAlerting() {
  lv_obj_set_hidden(enableSwitch, true);
  lv_obj_set_hidden(btnSnooze, false);
  lv_obj_set_hidden(btnStop, false);
  lv_obj_set_hidden(btnSuggestedAlarm, true);
  lv_obj_set_hidden(txtSuggestedAlarm, true);
  lv_obj_set_hidden(iconSuggestedAlarm, true);
  NRF_LOG_INFO("Alarm is alerting");
  if (!infiniSleepController.infiniSleepSettings.naturalWake) {
    if (taskSnoozeWakeAlarm != nullptr) {
      lv_task_del(taskSnoozeWakeAlarm);
      taskSnoozeWakeAlarm = nullptr;
    }
    taskSnoozeWakeAlarm = lv_task_create(SnoozeAlarmTaskCallback, 120 * 1000, LV_TASK_PRIO_MID, this);
  }
  if (infiniSleepController.infiniSleepSettings.naturalWake) {
    motorController.StartNaturalWakeAlarm();
  } else {
    motorController.StartWakeAlarm();
  }
  wakeLock.Lock();
  alreadyAlerting = true;
}

void Sleep::RedrawSetAlerting() {
  lv_obj_set_hidden(enableSwitch, true);
  lv_obj_set_hidden(btnSnooze, false);
  lv_obj_set_hidden(btnStop, false);
  lv_obj_set_hidden(btnSuggestedAlarm, true);
  lv_obj_set_hidden(txtSuggestedAlarm, true);
  lv_obj_set_hidden(iconSuggestedAlarm, true);
  wakeLock.Lock();
}

void Sleep::StopAlerting(bool setSwitch) {
  if (taskSnoozeWakeAlarm != nullptr) {
    lv_task_del(taskSnoozeWakeAlarm);
    taskSnoozeWakeAlarm = nullptr;
  }
  infiniSleepController.StopAlerting();
  if (infiniSleepController.infiniSleepSettings.naturalWake) {
    motorController.StopNaturalWakeAlarm();
  } else {
    motorController.StopWakeAlarm();
  }
  if (setSwitch) {
    SetSwitchState(LV_ANIM_OFF);
  }
  wakeLock.Release();
  lv_obj_set_hidden(enableSwitch, false);
  lv_obj_set_hidden(btnSnooze, true);
  lv_obj_set_hidden(btnStop, true);
  lv_obj_set_hidden(btnSuggestedAlarm, false);
  lv_obj_set_hidden(txtSuggestedAlarm, false);
  lv_obj_set_hidden(iconSuggestedAlarm, false);
  alreadyAlerting = false;
}

void Sleep::SetSwitchState(lv_anim_enable_t anim) {
  if (displayState == SleepDisplayState::Alarm && infiniSleepController.GetWakeAlarm().isEnabled) {
    lv_switch_on(enableSwitch, anim);
  } else {
    lv_switch_off(enableSwitch, anim);
  }
}