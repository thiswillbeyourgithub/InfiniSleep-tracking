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
      /// Bytes rather than a struct, because there are two layouts and the watch picks one at
      /// runtime:
      ///
      ///   byte 0, 1  delta in minutes since the base (14 bits), then kind (2 bits)
      ///   byte 2     heart rate in bpm, or 0 for not measured
      ///   byte 3, 4  motion counts, in the wide layout only
      ///
      /// The timestamp is the expensive field and the least surprising one to compress: every
      /// record in the ring is within a few days of every other, so 14 bits of minutes since a
      /// single base replaces four bytes of absolute time. Kind has four values and rides in
      /// the two bits left over.
      ///
      /// Motion is the field a watch may have no use for at all. A PineTime whose accelerometer
      /// never answers records "not measured" for every epoch of every night, and spending two
      /// bytes a record saying so costs a third of the nights the log could hold. So it is only
      /// stored once something measures it, which is decided by the records themselves rather
      /// than by a setting: the log starts narrow and widens for good on the first record that
      /// carries motion. Nothing is quantised either way, so no reading loses precision.
      ///
      /// The stride is fixed within a layout, which the ring needs: it indexes by
      /// multiplication and Release() trims from the front.
      static constexpr uint8_t narrowRecordSize = 3;
      static constexpr uint8_t wideRecordSize = 5;
      static constexpr uint8_t deltaBits = 14;

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

      /// Most records the log can hold, which is what it holds while nothing measures motion.
      /// Derived, so that making a record smaller buys nights rather than free RAM nobody
      /// notices. At eight hours of tracking a night:
      ///   5 minute epoch -> 96 records a night, so about 7 nights
      ///   15 minute epoch -> 32 records a night, so about 21 nights
      /// A watch that does measure motion holds fewer, hence Capacity() rather than a constant.
      static constexpr uint16_t maxCapacity = ramBudget / narrowRecordSize;

      /// How many records fit as things stand, which is maxCapacity until motion turns up.
      uint16_t Capacity() const;

    private:
      /// Bumped whenever the stored layout changes. A file written by an older firmware is
      /// discarded rather than misread. Version 3 is the byte layout above, version 2 was a
      /// 6 byte packed record, version 1 whole ActivityRecords.
      static constexpr uint8_t fileFormatVersion = 3;
      static constexpr const char* filePath = "/.system/activity.dat";

      /// The largest delta a record can hold, which is 11.4 days from the base. A log whose
      /// oldest record is older than that loses its oldest records rather than all of them, so
      /// this bounds how far back the log reaches at the longest tracker intervals.
      static constexpr uint16_t deltaMax = (1 << deltaBits) - 1;

      /// Expands one stored record. Valid for 0 <= offset < count, and by value because there
      /// is no ActivityRecord in memory to point at.
      ActivityRecord At(uint16_t offset) const;
      /// The same expansion for callers that only need the time, which is most of them.
      uint32_t TimestampAt(uint16_t offset) const;

      /// How many records fit, which the caller is expected to already hold the lock for.
      uint16_t Slots() const;

      /// The bytes of one stored record. Valid for 0 <= offset < count.
      uint8_t* Slot(uint16_t offset);
      const uint8_t* Slot(uint16_t offset) const;

      /// The stored delta of one record, in minutes since the base.
      uint16_t DeltaAt(uint16_t offset) const;

      /// Writes one record into a slot, in whichever layout the ring is in.
      void Store(uint16_t offset, uint16_t delta, const ActivityRecord& record);

      /// Moves base forward to the oldest record held, so the deltas start from zero again.
      /// Assumes the caller holds the lock and that there is at least one record.
      void Rebase();

      /// Moves the oldest record to slot zero, so that a walk over the ring is a walk over the
      /// buffer. Only needed when the layout changes under the records, since everything else
      /// here is happy to index through the rotation.
      void Rotate();
      void ReverseSlots(uint16_t from, uint16_t to);

      /// Switches to the wide layout, keeping the newest records that still fit. Called once,
      /// from the first Add() that carries motion.
      void Widen();

      /// Both assume the caller holds the lock.
      void LoadFromFile();
      void SaveToFile() const;

      bool Lock() const;
      void Unlock() const;

      Controllers::FS& fs;

      /// The ring itself, as bytes rather than records, because the stride depends on whether
      /// this watch measures motion at all. Indexed through Slot().
      uint8_t storage[ramBudget] = {};

      /// Which of the two layouts the ring currently holds, narrow until the first record that
      /// carries motion. Saved in the file, so a watch that measures motion does not spend a
      /// widening on every boot.
      uint8_t recordSize = narrowRecordSize;
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
