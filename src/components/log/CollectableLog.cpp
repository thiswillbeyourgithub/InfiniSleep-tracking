#include "components/log/CollectableLog.h"

#include <algorithm>
#include <cstring>
#include <libraries/log/nrf_log.h>

using namespace Pinetime::Controllers;

namespace {
  /// Precedes the records in the file. The count is stored rather than derived from the file size
  /// so a short write, or a stale tail left by a previous longer file, cannot be read back as
  /// records. The base has to be here too, since the records are meaningless without it.
  struct FileHeader {
    uint8_t version;
    uint8_t recordSize;
    uint16_t count;
    uint32_t base;
  };

  constexpr uint32_t secondsPerMinute = 60;

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

CollectableLog::CollectableLog(Controllers::FS& fs,
                               const char* filePath,
                               uint8_t fileFormatVersion,
                               uint8_t* storage,
                               uint16_t budget,
                               uint8_t recordSize)
  : fs {fs},
    filePath {filePath},
    fileFormatVersion {fileFormatVersion},
    storage {storage},
    budget {budget},
    initialRecordSize {recordSize},
    recordSize {recordSize} {
}

void CollectableLog::Init() {
  if (mutex == nullptr) {
    mutex = xSemaphoreCreateMutex();
  }

  Lock();
  LoadFromFile();
  Unlock();
}

bool CollectableLog::Lock() const {
  // Before Init() there is no other task to race against, so carrying on unlocked is correct
  // rather than merely convenient.
  return mutex != nullptr && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE;
}

void CollectableLog::Unlock() const {
  if (mutex != nullptr) {
    xSemaphoreGive(mutex);
  }
}

uint16_t CollectableLog::Slots() const {
  return budget / recordSize;
}

uint8_t* CollectableLog::Slot(uint16_t offset) {
  return &storage[((head + offset) % Slots()) * recordSize];
}

const uint8_t* CollectableLog::Slot(uint16_t offset) const {
  return &storage[((head + offset) % Slots()) * recordSize];
}

uint8_t CollectableLog::TagAt(uint16_t offset) const {
  return ReadU16(Slot(offset)) >> deltaBits;
}

void CollectableLog::SetTagAt(uint16_t offset, uint8_t tag) {
  uint8_t* slot = Slot(offset);
  WriteU16(slot, static_cast<uint16_t>((ReadU16(slot) & deltaMax) | (static_cast<uint16_t>(tag) << deltaBits)));
}

uint32_t CollectableLog::TimestampAt(uint16_t offset) const {
  return base + static_cast<uint32_t>(ReadU16(Slot(offset)) & deltaMax) * secondsPerMinute;
}

void CollectableLog::Rebase() {
  const uint16_t oldest = ReadU16(Slot(0)) & deltaMax;
  if (oldest == 0) {
    return;
  }
  base += static_cast<uint32_t>(oldest) * secondsPerMinute;
  for (uint16_t offset = 0; offset < count; offset++) {
    uint8_t* slot = Slot(offset);
    const uint16_t packed = ReadU16(slot);
    WriteU16(slot, static_cast<uint16_t>(((packed & deltaMax) - oldest) | (packed & ~deltaMax)));
  }
}

uint16_t CollectableLog::AppendSlot(uint32_t timestamp, bool requireNewMinute) {
  if (count > 0) {
    const uint32_t newest = TimestampAt(count - 1);
    if (timestamp < newest || (requireNewMinute && timestamp <= newest)) {
      // The clock moved backwards, or the caller is replaying. Either way, accepting this would
      // break the ordering that lets a host page through with "everything after T" and be sure it
      // has skipped nothing. This also guarantees the subtraction below cannot go negative, since
      // the newest record is never older than the base.
      NRF_LOG_WARNING("[CollectableLog] Dropping out of order record at %u", timestamp);
      return noSlot;
    }
  } else {
    base = timestamp;
  }

  uint32_t elapsed = timestamp - base;
  if (elapsed / secondsPerMinute > deltaMax) {
    // More than the delta field can say since the base. Measuring from the oldest record held
    // instead almost always recovers, since the ring rarely spans that long by itself.
    Rebase();
    elapsed = timestamp - base;
    while (count > 0 && elapsed / secondsPerMinute > deltaMax) {
      // It did not, so the oldest records held are themselves out of reach of the new one. They
      // are dropped one at a time, oldest first, rather than the whole log at once: losing the far
      // end of a log nobody has collected is the point, losing what is being recorded now is not.
      head = (head + 1) % Slots();
      count--;
      if (count > 0) {
        Rebase();
      } else {
        base = timestamp;
      }
      elapsed = timestamp - base;
    }
    if (count == 0) {
      NRF_LOG_WARNING("[CollectableLog] Dropped every record, none was within reach of %u", timestamp);
      head = 0;
      base = timestamp;
      elapsed = 0;
    }
  }

  const uint16_t delta = elapsed / secondsPerMinute;
  if (requireNewMinute && count > 0 && delta <= (ReadU16(Slot(count - 1)) & deltaMax)) {
    // Two records inside the same minute. Cannot happen while nothing samples faster than once a
    // minute, and if that ever changes this is the ordering guarantee failing, not a rounding
    // detail, so it is dropped rather than stored out of order.
    NRF_LOG_WARNING("[CollectableLog] Dropping record that rounds onto the previous one");
    return noSlot;
  }

  if (count == Slots()) {
    // Full: the oldest record is overwritten. Losing the oldest is the right end to lose from,
    // since a host that has been away long enough for this to happen wants the recent records.
    head = (head + 1) % Slots();
  } else {
    count++;
  }

  const uint16_t offset = count - 1;
  WriteU16(Slot(offset), delta);
  // Not written to flash here. Records land while the watch is asleep, which is precisely when the
  // external flash is powered down and the SPI peripheral disabled, and a write then hangs the
  // caller forever. The caller flushes when the watch is awake instead. See Flush().
  dirty = true;
  return offset;
}

void CollectableLog::DropOldest(uint16_t records) {
  if (records == 0) {
    return;
  }
  head = (head + records) % Slots();
  count -= records;
  dirty = true;
}

void CollectableLog::DropNewest(uint16_t records) {
  if (records == 0) {
    return;
  }
  count -= records;
  dirty = true;
}

void CollectableLog::ReverseSlots(uint16_t from, uint16_t to) {
  uint8_t scratch[maxRecordSize];
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

void CollectableLog::Rotate() {
  if (head == 0) {
    return;
  }
  // A left rotation by head, as three reversals, which needs one record of scratch rather than a
  // second copy of the ring. The unused slots come along for the ride, which is harmless.
  const uint16_t slots = Slots();
  ReverseSlots(0, head - 1);
  ReverseSlots(head, slots - 1);
  ReverseSlots(0, slots - 1);
  head = 0;
}

void CollectableLog::SetRecordSize(uint8_t newRecordSize) {
  recordSize = newRecordSize;
}

void CollectableLog::Relayout(uint8_t newRecordSize, uint16_t records) {
  recordSize = newRecordSize;
  head = 0;
  count = records;
  dirty = true;
}

void CollectableLog::MarkRead() const {
  read = true;
  readAtTicks = xTaskGetTickCount();
}

void CollectableLog::MarkCollected() {
  collected = true;
  collectedAtTicks = xTaskGetTickCount();
}

bool CollectableLog::AdoptRecordSize(uint8_t size) {
  return size == recordSize;
}

void CollectableLog::Clear() {
  Lock();
  head = 0;
  count = 0;
  // Not strictly needed, since the next record sets it on an empty ring, but leaving a base behind
  // that no record refers to makes a saved file harder to read by hand.
  base = 0;
  recordSize = initialRecordSize;
  dirty = true;
  OnCleared();
  Unlock();
}

void CollectableLog::Flush() {
  Lock();
  if (dirty) {
    SaveToFile();
    dirty = false;
  }
  Unlock();
}

uint16_t CollectableLog::RecordCount() const {
  Lock();
  const uint16_t result = count;
  Unlock();
  return result;
}

uint16_t CollectableLog::Capacity() const {
  Lock();
  const uint16_t result = Slots();
  Unlock();
  return result;
}

uint32_t CollectableLog::OldestTimestamp() const {
  Lock();
  const uint32_t result = count == 0 ? 0 : TimestampAt(0);
  Unlock();
  return result;
}

uint32_t CollectableLog::NewestTimestamp() const {
  Lock();
  const uint32_t result = count == 0 ? 0 : TimestampAt(count - 1);
  Unlock();
  return result;
}

void CollectableLog::LoadFromFile() {
  lfs_file_t file;
  if (fs.FileOpen(&file, filePath, LFS_O_RDONLY) != LFS_ERR_OK) {
    // Nothing stored yet, which is the normal state on a watch that has never used this log.
    return;
  }

  FileHeader header;
  const int bytesRead = fs.FileRead(&file, reinterpret_cast<uint8_t*>(&header), sizeof(header));
  if (bytesRead != static_cast<int>(sizeof(header)) || header.version != fileFormatVersion ||
      !AdoptRecordSize(header.recordSize) || header.count > budget / header.recordSize) {
    NRF_LOG_WARNING("[CollectableLog] Discarding unreadable log file");
    recordSize = initialRecordSize;
    fs.FileClose(&file);
    return;
  }

  const uint32_t bytes = header.count * recordSize;
  if (bytes > 0 && fs.FileRead(&file, storage, bytes) != static_cast<int>(bytes)) {
    NRF_LOG_WARNING("[CollectableLog] Log file is shorter than its header claims, discarding");
    recordSize = initialRecordSize;
    fs.FileClose(&file);
    return;
  }
  fs.FileClose(&file);

  head = 0;
  count = header.count;
  base = header.base;
  NRF_LOG_INFO("[CollectableLog] Loaded %u records", count);
}

void CollectableLog::SaveToFile() const {
  lfs_dir systemDir;
  if (fs.DirOpen("/.system", &systemDir) == LFS_ERR_OK) {
    fs.DirClose(&systemDir);
  } else {
    // Closing a directory that was never opened is not a no-op: lfs_dir_close unlinks the handle
    // from the filesystem's list of open handles, and the handle here is uninitialised stack, so
    // it would unlink whatever the garbage happens to point at.
    fs.DirCreate("/.system");
  }

  lfs_file_t file;
  if (fs.FileOpen(&file, filePath, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != LFS_ERR_OK) {
    NRF_LOG_WARNING("[CollectableLog] Failed to open the log file for saving");
    return;
  }

  const FileHeader header {fileFormatVersion, recordSize, count, base};
  fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&header), sizeof(header));

  // Stored oldest first, so loading can drop the ring's rotation entirely. The ring wraps at most
  // once, hence at most two runs of contiguous records.
  const uint16_t firstRun = std::min<uint16_t>(count, Slots() - head);
  if (firstRun > 0) {
    fs.FileWrite(&file, &storage[head * recordSize], firstRun * recordSize);
  }
  if (count > firstRun) {
    fs.FileWrite(&file, &storage[0], (count - firstRun) * recordSize);
  }

  fs.FileClose(&file);
}
