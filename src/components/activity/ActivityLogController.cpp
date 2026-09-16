#include "components/activity/ActivityLogController.h"

#include <algorithm>
#include <cstring>
#include <libraries/log/nrf_log.h>

using namespace Pinetime::Controllers;

namespace {
  /// Precedes the records in the file. The count is stored rather than derived from the file
  /// size so a short write, or a stale tail left by a previous longer file, cannot be read
  /// back as records. The base has to be here too, since the records are meaningless without it.
  struct FileHeader {
    uint8_t version;
    uint8_t recordSize;
    uint16_t count;
    uint32_t base;
  };

  constexpr uint32_t secondsPerMinute = 60;

  /// The two bits of a stored record that hold the kind rather than the delta.
  constexpr uint16_t kindMask = 0xC000;

  /// Little endian by hand rather than by cast, so the file stays readable whatever the
  /// compiler thinks the alignment of a byte buffer allows.
  uint16_t ReadU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
  }

  void WriteU16(uint8_t* p, uint16_t value) {
    p[0] = value & 0xFF;
    p[1] = value >> 8;
  }
}

ActivityLogController::ActivityLogController(Controllers::FS& fs) : fs {fs} {
}

void ActivityLogController::Init() {
  if (mutex == nullptr) {
    mutex = xSemaphoreCreateMutex();
  }

  Lock();
  LoadFromFile();
  Unlock();
}

bool ActivityLogController::Lock() const {
  // Before Init() there is no other task to race against, so carrying on unlocked is correct
  // rather than merely convenient.
  return mutex != nullptr && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE;
}

void ActivityLogController::Unlock() const {
  if (mutex != nullptr) {
    xSemaphoreGive(mutex);
  }
}

uint16_t ActivityLogController::Slots() const {
  return ramBudget / recordSize;
}

uint16_t ActivityLogController::Capacity() const {
  Lock();
  const uint16_t result = Slots();
  Unlock();
  return result;
}

uint8_t* ActivityLogController::Slot(uint16_t offset) {
  return &storage[((head + offset) % Slots()) * recordSize];
}

const uint8_t* ActivityLogController::Slot(uint16_t offset) const {
  return &storage[((head + offset) % Slots()) * recordSize];
}

uint16_t ActivityLogController::DeltaAt(uint16_t offset) const {
  return ReadU16(Slot(offset)) & deltaMax;
}

void ActivityLogController::Store(uint16_t offset, uint16_t delta, const ActivityRecord& record) {
  uint8_t* slot = Slot(offset);
  WriteU16(slot, static_cast<uint16_t>((delta & deltaMax) | (static_cast<uint16_t>(record.kind) << deltaBits)));
  slot[2] = record.heartRate;
  if (recordSize == wideRecordSize) {
    WriteU16(slot + 3, record.motion);
  }
}

ActivityRecord ActivityLogController::At(uint16_t offset) const {
  const uint8_t* slot = Slot(offset);

  ActivityRecord record;
  record.timestamp = TimestampAt(offset);
  record.motion = recordSize == wideRecordSize ? ReadU16(slot + 3) : ActivityRecord::motionNotMeasured;
  record.heartRate = slot[2];
  record.kind = static_cast<ActivityKind>(ReadU16(slot) >> deltaBits);
  return record;
}

uint32_t ActivityLogController::TimestampAt(uint16_t offset) const {
  return base + static_cast<uint32_t>(DeltaAt(offset)) * secondsPerMinute;
}

void ActivityLogController::Rebase() {
  const uint16_t oldest = DeltaAt(0);
  if (oldest == 0) {
    return;
  }
  base += static_cast<uint32_t>(oldest) * secondsPerMinute;
  for (uint16_t offset = 0; offset < count; offset++) {
    uint8_t* slot = Slot(offset);
    const uint16_t packed = ReadU16(slot);
    WriteU16(slot, static_cast<uint16_t>(((packed & deltaMax) - oldest) | (packed & kindMask)));
  }
}

