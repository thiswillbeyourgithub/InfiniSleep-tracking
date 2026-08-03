#include "systemtask/SystemTask.h"
#include <chrono>
#include <hal/nrf_rtc.h>
#include <libraries/gpiote/app_gpiote.h>
#include <libraries/log/nrf_log.h>
#include "BootloaderVersion.h"
#include "components/battery/BatteryController.h"
#include "components/ble/BleController.h"
#include "displayapp/TouchEvents.h"
#include "drivers/Cst816s.h"
#include "drivers/St7789.h"
#include "drivers/InternalFlash.h"
#include "drivers/SpiMaster.h"
#include "drivers/SpiNorFlash.h"
#include "drivers/TwiMaster.h"
#include "drivers/Hrs3300.h"
#include "drivers/PinMap.h"
#include "main.h"
#include "BootErrors.h"

#include <memory>

using namespace Pinetime::System;

namespace {
  inline bool in_isr() {
    return (SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) != 0;
  }
}

void MeasureBatteryTimerCallback(TimerHandle_t xTimer) {
  auto* sysTask = static_cast<SystemTask*>(pvTimerGetTimerID(xTimer));
  sysTask->PushMessage(Pinetime::System::Messages::MeasureBatteryTimerExpired);
}

void HeartRateSettleTimerCallback(TimerHandle_t xTimer) {
  auto* sysTask = static_cast<SystemTask*>(pvTimerGetTimerID(xTimer));
  sysTask->PushMessage(Pinetime::System::Messages::SleepTrackerHeartRateReady);
}

void HeartRatePollTimerCallback(TimerHandle_t xTimer) {
  auto* sysTask = static_cast<SystemTask*>(pvTimerGetTimerID(xTimer));
  sysTask->PushMessage(Pinetime::System::Messages::HeartRatePollTimerExpired);
}

SystemTask::SystemTask(Drivers::SpiMaster& spi,
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
                       Pinetime::Controllers::ActivityLogController& activityLogController)
  : spi {spi},
    spiNorFlash {spiNorFlash},
    twiMaster {twiMaster},
    touchPanel {touchPanel},
    batteryController {batteryController},
    bleController {bleController},
    dateTimeController {dateTimeController},
    alarmController {alarmController},
    watchdog {watchdog},
    notificationManager {notificationManager},
    heartRateSensor {heartRateSensor},
    motionSensor {motionSensor},
    settingsController {settingsController},
    heartRateController {heartRateController},
    motionController {motionController},
    displayApp {displayApp},
    heartRateApp(heartRateApp),
    fs {fs},
    touchHandler {touchHandler},
    buttonHandler {buttonHandler},
    nimbleController(*this,
                     bleController,
                     dateTimeController,
                     notificationManager,
                     batteryController,
                     spiNorFlash,
                     heartRateController,
                     motionController,
                     activityLogController,
                     fs),
    infiniSleepController {infiniSleepController},
    activityLogController {activityLogController} {
}

void SystemTask::Start() {
  systemTasksMsgQueue = xQueueCreate(10, 1);
  if (pdPASS != xTaskCreate(SystemTask::Process, "MAIN", 350, this, 1, &taskHandle)) {
    APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
  }
}

void SystemTask::Process(void* instance) {
  auto* app = static_cast<SystemTask*>(instance);
  NRF_LOG_INFO("systemtask task started!");
  app->Work();
}

