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
#include "components/pomodoro/PomodoroController.h"
#include "components/motor/MotorController.h"
#include "task.h"
#include <cstring>
#include <libraries/log/nrf_log.h>

using namespace Pinetime::Controllers;

namespace {
  /* The presets cycled through by swiping left and right on the keypad, as minute queues. Add your
   * own here; the only constraint is that they parse, so digits and ',' up to the queue length. */
  constexpr const char* presets[] = {"2,10", "10,2", "20,5"};
  constexpr uint8_t presetCount = sizeof(presets) / sizeof(presets[0]);

  /* A xorshift32 rather than <random>, because the only consumer is the vibration pattern and
   * std::mt19937 would cost a couple of kilobytes of state and flash for no added value here. */
  uint32_t randomState = 1;

  uint32_t NextRandom(uint32_t bound) {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState % bound;
  }

  void IntervalTimerCallback(TimerHandle_t xTimer) {
    auto* controller = static_cast<PomodoroController*>(pvTimerGetTimerID(xTimer));
    controller->OnIntervalElapsed();
  }

  void RingTimerCallback(TimerHandle_t xTimer) {
    auto* controller = static_cast<PomodoroController*>(pvTimerGetTimerID(xTimer));
    controller->OnRingTick();
  }

  void BurstTimerCallback(TimerHandle_t xTimer) {
    auto* controller = static_cast<PomodoroController*>(pvTimerGetTimerID(xTimer));
    controller->OnBurstTick();
  }
}

PomodoroController::PomodoroController(Controllers::FS& fs,
                                       Controllers::MotorController& motorController,
                                       void* alertData,
                                       void (*alertHandler)(void*))
  : fs {fs}, motorController {motorController}, alertData {alertData}, alertHandler {alertHandler} {
  intervalTimer = xTimerCreate("PomodoroInterval", 1, pdFALSE, this, IntervalTimerCallback);
  ringTimer = xTimerCreate("PomodoroRing", pdMS_TO_TICKS(ringTickMs), pdTRUE, this, RingTimerCallback);
  burstTimer = xTimerCreate("PomodoroBurst", 1, pdFALSE, this, BurstTimerCallback);
}

void PomodoroController::Init() {
  // Any tick count will do as a seed: the patterns only need to differ from one alert to the next.
  randomState = xTaskGetTickCount() | 1;

  lfs_file_t pomodoroFile;
  PomodoroSettings buffer;

  if (fs.FileOpen(&pomodoroFile, "/.system/pomodoro.dat", LFS_O_RDONLY) != LFS_ERR_OK) {
    NRF_LOG_WARNING("[PomodoroController] Failed to open pomodoro data file");
    return;
  }

  fs.FileRead(&pomodoroFile, reinterpret_cast<uint8_t*>(&buffer), sizeof(buffer));
  fs.FileClose(&pomodoroFile);
  if (buffer.version != pomodoroFormatVersion) {
    NRF_LOG_WARNING("[PomodoroController] Loaded pomodoro settings has version %u instead of %u, discarding",
                    buffer.version,
                    pomodoroFormatVersion);
    return;
  }

  // A corrupt file must not leave an unterminated string behind for the screen to print.
  buffer.queue[maxQueueLength] = '\0';
  if (buffer.vibrationsPerAlarm < 1 || buffer.vibrationsPerAlarm > maxVibrations) {
    NRF_LOG_WARNING("[PomodoroController] Loaded vibration count out of range, discarding");
    return;
  }

  settings = buffer;
  NRF_LOG_INFO("[PomodoroController] Loaded pomodoro settings from file");
}

void PomodoroController::SaveSettings() {
  if (!settingsChanged) {
    return;
  }

  lfs_dir systemDir;
  if (fs.DirOpen("/.system", &systemDir) != LFS_ERR_OK) {
    fs.DirCreate("/.system");
  }
  fs.DirClose(&systemDir);

  lfs_file_t pomodoroFile;
  if (fs.FileOpen(&pomodoroFile, "/.system/pomodoro.dat", LFS_O_WRONLY | LFS_O_CREAT) != LFS_ERR_OK) {
    NRF_LOG_WARNING("[PomodoroController] Failed to open pomodoro data file for saving");
    return;
  }

  fs.FileWrite(&pomodoroFile, reinterpret_cast<const uint8_t*>(&settings), sizeof(settings));
  fs.FileClose(&pomodoroFile);
  settingsChanged = false;
  NRF_LOG_INFO("[PomodoroController] Saved pomodoro settings with format version %u to file", settings.version);
}

