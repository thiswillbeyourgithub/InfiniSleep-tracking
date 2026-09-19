#include "components/log/LogSlots.h"

#include <cstring>
#include <libraries/log/nrf_log.h>

using namespace Pinetime::Controllers;

namespace {
  struct FileHeader {
    uint8_t version;
    uint8_t count;
    uint16_t revision;
  };

  /// One slot on disk, which is the struct without whatever the compiler might pad it with.
  constexpr uint8_t storedSlotSize = 3 + LogSlot::labelSize;
}

LogSlots::LogSlots(Controllers::FS& fs) : fs {fs} {
}

void LogSlots::Init() {
  if (mutex == nullptr) {
    mutex = xSemaphoreCreateMutex();
  }

  Lock();
  LoadFromFile();
  Unlock();
}

bool LogSlots::Lock() const {
  // Before Init() there is no other task to race against, so carrying on unlocked is correct
  // rather than merely convenient.
  return mutex != nullptr && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE;
}

void LogSlots::Unlock() const {
  if (mutex != nullptr) {
    xSemaphoreGive(mutex);
  }
}

void LogSlots::BeginUpdate(uint16_t revision) {
  Lock();
  // The arriving slots are about to be written over the ones being shown, so the table stops being
  // a table now, while the logging app can still be told so. Leaving the count up and emptying it
  // at the end instead would give that app, which runs on another task, a window in which it reads
  // slots that are half of the old table and half of the new one.
  count = 0;
  this->revision = 0;
  updating = true;
  incoming = 0;
  incomingRevision = revision;
  Unlock();
}

bool LogSlots::AddSlot(const LogSlot& slot) {
  Lock();
  if (!updating || incoming == maxSlots) {
    AbandonUpdateLocked();
    Unlock();
    return false;
  }

  // Whatever the phone sent, the label ends where the watch says it does: it is printed straight
  // onto a screen from here. Terminated in this copy rather than in the table, so that the table
  // never holds a label without a terminator even for the few instructions in between.
  LogSlot staged = slot;
  staged.label[LogSlot::labelSize - 1] = '\0';
  slots[incoming] = staged;
  incoming++;
  Unlock();
  return true;
}

bool LogSlots::CommitUpdate() {
  Lock();
  if (!updating) {
    Unlock();
    return false;
  }

  if (!IsValid(incoming)) {
    NRF_LOG_WARNING("[LogSlots] Rejecting a table of %u slots that does not hold together", incoming);
    AbandonUpdateLocked();
    Unlock();
    return false;
  }

  count = incoming;
  revision = incomingRevision;
  updating = false;
  incoming = 0;
  // The file is brought into step by the system task instead: see Flush().
  dirty = true;
  pendingReload = false;
  NRF_LOG_INFO("[LogSlots] Showing revision %u, %u slots", revision, count);
  Unlock();
  return true;
}

void LogSlots::AbandonUpdate() {
  Lock();
  AbandonUpdateLocked();
  Unlock();
}

void LogSlots::AbandonUpdateLocked() {
  updating = false;
  incoming = 0;
  // The slots that arrived went over the ones being shown, so what the watch had is whatever it
  // last wrote down. Reading it back is left to Flush(), which runs where reaching the flash is
  // allowed; until then the watch offers nothing rather than half a table.
  count = 0;
  revision = 0;
  dirty = false;
  pendingReload = true;
}

bool LogSlots::IsValid(uint8_t entries) const {
  for (uint8_t i = 0; i < entries; i++) {
    const LogSlot& slot = slots[i];

    if (slot.label[0] == '\0') {
      return false;
    }
    if (slot.id == LogSlot::noParent) {
      // Reserved, since it is what a slot at the top of the table has for a parent.
      return false;
    }
    for (uint8_t other = 0; other < i; other++) {
      if (slots[other].id == slot.id) {
        return false;
      }
    }

    // Walked up to the top, which also catches a slot that is its own ancestor, since a parent has
    // to appear before its children for the walk to terminate.
    uint8_t depth = 0;
    uint8_t parent = slot.parent;
    while (parent != LogSlot::noParent) {
      const LogSlot* above = nullptr;
      for (uint8_t candidate = 0; candidate < i; candidate++) {
        if (slots[candidate].id == parent) {
          above = &slots[candidate];
          break;
        }
      }
      if (above == nullptr || above->behaviour != LogSlotBehaviour::Group) {
        return false;
      }
      depth++;
      if (depth > maxDepth) {
        return false;
      }
      parent = above->parent;
    }
    if (slot.behaviour == LogSlotBehaviour::Group && depth == maxDepth) {
      // A group that deep could only hold groups deeper still.
      return false;
    }
  }
  return true;
}

