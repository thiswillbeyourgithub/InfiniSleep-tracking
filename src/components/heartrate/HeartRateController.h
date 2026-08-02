#pragma once

#include <cstdint>
#include <components/ble/HeartRateService.h>

namespace Pinetime {
  namespace Applications {
    class HeartRateTask;
  }

  namespace System {
    class SystemTask;
  }

  namespace Controllers {
    class HeartRateController {
    public:
      enum class States { Stopped, NotEnoughData, NoTouch, Running };

      HeartRateController() = default;
      void Start();
      void Stop();
      void Update(States newState, uint8_t heartRate);

      void SetHeartRateTask(Applications::HeartRateTask* task);

      States State() const {
        return state;
      }

      uint8_t HeartRate() const {
        return heartRate;
      }

      void SetService(Pinetime::Controllers::HeartRateService* service);

      /// Whether a new reading is published on the BLE heart rate characteristic.
      ///
      /// The characteristic reports a measurement somebody asked to watch, which is why it
      /// notifies every time the value changes. A measurement the watch started for its own
      /// activity log is not that: it converges through a handful of values over its settle
      /// window, and a subscriber that stores what it is notified ends up with a burst of
      /// samples seconds apart every time the watch polls, none of which anybody asked for.
      /// Those measurements reach the phone through the activity log instead, once, with the
      /// timestamp and the kind the watch actually recorded.
      ///
      /// The reading is still kept either way, since the log reads it back from here.
      void SetBleNotificationsEnabled(bool enabled) {
        bleNotificationsEnabled = enabled;
      }

    private:
      Applications::HeartRateTask* task = nullptr;
      States state = States::Stopped;
      uint8_t heartRate = 0;
      bool bleNotificationsEnabled = true;
      Pinetime::Controllers::HeartRateService* service = nullptr;
    };
  }
}