void PomodoroController::AppendDigit(char digit) {
  size_t length = std::strlen(settings.queue);
  if (length >= maxQueueLength) {
    return;
  }
  settings.queue[length] = digit;
  settings.queue[length + 1] = '\0';
  settingsChanged = true;
}

void PomodoroController::AppendSeparator() {
  size_t length = std::strlen(settings.queue);
  // A leading or doubled ',' would produce an empty interval, which ParseQueue rejects anyway.
  if (length == 0 || length >= maxQueueLength || settings.queue[length - 1] == ',') {
    return;
  }
  settings.queue[length] = ',';
  settings.queue[length + 1] = '\0';
  settingsChanged = true;
}

void PomodoroController::DeleteLastCharacter() {
  size_t length = std::strlen(settings.queue);
  if (length == 0) {
    return;
  }
  settings.queue[length - 1] = '\0';
  settingsChanged = true;
}

void PomodoroController::SetQueue(const char* queue) {
  std::strncpy(settings.queue, queue, maxQueueLength);
  settings.queue[maxQueueLength] = '\0';
  settingsChanged = true;
}

void PomodoroController::LoadNextPreset() {
  presetIndex = (presetIndex + 1) % presetCount;
  SetQueue(presets[presetIndex]);
}

void PomodoroController::LoadPreviousPreset() {
  presetIndex = (presetIndex + presetCount - 1) % presetCount;
  SetQueue(presets[presetIndex]);
}

void PomodoroController::IncreaseVibrations() {
  settings.vibrationsPerAlarm = (settings.vibrationsPerAlarm % maxVibrations) + 1;
  settingsChanged = true;
}

void PomodoroController::DecreaseVibrations() {
  settings.vibrationsPerAlarm = settings.vibrationsPerAlarm > 1 ? settings.vibrationsPerAlarm - 1 : maxVibrations;
  settingsChanged = true;
}

bool PomodoroController::ParseQueue() {
  uint8_t count = 0;
  uint32_t value = 0;
  bool digitSeen = false;

  for (const char* c = settings.queue; *c != '\0'; c++) {
    if (*c == ',') {
      if (!digitSeen || count >= maxIntervals) {
        return false;
      }
      intervals[count++] = value;
      value = 0;
      digitSeen = false;
      continue;
    }
    if (*c < '0' || *c > '9') {
      return false;
    }
    // Clamp rather than overflow: 9999 minutes is already far past any sane pomodoro.
    value = value * 10 + static_cast<uint32_t>(*c - '0');
    if (value > 9999) {
      value = 9999;
    }
    digitSeen = true;
  }

  // Catches both an empty queue and a trailing ',', the two cases the "Go" button must refuse.
  if (!digitSeen || count >= maxIntervals) {
    return false;
  }
  intervals[count++] = value;
  intervalCount = count;
  return true;
}

bool PomodoroController::Start() {
  if (!ParseQueue()) {
    return false;
  }
  currentInterval = 0;
  vibrationsTotal = 0;
  StartCurrentInterval(true);
  SaveSettings();
  return true;
}

void PomodoroController::StartCurrentInterval(bool firstRun) {
  StopVibration();
  xTimerStop(ringTimer, 0);
  state = State::Running;

  uint32_t seconds = static_cast<uint32_t>(intervals[currentInterval]) * 60;
  /* On repeats the time spent vibrating can be taken out of the interval so that the alerts do not
   * slowly drift later and later. Disabled by default, as in the wasp-os app. */
  if (!firstRun && discountVibrationTime && seconds > settings.vibrationsPerAlarm) {
    seconds -= settings.vibrationsPerAlarm;
  }
  // A queue entry of 0 minutes still has to advance, otherwise the timer would never fire.
  if (seconds < 1) {
    seconds = 1;
  }

  xTimerChangePeriod(intervalTimer, pdMS_TO_TICKS(seconds * 1000), 0);
  xTimerStart(intervalTimer, 0);
}

void PomodoroController::Stop() {
  state = State::Stopped;
  xTimerStop(intervalTimer, 0);
  xTimerStop(ringTimer, 0);
  StopVibration();
  currentInterval = 0;
  vibrationsTotal = 0;
}

