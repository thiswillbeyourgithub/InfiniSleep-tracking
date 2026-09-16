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
  LoadFromFile();
}

void LogSlots::BeginUpdate(uint16_t revision) {
  updating = true;
  incoming = 0;
  incomingRevision = revision;
}

bool LogSlots::AddSlot(const LogSlot& slot) {
  if (!updating || incoming == maxSlots) {
    AbandonUpdate();
    return false;
  }

  slots[incoming] = slot;
  // Whatever the phone sent, the label ends where the watch says it does: it is printed straight
  // onto a screen from here.
  slots[incoming].label[LogSlot::labelSize - 1] = '\0';
  incoming++;
  return true;
}

bool LogSlots::CommitUpdate() {
  if (!updating) {
    return false;
  }

  if (!IsValid(incoming)) {
    NRF_LOG_WARNING("[LogSlots] Rejecting a table of %u slots that does not hold together", incoming);
    AbandonUpdate();
    return false;
  }

  count = incoming;
  revision = incomingRevision;
  updating = false;
  incoming = 0;
  SaveToFile();
  NRF_LOG_INFO("[LogSlots] Showing revision %u, %u slots", revision, count);
  return true;
}

void LogSlots::AbandonUpdate() {
  updating = false;
  incoming = 0;
  // The slots that arrived went over the ones being shown, so what the watch had is whatever it
  // last wrote down.
  count = 0;
  revision = 0;
  LoadFromFile();
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
  return index < count ? &slots[index] : nullptr;
}

const LogSlot* LogSlots::Find(uint8_t id) const {
  for (uint8_t i = 0; i < count; i++) {
    if (slots[i].id == id) {
      return &slots[i];
    }
  }
  return nullptr;
}

uint8_t LogSlots::ChildrenOf(uint8_t parentId, uint8_t* out, uint8_t max) const {
  uint8_t found = 0;
  for (uint8_t i = 0; i < count && found < max; i++) {
    if (slots[i].parent == parentId) {
      out[found] = i;
      found++;
    }
  }
  return found;
}

bool LogSlots::HasSingleGroup() const {
  uint8_t groups = 0;
  for (uint8_t i = 0; i < count; i++) {
    if (slots[i].parent != LogSlot::noParent) {
      continue;
    }
    if (slots[i].behaviour != LogSlotBehaviour::Group) {
      return false;
    }
    groups++;
    if (groups > 1) {
      return false;
    }
  }
  return groups == 1;
}

void LogSlots::Clear() {
  count = 0;
  revision = 0;
  updating = false;
  incoming = 0;
  SaveToFile();
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