void SystemTask::Work() {
  BootErrors bootError = BootErrors::None;

  watchdog.Setup(7, Drivers::Watchdog::SleepBehaviour::Run, Drivers::Watchdog::HaltBehaviour::Pause);
  watchdog.Start();
  NRF_LOG_INFO("Last reset reason : %s", Pinetime::Drivers::ResetReasonToString(watchdog.GetResetReason()));
  if (!nrfx_gpiote_is_init()) {
    nrfx_gpiote_init();
  }

  spi.Init();
  spiNorFlash.Init();
  spiNorFlash.Wakeup();

  fs.Init();

  // Before the BLE stack, which exposes the log to a host as soon as it is up.
  activityLogController.Init();

  nimbleController.Init();

  twiMaster.Init();
  /*
   * TODO We disable this warning message until we ensure it won't be displayed
   * on legitimate PineTime equipped with a compatible touch controller.
   * (some users reported false positive). See https://github.com/InfiniTimeOrg/InfiniTime/issues/763
  if (!touchPanel.Init()) {
    bootError = BootErrors::TouchController;
  }
   */
  touchPanel.Init();
  dateTimeController.Register(this);
  batteryController.Register(this);
  motionSensor.SoftReset();
  alarmController.Init(this);
  infiniSleepController.Init(this);

  // Reset the TWI device because the motion sensor chip most probably crashed it...
  twiMaster.Sleep();
  twiMaster.Init();

  motionSensor.Init();
  motionController.Init(motionSensor.DeviceType(), motionSensor.GetDiagnostics());
  settingsController.Init();

  displayApp.Register(this);
  displayApp.Register(&nimbleController.weather());
  displayApp.Register(&nimbleController.music());
  displayApp.Register(&nimbleController.navigation());
  displayApp.Start(bootError);

  heartRateSensor.Init();
  heartRateSensor.Disable();
  heartRateApp.Start();

  buttonHandler.Init(this);

  // Setup Interrupts
  nrfx_gpiote_in_config_t pinConfig;
  pinConfig.skip_gpio_setup = false;
  pinConfig.hi_accuracy = false;
  pinConfig.is_watcher = false;

  // Button
  nrf_gpio_cfg_output(PinMap::ButtonEnable);
  nrf_gpio_pin_set(PinMap::ButtonEnable);
  pinConfig.sense = NRF_GPIOTE_POLARITY_TOGGLE;
  pinConfig.pull = NRF_GPIO_PIN_PULLDOWN;
  nrfx_gpiote_in_init(PinMap::Button, &pinConfig, nrfx_gpiote_evt_handler);
  nrfx_gpiote_in_event_enable(PinMap::Button, true);

  // Touchscreen
  pinConfig.sense = NRF_GPIOTE_POLARITY_HITOLO;
  pinConfig.pull = NRF_GPIO_PIN_PULLUP;
  nrfx_gpiote_in_init(PinMap::Cst816sIrq, &pinConfig, nrfx_gpiote_evt_handler);
  nrfx_gpiote_in_event_enable(PinMap::Cst816sIrq, true);

  // Power present
  pinConfig.sense = NRF_GPIOTE_POLARITY_TOGGLE;
  pinConfig.pull = NRF_GPIO_PIN_NOPULL;
  nrfx_gpiote_in_init(PinMap::PowerPresent, &pinConfig, nrfx_gpiote_evt_handler);
  nrfx_gpiote_in_event_enable(PinMap::PowerPresent, true);

  batteryController.MeasureVoltage();

  measureBatteryTimer = xTimerCreate("measureBattery", batteryMeasurementPeriod, pdTRUE, this, MeasureBatteryTimerCallback);
  xTimerStart(measureBatteryTimer, portMAX_DELAY);

  heartRateSettleTimer = xTimerCreate("hrSettle", heartRateSettlePeriod, pdFALSE, this, HeartRateSettleTimerCallback);

  // Created dormant with a placeholder period, since the setting decides both whether it runs at
  // all and how often. ApplyHeartRatePollInterval does that, here and whenever the watch is put
  // down after the setting may have been changed.
  heartRatePollTimer = xTimerCreate("hrPoll", pdMS_TO_TICKS(60 * 1000), pdTRUE, this, HeartRatePollTimerCallback);
  ApplyHeartRatePollInterval();

#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"
  while (true) {
    UpdateMotion();

    // Epochs are recorded while the watch is asleep, when the external flash is powered down
    // and the SPI peripheral disabled, so the ring in RAM is the authority and this mirrors it
    // whenever the watch happens to be awake. AODSleeping does not count: it keeps SPI alive
    // for the display but still sleeps the flash.
    if (state == SystemTaskState::Running && activityLogController.IsDirty()) {
      activityLogController.Flush();
    }

    // The night's records only exist in RAM until the watch is next woken, so a battery that
    // dies before morning takes them with it. This is the one moment where that stops being
    // hypothetical, so it is worth the cost of waking the flash to write them out. Checked here
    // rather than when the battery is measured, because the level is updated asynchronously.
    const bool batteryLow = IsBatteryLow();
    if (batteryLow && !batteryWasLow) {
      NRF_LOG_INFO("[systemtask] Battery is low, saving the activity log while there is power to");
      FlushActivityLogFromAnyState();
    }
    batteryWasLow = batteryLow;

    Messages msg;
    if (xQueueReceive(systemTasksMsgQueue, &msg, MotionPollPeriod()) == pdTRUE) {
      switch (msg) {
        case Messages::EnableSleeping:
          wakeLocksHeld--;
          break;
        case Messages::DisableSleeping:
          GoToRunning();
          wakeLocksHeld++;
          break;
        case Messages::GoToRunning:
          GoToRunning();
          break;
        case Messages::GoToSleep:
          infiniSleepController.pushesLeftToStopWakeAlarm = infiniSleepController.infiniSleepSettings.pushesToStopAlarm;
          GoToSleep();
          break;
        case Messages::OnNewTime:
          if (alarmController.IsEnabled()) {
            alarmController.ScheduleAlarm();
          }
          if (infiniSleepController.GetWakeAlarm().isEnabled) {
            infiniSleepController.ScheduleWakeAlarm();
          }
          break;
        case Messages::OnNewNotification:
          if (settingsController.GetNotificationStatus() == Pinetime::Controllers::Settings::Notification::On) {
            if (IsSleeping()) {
              GoToRunning();
            }
            displayApp.PushMessage(Pinetime::Applications::Display::Messages::NewNotification);
          }
          break;
        case Messages::SetOffAlarm:
          GoToRunning();
          displayApp.PushMessage(Pinetime::Applications::Display::Messages::AlarmTriggered);
          break;
        case Messages::SetOffWakeAlarm:
          // Code the screen trigger here
          GoToRunning();
          displayApp.PushMessage(Pinetime::Applications::Display::Messages::WakeAlarmTriggered);
          break;
        case Messages::SetOffGradualWake:
          // GoToRunning();
          displayApp.PushMessage(Pinetime::Applications::Display::Messages::GradualWakeTriggered);
          break;
        case Messages::BleConnected:
          displayApp.PushMessage(Pinetime::Applications::Display::Messages::NotifyDeviceActivity);
          isBleDiscoveryTimerRunning = true;
          bleDiscoveryTimer = 5;
          break;
        case Messages::BleFirmwareUpdateStarted:
          GoToRunning();
          wakeLocksHeld++;
          displayApp.PushMessage(Pinetime::Applications::Display::Messages::BleFirmwareUpdateStarted);
          break;
        case Messages::BleFirmwareUpdateFinished:
          if (bleController.State() == Pinetime::Controllers::Ble::FirmwareUpdateStates::Validated) {
            NVIC_SystemReset();
          }
          wakeLocksHeld--;
          break;
        case Messages::StartFileTransfer:
          NRF_LOG_INFO("[systemtask] FS Started");
          GoToRunning();
          wakeLocksHeld++;
          // TODO add intent of fs access icon or something
          break;
        case Messages::StopFileTransfer:
          NRF_LOG_INFO("[systemtask] FS Stopped");
          wakeLocksHeld--;
          // TODO add intent of fs access icon or something
          break;
        case Messages::OnTouchEvent:
          // Finish immediately if no new events
          if (!touchHandler.ProcessTouchInfo(touchPanel.GetTouchInfo())) {
            break;
          }
          if (state == SystemTaskState::Running) {
            displayApp.PushMessage(Pinetime::Applications::Display::Messages::TouchEvent);
          } else {
            // If asleep, check for touch panel wake triggers
            auto gesture = touchHandler.GestureGet();
            if (settingsController.GetNotificationStatus() != Controllers::Settings::Notification::Sleep &&
                gesture != Pinetime::Applications::TouchEvents::None &&
                ((gesture == Pinetime::Applications::TouchEvents::DoubleTap &&
                  settingsController.isWakeUpModeOn(Pinetime::Controllers::Settings::WakeUpMode::DoubleTap)) ||
                 (gesture == Pinetime::Applications::TouchEvents::Tap &&
                  settingsController.isWakeUpModeOn(Pinetime::Controllers::Settings::WakeUpMode::SingleTap)))) {
              NoteWearerAwake();
              GoToRunning();
            }
          }
          break;
        case Messages::HandleButtonEvent: {
          Controllers::ButtonActions action = Controllers::ButtonActions::None;
          if (nrf_gpio_pin_read(Pinetime::PinMap::Button) == 0) {
            action = buttonHandler.HandleEvent(Controllers::ButtonHandler::Events::Release);
          } else {
            action = buttonHandler.HandleEvent(Controllers::ButtonHandler::Events::Press);
            // This is for faster wakeup, sacrificing special longpress and doubleclick handling while sleeping
            if (IsSleeping()) {
              fastWakeUpDone = true;
              NoteWearerAwake();
              GoToRunning();
              break;
            }
          }
          HandleButtonAction(action);
        } break;
        case Messages::HandleButtonTimerEvent: {
          auto action = buttonHandler.HandleEvent(Controllers::ButtonHandler::Events::Timer);
          HandleButtonAction(action);
        } break;
        case Messages::OnDisplayTaskSleeping:
        case Messages::OnDisplayTaskAOD:
          // The state was set to GoingToSleep when GoToSleep() was called
          // If the state is no longer GoingToSleep, we have since transitioned back to Running
          // In this case absorb the OnDisplayTaskSleeping/AOD
          // as DisplayApp is about to receive GoToRunning
          if (state != SystemTaskState::GoingToSleep) {
            break;
          }
          if (BootloaderVersion::IsValid()) {
            // First versions of the bootloader do not expose their version and cannot initialize the SPI NOR FLASH
            // if it's in sleep mode. Avoid bricked device by disabling sleep mode on these versions.
            spiNorFlash.Sleep();
          }

          // Must keep SPI awake when still updating the display for always on
          if (msg == Messages::OnDisplayTaskSleeping) {
            spi.Sleep();
          }

          // Double Tap needs the touch screen to be in normal mode
          if (!settingsController.isWakeUpModeOn(Pinetime::Controllers::Settings::WakeUpMode::DoubleTap)) {
            touchPanel.Sleep();
          }

          if (msg == Messages::OnDisplayTaskSleeping) {
            state = SystemTaskState::Sleeping;
          } else {
            state = SystemTaskState::AODSleeping;
          }
          break;
        case Messages::OnNewDay:
          // We might be sleeping (with TWI device disabled.
          // Remember we'll have to reset the counter next time we're awake
          stepCounterMustBeReset = true;
          break;
        case Messages::OnNewHour:
          using Pinetime::Controllers::AlarmController;
          if (settingsController.GetNotificationStatus() != Controllers::Settings::Notification::Sleep &&
              settingsController.GetChimeOption() == Controllers::Settings::ChimesOption::Hours && !alarmController.IsAlerting()) {
            GoToRunning();
            displayApp.PushMessage(Pinetime::Applications::Display::Messages::Chime);
          }
          break;
        case Messages::OnNewHalfHour:
          using Pinetime::Controllers::AlarmController;
          if (settingsController.GetNotificationStatus() != Controllers::Settings::Notification::Sleep &&
              settingsController.GetChimeOption() == Controllers::Settings::ChimesOption::HalfHours && !alarmController.IsAlerting()) {
            GoToRunning();
            displayApp.PushMessage(Pinetime::Applications::Display::Messages::Chime);
          }
          break;
        case Messages::OnChargingEvent:
          batteryController.ReadPowerState();
          GoToRunning();
          break;
        case Messages::MeasureBatteryTimerExpired:
          batteryController.MeasureVoltage();
          break;
        case Messages::BatteryPercentageUpdated:
          nimbleController.NotifyBatteryLevel(batteryController.PercentRemaining());
          break;
        case Messages::OnPairing:
          GoToRunning();
          displayApp.PushMessage(Pinetime::Applications::Display::Messages::ShowPairingKey);
          break;
        case Messages::BleRadioEnableToggle:
          if (settingsController.GetBleRadioEnabled()) {
            nimbleController.EnableRadio();
          } else {
            nimbleController.DisableRadio();
          }
          break;
        case Messages::SleepTrackerUpdate:
          // Reapplied every epoch rather than once when tracking starts, because the battery
          // crosses the threshold during the night, not before it.
          infiniSleepController.SetTrackerPeriodMinutes(EffectiveTrackerIntervalMinutes());
          BeginActivityEpoch(Controllers::ActivityKind::Asleep, infiniSleepController.GetInfiniSleepSettings().heartRateTracking);
          displayApp.PushMessage(Pinetime::Applications::Display::Messages::SleepTrackerUpdate);
          break;
        case Messages::SleepTrackerHeartRateReady:
          RecordActivityEpoch();
          break;
        case Messages::HeartRatePollTimerExpired:
          PollHeartRate();
          break;
        case Messages::SleepTrackerToggled:
          if (infiniSleepController.IsTrackerEnabled()) {
            activitySessionStart = UtcNowSeconds();
            RecordSessionBoundary();
            // A record's timestamp is stored rounded down to the minute, so the boundary just
            // added is usually a few seconds before the session started. Taking the stored value
            // back is what makes a discard below reach it, rather than leaving one record behind
            // claiming a session that was taken away.
            {
              const uint32_t stored = activityLogController.NewestTimestamp();
              if (stored != 0 && stored < activitySessionStart) {
                activitySessionStart = stored;
              }
            }
          } else if (activitySessionStart != 0) {
            if (UtcNowSeconds() - activitySessionStart < minimumSessionSeconds) {
              // Started by accident, or to look at the app, and stopped again straight away. What
              // it recorded is not a short night, it is nothing at all, and left in the log it
              // would be handed to the phone as sleep with no way to tell it apart from the real
              // thing. Nothing else writes to the log during a session, so the whole tail is this
              // session's, boundary record included.
              activityLogController.DropSince(activitySessionStart);
            } else {
              RecordSessionBoundary();
            }
            activitySessionStart = 0;
          }
          break;
        default:
          break;
      }
    }

    if (isBleDiscoveryTimerRunning) {
      if (bleDiscoveryTimer == 0) {
        isBleDiscoveryTimerRunning = false;
        // Services discovery is deferred from 3 seconds to avoid the conflicts between the host communicating with the
        // target and vice-versa. I'm not sure if this is the right way to handle this...
        nimbleController.StartDiscovery();
      } else {
        bleDiscoveryTimer--;
      }
    }

    monitor.Process();
    NoInit_BackUpTime = dateTimeController.CurrentDateTime();
    if (nrf_gpio_pin_read(PinMap::Button) == 0) {
      watchdog.Reload();
    }
  }
#pragma clang diagnostic pop
}

