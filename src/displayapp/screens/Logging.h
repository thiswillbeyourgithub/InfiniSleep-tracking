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
#pragma once

#include <lvgl/lvgl.h>

#include "displayapp/apps/Apps.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/Controllers.h"
#include "components/datetime/DateTimeController.h"
#include "components/log/EventLogController.h"
#include "components/log/LogSlots.h"
#include "components/motor/MotorController.h"
#include "Symbols.h"

namespace Pinetime {
  namespace Applications {
    namespace Screens {
      /* Where the wearer logs what is happening to them: a medication taken, a nap started, how
       * bad the pain is right now.
       *
       * Every screen here is built at runtime from the table of slots the phone pushed, because
       * what is worth logging is the wearer's business and changes without the firmware changing.
       * The watch stores ids and never names: renaming a slot, reordering the table or moving a
       * slot into another group is a push away and costs nothing here.
       *
       * Three views, since logging is meant to be quick: the menu, the slider a slot that asks for
       * a reading opens, and a short confirmation that offers to flag what was just logged. */
      class Logging : public Screen {
      public:
        Logging(Controllers::EventLogController& eventLog,
                Controllers::LogSlots& logSlots,
                Controllers::DateTime& dateTimeController,
                Controllers::MotorController& motorController);
        ~Logging() override;

        void Refresh() override;
        void OnButtonEvent(lv_obj_t* obj, lv_event_t event);
        bool OnButtonPushed() override;
        bool OnTouchEvent(TouchEvents event) override;

      private:
        enum class View { Menu, Value, Done };

        /* Four rows is what fits at a size that can be hit without looking, and is what the
         * settings menus already use. */
        static constexpr uint8_t itemsPerPage = 4;

        /* How long the confirmation stays before the menu comes back. Long enough to read it and
         * to reach the flag, short enough that a wearer who has dropped their wrist is back where
         * they would want to start next time. */
        static constexpr uint32_t confirmationMs = 4000;

        static constexpr uint8_t initialValue = 5;

        Controllers::EventLogController& eventLog;
        Controllers::LogSlots& logSlots;
        Controllers::DateTime& dateTimeController;
        Controllers::MotorController& motorController;

        View view = View::Menu;

        /* Which group the menu is showing, or LogSlot::noParent for the top of the table. */
        uint8_t openGroup = Controllers::LogSlot::noParent;

        /* The level the app opens at, and therefore the one that a swipe back leaves the app from.
         * Not always the top of the table: a table with a single group at the top would open on a
         * screen holding one button, so that screen is skipped. */
        uint8_t rootGroup = Controllers::LogSlot::noParent;

        uint8_t page = 0;

        /* Positions in the slot table of what the open group holds, filled in whenever the menu is
         * built. Positions rather than ids because the table order is the order the phone listed
         * them in, and that is the order they are shown in. */
        uint8_t children[Controllers::LogSlots::maxSlots] = {};
        uint8_t childCount = 0;

        /* Which revision the menu was built from, so that a table arriving while the app is open
         * is noticed rather than leaving the wearer tapping slots that are gone. */
        uint16_t builtRevision = 0;

        /* What the value and confirmation views are about. */
        uint8_t subjectSlot = 0;
        uint32_t loggedSequence = 0;
        uint32_t confirmationShownAt = 0;

        lv_obj_t* itemButtons[itemsPerPage] = {};
        lv_obj_t* valueLabel = nullptr;
        lv_obj_t* slider = nullptr;
        lv_obj_t* logButton = nullptr;
        lv_obj_t* flagButton = nullptr;
        lv_obj_t* flagLabel = nullptr;
        lv_task_t* taskRefresh = nullptr;

        void BuildMenu();
        void BuildValue();
        void BuildConfirmation(const char* what);
        void ClearScreen();

        /* The id of the only group at the top of the table, or noParent when there is not exactly
         * one, which is the whole of the rule about skipping that screen. */
        uint8_t SingleTopGroup() const;

        void ItemTapped(uint8_t index);
        void GoUp();
        bool AtRoot() const;
        uint8_t PageCount() const;
        static const char* IconFor(const Controllers::LogSlot& slot, bool running);

        /* Logs one event, buzzes, and moves to the confirmation. */
        void Log(uint8_t slot, Controllers::LoggedEventType type, uint8_t value, const char* what);
      };
    }

    template <>
    struct AppTraits<Apps::Logging> {
      static constexpr Apps app = Apps::Logging;
      static constexpr const char* icon = Screens::Symbols::lapsFlag;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::Logging(controllers.eventLogController,
                                   controllers.logSlots,
                                   controllers.dateTimeController,
                                   controllers.motorController);
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      };
    };
  }
}
