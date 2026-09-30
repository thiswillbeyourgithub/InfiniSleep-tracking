#include "components/timer/Timer.h"

using namespace Pinetime::Controllers;

Timer::Timer(void* const timerData, TimerCallbackFunction_t timerCallbackFunction) {
  timer = xTimerCreate("Timer", 1, pdFALSE, timerData, timerCallbackFunction);
}

void Timer::StartTimer(std::chrono::milliseconds duration) {
  lastDuration = duration;
  paused = false;
  Run(duration);
}

void Timer::PauseTimer() {
  if (!IsRunning()) {
    return;
  }
  pausedRemaining = GetTimeRemaining();
  xTimerStop(timer, 0);
  paused = true;
}

void Timer::ResumeTimer() {
  if (!paused) {
    return;
  }
  paused = false;
  Run(pausedRemaining);
}

void Timer::Run(std::chrono::milliseconds duration) {
  xTimerChangePeriod(timer, pdMS_TO_TICKS(duration.count()), 0);
  xTimerStart(timer, 0);
}

std::chrono::milliseconds Timer::GetTimeRemaining() {
  if (IsRunning()) {
    TickType_t remainingTime = xTimerGetExpiryTime(timer) - xTaskGetTickCount();
    return std::chrono::milliseconds(remainingTime * 1000 / configTICK_RATE_HZ);
  }
  if (paused) {
    return pausedRemaining;
  }
  return std::chrono::milliseconds(0);
}

void Timer::StopTimer() {
  xTimerStop(timer, 0);
  paused = false;
}

bool Timer::IsRunning() {
  return (xTimerIsTimerActive(timer) == pdTRUE);
}