void SystemTask::GoToRunning() {
  if (state == SystemTaskState::Running) {
    return;
  }
  if (state == SystemTaskState::Sleeping || state == SystemTaskState::AODSleeping) {
    // SPI only switched off when entering Sleeping, not AOD or GoingToSleep
    if (state == SystemTaskState::Sleeping) {
      spi.Wakeup();
    }

    // Double Tap needs the touch screen to be in normal mode
    if (!settingsController.isWakeUpModeOn(Pinetime::Controllers::Settings::WakeUpMode::DoubleTap)) {
      touchPanel.Wakeup();
    }

    spiNorFlash.Wakeup();
  }

  displayApp.PushMessage(Pinetime::Applications::Display::Messages::GoToRunning);
  heartRateApp.PushMessage(Pinetime::Applications::HeartRateTask::Messages::WakeUp);

  // The screen is on, so any measurement from here is one the wearer started and can see. Those
  // are the readings the heart rate characteristic exists to publish, so it is turned back on.
  heartRateController.SetBleNotificationsEnabled(true);

  if (bleController.IsRadioEnabled() && !bleController.IsConnected()) {
    nimbleController.RestartFastAdv();
  }

  state = SystemTaskState::Running;
};

void SystemTask::GoToSleep() {
  if (IsSleeping()) {
    return;
  }
  if (IsSleepDisabled()) {
    return;
  }
  NRF_LOG_INFO("[systemtask] Going to sleep");
  if (settingsController.GetAlwaysOnDisplay()) {
    displayApp.PushMessage(Pinetime::Applications::Display::Messages::GoToAOD);
  } else {
    displayApp.PushMessage(Pinetime::Applications::Display::Messages::GoToSleep);
  }
  heartRateApp.PushMessage(Pinetime::Applications::HeartRateTask::Messages::GoToSleep);

  // From here the only measurements are the ones this class starts for the activity log, at an
  // epoch or a background poll. Notifying them would hand a subscriber a burst of readings a
  // second apart every time the watch polls, which Gadgetbridge stores one row each, as activity
  // rather than as sleep, in the middle of a night it is charting. They go into the log instead,
  // which is where a timestamp and a kind can be attached to them. Cleared here rather than
  // around each epoch so that the reading the sensor produces on its way down is covered too.
  heartRateController.SetBleNotificationsEnabled(false);

  // The settings screen can only have been used with the screen on, so this is the first moment
  // a change to the interval can matter, and the last one before the polling actually happens.
  ApplyHeartRatePollInterval();

  state = SystemTaskState::GoingToSleep;
};

