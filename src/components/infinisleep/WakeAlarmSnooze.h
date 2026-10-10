#pragma once

#include <cstdint>

namespace Pinetime {
  namespace Controllers {
    /// The wake up time the wearer set, remembered while a snooze has moved the alarm off it.
    ///
    /// One object rather than a flag beside a remembered time, because those two could disagree.
    /// Stopping the tracker after a snooze cleared neither, so the next evening the remembered
    /// time was still that morning's, and switching the alarm on put it back over the time just
    /// set: 10:00 set by hand became the 8:00 of the night before.
    ///
    /// Its own header, free of the nRF headers InfiniSleepController drags in, so that it can be
    /// checked on a host. The alarm's hours and minutes are passed in rather than held, since they
    /// live in the settings struct that is written to flash as it is.
    class WakeAlarmSnooze {
    public:
      bool IsSnoozing() const {
        return snoozing;
      }

      /// Moves the alarm to the snoozed time. Only the first snooze of a ring remembers the time it
      /// moves the alarm from, since the later ones move it from a snoozed time.
      void Snooze(uint8_t& alarmHours, uint8_t& alarmMinutes, uint8_t hours, uint8_t minutes) {
        if (!snoozing) {
          setHours = alarmHours;
          setMinutes = alarmMinutes;
          snoozing = true;
        }
        alarmHours = hours;
        alarmMinutes = minutes;
      }

      /// The ring is over, however it ended: the alarm goes back to the time the wearer set.
      void End(uint8_t& alarmHours, uint8_t& alarmMinutes) {
        if (!snoozing) {
          return;
        }
        alarmHours = setHours;
        alarmMinutes = setMinutes;
        snoozing = false;
      }

      /// A time set by hand is the wearer's new choice, so whatever a snooze remembered is dropped.
      void SetByHand(uint8_t& alarmHours, uint8_t& alarmMinutes, uint8_t hours, uint8_t minutes) {
        snoozing = false;
        alarmHours = hours;
        alarmMinutes = minutes;
      }

      /// The time worth saving: the one the wearer set, not the one a snooze moved the alarm to.
      uint8_t SetHours(uint8_t alarmHours) const {
        return snoozing ? setHours : alarmHours;
      }

      uint8_t SetMinutes(uint8_t alarmMinutes) const {
        return snoozing ? setMinutes : alarmMinutes;
      }

    private:
      bool snoozing = false;
      uint8_t setHours = 0;
      uint8_t setMinutes = 0;
    };
  }
}