void ActivityLogController::ReverseSlots(uint16_t from, uint16_t to) {
  uint8_t scratch[wideRecordSize];
  while (from < to) {
    uint8_t* a = &storage[from * recordSize];
    uint8_t* b = &storage[to * recordSize];
    std::memcpy(scratch, a, recordSize);
    std::memcpy(a, b, recordSize);
    std::memcpy(b, scratch, recordSize);
    from++;
    to--;
  }
}

void ActivityLogController::Rotate() {
  if (head == 0) {
    return;
  }
  // A left rotation by head, as three reversals, which needs one record of scratch rather than
  // a second copy of the ring. The unused slots come along for the ride, which is harmless.
  const uint16_t slots = Slots();
  ReverseSlots(0, head - 1);
  ReverseSlots(head, slots - 1);
  ReverseSlots(0, slots - 1);
  head = 0;
}

void ActivityLogController::Widen() {
  // Something measured motion, so every record from now on has to carry it, and fewer of them
  // fit. The records already held are rewritten into the wider layout rather than thrown away,
  // saying "not measured" as they always did.
  Rotate();

  const uint16_t slots = ramBudget / wideRecordSize;
  const uint16_t kept = std::min<uint16_t>(count, slots);
  if (kept < count) {
    NRF_LOG_WARNING("[ActivityLog] Dropping %u records that no longer fit now motion is stored", count - kept);
    // Moved to the front first, so the walk below only ever writes ahead of what it has left to
    // read. Backwards, since a wide record reaches further into the buffer than the narrow one
    // it is built from.
    std::memmove(storage, &storage[(count - kept) * narrowRecordSize], kept * narrowRecordSize);
  }
  for (uint16_t offset = kept; offset > 0; offset--) {
    const uint8_t* from = &storage[(offset - 1) * narrowRecordSize];
    const uint16_t packed = ReadU16(from);
    const uint8_t heartRate = from[2];
    uint8_t* to = &storage[(offset - 1) * wideRecordSize];
    WriteU16(to, packed);
    to[2] = heartRate;
    WriteU16(to + 3, ActivityRecord::motionNotMeasured);
  }

  recordSize = wideRecordSize;
  head = 0;
  count = kept;
  dirty = true;
  NRF_LOG_INFO("[ActivityLog] Now storing motion, %u records fit", slots);
}

void ActivityLogController::Add(const ActivityRecord& record) {
  Lock();

  if (record.motion != ActivityRecord::motionNotMeasured && recordSize == narrowRecordSize) {
    Widen();
  }

  if (count == 0) {
    base = record.timestamp;
  } else if (record.timestamp <= TimestampAt(count - 1)) {
    // The clock moved backwards, or the caller is replaying. Either way, accepting this would
    // break the ordering that lets a host page through with "everything after T" and be sure
    // it has skipped nothing. This also guarantees the subtraction below cannot go negative,
    // since the newest record is never older than the base.
    NRF_LOG_WARNING("[ActivityLog] Dropping out of order record at %u", record.timestamp);
    Unlock();
    return;
  }

  uint32_t elapsed = record.timestamp - base;
  if (elapsed / secondsPerMinute > deltaMax) {
    // More than the delta field can say since the base. Measuring from the oldest record held
    // instead almost always recovers, since the ring rarely spans that long by itself.
    Rebase();
    elapsed = record.timestamp - base;
    while (count > 0 && elapsed / secondsPerMinute > deltaMax) {
      // It did not, so the oldest records held are themselves out of reach of the new one.
      // They are dropped one at a time, oldest first, rather than the whole log at once:
      // losing the far end of a log nobody has collected is the point, losing tonight is not.
      head = (head + 1) % Slots();
      count--;
      if (count > 0) {
        Rebase();
      } else {
        base = record.timestamp;
      }
      elapsed = record.timestamp - base;
    }
    if (count == 0) {
      NRF_LOG_WARNING("[ActivityLog] Dropped every record, none was within reach of %u", record.timestamp);
      head = 0;
      base = record.timestamp;
      elapsed = 0;
    }
  }

  const uint16_t delta = elapsed / secondsPerMinute;
  if (count > 0 && delta <= DeltaAt(count - 1)) {
    // Two records inside the same minute. Cannot happen while nothing samples faster than
    // once a minute, and if that ever changes this is the ordering guarantee failing, not a
    // rounding detail, so it is dropped rather than stored out of order.
    NRF_LOG_WARNING("[ActivityLog] Dropping record that rounds onto the previous one");
    Unlock();
    return;
  }

  if (count == Slots()) {
    // Full: the oldest record is overwritten. Losing the oldest is the right end to lose from,
    // since a host that has been away long enough for this to happen wants the recent nights.
    Store(0, delta, record);
    head = (head + 1) % Slots();
  } else {
    count++;
    Store(count - 1, delta, record);
  }

  // Not written to flash here. Epochs land while the watch is asleep, which is precisely when
  // the external flash is powered down and the SPI peripheral disabled, and a write then hangs
  // the caller forever. The caller flushes when the watch is awake instead. See Flush().
  dirty = true;

  Unlock();
}