bool SystemTask::MotionWantedBySleepTracker() const {
  // Both conditions matter. Without the setting the log would carry a motion figure the user
  // did not ask for, and without the device check a watch whose accelerometer never answered,
  // which is the case on the watch this was written on, would be woken to read it all night
  // and record nothing but "not measured" for the trouble.
  return infiniSleepController.IsTrackerEnabled() && infiniSleepController.GetInfiniSleepSettings().bodyTracking &&
         motionController.DeviceType() != Controllers::MotionController::DeviceTypes::Unknown;
}

bool SystemTask::MotionNeededAtFullRate() const {
  return settingsController.isWakeUpModeOn(Pinetime::Controllers::Settings::WakeUpMode::RaiseWrist) ||
         settingsController.isWakeUpModeOn(Pinetime::Controllers::Settings::WakeUpMode::Shake) ||
         motionController.GetService()->IsMotionNotificationSubscribed();
}

void SystemTask::FlushActivityLogFromAnyState() {
  if (!activityLogController.IsDirty()) {
    return;
  }

  // Writing to flash while the peripherals that reach it are powered down does not fail, it
  // blocks forever on a DMA completion that never comes, holding the SPI mutex the display
  // needs. So they are woken here for the duration and put back exactly as they were found,
  // mirroring GoToRunning() and the OnDisplayTaskSleeping handler. Safe without a wake lock
  // because this runs on the task that would process a wake up message, so no state change can
  // land in the middle of it.
  const bool spiWasAsleep = state == SystemTaskState::Sleeping;
  const bool flashWasAsleep = spiWasAsleep || state == SystemTaskState::AODSleeping;

  if (spiWasAsleep) {
    spi.Wakeup();
  }
  if (flashWasAsleep) {
    spiNorFlash.Wakeup();
  }

  activityLogController.Flush();

  // Same condition the sleep path uses: the earliest bootloaders cannot bring the flash back
  // out of sleep, so on those it is never put to sleep in the first place.
  if (flashWasAsleep && BootloaderVersion::IsValid()) {
    spiNorFlash.Sleep();
  }
  if (spiWasAsleep) {
    spi.Sleep();
  }
}