const LogSlot* LogSlots::At(uint8_t index) const {
  Lock();
  const LogSlot* slot = index < count ? &slots[index] : nullptr;
  Unlock();
  return slot;
}

const LogSlot* LogSlots::Find(uint8_t id) const {
  Lock();
  const LogSlot* found = nullptr;
  for (uint8_t i = 0; i < count; i++) {
    if (slots[i].id == id) {
      found = &slots[i];
      break;
    }
  }
  Unlock();
  return found;
}

uint8_t LogSlots::ChildrenOf(uint8_t parentId, uint8_t* out, uint8_t max) const {
  Lock();
  uint8_t found = 0;
  for (uint8_t i = 0; i < count && found < max; i++) {
    if (slots[i].parent == parentId) {
      out[found] = i;
      found++;
    }
  }
  Unlock();
  return found;
}

bool LogSlots::HasSingleGroup() const {
  Lock();
  // One exit, because every way out of here has to give the lock back.
  bool single = true;
  uint8_t groups = 0;
  for (uint8_t i = 0; i < count; i++) {
    if (slots[i].parent != LogSlot::noParent) {
      continue;
    }
    if (slots[i].behaviour != LogSlotBehaviour::Group) {
      single = false;
      break;
    }
    groups++;
    if (groups > 1) {
      single = false;
      break;
    }
  }
  Unlock();
  return single && groups == 1;
}

void LogSlots::Clear() {
  Lock();
  count = 0;
  revision = 0;
  updating = false;
  incoming = 0;
  dirty = true;
  pendingReload = false;
  Unlock();
}

void LogSlots::Flush() {
  Lock();
  if (pendingReload) {
    LoadFromFile();
    pendingReload = false;
    dirty = false;
  } else if (dirty) {
    SaveToFile();
    dirty = false;
  }
  Unlock();
}

void LogSlots::LoadFromFile() {
  count = 0;
  revision = 0;

  lfs_file_t file;
  if (fs.FileOpen(&file, filePath, LFS_O_RDONLY) != LFS_ERR_OK) {
    // Nothing pushed yet, which is the normal state until the phone gets around to it.
    return;
  }

  FileHeader header;
  const int bytesRead = fs.FileRead(&file, reinterpret_cast<uint8_t*>(&header), sizeof(header));
  if (bytesRead != static_cast<int>(sizeof(header)) || header.version != fileFormatVersion || header.count > maxSlots) {
    NRF_LOG_WARNING("[LogSlots] Discarding an unreadable slot file");
    fs.FileClose(&file);
    return;
  }

  for (uint8_t i = 0; i < header.count; i++) {
    uint8_t stored[storedSlotSize];
    if (fs.FileRead(&file, stored, storedSlotSize) != static_cast<int>(storedSlotSize)) {
      NRF_LOG_WARNING("[LogSlots] Slot file is shorter than its header claims, discarding");
      count = 0;
      fs.FileClose(&file);
      return;
    }
    slots[i].id = stored[0];
    slots[i].parent = stored[1];
    slots[i].behaviour = static_cast<LogSlotBehaviour>(stored[2]);
    std::memcpy(slots[i].label, &stored[3], LogSlot::labelSize);
    slots[i].label[LogSlot::labelSize - 1] = '\0';
    count = i + 1;
  }
  fs.FileClose(&file);

  if (!IsValid(count)) {
    // Written by something that did not check, or corrupted since.
    NRF_LOG_WARNING("[LogSlots] Discarding a slot file that does not hold together");
    count = 0;
    return;
  }

  revision = header.revision;
  NRF_LOG_INFO("[LogSlots] Loaded revision %u, %u slots", revision, count);
}

void LogSlots::SaveToFile() const {
  lfs_dir systemDir;
  if (fs.DirOpen("/.system", &systemDir) == LFS_ERR_OK) {
    fs.DirClose(&systemDir);
  } else {
    // Closing a directory that was never opened is not a no-op: lfs_dir_close unlinks the handle
    // from the filesystem's list of open handles, and the handle here is uninitialised stack.
    fs.DirCreate("/.system");
  }

  lfs_file_t file;
  if (fs.FileOpen(&file, filePath, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != LFS_ERR_OK) {
    NRF_LOG_WARNING("[LogSlots] Failed to open the slot file for saving");
    return;
  }

  const FileHeader header {fileFormatVersion, count, revision};
  fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&header), sizeof(header));

  for (uint8_t i = 0; i < count; i++) {
    uint8_t stored[storedSlotSize];
    stored[0] = slots[i].id;
    stored[1] = slots[i].parent;
    stored[2] = static_cast<uint8_t>(slots[i].behaviour);
    std::memcpy(&stored[3], slots[i].label, LogSlot::labelSize);
    fs.FileWrite(&file, stored, storedSlotSize);
  }

  fs.FileClose(&file);
}
