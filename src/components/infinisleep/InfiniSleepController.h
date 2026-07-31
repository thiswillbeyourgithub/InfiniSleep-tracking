#pragma once

#include <FreeRTOS.h>
#include <timers.h>
#include <cstdint>
#include "components/datetime/DateTimeController.h"
#include "components/fs/FS.h"
#include "components/heartrate/HeartRateController.h"
#include "components/alarm/AlarmController.h"

#include <chrono>

#define SNOOZE_MINUTES               3
#define PUSHES_TO_STOP_ALARM         5
#define TRACKER_UPDATE_INTERVAL_MINS 5
#define TRACKER_DATA_FILE_NAME       "SleepTracker_Data.csv"
#define PREV_SESSION_DATA_FILE_NAME  "SleepTracker_PrevSession.csv"
#define SLEEP_CYCLE_DURATION         90 // sleep cycle duration in minutes
#define DESIRED_CYCLES               5  // desired number of sleep cycles
#define PUSHES_TO_STOP_ALARM_TIMEOUT 2  // in seconds
#define SESSION_DATA_VERSION         2  // Version of the session data struct

namespace Pinetime {
  namespace System {
    class SystemTask;
  }

  namespace Controllers {
    namespace InfiniSleepControllerTypes {
      // Struct for sessions
      struct SessionData {
        uint8_t day = 0;
        uint8_t month = 0;
        uint16_t year = 0;

        uint8_t startTimeHours = 0;
        uint8_t startTimeMinutes = 0;
        uint8_t endTimeHours = 0;
        uint8_t endTimeMinutes = 0;

        uint16_t totalSleepMinutes = 0;

        uint32_t startTimeStamp = 0;

        uint8_t version = SESSION_DATA_VERSION;
      };
    }

    class InfiniSleepController {
    public:
      InfiniSleepController(Controllers::DateTime& dateTimeCOntroller,
                            Controllers::FS&,
                            Controllers::HeartRateController& heartRateController,
                            Controllers::BrightnessController& brightnessController);

      void Init(System::SystemTask* systemTask);
      void SaveWakeAlarm();
      void SaveInfiniSleepSettings();
      void SetWakeAlarmTime(uint8_t wakeAlarmHr, uint8_t wakeAlarmMin);
      void ScheduleWakeAlarm();
      void DisableWakeAlarm();
      void EnableWakeAlarm();
      void SetOffWakeAlarmNow();
      void SetOffGradualWakeNow();
      void UpdateGradualWake();
      uint32_t SecondsToWakeAlarm() const;
      void StopAlerting();

      uint8_t pushesLeftToStopWakeAlarm = PUSHES_TO_STOP_ALARM;

      bool isSnoozing = false;
      uint8_t preSnoozeMinutes = 255;
      uint8_t preSnnoozeHours = 255;

      InfiniSleepControllerTypes::SessionData prevSessionData;

      void SetPreSnoozeTime() {
        if (preSnoozeMinutes != 255 || preSnnoozeHours != 255) {
          return;
        }
        preSnoozeMinutes = wakeAlarm.minutes;
        preSnnoozeHours = wakeAlarm.hours;
      }

      void RestorePreSnoozeTime() {
        if (preSnoozeMinutes == 255 || preSnnoozeHours == 255) {
          return;
        }
        wakeAlarm.minutes = preSnoozeMinutes;
        wakeAlarm.hours = preSnnoozeHours;
        preSnoozeMinutes = 255;
        preSnnoozeHours = 255;
      }

      uint8_t Hours() const {
        return wakeAlarm.hours;
      }

      uint8_t Minutes() const {
        return wakeAlarm.minutes;
      }

      bool IsAlerting() const {
        return isAlerting;
      }

      bool IsEnabled() const {
        return isEnabled;
      }

      void EnableTracker();

      /// Retunes the running epoch timer. Used to back the tracker off when the battery gets
      /// low, which SystemTask decides because it is the one holding the battery controller.
      /// A no-op if the timer does not exist yet, so it is safe to call before tracking starts.
      void SetTrackerPeriodMinutes(uint8_t minutes);
      void DisableTracker();
      void UpdateTracker();

      void SetSettingsChanged() {
        settingsChanged = true;
      }

      // Versions 255 is reserved for now, so the version field can be made
      // bigger, should it ever be needed.
      static constexpr uint8_t wakeAlarmFormatVersion = 1;

      static constexpr uint16_t minutesPerDay = 24 * 60;

      struct WakeAlarmSettings {
        static constexpr uint8_t version = wakeAlarmFormatVersion;
        uint8_t hours = 7;
        uint8_t minutes = 0;
        AlarmController::RecurType recurrence = AlarmController::RecurType::Daily;
        bool isEnabled = false;
      };

