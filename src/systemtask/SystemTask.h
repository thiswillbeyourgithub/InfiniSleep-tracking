#pragma once

#include <memory>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>
#include <timers.h>
#include <heartratetask/HeartRateTask.h>
#include <components/settings/Settings.h>
#include <drivers/Bma421.h>
#include <drivers/PinMap.h>
#include <components/motion/MotionController.h>

#include "systemtask/SystemMonitor.h"
#include "components/ble/NimbleController.h"
#include "components/ble/NotificationManager.h"
#include "components/alarm/AlarmController.h"
#include "components/infinisleep/InfiniSleepController.h"
#include "components/activity/ActivityLogController.h"
#include "components/fs/FS.h"
#include "touchhandler/TouchHandler.h"
#include "buttonhandler/ButtonHandler.h"
#include "buttonhandler/ButtonActions.h"

#ifdef PINETIME_IS_RECOVERY
  #include "displayapp/DisplayAppRecovery.h"
#else
  #include "components/settings/Settings.h"
  #include "displayapp/DisplayApp.h"
#endif

#include "drivers/Watchdog.h"
#include "systemtask/Messages.h"

extern std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds> NoInit_BackUpTime;

namespace Pinetime {
  namespace Drivers {
    class Cst816S;
    class SpiMaster;
    class SpiNorFlash;
    class St7789;
    class TwiMaster;
    class Hrs3300;
  }

  namespace Controllers {
    class Battery;
    class TouchHandler;
    class ButtonHandler;
  }

  namespace System {
    class SystemTask {
    public:
      enum class SystemTaskState { Sleeping, Running, GoingToSleep, AODSleeping };
      SystemTask(Drivers::SpiMaster& spi,
                 Pinetime::Drivers::SpiNorFlash& spiNorFlash,
                 Drivers::TwiMaster& twiMaster,
                 Drivers::Cst816S& touchPanel,
                 Controllers::Battery& batteryController,
                 Controllers::Ble& bleController,
                 Controllers::DateTime& dateTimeController,
                 Controllers::AlarmController& alarmController,
                 Drivers::Watchdog& watchdog,
                 Pinetime::Controllers::NotificationManager& notificationManager,
                 Pinetime::Drivers::Hrs3300& heartRateSensor,
                 Pinetime::Controllers::MotionController& motionController,
                 Pinetime::Drivers::Bma421& motionSensor,
                 Controllers::Settings& settingsController,
                 Pinetime::Controllers::HeartRateController& heartRateController,
                 Pinetime::Applications::DisplayApp& displayApp,
                 Pinetime::Applications::HeartRateTask& heartRateApp,
                 Pinetime::Controllers::FS& fs,
                 Pinetime::Controllers::TouchHandler& touchHandler,
                 Pinetime::Controllers::ButtonHandler& buttonHandler,
                 Pinetime::Controllers::InfiniSleepController& infiniSleepController,
                 Pinetime::Controllers::ActivityLogController& activityLogController);

      void Start();
      void PushMessage(Messages msg);

      bool IsSleepDisabled() {
        return wakeLocksHeld > 0;
      }

      Pinetime::Controllers::NimbleController& nimble() {
        return nimbleController;
      };

      bool IsSleeping() const {
        return state != SystemTaskState::Running;
      }

    private:
      TaskHandle_t taskHandle;

      Pinetime::Drivers::SpiMaster& spi;
      Pinetime::Drivers::SpiNorFlash& spiNorFlash;
      Pinetime::Drivers::TwiMaster& twiMaster;
      Pinetime::Drivers::Cst816S& touchPanel;
      Pinetime::Controllers::Battery& batteryController;

      Pinetime::Controllers::Ble& bleController;
      Pinetime::Controllers::DateTime& dateTimeController;
      Pinetime::Controllers::AlarmController& alarmController;
      QueueHandle_t systemTasksMsgQueue;
      Pinetime::Drivers::Watchdog& watchdog;
      Pinetime::Controllers::NotificationManager& notificationManager;
      Pinetime::Drivers::Hrs3300& heartRateSensor;
      Pinetime::Drivers::Bma421& motionSensor;
      Pinetime::Controllers::Settings& settingsController;
      Pinetime::Controllers::HeartRateController& heartRateController;
      Pinetime::Controllers::MotionController& motionController;

