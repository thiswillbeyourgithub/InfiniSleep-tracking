/*  Copyright (C) 2026 github user thiswillbeyourgithub

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
#include "displayapp/screens/Logging.h"

#include <cstdio>

#include "displayapp/InfiniTimeTheme.h"
#include "displayapp/screens/Symbols.h"

using namespace Pinetime::Applications::Screens;
using Pinetime::Controllers::LoggedEvent;
using Pinetime::Controllers::LoggedEventType;
using Pinetime::Controllers::LogSlot;
using Pinetime::Controllers::LogSlotBehaviour;

namespace {
  void EventHandler(lv_obj_t* obj, lv_event_t event) {
    auto* screen = static_cast<Logging*>(obj->user_data);
    screen->OnButtonEvent(obj, event);
  }

  constexpr int16_t titleHeight = 28;
  constexpr int16_t itemGap = 4;
  constexpr int16_t itemHeight = (LV_VER_RES_MAX - titleHeight - 3 * itemGap) / 4;

  /* Long enough for "Something started", which is the longest line a fifteen character label can
   * produce here. */
  constexpr size_t lineSize = LogSlot::labelSize + 16;
}

Logging::Logging(Controllers::EventLogController& eventLog,
                 Controllers::LogSlots& logSlots,
                 Controllers::DateTime& dateTimeController,
                 Controllers::MotorController& motorController)
  : eventLog {eventLog}, logSlots {logSlots}, dateTimeController {dateTimeController}, motorController {motorController} {
  rootGroup = SingleTopGroup();
  openGroup = rootGroup;
  BuildMenu();
  taskRefresh = lv_task_create(RefreshTaskCallback, LV_DISP_DEF_REFR_PERIOD, LV_TASK_PRIO_MID, this);
}

Logging::~Logging() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

uint8_t Logging::SingleTopGroup() const {
  if (!logSlots.HasSingleGroup()) {
    return LogSlot::noParent;
  }
  for (uint8_t i = 0; i < logSlots.Count(); i++) {
    const LogSlot* slot = logSlots.At(i);
    if (slot != nullptr && slot->parent == LogSlot::noParent && slot->behaviour == LogSlotBehaviour::Group) {
      return slot->id;
    }
  }
  return LogSlot::noParent;
}

void Logging::ClearScreen() {
  lv_obj_clean(lv_scr_act());
  for (auto& button : itemButtons) {
    button = nullptr;
  }
  valueLabel = nullptr;
  slider = nullptr;
  logButton = nullptr;
  backButton = nullptr;
  flagButton = nullptr;
  flagLabel = nullptr;
}

uint8_t Logging::PageCount() const {
  if (childCount == 0) {
    return 1;
  }
  return (childCount + itemsPerPage - 1) / itemsPerPage;
}

bool Logging::AtRoot() const {
  return openGroup == rootGroup;
}

const char* Logging::IconFor(const LogSlot& slot, bool running) {
  switch (slot.behaviour) {
    case LogSlotBehaviour::Group:
      return Symbols::list;
    case LogSlotBehaviour::Continuous:
      /* What tapping it will do, rather than what it is: a slot that is running offers stopping
       * it, which is the only way the watch says a session is open. */
      return running ? Symbols::stop : Symbols::play;
    case LogSlotBehaviour::Valued:
      return Symbols::tachometer;
    case LogSlotBehaviour::Punctual:
    default:
      return Symbols::check;
  }
}

