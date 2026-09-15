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

#include <FreeRTOS.h>
#include <timers.h>
#include <cstdint>
#include "components/fs/FS.h"

namespace Pinetime {
  namespace Controllers {
    class MotorController;

    /* Pomodoro: a queue of timers that chain into each other forever.
     *
     * Ported from the wasp-os app of the same name. The state machine lives here rather than in
     * Screens::Pomodoro because InfiniTime destroys a Screen as soon as the user navigates away,
     * while a pomodoro is expected to keep running in the background and to pull its own screen
     * back up when an interval elapses. Everything the screen shows is therefore read from here.
     *
     * Three states, cycling: Stopped (the user is typing a queue) -> Running (an interval is
     * counting down) -> Ringing (the watch vibrates once a second for VibrationsPerAlarm seconds)
     * -> Running on the next entry of the queue, and so on until Stop() or repeatMax cycles.
     */
    class PomodoroController {
    public:
      enum class State : uint8_t { Stopped, Running, Ringing };

      /* alertHandler is called from the FreeRTOS timer task when an interval elapses, so that the
       * owner (DisplayApp) can wake the screen and switch to the Pomodoro app. A plain function
       * pointer is used rather than a reference to DisplayApp because DisplayApp owns this object,
       * and an include the other way round would be circular. */
      PomodoroController(Controllers::FS& fs, Controllers::MotorController& motorController, void* alertData, void (*alertHandler)(void*));

      PomodoroController(const PomodoroController&) = delete;
      PomodoroController& operator=(const PomodoroController&) = delete;
      PomodoroController(PomodoroController&&) = delete;
      PomodoroController& operator=(PomodoroController&&) = delete;

      /* Loads the saved queue and vibration count. Must be called once the filesystem is mounted,
       * which is why it is not done in the constructor. */
      void Init();
      void SaveSettings();

      State GetState() const {
        return state;
      }

      // --- Queue editing, only meaningful while Stopped ---

      /* The queue as the user typed it, for example "25,5". Kept as the string rather than as a
       * parsed array so that a half-typed entry ("25," or "2") has an obvious representation, and
       * so that persisting it is a straight copy. */
      const char* GetQueue() const {
        return settings.queue;
      }

      void AppendDigit(char digit);
      /* Appends the ',' that separates two intervals. Does nothing if that would produce an empty
       * field, which keeps the queue parseable at all times. */
      void AppendSeparator();
      void DeleteLastCharacter();
      void LoadNextPreset();
      void LoadPreviousPreset();

      uint8_t GetVibrationsPerAlarm() const {
        return settings.vibrationsPerAlarm;
      }

      /* Both wrap around within [1, maxVibrations] so the user can reach any value from either
       * direction, as in the wasp-os app. */
      void IncreaseVibrations();
      void DecreaseVibrations();

      // --- Running a pomodoro ---

      /* Parses the queue and starts its first interval. Returns false and changes nothing if the
       * queue is empty or malformed, which is how the "Go" button rejects input such as "25,". */
      bool Start();
      void Stop();
      /* Pushes the current interval one minute further away, without disturbing the queue. */
      void AddMinute();
      /* Ends the current vibration burst early and moves on to the next interval. The wasp-os app
       * had no way out of a ringing alert other than waiting it out; the physical button is wired
       * to this so the watch behaves like every other alerting app in InfiniTime. */
      void StopAlerting();

      uint32_t SecondsRemaining() const;

      uint8_t GetCurrentInterval() const {
        return currentInterval;
      }

      uint8_t GetIntervalCount() const {
        return intervalCount;
      }

      /* How many times the whole queue has been walked through since Start(). */
      uint32_t GetCompletedCycles() const;

      // Called from the FreeRTOS timer task only.
      void OnIntervalElapsed();
      void OnRingTick();
      void OnBurstTick();

    private:
      // Version 255 is reserved so that the field can be widened later, as in AlarmController.
      static constexpr uint8_t pomodoroFormatVersion = 1;

      /* 13 characters is the limit the wasp-os app imposed on the typed queue. It caps both the
       * width of the on-screen string and, with at least one digit per field, the interval count. */
      static constexpr uint8_t maxQueueLength = 13;
      static constexpr uint8_t maxIntervals = (maxQueueLength + 1) / 2;
      static constexpr uint8_t maxVibrations = 15;
      /* Stop after this many walks through the queue, so a pomodoro left running by accident does
       * not vibrate for days. */
      static constexpr uint32_t repeatMax = 99;

      /* When false, the seconds spent vibrating are added on top of each interval rather than
       * taken out of it. This mirrors _TIME_MODE in the wasp-os app, which shipped set this way. */
      static constexpr bool discountVibrationTime = false;

      /* One alert lasts this long in wasp-os: one vibration per second, VibrationsPerAlarm times. */
      static constexpr uint32_t ringTickMs = 1000;
      /* A randomised burst is spread over this much of a ring tick, leaving the rest silent. */
      static constexpr uint32_t burstWindowMs = 900;

      struct PomodoroSettings {
        uint8_t version = pomodoroFormatVersion;
        uint8_t vibrationsPerAlarm = 10;
        char queue[maxQueueLength + 1] = "2,10";
      };

      Controllers::FS& fs;
      Controllers::MotorController& motorController;
      void* alertData;
      void (*alertHandler)(void*);

      PomodoroSettings settings;
      bool settingsChanged = false;

      State state = State::Stopped;
      TimerHandle_t intervalTimer;
      /* Fires once a second while Ringing: one vibration per tick. */
      TimerHandle_t ringTimer;
      /* Self-rescheduling one shot that plays the random pulses making up a single vibration. */
      TimerHandle_t burstTimer;

      uint16_t intervals[maxIntervals] = {};
      uint8_t intervalCount = 0;
      uint8_t currentInterval = 0;
      /* Counts every vibration since Start(), which is what tells us when an alert is over and how
       * many times the queue has been walked. */
      uint32_t vibrationsTotal = 0;
      uint8_t presetIndex = 0;
      uint16_t burstElapsedMs = 0;

      bool ParseQueue();
      void StartCurrentInterval(bool firstRun);
      void StartVibration();
      void StopVibration();
      void SetQueue(const char* queue);
    };
  }
}