      Pinetime::Applications::DisplayApp& displayApp;
      Pinetime::Applications::HeartRateTask& heartRateApp;
      Pinetime::Controllers::FS& fs;
      Pinetime::Controllers::TouchHandler& touchHandler;
      Pinetime::Controllers::ButtonHandler& buttonHandler;
      Pinetime::Controllers::NimbleController nimbleController;
      Pinetime::Controllers::InfiniSleepController& infiniSleepController;
      Pinetime::Controllers::ActivityLogController& activityLogController;

      static void Process(void* instance);
      void Work();
      bool isBleDiscoveryTimerRunning = false;
      uint8_t bleDiscoveryTimer = 0;
      TimerHandle_t measureBatteryTimer;
      // Initialised, unlike the one above, because BeginActivityEpoch null checks it before
      // use. It is created partway through Work(), so an uninitialised handle here would pass
      // that check with garbage during the whole of startup.
      TimerHandle_t heartRateSettleTimer = nullptr;
      uint8_t wakeLocksHeld = 0;
      SystemTaskState state = SystemTaskState::Running;

      void HandleButtonAction(Controllers::ButtonActions action);
      bool fastWakeUpDone = false;

      void GoToRunning();
      void GoToSleep();
      void UpdateMotion();
      /// True when something that reacts to movement as it happens, a wake gesture or a BLE
      /// subscriber, needs the accelerometer read at the full rate.
      bool MotionNeededAtFullRate() const;
      /// True when the sleep tracker wants the accelerometer kept alive overnight, which needs
      /// both the setting and a sensor that actually answered at boot.
      bool MotionWantedBySleepTracker() const;
      /// How long to wait between accelerometer reads, in ticks.
      TickType_t MotionPollPeriod() const;

      /// Writes the activity log to flash whatever state the watch is in, waking the flash and
      /// the SPI peripheral for the write when they are asleep and putting them back after.
      /// Only worth its cost at a moment the records are about to be lost; the ordinary path is
      /// the flush in the main loop, which waits until the watch is awake anyway.
      void FlushActivityLogFromAnyState();

      /// True when the battery is low enough that sleep tracking should back off. Charging does
      /// not count as low however far down it started, since the cost no longer matters.
      bool IsBatteryLow() const;
      /// So that crossing the threshold can be acted on once, rather than every loop below it.
      bool batteryWasLow = false;
      /// The two sampling rates the tracker actually runs at, which are the user's settings
      /// except when the battery is low, where they are only ever made slower, never faster.
      uint8_t EffectiveTrackerIntervalMinutes() const;
      uint8_t EffectiveMotionSampleIntervalDs() const;

      /// Begins one epoch: turns the heart rate sensor on if it is wanted, otherwise records
      /// straight away. The kind is what the record will claim the wearer was doing, and is the
      /// caller's to decide, since the sleep tracker knows something a background poll does not.
      void BeginActivityEpoch(Controllers::ActivityKind kind, bool wantsHeartRate);
      /// Closes one tracker epoch into the activity log, and turns the sensor back off.
      void RecordActivityEpoch();

      /// Writes one Awake record at the instant a session starts or ends.
      ///
      /// Worth two records a night because of how a host reads the log: Gadgetbridge attributes
      /// the time between two samples to the later one's kind, with no cap on the gap, so the
      /// first Asleep record of the night otherwise drags everything back to the previous sample
      /// into sleep. A record on each side bounds the session to what was actually tracked.
      void RecordSessionBoundary();
      /// The clock as the activity log wants it: UTC seconds since the epoch.
      uint32_t UtcNowSeconds();

      /// Called when the wearer physically acted on the watch, which during a sleep session is
      /// the one thing that separates being awake from lying still. Only deliberate acts count,
      /// so a wrist raise does not: rolling over triggers it, and a night spent recorded as awake
      /// would be worse than one recorded as unbroken sleep.
      ///
      /// Marks time in both directions: forward through the window below on epochs not yet
      /// recorded, and backward by rewriting ones already in the log.
      void NoteWearerAwake();