void PomodoroController::AddMinute() {
  if (state != State::Running) {
    return;
  }
  // xTimerChangePeriod restarts the timer, so the new period has to be the whole remaining time.
  TickType_t remaining = xTimerGetExpiryTime(intervalTimer) - xTaskGetTickCount();
  xTimerChangePeriod(intervalTimer, remaining + pdMS_TO_TICKS(60 * 1000), 0);
  motorController.SetMotorStrength(100);
  motorController.RunForDuration(50);
}

uint32_t PomodoroController::SecondsRemaining() const {
  if (xTimerIsTimerActive(intervalTimer) != pdTRUE) {
    return 0;
  }
  TickType_t remaining = xTimerGetExpiryTime(intervalTimer) - xTaskGetTickCount();
  return remaining / configTICK_RATE_HZ;
}

uint32_t PomodoroController::GetCompletedCycles() const {
  if (intervalCount == 0) {
    return 0;
  }
  return vibrationsTotal / settings.vibrationsPerAlarm / intervalCount;
}

void PomodoroController::OnIntervalElapsed() {
  state = State::Ringing;
  // Wake the watch and bring the app up before the first buzz, so the user sees which timer fired.
  alertHandler(alertData);
  xTimerStart(ringTimer, 0);
  // The periodic timer only fires a second from now, so the first vibration is issued by hand.
  OnRingTick();
}

void PomodoroController::OnRingTick() {
  StartVibration();
  vibrationsTotal++;
  if (vibrationsTotal % settings.vibrationsPerAlarm != 0) {
    return;
  }
  // Vibrated the full count for this alert, so the alert is over.
  if (GetCompletedCycles() < repeatMax) {
    currentInterval = (currentInterval + 1) % intervalCount;
    StartCurrentInterval(false);
  } else {
    // Give up rather than keep a forgotten pomodoro vibrating for days.
    Stop();
  }
}

void PomodoroController::StopAlerting() {
  if (state != State::Ringing) {
    return;
  }
  /* Skip the vibrations left in this alert rather than simply stopping the motor, so that the
   * cycle counter stays in step with a queue that ran to completion. */
  vibrationsTotal += settings.vibrationsPerAlarm - (vibrationsTotal % settings.vibrationsPerAlarm);
  if (GetCompletedCycles() < repeatMax) {
    currentInterval = (currentInterval + 1) % intervalCount;
    StartCurrentInterval(false);
  } else {
    Stop();
  }
}

void PomodoroController::StartVibration() {
  /* Short alerts are easy to miss, so they get one long unmistakable buzz. Longer ones are
   * randomised instead: a pattern that changes every second is much harder to tune out than the
   * same pulse over and over, which is the whole point of the vibration count setting. */
  if (settings.vibrationsPerAlarm <= 3) {
    motorController.SetMotorStrength(100);
    motorController.RunForDuration(650);
    return;
  }

  if (NextRandom(100) >= 70) {
    // Roughly a third of the time, one long buzz of varying strength instead of a burst. The range
    // matches what MotorController already uses for the wake alarm, so it is known to be felt.
    motorController.SetMotorStrength(40 + NextRandom(61));
    motorController.RunForDuration(burstWindowMs);
    return;
  }

  burstElapsedMs = 0;
  OnBurstTick();
}

void PomodoroController::OnBurstTick() {
  uint16_t pulseMs = 20 + NextRandom(181);
  uint16_t gapMs = NextRandom(301);
  /* Stop as soon as the next pulse would not fit, rather than drawing new pairs until one does.
   * The burst then always ends inside its window and never bleeds into the following second. */
  if (static_cast<uint32_t>(burstElapsedMs) + pulseMs + gapMs > burstWindowMs) {
    return;
  }

  motorController.SetMotorStrength(100);
  motorController.RunForDuration(pulseMs);
  burstElapsedMs += pulseMs + gapMs;

  // Reschedule for the end of this pulse plus its gap, which is when the next one is due.
  xTimerChangePeriod(burstTimer, pdMS_TO_TICKS(pulseMs + gapMs), 0);
  xTimerStart(burstTimer, 0);
}

void PomodoroController::StopVibration() {
  /* Only the burst schedule needs cancelling: a pulse started by RunForDuration always ends itself
   * within its own duration, so there is no motor left running to turn off here. */
  xTimerStop(burstTimer, 0);
  burstElapsedMs = burstWindowMs;
}