      WakeAlarmSettings wakeAlarm;

      // Dertermine the steps for the gradual wake alarm, the corresponding vibration durations determine the power of the vibration
      static constexpr uint16_t gradualWakeSteps[9] = {30, 60, 90, 120, 180, 240, 300, 350, 600}; // In seconds

      uint8_t gradualWakeStep = 9; // used to keep track of which step to use, in position form not idex

      uint16_t GetSleepCycles() const {
        return (GetTotalSleep() * 100 / infiniSleepSettings.sleepCycleDuration);
      }

      uint16_t GetTotalSleep() const {
        uint8_t endHours = IsEnabled() ? GetCurrentHour() : prevSessionData.endTimeHours;
        uint8_t endMinutes = IsEnabled() ? GetCurrentMinute() : prevSessionData.endTimeMinutes;

        // Calculate total minutes for start and end times
        uint16_t startTotalMinutes = prevSessionData.startTimeHours * 60 + prevSessionData.startTimeMinutes;
        uint16_t endTotalMinutes = endHours * 60 + endMinutes;

        // If end time is before start time, add 24 hours to end time (handle crossing midnight)
        if (endTotalMinutes < startTotalMinutes) {
          endTotalMinutes += 24 * 60;
        }

        uint16_t sleepMinutes = endTotalMinutes - startTotalMinutes;

        return sleepMinutes;
      }

      uint16_t GetSuggestedSleepTime() const {
        return infiniSleepSettings.desiredCycles * infiniSleepSettings.sleepCycleDuration;
      }

      // Time of day, in minutes since midnight, that is offsetMinutes from now.
      // The whole time of day is carried through a single modulo so that the hour
      // is never dropped when the minutes wrap over the hour or over midnight.
      uint16_t GetTimeOfDayInMinutesFromNow(uint16_t offsetMinutes) const {
        return (GetCurrentHour() * 60 + GetCurrentMinute() + offsetMinutes) % minutesPerDay;
      }

      WakeAlarmSettings GetWakeAlarm() const {
        return wakeAlarm;
      }

      // This struct is written to and read from flash as raw bytes with no version field, so
      // new members may only be appended, never inserted or reordered. A file written by an
      // older firmware is shorter, and the loader leaves whatever it does not cover at the
      // defaults below.
      struct InfiniSleepSettings {
        // Off by default on purpose. The accelerometer in the watch this was developed on is
        // dead, so the motion path is written for watches whose sensor works but has never
        // produced a real number on hardware. Shipping it off means nobody gets a column of
        // numbers nobody has checked, while a user with a working sensor can turn it on from
        // the Sensors page. SystemTask also refuses to run it when no sensor answered at boot,
        // so turning it on cannot cost battery on a watch like this one.
        bool bodyTracking = false;
        bool heartRateTracking = true;
        bool graddualWake = false;
        bool smartAlarm = false;
        uint8_t sleepCycleDuration = SLEEP_CYCLE_DURATION;
        uint8_t desiredCycles = DESIRED_CYCLES;
        uint8_t motorStrength = 100;
        bool naturalWake = false;
        uint8_t pushesToStopAlarm = PUSHES_TO_STOP_ALARM;

        // How often an activity record is written, and so how often the heart rate is measured
        // overnight. Longer is cheaper in battery and covers more of the night in the fixed
        // size activity log; shorter draws a finer curve.
        uint8_t trackerIntervalMinutes = TRACKER_UPDATE_INTERVAL_MINS;

        // How often the accelerometer is read while sleep tracking, in tenths of a second.
        // Only takes effect while the watch is asleep and nothing else needs motion, since
        // wake gestures need the full rate to work at all.
        //
        // Note this changes the scale of the recorded motion counts, which are a sum over the
        // epoch: half the samples, roughly half the count for the same movement. Comparing
        // nights recorded at different rates is not meaningful.
        uint8_t motionSampleIntervalDs = 1;
      };

      // Guard rails for the two above, so a corrupt or truncated settings file cannot leave
      // the tracker with a zero period timer.
      static constexpr uint8_t minTrackerIntervalMinutes = 1;
      static constexpr uint8_t maxTrackerIntervalMinutes = 60;
      static constexpr uint8_t minMotionSampleIntervalDs = 1;
      static constexpr uint8_t maxMotionSampleIntervalDs = 50;

