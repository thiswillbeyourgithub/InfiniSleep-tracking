#pragma once

#include <cstdint>

namespace Pinetime {
  namespace Controllers {

    /// Whether the hourly (or more frequent) chime should go off now.
    ///
    /// Header only and free of the nRF headers on purpose, so tests/chimes/ can step it through a
    /// whole day on the host. SystemTask gathers the state and asks this once a quarter of an hour,
    /// which is the finest interval offered.
    struct Chimes {
      /// Everything the decision depends on, gathered by the caller.
      struct State {
        /// Minutes between chimes: 0 for off, otherwise 15, 30 or 60.
        uint8_t intervalMinutes;
        /// First and last hour of the day that may chime, both inclusive at their full hour, so
        /// the default 10 to 21 chimes from 10:00 to 21:00 and not at 21:30. A start after the end
        /// is a window across midnight, 22 to 6 chiming through the night. Equal hours chime once.
        uint8_t startHour;
        uint8_t endHour;
        /// The wearer asked the chime to ring even when notifications are turned off.
        bool ignoreDoNotDisturb;
        /// Notifications are turned off (Settings::Notification::Off).
        bool notificationsOff;
        /// Sleep mode (Settings::Notification::Sleep). Always silences, override or not, since a
        /// wearer who said they are asleep does not want waking every half hour.
        bool sleepMode;
        /// InfiniSleep is tracking a night. Always silences, for the same reason as sleep mode.
        bool sleepTracking;
        /// An alarm is ringing, which the chime would interrupt.
        bool alarmRinging;
      };

      static constexpr bool IsChimeMinute(uint8_t intervalMinutes, uint8_t minute) {
        return intervalMinutes != 0 && minute % intervalMinutes == 0;
      }

      static constexpr bool InWindow(uint8_t startHour, uint8_t endHour, uint8_t hour, uint8_t minute) {
        const int now = hour * 60 + minute;
        const int start = startHour * 60;
        const int end = endHour * 60;
        if (start <= end) {
          return now >= start && now <= end;
        }
        return now >= start || now <= end;
      }

      static constexpr bool ShouldChime(const State& state, uint8_t hour, uint8_t minute) {
        if (state.sleepMode || state.sleepTracking || state.alarmRinging) {
          return false;
        }
        if (state.notificationsOff && !state.ignoreDoNotDisturb) {
          return false;
        }
        return IsChimeMinute(state.intervalMinutes, minute) && InWindow(state.startHour, state.endHour, hour, minute);
      }
    };
  }
}