bool SystemTask::IsBatteryLow() const {
  return !batteryController.IsPowerPresent() && batteryController.PercentRemaining() < lowBatteryPercentage;
}

uint8_t SystemTask::EffectiveTrackerIntervalMinutes() const {
  const uint8_t wanted = infiniSleepController.GetTrackerIntervalMinutes();
  if (IsBatteryLow() && wanted < lowBatteryTrackerIntervalMinutes) {
    return lowBatteryTrackerIntervalMinutes;
  }
  return wanted;
}

uint8_t SystemTask::EffectiveMotionSampleIntervalDs() const {
  const uint8_t wanted = infiniSleepController.GetMotionSampleIntervalDs();
  if (IsBatteryLow() && wanted < lowBatteryMotionSampleIntervalDs) {
    return lowBatteryMotionSampleIntervalDs;
  }
  return wanted;
}

TickType_t SystemTask::MotionPollPeriod() const {
  // ~10 Hz at the 1024 Hz tick, which is what the wake gesture thresholds are tuned for.
  constexpr TickType_t defaultPeriod = 100;

  if (state != SystemTaskState::Sleeping) {
    return defaultPeriod;
  }
  if (MotionNeededAtFullRate()) {
    return defaultPeriod;
  }
  if (!MotionWantedBySleepTracker()) {
    return defaultPeriod;
  }

  // Asleep, with the sleep tracker the only thing that wants motion. Nothing here needs to
  // react quickly, so the sampling rate is the user's to trade against battery.
  return pdMS_TO_TICKS(EffectiveMotionSampleIntervalDs() * 100);
}

