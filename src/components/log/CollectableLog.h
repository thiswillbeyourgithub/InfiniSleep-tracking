#pragma once

#include <cstdint>

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include "components/fs/FS.h"

namespace Pinetime {
  namespace Controllers {

    /// A ring of records that a companion application collects and then releases.
    ///
    /// Written once and shared, because the watch keeps more than one log of this shape: the
    /// activity the sleep tracker records, and the events the wearer logs by hand. Both want a
    /// fixed ring in RAM, the oldest dropped when it fills, mirrored to a file so a reboot in the
    /// middle of the night does not throw the night away, records handed out oldest first, and
    /// the space reclaimed only once a host says it stored them. What a record holds, and what
    /// "everything up to here" means when a host acknowledges, belong to whichever log it is.
    ///
    /// The one thing every layout has to agree to is the first two bytes of a record: a 14 bit
    /// delta in minutes since a shared base, and two bits the layout may use for whatever it
    /// likes. The timestamp is the field worth compressing, since every record in a ring is
    /// within days of every other, and holding it here is what lets this hand out timestamps and
    /// reclaim by them.
    ///
    /// Nothing here locks on behalf of a caller that is already inside the lock, so every
    /// protected member assumes the lock is held and every public one takes it.
    class CollectableLog {
    public:
      /// @param filePath where the mirror lives, which each log names for itself
      /// @param fileFormatVersion bumped by a log whenever its stored layout changes
      /// @param storage where the records live, owned by whoever derives from this
      /// @param budget how many bytes of storage there are
      /// @param recordSize the stride, which a log with one layout never changes
      CollectableLog(Controllers::FS& fs,
                     const char* filePath,
                     uint8_t fileFormatVersion,
                     uint8_t* storage,
                     uint16_t budget,
                     uint8_t recordSize);
      virtual ~CollectableLog() = default;

      CollectableLog(const CollectableLog&) = delete;
      CollectableLog& operator=(const CollectableLog&) = delete;

      /// Must be called once the scheduler and the filesystem are up, before anything else.
      void Init();

      /// Drops everything. Meant for a factory reset or a settings toggle, not for the normal
      /// collection path, which is whatever the log calls its release.
      void Clear();

      /// Writes the log to flash if anything changed since the last time, and does nothing
      /// otherwise.
      ///
      /// This is separate from the methods that change the log because the caller is the only one
      /// that knows whether the flash can be reached at all. The PineTime powers the external
      /// flash down and disables the SPI peripheral while the watch sleeps, and a write in that
      /// state blocks forever on a DMA completion that never comes, taking the SPI mutex and
      /// therefore the display with it. So the ring in RAM is the authority and this is a mirror,
      /// taken whenever the watch is awake anyway.
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

      /// Ticks since that acknowledgement, meaningless unless HasBeenCollected(). Measured on the
      /// tick counter rather than the clock because the question it answers is how long ago, and
      /// the clock can be corrected by the very host being waited on.
      uint32_t TicksSinceCollection() const {
        return xTaskGetTickCount() - collectedAtTicks;
      }

      /// Number of records currently held.
      uint16_t RecordCount() const;

      /// How many records fit as things stand, which a log with two layouts answers differently
      /// depending on which one it is in.
      uint16_t Capacity() const;

      /// Oldest and newest timestamps held, both 0 when there are no records.
      uint32_t OldestTimestamp() const;
      uint32_t NewestTimestamp() const;

    protected:
      /// What AppendSlot() returns when the record cannot be held.
      static constexpr uint16_t noSlot = 0xFFFF;

      static constexpr uint8_t deltaBits = 14;
      static constexpr uint16_t deltaMax = (1 << deltaBits) - 1;

      /// The widest record any of these logs holds, which is only here to size the scratch a
      /// rotation swaps through. Raise it when a log needs more, and nothing else changes.
      static constexpr uint8_t maxRecordSize = 8;

      uint16_t Slots() const;
      uint16_t Count() const {
        return count;
      }
      uint8_t RecordSize() const {
        return recordSize;
      }
      uint8_t* Buffer() {
        return storage;
      }

