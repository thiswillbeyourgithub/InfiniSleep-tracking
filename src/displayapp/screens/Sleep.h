#pragma once

#include "displayapp/apps/Apps.h"
#include "components/settings/Settings.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/widgets/Counter.h"
#include "displayapp/widgets/PageIndicator.h"
#include "displayapp/Controllers.h"
#include "systemtask/SystemTask.h"
#include "systemtask/WakeLock.h"
#include "Symbols.h"

namespace Pinetime {
  namespace Applications {
    namespace Screens {
      class Sleep : public Screen {
      public:
        explicit Sleep(Controllers::InfiniSleepController& infiniSleepController,
                       Controllers::Settings::ClockType clockType,
                       System::SystemTask& systemTask,
                       Controllers::MotorController& motorController,
                       DisplayApp& displayApp);
        ~Sleep() override;
        void Refresh() override;
        void SetAlerting();
        void RedrawSetAlerting();
        void OnButtonEvent(lv_obj_t* obj, lv_event_t event);
        bool OnButtonPushed() override;
        bool OnTouchEvent(TouchEvents event) override;
        void OnValueChanged();
        void StopAlerting(bool setSwitch = true);
        void SnoozeWakeAlarm();
        void UpdateDisplay();
        // Pages from top to bottom: swiping down goes to the settings, swiping up to the info page.
        // Alarm (the wake up time setter) is the page the app starts on.
        enum class SleepDisplayState { Settings, Sensors, Alarm, Info };
        static constexpr SleepDisplayState firstPage = SleepDisplayState::Settings;
        static constexpr SleepDisplayState lastPage = SleepDisplayState::Info;
        SleepDisplayState displayState = SleepDisplayState::Alarm;

        Controllers::InfiniSleepController& infiniSleepController;

        bool ignoreButtonPush = false;

        lv_obj_t* btnSnooze;

      private:
        System::WakeLock wakeLock;
        Controllers::MotorController& motorController;
        Controllers::Settings::ClockType clockType;
        DisplayApp& displayApp;

        lv_obj_t *btnStop, *txtStop, *txtSnooze, /**btnRecur, *txtRecur,*/ *btnInfo, *enableSwitch;
        lv_obj_t *trackerToggleBtn, *trackerToggleLabel;
        lv_obj_t* lblampm = nullptr;
        lv_obj_t* txtMessage = nullptr;
        lv_obj_t* btnMessage = nullptr;
        lv_task_t* taskSnoozeWakeAlarm = nullptr;

        lv_task_t* taskRefresh = nullptr;

        lv_task_t* taskPressesToStopAlarmTimeout = nullptr;

        // enum class EnableButtonState { On, Off, Alerting };
        void DisableWakeAlarm();
        void SetSwitchState(lv_anim_enable_t anim);
        void SetWakeAlarm();
        void UpdateWakeAlarmTime();
        Widgets::Counter hourCounter = Widgets::Counter(0, 23, jetbrains_mono_76);
        Widgets::Counter minuteCounter = Widgets::Counter(0, 59, jetbrains_mono_76);

        void DrawAlarmScreen();
        void DrawInfoScreen();
        void DrawSettingsScreen();
        /// What is recorded overnight and how often, as opposed to how the alarm behaves.
        void DrawSensorsScreen();
        /// One "Name  [value]" row of a settings page. Returns the button, whose only child is
        /// the value label, so that OnButtonEvent can update it through lv_obj_get_child.
        lv_obj_t* CreateSettingRow(const char* name, int16_t yOffset);
        bool StopAlarmPush();
        // Counts the pushes needed to stop the alarm or the tracker, returns true once there are enough of them
        bool StopPushConfirmed();

        bool alreadyAlerting = false;
        // The page currently on screen, so that the alarm page isn't redrawn under the fingers
        // of the user while the time is being set
        bool screenDrawn = false;
        SleepDisplayState drawnState = SleepDisplayState::Alarm;

        lv_obj_t* label_hr;
        lv_obj_t* label_start_time;
        lv_obj_t* label_alarm_time;
        lv_obj_t* label_gradual_wake;
        lv_obj_t* label_total_sleep;
        lv_obj_t* label_sleep_cycles;
        lv_obj_t *btnSuggestedAlarm, *txtSuggestedAlarm, *iconSuggestedAlarm;

        lv_obj_t *btnWakeMode, *btnCycles, *btnTestMotorGradual, *lblMotorStrength, *btnMotorStrength, *btnPushesToStop;

        lv_obj_t *btnHeartRateTracking, *btnBodyTracking, *btnTrackerInterval, *btnMotionInterval;

        Widgets::PageIndicator pageIndicatorSettings = Widgets::PageIndicator(0, 4);
        Widgets::PageIndicator pageIndicatorSensors = Widgets::PageIndicator(1, 4);
        Widgets::PageIndicator pageIndicatorAlarm = Widgets::PageIndicator(2, 4);
        Widgets::PageIndicator pageIndicatorInfo = Widgets::PageIndicator(3, 4);
      };
    }

    template <>
    struct AppTraits<Apps::Sleep> {
      static constexpr Apps app = Apps::Sleep;
      static constexpr const char* icon = Screens::Symbols::bed;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::Sleep(controllers.infiniSleepController,
                                  controllers.settingsController.GetClockType(),
                                  *controllers.systemTask,
                                  controllers.motorController,
                                  *controllers.displayApp);
      }

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      };
    };
  }
}