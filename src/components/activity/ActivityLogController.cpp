#include "components/activity/ActivityLogController.h"

#include <algorithm>
#include <cstring>
#include <libraries/log/nrf_log.h>

using namespace Pinetime::Controllers;

namespace {
  /// Little endian by hand rather than by cast, so the file stays readable whatever the compiler
  /// thinks the alignment of a byte buffer allows.
  uint16_t ReadU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
  }

  void WriteU16(uint8_t* p, uint16_t value) {
    p[0] = value & 0xFF;
    p[1] = value >> 8;
  }
}

ActivityLogController::ActivityLogController(Controllers::FS& fs)
  : CollectableLog {fs, filePath, fileFormatVersion, storage, ramBudget, narrowRecordSize} {
}

bool ActivityLogController::AdoptRecordSize(uint8_t size) {
  if (size != narrowRecordSize && size != wideRecordSize) {
    return false;
  }
  SetRecordSize(size);
  return true;
}

void ActivityLogController::OnCleared() {
  SetRecordSize(narrowRecordSize);
}

ActivityRecord ActivityLogController::At(uint16_t offset) const {
  const uint8_t* slot = Slot(offset);

  ActivityRecord record;
  record.timestamp = TimestampAt(offset);
  record.motion = RecordSize() == wideRecordSize ? ReadU16(slot + 3) : ActivityRecord::motionNotMeasured;
  record.heartRate = slot[2];
  record.kind = static_cast<ActivityKind>(TagAt(offset));
  return record;
}

void ActivityLogController::Store(uint16_t offset, const ActivityRecord& record) {
  SetTagAt(offset, static_cast<uint8_t>(record.kind));
  uint8_t* slot = Slot(offset);
  slot[2] = record.heartRate;
  if (RecordSize() == wideRecordSize) {
    WriteU16(slot + 3, record.motion);
  }
}

void ActivityLogController::Widen() {
  // Something measured motion, so every record from now on has to carry it, and fewer of them fit.
  // The records already held are rewritten into the wider layout rather than thrown away, saying
  // "not measured" as they always did.
  Rotate();

  const uint16_t slots = ramBudget / wideRecordSize;
  const uint16_t kept = std::min<uint16_t>(Count(), slots);
  if (kept < Count()) {
    NRF_LOG_WARNING("[ActivityLog] Dropping %u records that no longer fit now motion is stored", Count() - kept);
    // Moved to the front first, so the walk below only ever writes ahead of what it has left to
    // read. Backwards, since a wide record reaches further into the buffer than the narrow one it
    // is built from.
    std::memmove(Buffer(), &Buffer()[(Count() - kept) * narrowRecordSize], kept * narrowRecordSize);
  }
  for (uint16_t offset = kept; offset > 0; offset--) {
    const uint8_t* from = &Buffer()[(offset - 1) * narrowRecordSize];
    const uint16_t packed = ReadU16(from);
    const uint8_t heartRate = from[2];
    uint8_t* to = &Buffer()[(offset - 1) * wideRecordSize];
    WriteU16(to, packed);
    to[2] = heartRate;
    WriteU16(to + 3, ActivityRecord::motionNotMeasured);
  }

  Relayout(wideRecordSize, kept);
  NRF_LOG_INFO("[ActivityLog] Now storing motion, %u records fit", slots);
}

void ActivityLogController::Add(const ActivityRecord& record) {
  Lock();

  if (record.motion != ActivityRecord::motionNotMeasured && RecordSize() == narrowRecordSize) {
    Widen();
  }

  // One a minute at most: two records in the same minute would round onto each other, and the read
  // side pages through by timestamp.
  const uint16_t offset = AppendSlot(record.timestamp, true);
  if (offset != noSlot) {
    Store(offset, record);
  }

  Unlock();
}

void ActivityLogController::DropSince(uint32_t sinceTimestamp) {
  Lock();

  uint16_t dropped = 0;
  while (dropped < Count() && TimestampAt(Count() - 1 - dropped) >= sinceTimestamp) {
    dropped++;
  }
  if (dropped > 0) {
    DropNewest(dropped);
    NRF_LOG_INFO("[ActivityLog] Dropped %u records from %u, %u left", dropped, sinceTimestamp, Count());
  }

  Unlock();
}

void ActivityLogController::Remark(uint32_t sinceTimestamp, ActivityKind from, ActivityKind to) {
  Lock();

  // Walked from the newest end and stopped at the first record that is too old, as DropSince does,
  // since the records are ordered and the stretch being corrected is always the tail.
  uint16_t changed = 0;
  for (uint16_t offset = Count(); offset > 0; offset--) {
    if (TimestampAt(offset - 1) < sinceTimestamp) {
      break;
    }
    if (TagAt(offset - 1) == static_cast<uint8_t>(from)) {
      SetTagAt(offset - 1, static_cast<uint8_t>(to));
      changed++;
    }
  }

  if (changed > 0) {
    MarkDirty();
    NRF_LOG_INFO("[ActivityLog] Remarked %u records from %u", changed, sinceTimestamp);
  }

  Unlock();
}

uint16_t ActivityLogController::RecordCount() const {
  return CollectableLog::RecordCount();
}

uint32_t ActivityLogController::OldestTimestamp() const {
  return CollectableLog::OldestTimestamp();
}

uint32_t ActivityLogController::NewestTimestamp() const {
  return CollectableLog::NewestTimestamp();
}

uint8_t ActivityLogController::ReadRecords(uint32_t sinceTimestamp, ActivityRecord* out, uint8_t maxRecords) const {
  if (out == nullptr || maxRecords == 0) {
    return 0;
  }

  Lock();
  MarkRead();

  uint8_t written = 0;
  for (uint16_t offset = 0; offset < Count() && written < maxRecords; offset++) {
    if (TimestampAt(offset) > sinceTimestamp) {
      out[written] = At(offset);
      written++;
    }
  }

  Unlock();
  return written;
}

void ActivityLogController::Release(uint32_t upToTimestamp) {
  Lock();

  uint16_t released = 0;
  while (released < Count() && TimestampAt(released) <= upToTimestamp) {
    released++;
  }
  // Marked rather than written: a release arrives on the BLE host task, and a host may well sync
  // while the watch is asleep and the flash is powered down. Losing the acknowledgement to a reboot
  // before the next flush only costs a re-send of records the host already has, which it merges by
  // timestamp.
  DropOldest(released);
  MarkCollected();
  NRF_LOG_INFO("[ActivityLog] Released %u records up to %u, %u left", released, upToTimestamp, Count());

  Unlock();
}
