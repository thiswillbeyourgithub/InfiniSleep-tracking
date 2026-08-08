#pragma once

#include <cstdint>

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include "components/activity/ActivityLog.h"
#include "components/fs/FS.h"

namespace Pinetime {
  namespace Controllers {

    /// Holds recorded activity until a companion application collects it.
    ///
    /// A fixed size ring in RAM, oldest dropped when it fills, mirrored to a file so a reboot
    /// in the middle of the night does not throw the night away. Nothing here knows anything
    /// about sleep: whichever tracker is running calls Add() once per epoch, and the BLE
    /// service reads through the ActivityLogProvider interface.
    class ActivityLogController : public ActivityLogProvider {
    private:
      /// How a record is held in RAM and in the file, which is not how it is handed out.
      ///
      /// The timestamp is the expensive field and the least surprising one to compress: every
      /// record in the ring is within a few days of every other, so they are stored as minutes
      /// since a single base rather than as four bytes of absolute time each. That is 6 bytes
      /// instead of 8, which is a third more nights held for the same RAM.
      ///
      /// The cost is that a record is handed back rounded down to the minute relative to the
      /// base. Nothing here samples faster than once a minute, so no two records can collapse
      /// into one, and Add() rejects it as out of order if they ever do.
      ///
      /// Deliberately not variable length. Compacting a record with no motion, or one whose
      /// spacing is the expected one, would save more, but the ring indexes by multiplication
      /// and Release() trims from the front, so both need a fixed stride.
      struct PackedRecord {
        uint16_t deltaMinutes;
        uint16_t motion;
        uint8_t heartRate;
        uint8_t kind;
      };
      static_assert(sizeof(PackedRecord) == 6, "PackedRecord is expected to pack into 6 bytes");

    public:
      explicit ActivityLogController(Controllers::FS& fs);

      /// Must be called once the scheduler and the filesystem are up, before any other method.
      void Init();

      /// Appends one epoch. Records must arrive in chronological order; one that is not newer
      /// than the last is dropped, because the read side relies on the ordering to guarantee a
      /// host never skips a record.
      void Add(const ActivityRecord& record);

      /// Drops everything. Meant for a factory reset or a settings toggle, not for the normal
      /// collection path, which is Release().
      void Clear();

      /// Drops every record at or after a timestamp, so a session can be taken back after the
      /// fact. Trims from the newest end, which the ring supports as cheaply as Release() trims
      /// from the oldest, and does nothing to records a host has already been given: they are
      /// gone from here by then, and a host that has stored them keeps them.
      void DropSince(uint32_t sinceTimestamp);

      /// Rewrites the kind of every record at or after a timestamp that currently reads `from`,
      /// so a stretch the wearer turns out to have been awake for can be corrected once that is
      /// known. Records already handed to a host are gone from here and keep whatever they said.
      ///
      /// Takes both kinds rather than assuming the sleep one, so this stays a log that holds
      /// records and knows nothing about what they mean. Nothing else here is affected: the
      /// timestamps do not move, so the ordering the read side relies on is untouched.
      void Remark(uint32_t sinceTimestamp, ActivityKind from, ActivityKind to);

      /// Writes the log to flash if anything changed since the last time, and does nothing
      /// otherwise.
      ///
      /// This is separate from the methods that change the log because the caller is the only
      /// one that knows whether the flash can be reached at all. The PineTime powers the
      /// external flash down and disables the SPI peripheral while the watch sleeps, and a
      /// write in that state blocks forever on a DMA completion that never comes, taking the
      /// SPI mutex and therefore the display with it. So the ring in RAM is the authority and
      /// this is a mirror, taken whenever the watch is awake anyway.
      void Flush();

      /// True when the RAM ring holds something the file does not.
      bool IsDirty() const {
        return dirty;
      }

      /// True once a host has asked for records since boot, whether or not there were any.
      ///
      /// Worth its two words of RAM because it splits a failed sync in half from the watch's own
      /// screen: a host that never asked is a discovery or a write that is not arriving, and one
      /// that asked but never acknowledged is an answer that is not getting back.
      bool HasBeenRead() const {
        return read;
      }