void SystemTask::BeginActivityEpoch(Controllers::ActivityKind kind, bool wantsHeartRate) {
  if (activityEpochMeasuring) {
    // The previous epoch is still waiting on the sensor. Skipping is better than stacking:
    // it can only happen if the settle window is longer than the epoch, and the reading in
    // flight is the fresher one anyway.
    return;
  }

  activityEpochKind = kind;
  activityEpochWantsHeartRate = wantsHeartRate;

  // While the watch is awake the heart rate task belongs to whatever the user is doing with
  // it, so it is read but never driven. While the watch is asleep the task has been stopped
  // by GoToSleep and nobody else is using it, which is the only case where taking it over is
  // safe, and also the only case where it would otherwise report nothing all night.
  // No timer means no way to wait for the sensor, so record what is already known rather than
  // handing FreeRTOS a null handle.
  const bool shouldMeasure = wantsHeartRate && state == SystemTaskState::Sleeping && heartRateSettleTimer != nullptr;

  if (!shouldMeasure) {
    RecordActivityEpoch();
    return;
  }

  activityEpochMeasuring = true;
  activityEpochOwnsHeartRate = true;
  heartRateApp.PushMessage(Pinetime::Applications::HeartRateTask::Messages::WakeUp);
  heartRateApp.PushMessage(Pinetime::Applications::HeartRateTask::Messages::StartMeasurement);
  xTimerStart(heartRateSettleTimer, 0);
}

