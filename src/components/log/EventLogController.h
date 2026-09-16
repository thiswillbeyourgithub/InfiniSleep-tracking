#pragma once

#include <cstdint>

#include "components/fs/FS.h"
#include "components/log/CollectableLog.h"
#include "components/log/EventLog.h"

namespace Pinetime {
  namespace Controllers {

    /// Holds what the wearer logged until a companion application collects it.
    ///
    /// The ring, the file mirror and the release come from CollectableLog, which the activity log
    /// uses too. What is here is what an event is and how it is numbered.
    class EventLogController : public EventLogProvider, public CollectableLog {
    public:
      /// How much RAM the events are allowed.
      ///
      /// Smaller than the activity log's share because events are something a wearer does, a
      /// handful of times a day, rather than something a timer does all night. At four bytes each
      /// this holds 128 of them, which is a few days of logging without a phone in reach, and a byte
      /// here is a byte LVGL does not get for building screens.
      static constexpr uint16_t ramBudget = 512;

      /// How an event is held in RAM and in the file.
      ///
      /// The first two bytes are CollectableLog's, holding a delta in minutes since a shared base
      /// and two bits for the layout, which here is the type. After them:
      ///
      ///   byte 2  the slot, as the host numbered it
      ///   byte 3  the value in the low four bits, and whether it is flagged in the top bit
      ///
      /// The seconds within the minute are not kept. An event is logged as it happens, and one that
      /// is being logged late is flagged and its time fixed on the phone, so a minute is as precise
      /// as the thing being recorded.
      static constexpr uint8_t eventRecordSize = 4;

      static constexpr uint16_t capacity = ramBudget / eventRecordSize;

      explicit EventLogController(Controllers::FS& fs);

      /// Appends one event, numbering it, and returns its sequence, or 0 when it was not stored.
      ///
      /// Events may arrive as fast as the wearer can tap, several in the same minute, which is why
      /// they are numbered rather than identified by when they happened. One dated earlier than the
      /// newest held is refused, since the ring is ordered.
      uint32_t Add(const LoggedEvent& event);

      /// Whether the newest thing said about a slot was that it started, which is how the app knows
      /// to offer stopping it.
      ///
      /// Read from the events still held, so it is forgotten once they are collected. That is the
      /// deal: the log is the record, and what is running is a convenience derived from it.
      bool IsRunning(uint8_t slot) const;

      uint16_t EventCount() const override;
      uint32_t NewestSequence() const override;
      uint32_t OldestSequence() const override;
      uint8_t ReadEvents(uint32_t sinceSequence, LoggedEvent* out, uint8_t maxEvents) const override;
      void ReleaseEvents(uint32_t upToSequence) override;

    protected:
      uint32_t ExtraToSave() const override;
      void ExtraLoaded(uint32_t extra) override;
      void OnCleared() override;

    private:
      /// Bumped whenever the stored layout changes. A file written by an older firmware is
      /// discarded rather than misread.
      static constexpr uint8_t fileFormatVersion = 1;
      static constexpr const char* filePath = "/.system/eventlog.dat";

      LoggedEvent At(uint16_t offset) const;

      /// What an event at an offset is numbered. The sequences are dense, oldest first, so the
      /// counter and the number held say all of them.
      uint32_t SequenceAt(uint16_t offset) const;

      uint8_t storage[ramBudget] = {};

      /// What the next event will be numbered. Starts at 1, so that 0 can mean "nothing yet" to a
      /// host and "not stored" to a caller, and is kept in the file so a reboot does not renumber
      /// events a host has already acknowledged.
      uint32_t nextSequence = 1;
    };
  }
}
