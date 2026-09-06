#pragma once

#include <cstdint>
#include <chrono>
#include "displayapp/screens/Screen.h"
#include "systemtask/SystemTask.h"
#include "systemtask/WakeLock.h"
#include "Symbols.h"
#include <lvgl/src/lv_core/lv_style.h>
#include <lvgl/src/lv_core/lv_obj.h>

namespace Pinetime {
  namespace Controllers {
    class HeartRateController;
  }

  namespace Applications {
    namespace Screens {

      class HeartRate : public Screen {
      public:
        HeartRate(Controllers::HeartRateController& HeartRateController, System::SystemTask& systemTask);
        ~HeartRate() override;

        void Refresh() override;

        void OnStartStopEvent(lv_event_t event);

      private:
        /// Hands the reading on screen to the system task for the activity log, at most once a minute
        /// per run, which is all the log's own resolution can hold.
        void LogReading();

        Controllers::HeartRateController& heartRateController;
        Pinetime::System::SystemTask& systemTask;
        Pinetime::System::WakeLock wakeLock;
        void UpdateStartStopButton(bool isRunning);
        lv_obj_t* label_hr;
        lv_obj_t* label_bpm;
        lv_obj_t* label_status;
        lv_obj_t* btn_startStop;
        lv_obj_t* label_startStop;

        lv_task_t* taskRefresh;

        static constexpr TickType_t logInterval = pdMS_TO_TICKS(60 * 1000);
        /// When the reading was last logged, and whether this run has been logged at all: the first
        /// reading of a run goes out immediately, since that is the one the wearer waited for.
        TickType_t lastLogTicks = 0;
        bool loggedThisRun = false;
      };
    }

    template <>
    struct AppTraits<Apps::HeartRate> {
      static constexpr Apps app = Apps::HeartRate;
      static constexpr const char* icon = Screens::Symbols::heartBeat;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::HeartRate(controllers.heartRateController, *controllers.systemTask);
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      };
    };
  }
}