void SystemTask::ApplyHeartRatePollInterval() {
  if (heartRatePollTimer == nullptr) {
    return;
  }

  const uint8_t minutes = settingsController.GetHeartRatePollInterval();
  // Only on a change, because rearming restarts the countdown: applied on every screen off, a
  // watch looked at often would never reach the end of a thirty minute interval.
  if (minutes == heartRatePollPeriodMinutes) {
    return;
  }
  heartRatePollPeriodMinutes = minutes;

  if (minutes == 0) {
    NRF_LOG_INFO("[systemtask] Background heart rate measurement off");
    xTimerStop(heartRatePollTimer, 0);
    return;
  }

  NRF_LOG_INFO("[systemtask] Measuring heart rate every %u minutes", minutes);
  // Ticks per minute times the count, rather than pdMS_TO_TICKS of the whole thing: that macro
  // multiplies by the tick rate in 32 bits, which an hour expressed in milliseconds comes
  // uncomfortably close to overflowing.
  // Changing the period of a dormant timer also starts it, which is what is wanted here.
  xTimerChangePeriod(heartRatePollTimer, pdMS_TO_TICKS(60 * 1000) * minutes, 0);
}

void SystemTask::PollHeartRate() {
  // The sleep tracker records epochs on its own schedule and drives the same sensor. Letting
  // both run would have them fighting over it, and the night is the more important of the two.
  if (infiniSleepController.IsTrackerEnabled()) {
    return;
  }

  // Only with the screen off. Awake, the sensor belongs to whoever opened the heart rate app,
  // and a measurement started behind their back would either be stopped by theirs or stop it.
  // Skipping costs one sample: the timer is periodic and the next one is along shortly.
  if (state != SystemTaskState::Sleeping) {
    return;
  }

  BeginActivityEpoch(Controllers::ActivityKind::Unknown, true);
}

uint32_t SystemTask::UtcNowSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(dateTimeController.UTCDateTime().time_since_epoch()).count();
}

void SystemTask::NoteWearerAwake() {
  if (!infiniSleepController.IsTrackerEnabled()) {
    // Outside a session there is nothing to correct: a background poll is recorded as Unknown
    // precisely because the watch has not been told what the wearer is doing.
    return;
  }
  awakeUntilTimestamp = UtcNowSeconds() + awakeWindowSeconds;
}

void SystemTask::RecordSessionBoundary() {
  Pinetime::Controllers::ActivityRecord record;
  record.timestamp = UtcNowSeconds();
  // Awake, and not a guess: someone who just pressed start or stop is awake. Neither sensor is
  // read, because a boundary marks an instant rather than an epoch, and a heart rate or a motion
  // count attached to it would describe a stretch of time nothing was tracking.
  record.kind = Controllers::ActivityKind::Awake;
  activityLogController.Add(record);

  // The accumulator is emptied so whatever movement it carried from before the session does not
  // land on the first epoch of it, which would report the walk to bed as the first five minutes
  // of sleep.
  motionController.TakeActivityCounts();
}

