#pragma once

#include <cstdint>

#include "components/activity/ActivityLog.h"
#include "components/fs/FS.h"
#include "components/log/CollectableLog.h"

namespace Pinetime {
  namespace Controllers {

    /// Holds recorded activity until a companion application collects it.
    ///
    /// The ring, the file mirror and the release once a host has stored what it was given all come
    /// from CollectableLog, which the event log uses too. What is left here is what a record of
    /// activity holds and what may be done to one. Nothing here knows anything about sleep:
    /// whichever tracker is running calls Add() once per epoch, and the BLE service reads through
    /// the ActivityLogProvider interface.
    class ActivityLogController : public ActivityLogProvider, public CollectableLog {
    public:
      /// How much RAM the log is allowed, and the only number here worth arguing about.
      ///
      /// RAM is expensive: heap_4_infinitime hands FreeRTOS everything between __HeapLimit and
      /// __StackLimit, lv_conf sets LV_MEM_CUSTOM, and a byte here is a byte LVGL does not get for
      /// building screens. Raise it only with the linker's RAM figure in front of you, and remember
      /// that running the heap out shows up as a screen refusing to open rather than as a link
      /// error.
      static constexpr uint16_t ramBudget = 2048;

      /// How a record is held in RAM and in the file, which is not how it is handed out.
      ///
      /// The first two bytes are CollectableLog's, holding a delta in minutes since a shared base
      /// and two bits for the layout, which here is the kind. After them:
      ///
      ///   byte 2     heart rate in bpm, or 0 for not measured
      ///   byte 3, 4  motion counts, in the wide layout only
      ///
      /// Motion is the field a watch may have no use for at all. A PineTime whose accelerometer
      /// never answers records "not measured" for every epoch of every night, and spending two
      /// bytes a record saying so costs a third of the nights the log could hold. So it is only
      /// stored once something measures it, which is decided by the records themselves rather than
      /// by a setting: the log starts narrow and widens for good on the first record that carries
      /// motion. Nothing is quantised either way, so no reading loses precision.
      static constexpr uint8_t narrowRecordSize = 3;
      static constexpr uint8_t wideRecordSize = 5;

      /// Most records the log can hold, which is what it holds while nothing measures motion.
      /// Derived, so that making a record smaller buys nights rather than free RAM nobody notices.
      /// At eight hours of tracking a night:
      ///   5 minute epoch -> 96 records a night, so about 7 nights
      ///   15 minute epoch -> 32 records a night, so about 21 nights
      /// A watch that does measure motion holds fewer, hence Capacity() rather than a constant.
      static constexpr uint16_t maxCapacity = ramBudget / narrowRecordSize;

      explicit ActivityLogController(Controllers::FS& fs);

      /// Appends one epoch. Records must arrive in chronological order, and no faster than one a
      /// minute; one that is not newer than the last is dropped, because the read side relies on
      /// the ordering to guarantee a host never skips a record.
      void Add(const ActivityRecord& record);

      /// Drops every record at or after a timestamp, so a session can be taken back after the
      /// fact. Trims from the newest end, which the ring supports as cheaply as a release trims
      /// from the oldest, and does nothing to records a host has already been given: they are gone
      /// from here by then, and a host that has stored them keeps them.
      void DropSince(uint32_t sinceTimestamp);

      /// Rewrites the kind of every record at or after a timestamp that currently reads `from`, so
      /// a stretch the wearer turns out to have been awake for can be corrected once that is known.
      /// Records already handed to a host are gone from here and keep whatever they said.
      ///
      /// Takes both kinds rather than assuming the sleep one, so this stays a log that holds
      /// records and knows nothing about what they mean. Nothing else here is affected: the
      /// timestamps do not move, so the ordering the read side relies on is untouched.
      void Remark(uint32_t sinceTimestamp, ActivityKind from, ActivityKind to);

      uint16_t RecordCount() const override;
      uint32_t OldestTimestamp() const override;
      uint32_t NewestTimestamp() const override;
      uint8_t ReadRecords(uint32_t sinceTimestamp, ActivityRecord* out, uint8_t maxRecords) const override;
      void Release(uint32_t upToTimestamp) override;

    protected:
      /// Both layouts are readable, so a watch that measures motion does not spend a widening, and
      /// the records it saved, on every boot.
      bool AdoptRecordSize(uint8_t size) override;

      /// An empty log has no reason to keep room for a motion column, and the next record that
      /// carries motion widens it again.
      void OnCleared() override;

    private:
      /// Bumped whenever the stored layout changes. A file written by an older firmware is
      /// discarded rather than misread. Version 4 added the field the shared log keeps for a log
      /// of its own, which this one does not use; version 3 is the byte layout above; version 2 was
      /// a 6 byte packed record and version 1 whole ActivityRecords.
      static constexpr uint8_t fileFormatVersion = 4;
      static constexpr const char* filePath = "/.system/activity.dat";

      /// Expands one stored record. Valid for 0 <= offset < the count, and by value because there
      /// is no ActivityRecord in memory to point at.
      ActivityRecord At(uint16_t offset) const;

      /// Writes the bytes of a record that AppendSlot() has already made room for.
      void Store(uint16_t offset, const ActivityRecord& record);

      /// Switches to the wide layout, keeping the newest records that still fit. Called once, from
      /// the first Add() that carries motion.
      void Widen();

      uint8_t storage[ramBudget] = {};
    };
  }
}