      /// The bytes of one stored record. Valid for 0 <= offset < Count().
      uint8_t* Slot(uint16_t offset);
      const uint8_t* Slot(uint16_t offset) const;

      /// The two bits of a record that belong to the layout rather than to the delta.
      uint8_t TagAt(uint16_t offset) const;
      void SetTagAt(uint16_t offset, uint8_t tag);

      uint32_t TimestampAt(uint16_t offset) const;

      /// Makes room for one record at a time and returns the offset to write the rest of it into,
      /// or noSlot when it cannot be held. The first two bytes are written here, with the tag
      /// cleared; the bytes after them belong to the caller.
      ///
      /// @param requireNewMinute rejects a record that would round onto the one before it, which
      ///        a log sampled on a timer wants, since two records in a minute would break the
      ///        ordering a host pages through with, and a log of what the wearer did does not,
      ///        since two things can happen in the same minute.
      uint16_t AppendSlot(uint32_t timestamp, bool requireNewMinute);

      /// Both take a count the caller has already checked against Count().
      void DropOldest(uint16_t records);
      void DropNewest(uint16_t records);

      /// Moves the oldest record to slot zero, so that a walk over the ring is a walk over the
      /// buffer. Only needed when the layout is about to change under the records, since
      /// everything else here is happy to index through the rotation.
      void Rotate();

      /// Changes the stride alone, for a log deciding which of its layouts a file holds. The
      /// records are the file's to fill in, so this is not a change to the ring and does not
      /// dirty it.
      void SetRecordSize(uint8_t newRecordSize);

      /// Changes the stride, for a log that has more than one layout. The caller has already
      /// rewritten the records it is keeping into the new layout, starting at the front of the
      /// buffer, which is what Rotate() is for.
      void Relayout(uint8_t newRecordSize, uint16_t records);

      void MarkDirty() {
        dirty = true;
      }

      /// Stamped before a read rather than after, and whatever the result, because the question it
      /// answers is whether the request arrived at all, which an empty log does not change.
      void MarkRead() const;
      void MarkCollected();

      /// Whether a record size read out of a file is one this log can hold, adopting it if so.
      /// Only a log with more than one layout has any reason to override this.
      virtual bool AdoptRecordSize(uint8_t size);

      /// Called with the lock held once the ring has been emptied, for a log that keeps something
      /// of its own alongside the records.
      virtual void OnCleared() {
      }

      bool Lock() const;
      void Unlock() const;

    private:
      /// Moves the base forward to the oldest record held, so the deltas start from zero again.
      /// Assumes there is at least one record.
      void Rebase();

      void ReverseSlots(uint16_t from, uint16_t to);

      /// Both assume the caller holds the lock.
      void LoadFromFile();
      void SaveToFile() const;

      Controllers::FS& fs;
      const char* const filePath;
      const uint8_t fileFormatVersion;

      uint8_t* const storage;
      const uint16_t budget;

      /// The stride this log was built with, which a failed load falls back to.
      const uint8_t initialRecordSize;
      uint8_t recordSize;

      /// What the deltas are measured from, in Unix epoch seconds. Set to the timestamp of the
      /// first record added to an empty ring.
      uint32_t base = 0;

      /// Position of the oldest record.
      uint16_t head = 0;
      uint16_t count = 0;

      /// Set by anything that changes the ring, cleared by a successful Flush().
      bool dirty = false;

      /// When a host last said it stored what it was given. Deliberately not saved with the
      /// records: after a reboot the watch has no idea how much time has passed, and reporting a
      /// stale figure as if it were current is worse than reporting nothing.
      bool collected = false;
      uint32_t collectedAtTicks = 0;

      /// When a host last asked for records. Mutable because being read is not a change to the
      /// log, and the read side is const for good reason: it hands out copies.
      mutable bool read = false;
      mutable uint32_t readAtTicks = 0;

      /// Guards everything above. Records are added from whichever task drives the log while a
      /// host reads on the BLE task, and a torn read here would be stored by the host as a real
      /// measurement.
      SemaphoreHandle_t mutex = nullptr;
    };
  }
}