void SystemTask::RecordActivityEpoch() {
  Pinetime::Controllers::ActivityRecord record;

  record.timestamp = UtcNowSeconds();

  // Asleep for a tracker epoch, since the wearer said so by starting a session. The watch has
  // no basis for a finer claim: actigraphy and an occasional heart rate cannot separate sleep
  // stages, and guessing awake from asleep within a session would be a guess presented as a
  // measurement. A background poll carries Unknown for the same reason, in the other
  // direction: nobody told the watch what the wearer is doing and it cannot tell.
  record.kind = activityEpochKind;

  // Except when the wearer said otherwise by picking the watch up. That is the one thing during
  // a session the watch does know, and without it a night checked at three in the morning is
  // charted as unbroken sleep.
  if (record.kind == Controllers::ActivityKind::Asleep && record.timestamp < awakeUntilTimestamp) {
    record.kind = Controllers::ActivityKind::Awake;
  }

  if (activityEpochWantsHeartRate) {
    // The state matters as much as the value. HeartRateController holds its last reading
    // indefinitely, so without this an epoch where the sensor never converged would be
    // recorded with a heart rate from hours earlier, indistinguishable from a real one.
    const uint8_t heartRate = heartRateController.HeartRate();
    if (heartRateController.State() == Controllers::HeartRateController::States::Running && heartRate > 0) {
      record.heartRate = heartRate;
    }
  }

  // Taken unconditionally so the accumulator does not carry movement from an epoch that was
  // not recorded into one that is.
  const uint16_t motion = motionController.TakeActivityCounts();
  if (MotionWantedBySleepTracker()) {
    record.motion = motion;
  }

  // A background poll where nothing converged is not worth a record: it would carry a timestamp
  // and two "not measured" fields, and take a slot in the ring from a reading that has something
  // in it. Tracker epochs are kept either way, since their timestamps map out the session.
  const bool measuredSomething = record.heartRate != Pinetime::Controllers::ActivityRecord::heartRateNotMeasured ||
                                 record.motion != Pinetime::Controllers::ActivityRecord::motionNotMeasured;
  if (activityEpochKind != Controllers::ActivityKind::Unknown || measuredSomething) {
    activityLogController.Add(record);
  }

  if (activityEpochOwnsHeartRate) {
    // Only hand the sensor back if the watch is still asleep. The user may have picked it up
    // during the settle window, in which case the heart rate app may now be driving the sensor
    // itself and stopping it here would kill a measurement they are watching.
    if (state == SystemTaskState::Sleeping) {
      heartRateApp.PushMessage(Pinetime::Applications::HeartRateTask::Messages::StopMeasurement);
      heartRateApp.PushMessage(Pinetime::Applications::HeartRateTask::Messages::GoToSleep);
    }
    activityEpochOwnsHeartRate = false;
  }
  activityEpochMeasuring = false;
}

void SystemTask::UpdateMotion() {
  // Only consider disabling motion updates specifically in the Sleeping state
  // AOD needs motion on to show up to date step counts
  // The sleep tracker needs it too, and it runs precisely while the watch is asleep
  if (state == SystemTaskState::Sleeping && !MotionNeededAtFullRate() && !MotionWantedBySleepTracker()) {
    return;
  }

  if (stepCounterMustBeReset) {
    motionSensor.ResetStepCounter();
    stepCounterMustBeReset = false;
  }

  auto motionValues = motionSensor.Process();

  motionController.Update(motionValues.x, motionValues.y, motionValues.z, motionValues.steps);

  if (settingsController.GetNotificationStatus() != Controllers::Settings::Notification::Sleep) {
    if ((settingsController.isWakeUpModeOn(Pinetime::Controllers::Settings::WakeUpMode::RaiseWrist) &&
         motionController.ShouldRaiseWake()) ||
        (settingsController.isWakeUpModeOn(Pinetime::Controllers::Settings::WakeUpMode::Shake) &&
         motionController.ShouldShakeWake(settingsController.GetShakeThreshold()))) {
      GoToRunning();
    }
  }
  if (settingsController.isWakeUpModeOn(Pinetime::Controllers::Settings::WakeUpMode::LowerWrist) && state == SystemTaskState::Running &&
      motionController.ShouldLowerSleep()) {
    GoToSleep();
  }
}

void SystemTask::HandleButtonAction(Controllers::ButtonActions action) {
  if (IsSleeping()) {
    return;
  }

  displayApp.PushMessage(Pinetime::Applications::Display::Messages::NotifyDeviceActivity);

  using Actions = Controllers::ButtonActions;

  switch (action) {
    case Actions::Click:
      // If the first action after fast wakeup is a click, it should be ignored.
      if (!fastWakeUpDone) {
        displayApp.PushMessage(Applications::Display::Messages::ButtonPushed);
      }
      break;
    case Actions::DoubleClick:
      displayApp.PushMessage(Applications::Display::Messages::ButtonDoubleClicked);
      break;
    case Actions::LongPress:
      displayApp.PushMessage(Applications::Display::Messages::ButtonLongPressed);
      break;
    case Actions::LongerPress:
      displayApp.PushMessage(Applications::Display::Messages::ButtonLongerPressed);
      break;
    default:
      return;
  }

  fastWakeUpDone = false;
}

void SystemTask::PushMessage(System::Messages msg) {
  if (in_isr()) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(systemTasksMsgQueue, &msg, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  } else {
    xQueueSend(systemTasksMsgQueue, &msg, portMAX_DELAY);
  }
}
