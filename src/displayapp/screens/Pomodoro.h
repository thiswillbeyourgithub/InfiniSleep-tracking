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
#pragma once

#include "displayapp/apps/Apps.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/Controllers.h"
#include "components/pomodoro/PomodoroController.h"
#include "systemtask/WakeLock.h"
#include "utility/DirtyValue.h"
#include "Symbols.h"

namespace Pinetime {
  namespace Applications {
    namespace Screens {
      /* The three views of the Pomodoro app, one per controller state. The screen owns no state of
       * its own beyond which view is currently built: everything else is read from the controller,
       * because the app keeps running after the screen has been destroyed. */
      class Pomodoro : public Screen {
      public:
        Pomodoro(Controllers::PomodoroController& pomodoroController, System::SystemTask& systemTask);
        ~Pomodoro() override;

        void Refresh() override;
        void OnButtonEvent(lv_obj_t* obj, lv_event_t event);
        bool OnButtonPushed() override;
        bool OnTouchEvent(TouchEvents event) override;

      private:
        Controllers::PomodoroController& pomodoroController;
        System::WakeLock wakeLock;

        /* Which view is on screen. The controller can move from Running to Ringing and back on its
         * own while the app is open, so every refresh checks whether the view is still the right
         * one and rebuilds it if not. */
        Controllers::PomodoroController::State displayedState = Controllers::PomodoroController::State::Stopped;

        lv_task_t* taskRefresh;

        // Stopped view: the keypad.
        lv_obj_t* buttonMatrix = nullptr;
        lv_obj_t* queueLabel = nullptr;
        lv_obj_t* vibrationsLabel = nullptr;

        // Running view: the countdown.
        lv_obj_t* positionLabel = nullptr;
        lv_obj_t* countdownLabel = nullptr;
        lv_obj_t* btnStop = nullptr;
        lv_obj_t* btnAddMinute = nullptr;

        // Redraws the countdown only when the displayed second actually changes.
        Utility::DirtyValue<uint32_t> displaySeconds;

        void BuildCurrentView();
        void BuildStoppedView();
        void BuildRunningView();
        void BuildRingingView();
        void UpdateQueueLabels();
        void UpdateCountdown();
        void RenderCountdown(uint32_t secondsRemaining);
        void HandleKeypadInput();
      };
    }

    template <>
    struct AppTraits<Apps::Pomodoro> {
      static constexpr Apps app = Apps::Pomodoro;
      // No tomato in the icon font, and the clock glyph is the only timekeeping one not already taken.
      static constexpr const char* icon = Screens::Symbols::clock;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::Pomodoro(controllers.pomodoroController, *controllers.systemTask);
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      };
    };
  }
}