void ActivityLogController::Clear() {
  Lock();
  head = 0;
  count = 0;
  // Not strictly needed, since Add() sets it when the ring is empty, but leaving a base behind
  // that no record refers to makes a saved file harder to read by hand.
  base = 0;
  recordSize = narrowRecordSize;
  dirty = true;
  Unlock();
}

void ActivityLogController::DropSince(uint32_t sinceTimestamp) {
  Lock();

  uint16_t dropped = 0;
  while (dropped < count && TimestampAt(count - 1 - dropped) >= sinceTimestamp) {
    dropped++;
  }
  count -= dropped;

  if (dropped > 0) {
    NRF_LOG_INFO("[ActivityLog] Dropped %u records from %u, %u left", dropped, sinceTimestamp, count);
    dirty = true;
  }

  Unlock();
}

void ActivityLogController::Remark(uint32_t sinceTimestamp, ActivityKind from, ActivityKind to) {
  Lock();

  // Walked from the newest end and stopped at the first record that is too old, as DropSince
  // does, since the records are ordered and the stretch being corrected is always the tail.
  uint16_t changed = 0;
  for (uint16_t offset = count; offset > 0; offset--) {
    if (TimestampAt(offset - 1) < sinceTimestamp) {
      break;
    }
    uint8_t* slot = Slot(offset - 1);
    const uint16_t packed = ReadU16(slot);
    if (packed >> deltaBits == static_cast<uint16_t>(from)) {
      WriteU16(slot, static_cast<uint16_t>((packed & deltaMax) | (static_cast<uint16_t>(to) << deltaBits)));
      changed++;
    }
  }

  if (changed > 0) {
    NRF_LOG_INFO("[ActivityLog] Remarked %u records from %u", changed, sinceTimestamp);
    dirty = true;
  }

  Unlock();
}

void ActivityLogController::Flush() {
  Lock();
  if (dirty) {
    SaveToFile();
    dirty = false;
  }
  Unlock();
}

uint16_t ActivityLogController::RecordCount() const {
  Lock();
  const uint16_t result = count;
  Unlock();
  return result;
}

uint32_t ActivityLogController::OldestTimestamp() const {
  Lock();
  const uint32_t result = count == 0 ? 0 : TimestampAt(0);
  Unlock();
  return result;
}

uint32_t ActivityLogController::NewestTimestamp() const {
  Lock();
  const uint32_t result = count == 0 ? 0 : TimestampAt(count - 1);
  Unlock();
  return result;
}