      uint8_t GetTrackerIntervalMinutes() const {
        if (infiniSleepSettings.trackerIntervalMinutes < minTrackerIntervalMinutes) {
          return minTrackerIntervalMinutes;
        }
        if (infiniSleepSettings.trackerIntervalMinutes > maxTrackerIntervalMinutes) {
          return maxTrackerIntervalMinutes;
        }
        return infiniSleepSettings.trackerIntervalMinutes;
      }

      uint8_t GetMotionSampleIntervalDs() const {
        if (infiniSleepSettings.motionSampleIntervalDs < minMotionSampleIntervalDs) {
          return minMotionSampleIntervalDs;
        }
        if (infiniSleepSettings.motionSampleIntervalDs > maxMotionSampleIntervalDs) {
          return maxMotionSampleIntervalDs;
        }
        return infiniSleepSettings.motionSampleIntervalDs;
      }

      InfiniSleepSettings infiniSleepSettings;

      InfiniSleepSettings GetInfiniSleepSettings() const {
        return infiniSleepSettings;
      }

      BrightnessController::Levels prevBrightnessLevel;

      bool ToggleTracker() {
        if (isEnabled) {
          prevSessionData.endTimeHours = GetCurrentHour();
          prevSessionData.endTimeMinutes = GetCurrentMinute();

          // Calculate total sleep time
          uint16_t startTotalMinutes = prevSessionData.startTimeHours * 60 + prevSessionData.startTimeMinutes;
          uint16_t endTotalMinutes = GetCurrentHour() * 60 + GetCurrentMinute();

          // If end time is before start time, add 24 hours to end time (handle crossing midnight)
          if (endTotalMinutes < startTotalMinutes) {
            endTotalMinutes += 24 * 60;
          }

          prevSessionData.totalSleepMinutes = endTotalMinutes - startTotalMinutes;

          SavePrevSessionData();
          DisableTracker();
          // Stopping the tracker means the night is over, which is usually because the wearer
          // woke up before the alarm. Leaving it armed would ring at them once they are up.
          // Done here rather than in the screen so every way of stopping the tracker agrees,
          // and it is harmless on the path where the alarm rang and was already turned off.
          DisableWakeAlarm();
        } else {
          // ClearDataCSV(TRACKER_DATA_FILE_NAME);
          prevSessionData.totalSleepMinutes = 0;
          prevSessionData.endTimeHours = 255;
          prevSessionData.endTimeMinutes = 255;
          prevSessionData.startTimeHours = GetCurrentHour();
          prevSessionData.startTimeMinutes = GetCurrentMinute();
          prevSessionData.day = dateTimeController.Day();
          prevSessionData.month = static_cast<uint8_t>(dateTimeController.Month());
          prevSessionData.year = dateTimeController.Year();
          prevSessionData.startTimeStamp = dateTimeController.CurrentDateTime().time_since_epoch().count();
          EnableTracker();
        }
        return isEnabled;
      }

      bool IsTrackerEnabled() const {
        return isEnabled;
      }

      uint8_t GetCurrentHour() const {
        return dateTimeController.Hours();
      }

      uint8_t GetCurrentMinute() const {
        return dateTimeController.Minutes();
      }

      void UpdateBPM();

      uint8_t GetGradualWakeStep() const {
        return (9 - gradualWakeStep) + 1;
      }

      BrightnessController& GetBrightnessController() {
        return brightnessController;
      }

    private:
      bool isAlerting = false;
      bool isGradualWakeAlerting = false;
      bool wakeAlarmChanged = false;
      bool isEnabled = false;
      bool settingsChanged = false;

      // uint8_t bpm = 0;
      // uint8_t prevBpm = 0;
      // uint8_t rollingBpm = 0;

      Controllers::DateTime& dateTimeController;
      Controllers::FS& fs;
      Controllers::HeartRateController& heartRateController;
      Controllers::BrightnessController& brightnessController;
      System::SystemTask* systemTask = nullptr;
      TimerHandle_t wakeAlarmTimer;
      TimerHandle_t gradualWakeTimer;
      // Created lazily by EnableTracker(), so it must start null for that check to work.
      TimerHandle_t trackerUpdateTimer = nullptr;
      /// The period currently loaded into trackerUpdateTimer, in minutes. Tracked here rather
      /// than read back from the timer so that no FreeRTOS query API is relied on.
      uint8_t trackerPeriodMinutes = 0;
      std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds> wakeAlarmTime;

      void LoadSettingsFromFile();
      void SaveSettingsToFile() const;
      void LoadPrevSessionData();
      void SavePrevSessionData() const;

      // For File IO
      // void WriteDataCSV(const char* fileName, const std::tuple<int, int, int, int, int>* data, int dataSize) const;
      // void ClearDataCSV(const char* fileName) const;
    };
  }

}