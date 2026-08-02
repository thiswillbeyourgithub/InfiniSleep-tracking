#include "components/activity/ActivityLogController.h"

#include <algorithm>
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

ActivityRecord ActivityLogController::At(uint16_t offset) const {
  const PackedRecord& packed = records[(head + offset) % capacity];

  ActivityRecord record;
  record.timestamp = TimestampAt(offset);
  record.motion = packed.motion;
  record.heartRate = packed.heartRate;
  record.kind = static_cast<ActivityKind>(packed.kind);
  return record;
}

uint32_t ActivityLogController::TimestampAt(uint16_t offset) const {
  return base + static_cast<uint32_t>(records[(head + offset) % capacity].deltaMinutes) * secondsPerMinute;
}

void ActivityLogController::Rebase() {
  const uint16_t oldest = records[head].deltaMinutes;
  if (oldest == 0) {
    return;
  }
  base += static_cast<uint32_t>(oldest) * secondsPerMinute;
  for (uint16_t offset = 0; offset < count; offset++) {
    records[(head + offset) % capacity].deltaMinutes -= oldest;
  }
}

void ActivityLogController::Add(const ActivityRecord& record) {
  Lock();

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
    // 45 days since the base. Measuring from the oldest record held instead almost always
    // recovers, since the ring cannot span that long by itself.
    Rebase();
    elapsed = record.timestamp - base;
    if (elapsed / secondsPerMinute > deltaMax) {
      // It did not, so the records held are themselves older than 45 days and were never
      // collected. Keeping them would cost every record from tonight, which is the worse trade.
      NRF_LOG_WARNING("[ActivityLog] Dropping %u records older than the 45 day span", count);
      head = 0;
      count = 0;
      base = record.timestamp;
      elapsed = 0;
    }
  }

  const uint16_t delta = elapsed / secondsPerMinute;
  if (count > 0 && delta <= records[(head + count - 1) % capacity].deltaMinutes) {
    // Two records inside the same minute. Cannot happen while nothing samples faster than
    // once a minute, and if that ever changes this is the ordering guarantee failing, not a
    // rounding detail, so it is dropped rather than stored out of order.
    NRF_LOG_WARNING("[ActivityLog] Dropping record that rounds onto the previous one");
    Unlock();
    return;
  }

  const PackedRecord packed {delta, record.motion, record.heartRate, static_cast<uint8_t>(record.kind)};

  if (count == capacity) {
    // Full: the oldest record is overwritten. Losing the oldest is the right end to lose from,
    // since a host that has been away long enough for this to happen wants the recent nights.
    records[head] = packed;
    head = (head + 1) % capacity;
  } else {
    records[(head + count) % capacity] = packed;
    count++;
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
  dirty = true;
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
  head = (head + released) % capacity;
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
  if (read != static_cast<int>(sizeof(header)) || header.version != fileFormatVersion ||
      header.recordSize != sizeof(PackedRecord) || header.count > capacity) {
    NRF_LOG_WARNING("[ActivityLog] Discarding unreadable log file");
    fs.FileClose(&file);
    return;
  }

  const uint32_t bytes = header.count * sizeof(PackedRecord);
  if (bytes > 0 && fs.FileRead(&file, reinterpret_cast<uint8_t*>(records), bytes) != static_cast<int>(bytes)) {
    NRF_LOG_WARNING("[ActivityLog] Log file is shorter than its header claims, discarding");
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

  const FileHeader header {fileFormatVersion, sizeof(PackedRecord), count, base};
  fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&header), sizeof(header));

  // Stored oldest first, so loading can drop the ring's rotation entirely. The ring wraps at
  // most once, hence at most two runs of contiguous records.
  const uint16_t firstRun = std::min<uint16_t>(count, capacity - head);
  if (firstRun > 0) {
    fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&records[head]), firstRun * sizeof(PackedRecord));
  }
  if (count > firstRun) {
    fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&records[0]), (count - firstRun) * sizeof(PackedRecord));
  }

  fs.FileClose(&file);
}