uint8_t ActivityLogController::ReadRecords(uint32_t sinceTimestamp, ActivityRecord* out, uint8_t maxRecords) const {
  if (out == nullptr || maxRecords == 0) {
    return 0;
  }

  Lock();

  // Stamped before the loop rather than after, and whatever the result, because the question it
  // answers is whether the request arrived at all, which an empty log does not change.
  read = true;
  readAtTicks = xTaskGetTickCount();

  uint8_t written = 0;
  for (uint16_t offset = 0; offset < count && written < maxRecords; offset++) {
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
  while (released < count && TimestampAt(released) <= upToTimestamp) {
    released++;
  }
  head = (head + released) % Slots();
  count -= released;

  NRF_LOG_INFO("[ActivityLog] Released %u records up to %u, %u left", released, upToTimestamp, count);

  // Stamped even when nothing was released, because the question this answers is whether a host
  // is collecting at all, and a host with nothing left to acknowledge is collecting.
  collected = true;
  collectedAtTicks = xTaskGetTickCount();

  if (released > 0) {
    // Marked rather than written: Release arrives on the BLE host task, and a host may well
    // sync while the watch is asleep and the flash is powered down. Losing the acknowledgement
    // to a reboot before the next flush only costs a re-send of records the host already has,
    // which it merges by timestamp.
    dirty = true;
  }

  Unlock();
}

void ActivityLogController::LoadFromFile() {
  lfs_file_t file;
  if (fs.FileOpen(&file, filePath, LFS_O_RDONLY) != LFS_ERR_OK) {
    // Nothing stored yet, which is the normal state on a watch that has never tracked.
    return;
  }

  FileHeader header;
  const int read = fs.FileRead(&file, reinterpret_cast<uint8_t*>(&header), sizeof(header));
  const bool knownSize = read == static_cast<int>(sizeof(header)) &&
                         (header.recordSize == narrowRecordSize || header.recordSize == wideRecordSize);
  if (!knownSize || header.version != fileFormatVersion || header.count > ramBudget / header.recordSize) {
    NRF_LOG_WARNING("[ActivityLog] Discarding unreadable log file");
    fs.FileClose(&file);
    return;
  }

  // The layout comes from the file rather than from what the sensor is doing right now, so a
  // watch that measures motion does not spend a widening, and the records it saved, on every
  // boot.
  recordSize = header.recordSize;

  const uint32_t bytes = header.count * recordSize;
  if (bytes > 0 && fs.FileRead(&file, storage, bytes) != static_cast<int>(bytes)) {
    NRF_LOG_WARNING("[ActivityLog] Log file is shorter than its header claims, discarding");
    recordSize = narrowRecordSize;
    fs.FileClose(&file);
    return;
  }
  fs.FileClose(&file);

  head = 0;
  count = header.count;
  base = header.base;
  NRF_LOG_INFO("[ActivityLog] Loaded %u records", count);
}
void ActivityLogController::SaveToFile() const {
  lfs_dir systemDir;
  if (fs.DirOpen("/.system", &systemDir) == LFS_ERR_OK) {
    fs.DirClose(&systemDir);
  } else {
    // Closing a directory that was never opened is not a no-op: lfs_dir_close unlinks the
    // handle from the filesystem's list of open handles, and the handle here is uninitialised
    // stack, so it would unlink whatever the garbage happens to point at.
    fs.DirCreate("/.system");
  }

  lfs_file_t file;
  if (fs.FileOpen(&file, filePath, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != LFS_ERR_OK) {
    NRF_LOG_WARNING("[ActivityLog] Failed to open the log file for saving");
    return;
  }

  const FileHeader header {fileFormatVersion, recordSize, count, base};
  fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&header), sizeof(header));

  // Stored oldest first, so loading can drop the ring's rotation entirely. The ring wraps at
  // most once, hence at most two runs of contiguous records.
  const uint16_t firstRun = std::min<uint16_t>(count, Slots() - head);
  if (firstRun > 0) {
    fs.FileWrite(&file, &storage[head * recordSize], firstRun * recordSize);
  }
  if (count > firstRun) {
    fs.FileWrite(&file, &storage[0], (count - firstRun) * recordSize);
  }

  fs.FileClose(&file);
}
