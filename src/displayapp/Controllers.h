#pragma once

namespace Pinetime {
  namespace Applications {
    class DisplayApp;
  }

  namespace Components {
    class LittleVgl;
  }

  namespace Controllers {
    class Battery;
    class Ble;
    class DateTime;
    class NotificationManager;
    class HeartRateController;
    class Settings;
    class MotorController;
    class MotionController;
    class AlarmController;
    class PomodoroController;
    class InfiniSleepController;
    class ActivityLogController;
    class EventLogController;
    class LogSlots;
    class BrightnessController;
    class SimpleWeatherService;
    class FS;
    class Timer;
    class StopWatchController;
    class MusicService;
    class NavigationService;
  }

  namespace System {
    class SystemTask;
  }

  namespace Applications {
    struct AppControllers {
      const Pinetime::Controllers::Battery& batteryController;
      const Pinetime::Controllers::Ble& bleController;
      Pinetime::Controllers::DateTime& dateTimeController;
      Pinetime::Controllers::NotificationManager& notificationManager;
      Pinetime::Controllers::HeartRateController& heartRateController;
      Pinetime::Controllers::Settings& settingsController;
      Pinetime::Controllers::MotorController& motorController;
      Pinetime::Controllers::MotionController& motionController;
      Pinetime::Controllers::AlarmController& alarmController;
      Pinetime::Controllers::PomodoroController& pomodoroController;
      Pinetime::Controllers::InfiniSleepController& infiniSleepController;
      Pinetime::Controllers::ActivityLogController& activityLogController;
      Pinetime::Controllers::EventLogController& eventLogController;
      Pinetime::Controllers::LogSlots& logSlots;
      Pinetime::Controllers::BrightnessController& brightnessController;
      Pinetime::Controllers::SimpleWeatherService* weatherController;
      Pinetime::Controllers::FS& filesystem;
      Pinetime::Controllers::Timer& timer;
      Pinetime::Controllers::StopWatchController& stopWatchController;
      Pinetime::System::SystemTask* systemTask;
      Pinetime::Applications::DisplayApp* displayApp;
      Pinetime::Components::LittleVgl& lvgl;
      Pinetime::Controllers::MusicService* musicService;
      Pinetime::Controllers::NavigationService* navigationService;
    };
  }
}
