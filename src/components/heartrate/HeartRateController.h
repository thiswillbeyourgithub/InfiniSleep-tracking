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
      void Update(States newState, uint8_t heartRate, uint8_t uncertainty);

      void SetHeartRateTask(Applications::HeartRateTask* task);

      States State() const {
        return state;
      }

      uint8_t HeartRate() const {
        return heartRate;
      }

      /// Half width in bpm of the spread of the readings behind HeartRate(), or 0 when there is no
      /// reading.
      ///
      /// A measurement publishes a coarse estimate off half a window a few seconds in and tightens
      /// it as full windows arrive and agree, so the number on its own says nothing about how much
      /// signal is behind it. See Ppg::Uncertainty.
      uint8_t Uncertainty() const {
        return uncertainty;
      }

      /// Whether the reading is settled enough to stand on its own, rather than being shown as a
      /// range and kept out of the activity log.
      ///
      /// The bound sits just under what half a window can resolve (Ppg::earlyDataLength, 10 bpm at
      /// the current window lengths), so a coarse estimate never passes for a settled reading, and a
      /// full window reading only passes while the recent windows behind it agree.
      static constexpr uint8_t convergedUncertainty = 9;

      bool IsConverged() const {
        return heartRate > 0 && uncertainty > 0 && uncertainty <= convergedUncertainty;
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
      uint8_t uncertainty = 0;
      bool bleNotificationsEnabled = true;
      Pinetime::Controllers::HeartRateService* service = nullptr;
    };
  }
}