void Logging::BuildMenu() {
  ClearScreen();
  view = View::Menu;

  builtRevision = logSlots.Revision();
  childCount = logSlots.ChildrenOf(openGroup, children, Controllers::LogSlots::maxSlots);
  if (page >= PageCount()) {
    page = PageCount() - 1;
  }

  lv_obj_t* title = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(title, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
  const char* titleText = "Log";
  if (openGroup != LogSlot::noParent) {
    const LogSlot* group = logSlots.Find(openGroup);
    if (group != nullptr) {
      titleText = group->label;
    }
  }
  if (PageCount() > 1) {
    lv_label_set_text_fmt(title, "%s  %d/%d", titleText, static_cast<int>(page) + 1, static_cast<int>(PageCount()));
  } else {
    lv_label_set_text(title, titleText);
  }
  lv_obj_align(title, nullptr, LV_ALIGN_IN_TOP_MID, 0, 4);

  if (logSlots.Count() == 0) {
    lv_obj_t* empty = lv_label_create(lv_scr_act(), nullptr);
    lv_label_set_long_mode(empty, LV_LABEL_LONG_BREAK);
    lv_obj_set_width(empty, LV_HOR_RES - 20);
    lv_label_set_align(empty, LV_LABEL_ALIGN_CENTER);
    lv_label_set_text_static(empty, "Nothing to log yet. The companion app decides what goes here.");
    lv_obj_align(empty, nullptr, LV_ALIGN_CENTER, 0, 0);
    return;
  }

  if (childCount == 0) {
    lv_obj_t* empty = lv_label_create(lv_scr_act(), nullptr);
    lv_label_set_text_static(empty, "Nothing in here");
    lv_obj_align(empty, nullptr, LV_ALIGN_CENTER, 0, 0);
    return;
  }

  const uint8_t first = page * itemsPerPage;
  for (uint8_t i = 0; i < itemsPerPage && first + i < childCount; i++) {
    const LogSlot* slot = logSlots.At(children[first + i]);
    if (slot == nullptr) {
      continue;
    }
    const bool running = slot->behaviour == LogSlotBehaviour::Continuous && eventLog.IsRunning(slot->id);

    lv_obj_t* button = lv_btn_create(lv_scr_act(), nullptr);
    button->user_data = this;
    lv_obj_set_event_cb(button, EventHandler);
    lv_btn_set_layout(button, LV_LAYOUT_OFF);
    lv_obj_set_style_local_radius(button, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 3);
    lv_obj_set_style_local_bg_color(button, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Colors::bgAlt);
    lv_obj_set_size(button, LV_HOR_RES - 8, itemHeight);
    lv_obj_set_pos(button, 4, titleHeight + i * (itemHeight + itemGap));
    itemButtons[i] = button;

    lv_obj_t* icon = lv_label_create(button, nullptr);
    lv_label_set_text_static(icon, IconFor(*slot, running));
    lv_obj_set_style_local_text_color(icon,
                                     LV_LABEL_PART_MAIN,
                                     LV_STATE_DEFAULT,
                                     running ? Colors::highlight : LV_COLOR_YELLOW);
    lv_obj_align(icon, nullptr, LV_ALIGN_IN_LEFT_MID, 4, 0);

    lv_obj_t* label = lv_label_create(button, nullptr);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CROP);
    lv_obj_set_width(label, LV_HOR_RES - 50);
    lv_label_set_text(label, slot->label);
    lv_obj_align(label, icon, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
  }
}

void Logging::BuildValue() {
  ClearScreen();
  view = View::Value;

  lv_obj_t* title = lv_label_create(lv_scr_act(), nullptr);
  const LogSlot* slot = logSlots.Find(subjectSlot);
  lv_label_set_text(title, slot == nullptr ? "?" : slot->label);
  lv_obj_align(title, nullptr, LV_ALIGN_IN_TOP_MID, 0, 8);

  valueLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(valueLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_42);
  lv_label_set_text_fmt(valueLabel, "%d", static_cast<int>(initialValue));
  lv_obj_align(valueLabel, nullptr, LV_ALIGN_IN_TOP_MID, 0, 45);

  /* A slider rather than a pair of buttons, because ten taps to say ten is a different act from
   * one tap to say one, and the reading would carry how awkward it was to enter. */
  slider = lv_slider_create(lv_scr_act(), nullptr);
  slider->user_data = this;
  lv_obj_set_event_cb(slider, EventHandler);
  lv_slider_set_range(slider, 0, LoggedEvent::maxValue);
  lv_slider_set_value(slider, initialValue, LV_ANIM_OFF);
  lv_obj_set_size(slider, LV_HOR_RES - 40, 20);
  lv_obj_set_style_local_pad_all(slider, LV_SLIDER_PART_KNOB, LV_STATE_DEFAULT, 10);
  lv_obj_align(slider, nullptr, LV_ALIGN_CENTER, 0, 20);

  logButton = lv_btn_create(lv_scr_act(), nullptr);
  logButton->user_data = this;
  lv_obj_set_event_cb(logButton, EventHandler);
  lv_obj_set_style_local_bg_color(logButton, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Colors::highlight);
  lv_obj_set_size(logButton, LV_HOR_RES - 80, 50);
  lv_obj_align(logButton, nullptr, LV_ALIGN_IN_BOTTOM_MID, 0, -8);
  lv_obj_t* logLabel = lv_label_create(logButton, nullptr);
  lv_label_set_text_static(logLabel, "Log");
}

void Logging::BuildConfirmation(const char* what) {
  ClearScreen();
  view = View::Done;

  lv_obj_t* mark = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(mark, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_42);
  const bool stored = loggedSequence != 0;
  lv_label_set_text_static(mark, stored ? Symbols::check : Symbols::ban);
  lv_obj_set_style_local_text_color(mark,
                                    LV_LABEL_PART_MAIN,
                                    LV_STATE_DEFAULT,
                                    stored ? Colors::highlight : Colors::deepOrange);
  lv_obj_align(mark, nullptr, LV_ALIGN_IN_TOP_MID, 0, 20);

  lv_obj_t* line = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_long_mode(line, LV_LABEL_LONG_BREAK);
  lv_obj_set_width(line, LV_HOR_RES - 20);
  lv_label_set_align(line, LV_LABEL_ALIGN_CENTER);
  lv_label_set_text(line, stored ? what : "Not logged");
  lv_obj_align(line, nullptr, LV_ALIGN_CENTER, 0, 0);

  /* A way back that does not mean waiting out confirmationMs, because logging several things in a
   * row is the normal case and four seconds of watching a tick each time is not. Swiping right and
   * the side button already do this; the button is here because on this screen the thumb is already
   * at the bottom, next to the flag it may also want. */
  backButton = lv_btn_create(lv_scr_act(), nullptr);
  backButton->user_data = this;
  lv_obj_set_event_cb(backButton, EventHandler);
  lv_obj_set_style_local_bg_color(backButton, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Colors::bgAlt);
  lv_obj_t* backLabel = lv_label_create(backButton, nullptr);
  lv_label_set_text_static(backLabel, "Back");

  if (!stored) {
    /* Nothing was written, so there is nothing to flag and the way back gets the whole row. */
    lv_obj_set_size(backButton, LV_HOR_RES - 80, 50);
    lv_obj_align(backButton, nullptr, LV_ALIGN_IN_BOTTOM_MID, 0, -8);
    return;
  }

  /* Side by side, each taking half of the row once the margins and the gap between them are out.
   * Not constexpr: LV_HOR_RES asks the driver for the width rather than naming it. */
  const lv_coord_t buttonWidth = (LV_HOR_RES - 30) / 2;
  lv_obj_set_size(backButton, buttonWidth, 50);
  lv_obj_align(backButton, nullptr, LV_ALIGN_IN_BOTTOM_LEFT, 10, -8);

  /* Flagging is offered here rather than asked for beforehand: logging something is meant to be a
   * single tap, and a flag is how the wearer says to come back to this one on the phone, usually
   * because it is being logged later than it happened. */
  flagButton = lv_btn_create(lv_scr_act(), nullptr);
  flagButton->user_data = this;
  lv_obj_set_event_cb(flagButton, EventHandler);
  lv_obj_set_style_local_bg_color(flagButton, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Colors::bgAlt);
  lv_obj_set_size(flagButton, buttonWidth, 50);
  lv_obj_align(flagButton, nullptr, LV_ALIGN_IN_BOTTOM_RIGHT, -10, -8);
  flagLabel = lv_label_create(flagButton, nullptr);
  lv_label_set_text_static(flagLabel, "Flag");
}

void Logging::Log(uint8_t slot, LoggedEventType type, uint8_t value, const char* what) {
  LoggedEvent event;
  event.timestamp = dateTimeController.UtcSecondsSinceEpoch();
  event.slot = slot;
  event.type = type;
  event.value = value;

  loggedSequence = eventLog.Add(event);
  if (loggedSequence != 0) {
    motorController.RunForDuration(35);
  }
  confirmationShownAt = lv_tick_get();
  BuildConfirmation(what);
}

void Logging::ItemTapped(uint8_t index) {
  const uint8_t position = page * itemsPerPage + index;
  if (position >= childCount) {
    return;
  }
  const LogSlot* slot = logSlots.At(children[position]);
  if (slot == nullptr) {
    return;
  }

  char line[lineSize];
  switch (slot->behaviour) {
    case LogSlotBehaviour::Group:
      openGroup = slot->id;
      page = 0;
      BuildMenu();
      break;
    case LogSlotBehaviour::Punctual:
      snprintf(line, sizeof(line), "%s", slot->label);
      Log(slot->id, LoggedEventType::Punctual, LoggedEvent::valueNotAsked, line);
      break;
    case LogSlotBehaviour::Continuous: {
      const bool running = eventLog.IsRunning(slot->id);
      snprintf(line, sizeof(line), "%s %s", slot->label, running ? "stopped" : "started");
      Log(slot->id, running ? LoggedEventType::Stopped : LoggedEventType::Started, LoggedEvent::valueNotAsked, line);
      break;
    }
    case LogSlotBehaviour::Valued:
      subjectSlot = slot->id;
      BuildValue();
      break;
  }
}

void Logging::GoUp() {
  const LogSlot* group = logSlots.Find(openGroup);
  openGroup = group == nullptr ? rootGroup : group->parent;
  page = 0;
  BuildMenu();
}

void Logging::OnButtonEvent(lv_obj_t* obj, lv_event_t event) {
  if (obj == slider) {
    if (event == LV_EVENT_VALUE_CHANGED && valueLabel != nullptr) {
      lv_label_set_text_fmt(valueLabel, "%d", static_cast<int>(lv_slider_get_value(slider)));
    }
    return;
  }

  if (event != LV_EVENT_CLICKED) {
    return;
  }

  if (obj == logButton) {
    const LogSlot* slot = logSlots.Find(subjectSlot);
    const int16_t value = slider == nullptr ? initialValue : lv_slider_get_value(slider);
    char line[lineSize];
    snprintf(line, sizeof(line), "%s %d", slot == nullptr ? "?" : slot->label, static_cast<int>(value));
    Log(subjectSlot, LoggedEventType::Valued, static_cast<uint8_t>(value), line);
    return;
  }

  if (obj == backButton) {
    BuildMenu();
    return;
  }

  if (obj == flagButton) {
    if (eventLog.Flag(loggedSequence)) {
      lv_label_set_text_static(flagLabel, "Flagged");
      lv_obj_set_state(flagButton, LV_STATE_DISABLED);
      /* Worth another look now that it says something different. */
      confirmationShownAt = lv_tick_get();
    }
    return;
  }

  for (uint8_t i = 0; i < itemsPerPage; i++) {
    if (obj == itemButtons[i]) {
      ItemTapped(i);
      return;
    }
  }
}

bool Logging::OnButtonPushed() {
  if (view != View::Menu) {
    BuildMenu();
    return true;
  }
  if (AtRoot()) {
    return false;
  }
  GoUp();
  return true;
}

bool Logging::OnTouchEvent(TouchEvents event) {
  switch (event) {
    case TouchEvents::SwipeRight:
      /* Back one step, which is out of a view and then up the table. At the level the app opened
       * on there is nothing left to go back to, so the gesture is not ours. */
      if (view != View::Menu) {
        BuildMenu();
        return true;
      }
      if (AtRoot()) {
        return false;
      }
      GoUp();
      return true;
    case TouchEvents::SwipeUp:
      if (view == View::Menu && page + 1 < PageCount()) {
        page++;
        BuildMenu();
        return true;
      }
      return false;
    case TouchEvents::SwipeDown:
      /* Only ours while there is a page above: a swipe down on the first page is how the app is
       * left, since that is the gesture that walks back out of anything opened from the launcher. */
      if (view == View::Menu && page > 0) {
        page--;
        BuildMenu();
        return true;
      }
      return false;
    default:
      return false;
  }
}

void Logging::Refresh() {
  if (view == View::Done && lv_tick_elaps(confirmationShownAt) > confirmationMs) {
    BuildMenu();
    return;
  }

  if (view == View::Menu && builtRevision != logSlots.Revision()) {
    /* A table arrived from the phone while the app was open, so what is on screen is no longer
     * what the wearer would be logging against. */
    rootGroup = SingleTopGroup();
    openGroup = rootGroup;
    page = 0;
    BuildMenu();
  }
}
