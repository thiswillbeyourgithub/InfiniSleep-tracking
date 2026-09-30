#pragma once

#include <FreeRTOS.h>
#include <timers.h>

#include <chrono>

namespace Pinetime {
  namespace Controllers {
    class Timer {
    public:
      Timer(void* timerData, TimerCallbackFunction_t timerCallbackFunction);

      void StartTimer(std::chrono::milliseconds duration);

      /* Ends the countdown for good, dropping any pause. */
      void StopTimer();

      /* A pause is kept here rather than in the screen for the same reason lastDuration is: the
       * screen is destroyed the moment the app is left, and a paused timer should still be paused,
       * with the same time left, when the wearer comes back to it. */
      void PauseTimer();

      /* Picks a paused countdown back up with what was left of it. Unlike StartTimer it leaves
       * lastDuration alone, so resetting after a pause goes back to the length the timer was first
       * set to, not to whatever remained when it was paused. */
      void ResumeTimer();

      /* While paused this is the time that was left at the pause, not zero, so the screen can keep
       * showing it. */
      std::chrono::milliseconds GetTimeRemaining();

      /* How long the timer was last started for, which is what the app offers again once there is
       * nothing left to count down. A timer is nearly always set to the same length twice in a row,
       * so dialling it in from zero every time is work the wearer should not have to do.
       *
       * Kept here rather than in the screen because the screen is destroyed the moment the app is
       * left, and this has to outlive that. It is not written to flash, so a reboot forgets it:
       * persisting it would mean growing the settings file, and every stored setting is lost when
       * its version is bumped, which is a steep price for a convenience. */
      std::chrono::milliseconds GetLastDuration() const {
        return lastDuration;
      }

      bool IsRunning();

      bool IsPaused() const {
        return paused;
      }

    private:
      void Run(std::chrono::milliseconds duration);

      TimerHandle_t timer;
      std::chrono::milliseconds lastDuration {0};
      bool paused = false;
      std::chrono::milliseconds pausedRemaining {0};
    };
  }
}
