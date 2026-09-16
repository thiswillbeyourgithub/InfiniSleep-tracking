#pragma once

#include <cstdint>

namespace Pinetime {
  namespace Controllers {

    /// What kind of thing the wearer logged.
    ///
    /// Started and Stopped are two independent events against the same slot rather than one event
    /// with a length: the watch would otherwise have to hold a session open across reboots, flat
    /// batteries and stops that never come, and pairing them is something the host can do after
    /// the fact and change its mind about.
    enum class LoggedEventType : uint8_t {
      /// It happened. "I took my medication".
      Punctual = 0,
      /// Something that lasts began.
      Started = 1,
      /// It ended.
      Stopped = 2,
      /// It happened and carries a reading, 0 to 10, for the likes of pain or mood.
      Valued = 3,
    };

    /// One thing the wearer logged.
    ///
    /// What a slot means is the host's business: the watch is handed a table of them to show and
    /// logs the id, so renaming one, reordering them or regrouping them changes nothing here.
    struct LoggedEvent {
      /// Unix epoch seconds, UTC, rounded down to the minute by the log.
      uint32_t timestamp = 0;

      /// Which slot this is, as the host numbered it.
      uint8_t slot = 0;

      LoggedEventType type = LoggedEventType::Punctual;

      /// 0 to 10, or valueNotAsked for a slot that does not ask.
      uint8_t value = valueNotAsked;

      /// The wearer wants to come back to this one, usually because it is being logged later than
      /// it happened and the time needs fixing on the phone.
      bool flagged = false;

      /// Assigned by the log, and what a host acknowledges. Ignored on the way in.
      uint32_t sequence = 0;

      static constexpr uint8_t valueNotAsked = 0x0F;
      static constexpr uint8_t maxValue = 10;
    };

    /// Where the BLE service gets the events from.
    ///
    /// Kept this narrow on purpose, as the activity log's provider is: the transport has no opinion
    /// about what is being logged, which is what lets it be reviewed and merged on its own.
    ///
    /// Acknowledged by sequence rather than by timestamp, because two things can happen in the same
    /// minute and the watch stores minutes. The sequence is the log's own counter, kept across
    /// reboots, so a host is always talking about the same events the watch is.
    class EventLogProvider {
    public:
      virtual ~EventLogProvider() = default;

      /// Number of events currently held.
      virtual uint16_t EventCount() const = 0;

      /// The sequence of the newest event held, and of the oldest, both 0 when there are none.
      virtual uint32_t NewestSequence() const = 0;
      virtual uint32_t OldestSequence() const = 0;

      /// Copies up to maxEvents events with a sequence greater than sinceSequence into out, oldest
      /// first, and returns how many were written. Fewer than maxEvents means the caller has
      /// reached the end of what is stored.
      virtual uint8_t ReadEvents(uint32_t sinceSequence, LoggedEvent* out, uint8_t maxEvents) const = 0;

      /// The host states it has durably stored everything up to and including upToSequence, so the
      /// space may be reclaimed.
      virtual void ReleaseEvents(uint32_t upToSequence) = 0;
    };
  }
}
