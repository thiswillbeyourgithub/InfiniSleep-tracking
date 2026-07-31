#pragma once
#include <cstdint>

namespace Pinetime {
  namespace System {
    enum class Messages : uint8_t {
      GoToSleep,
      GoToRunning,
      OnNewTime,
      OnNewNotification,
      OnNewCall,
      BleConnected,
      BleFirmwareUpdateStarted,
      BleFirmwareUpdateFinished,
      OnTouchEvent,
      HandleButtonEvent,
      HandleButtonTimerEvent,
      OnDisplayTaskSleeping,
      OnDisplayTaskAOD,
      EnableSleeping,
      DisableSleeping,
      OnNewDay,
      OnNewHour,
      OnNewHalfHour,
      OnChargingEvent,
      OnPairing,
      SetOffAlarm,
      SetOffWakeAlarm,
      SetOffGradualWake,
      MeasureBatteryTimerExpired,
      BatteryPercentageUpdated,
      StartFileTransfer,
      StopFileTransfer,
      BleRadioEnableToggle,
      SleepTrackerUpdate,
      // The heart rate sensor has had long enough to settle, so the epoch can be recorded.
      SleepTrackerHeartRateReady,
      // Time for one of the background heart rate measurements the wearer asked for in settings.
      HeartRatePollTimerExpired,
    };
  }
}