      /// Rewrites everything recorded since the session started as Awake, for the wearer who
      /// started the tracker and then read for an hour. Nothing the watch can measure tells that
      /// apart from lying still asleep, so it is the wearer's to say, and saying it is the whole
      /// point of the marks page.
      void MarkSessionAwakeSoFar();
      /// Until when epochs are recorded as Awake rather than Asleep, in UTC seconds. Zero when
      /// nothing has happened, which is safe: no timestamp is ever below it.
      uint32_t awakeUntilTimestamp = 0;
      /// How long one look at the watch claims. Generous on purpose: someone who wakes enough to
      /// press a button is rarely asleep again in the minute after, and the sleep this costs is
      /// at most one epoch of it, while the fragmentation it catches is the thing being measured.
      static constexpr uint32_t awakeWindowSeconds = 15 * 60;
      /// How long before the act counts as awake as well, because waking is not instantaneous
      /// and someone who wakes at night tends to lie still a while before reaching for the watch,
      /// in case they fall back asleep. Reaches one epoch further back than it says in practice,
      /// since a host attributes an epoch to the record that closes it, so the record inside the
      /// lead in carries the epoch before it too.
      static constexpr uint32_t awakeLeadInSeconds = 5 * 60;
      /// How close two acts have to be for the stretch between them to count as awake throughout.
      /// Wider than the window ahead, which is what makes it do anything, and well short of the
      /// hour Gadgetbridge splits a session at, so a real stretch of falling back asleep between
      /// two checks is still recorded as the sleep it was.
      static constexpr uint32_t awakeCoalesceSeconds = 30 * 60;
      /// When the wearer last acted on the watch during this session, in UTC seconds, or zero.
      /// Only ever set inside a session, since NoteWearerAwake() returns early outside one.
      uint32_t lastAwakeSignalTimestamp = 0;

      /// When the current sleep session started, in UTC seconds, or zero outside one.
      uint32_t activitySessionStart = 0;
      /// Shorter than this and a session is discarded rather than recorded. Matches the minimum
      /// Gadgetbridge itself applies before it will call a run of samples a sleep session, so
      /// what the watch keeps and what the phone charts agree. Dropping it here also keeps the
      /// ring and the database free of records nothing will ever draw.
      static constexpr uint32_t minimumSessionSeconds = 5 * 60;
      /// True while an epoch is waiting on the heart rate sensor, so a second tracker tick
      /// cannot start a measurement on top of the one already running.
      bool activityEpochMeasuring = false;
      /// True when this class turned the sensor on and therefore owes it a turn off. Never set
      /// while the watch is awake, where the user's own measurement must not be interfered with.
      bool activityEpochOwnsHeartRate = false;
      /// What the epoch in flight will be recorded as, and whether it asked for a heart rate.
      /// Held here because the epoch is closed by a message from a timer, several hundred
      /// milliseconds after the caller that started it has returned.
      Controllers::ActivityKind activityEpochKind = Controllers::ActivityKind::Unknown;
      bool activityEpochWantsHeartRate = false;

      /// Measures heart rate on its own schedule, outside any sleep session, when the wearer
      /// asked for it in the settings. Does nothing while the tracker runs or the screen is on.
      void PollHeartRate();
      /// Starts, stops or repitches the poll timer to match the setting. Cheap and idempotent:
      /// it returns immediately unless the interval actually changed.
      void ApplyHeartRatePollInterval();
      TimerHandle_t heartRatePollTimer = nullptr;
      /// The interval currently loaded into that timer, so a change can be told from a repeat.
      /// Deliberately not initialised to the setting: at boot nothing is armed yet.
      uint8_t heartRatePollPeriodMinutes = 0;

      bool stepCounterMustBeReset = false;
      static constexpr TickType_t batteryMeasurementPeriod = pdMS_TO_TICKS(10 * 60 * 1000);
      /// How long the photoplethysmograph is given to produce a reading before the epoch is
      /// recorded with whatever it has. Long enough that a still wrist usually converges,
      /// short enough that at a fifteen minute epoch the sensor is on about 3% of the night.
      static constexpr TickType_t heartRateSettlePeriod = pdMS_TO_TICKS(30 * 1000);

      /// Below this, and not charging, the tracker trades resolution for the chance of still
      /// being alive in the morning. A tracker that flattens the battery at 4am records a
      /// truncated night and cannot report the wake up it exists to catch.
      static constexpr uint8_t lowBatteryPercentage = 25;
      /// Floors, not values: a user who already asked for something slower keeps it.
      static constexpr uint8_t lowBatteryTrackerIntervalMinutes = 30;
      static constexpr uint8_t lowBatteryMotionSampleIntervalDs = 10;

      SystemMonitor monitor;
    };
  }
}