      uint32_t TicksSinceRead() const {
        return xTaskGetTickCount() - readAtTicks;
      }

      /// True once a host has acknowledged records since boot, so "nothing has been collected"
      /// can be told from "everything was collected and this is what came after".
      bool HasBeenCollected() const {
        return collected;
      }

      /// Ticks since that acknowledgement, meaningless unless HasBeenCollected(). Measured on
      /// the tick counter rather than the clock because the question it answers is how long ago,
      /// and the clock can be corrected by the very host being waited on.
      uint32_t TicksSinceCollection() const {
        return xTaskGetTickCount() - collectedAtTicks;
      }

      uint16_t RecordCount() const override;
      uint32_t OldestTimestamp() const override;
      uint32_t NewestTimestamp() const override;
      uint8_t ReadRecords(uint32_t sinceTimestamp, ActivityRecord* out, uint8_t maxRecords) const override;
      void Release(uint32_t upToTimestamp) override;

      /// How much RAM the log is allowed, and the only number here worth arguing about.
      ///
      /// That RAM is expensive: heap_4_infinitime hands FreeRTOS everything between __HeapLimit
      /// and __StackLimit, and lv_conf sets LV_MEM_CUSTOM, so every byte here is a byte LVGL
      /// does not get for building screens. Raise it only with the linker's RAM figure in front
      /// of you, and remember that running the heap out shows up as a screen refusing to open
      /// rather than as a link error.
      static constexpr uint16_t ramBudget = 2048;

      /// Derived, so that making the record smaller buys nights rather than free RAM nobody
      /// notices. At eight hours of tracking a night, the current 6 byte record gives:
      ///   5 minute epoch  -> 96 records a night, so about 3.5 nights
      ///   15 minute epoch -> 32 records a night, so about 10 nights
      static constexpr uint16_t capacity = ramBudget / sizeof(PackedRecord);

    private:
      /// Bumped whenever the stored layout changes. A file written by an older firmware is
      /// discarded rather than misread. Version 2 is the packed record below; version 1 stored
      /// whole ActivityRecords.
      static constexpr uint8_t fileFormatVersion = 2;
      static constexpr const char* filePath = "/.system/activity.dat";

      /// The largest delta a record can hold, which is 45 days from the base.
      static constexpr uint16_t deltaMax = 0xFFFF;

      /// Expands one stored record. Valid for 0 <= offset < count, and by value because there
      /// is no ActivityRecord in memory to point at.
      ActivityRecord At(uint16_t offset) const;
      /// The same expansion for callers that only need the time, which is most of them.
      uint32_t TimestampAt(uint16_t offset) const;

      /// Moves base forward to the oldest record held, so the deltas start from zero again.
      /// Assumes the caller holds the lock and that there is at least one record.
      void Rebase();

      /// Both assume the caller holds the lock.
      void LoadFromFile();
      void SaveToFile() const;

      bool Lock() const;
      void Unlock() const;

      Controllers::FS& fs;

      PackedRecord records[capacity];
      /// What the deltas are measured from, in Unix epoch seconds. Set to the timestamp of the
      /// first record added to an empty ring.
      uint32_t base = 0;
      /// Position of the oldest record.
      uint16_t head = 0;
      uint16_t count = 0;
      /// Set by anything that changes the ring, cleared by a successful Flush().
      bool dirty = false;

      /// When a host last said it had stored what it was given. Deliberately not saved with the
      /// records: after a reboot the watch has no idea how much time passed, and reporting a
      /// stale figure as if it were current is worse than reporting nothing.
      bool collected = false;
      uint32_t collectedAtTicks = 0;

      /// When a host last asked for records. Mutable because being read is not a change to the
      /// log, and ReadRecords() is const for the good reason that it hands out copies.
      mutable bool read = false;
      mutable uint32_t readAtTicks = 0;

      /// Guards the three members above. Add() runs on the task driving the tracker while
      /// ReadRecords() runs on the BLE host task, and a torn read here would be stored by the
      /// host as a real measurement.
      SemaphoreHandle_t mutex = nullptr;
    };
  }
}
