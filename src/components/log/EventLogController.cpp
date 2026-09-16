#include "components/log/EventLogController.h"

#include <libraries/log/nrf_log.h>

using namespace Pinetime::Controllers;

namespace {
  constexpr uint8_t valueMask = 0x0F;
  constexpr uint8_t flaggedBit = 0x80;
}

EventLogController::EventLogController(Controllers::FS& fs)
  : CollectableLog {fs, filePath, fileFormatVersion, storage, ramBudget, eventRecordSize} {
}

uint32_t EventLogController::ExtraToSave() const {
  return nextSequence;
}

void EventLogController::ExtraLoaded(uint32_t extra) {
  // A file that says nothing sensible about numbering is still worth its records: the host asks
  // for everything after what it has, and a counter that starts behind where it left off only
  // costs a re-send.
  nextSequence = extra == 0 ? Count() + 1 : extra;
}

void EventLogController::OnCleared() {
  // The numbering carries on rather than starting over, so that a host holding events from before
  // the clear can still tell them apart from what comes after.
}

uint32_t EventLogController::SequenceAt(uint16_t offset) const {
  return nextSequence - Count() + offset;
}

LoggedEvent EventLogController::At(uint16_t offset) const {
  const uint8_t* slot = Slot(offset);

  LoggedEvent event;
  event.timestamp = TimestampAt(offset);
  event.type = static_cast<LoggedEventType>(TagAt(offset));
  event.slot = slot[2];
  event.value = slot[3] & valueMask;
  event.flagged = (slot[3] & flaggedBit) != 0;
  event.sequence = SequenceAt(offset);
  return event;
}

uint32_t EventLogController::Add(const LoggedEvent& event) {
  Lock();

  // Several events a minute are expected: a wearer logging three things at once taps them one
  // after the other, and they are told apart by their sequence rather than by their time.
  const uint16_t offset = AppendSlot(event.timestamp, false);
  if (offset == noSlot) {
    Unlock();
    return 0;
  }

  SetTagAt(offset, static_cast<uint8_t>(event.type));
  uint8_t* slot = Slot(offset);
  slot[2] = event.slot;
  const uint8_t value = event.value > LoggedEvent::maxValue ? LoggedEvent::valueNotAsked : event.value;
  slot[3] = static_cast<uint8_t>(value | (event.flagged ? flaggedBit : 0));

  const uint32_t sequence = nextSequence;
  nextSequence++;
  NRF_LOG_INFO("[EventLog] Logged event %u, slot %u, type %u", sequence, event.slot, static_cast<uint8_t>(event.type));

  Unlock();
  return sequence;
}

bool EventLogController::IsRunning(uint8_t slot) const {
  Lock();

  bool running = false;
  for (uint16_t offset = Count(); offset > 0; offset--) {
    const uint8_t* record = Slot(offset - 1);
    if (record[2] != slot) {
      continue;
    }
    const LoggedEventType type = static_cast<LoggedEventType>(TagAt(offset - 1));
    if (type == LoggedEventType::Started) {
      running = true;
      break;
    }
    if (type == LoggedEventType::Stopped) {
      break;
    }
  }

  Unlock();
  return running;
}

bool EventLogController::Flag(uint32_t sequence) {
  Lock();

  bool found = false;
  for (uint16_t offset = 0; offset < Count(); offset++) {
    if (SequenceAt(offset) == sequence) {
      Slot(offset)[3] |= flaggedBit;
      MarkDirty();
      found = true;
      break;
    }
  }

  Unlock();
  return found;
}

uint16_t EventLogController::EventCount() const {
  return RecordCount();
}

uint32_t EventLogController::NewestSequence() const {
  Lock();
  const uint32_t result = Count() == 0 ? 0 : SequenceAt(Count() - 1);
  Unlock();
  return result;
}

uint32_t EventLogController::OldestSequence() const {
  Lock();
  const uint32_t result = Count() == 0 ? 0 : SequenceAt(0);
  Unlock();
  return result;
}

uint8_t EventLogController::ReadEvents(uint32_t sinceSequence, LoggedEvent* out, uint8_t maxEvents) const {
  if (out == nullptr || maxEvents == 0) {
    return 0;
  }

  Lock();
  MarkRead();

  uint8_t written = 0;
  for (uint16_t offset = 0; offset < Count() && written < maxEvents; offset++) {
    if (SequenceAt(offset) > sinceSequence) {
      out[written] = At(offset);
      written++;
    }
  }

  Unlock();
  return written;
}

void EventLogController::ReleaseEvents(uint32_t upToSequence) {
  Lock();

  uint16_t released = 0;
  while (released < Count() && SequenceAt(released) <= upToSequence) {
    released++;
  }
  DropOldest(released);
  MarkCollected();
  NRF_LOG_INFO("[EventLog] Released %u events up to %u, %u left", released, upToSequence, Count());

  Unlock();
}
