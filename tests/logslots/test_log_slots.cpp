// Host harness for LogSlots, built against stub headers so the real .cpp compiles unchanged. Exists
// because a table that does not hold together has to be refused rather than shown, and that is not
// something to find out on the watch.
#include <cstdio>
#include <cstring>

#include "components/log/LogSlots.h"

using namespace Pinetime::Controllers;

static int failures = 0;

#define CHECK(cond)                                                                                                            \
  do {                                                                                                                         \
    if (!(cond)) {                                                                                                             \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                                                 \
      failures++;                                                                                                              \
    }                                                                                                                          \
  } while (0)

namespace {
  LogSlot Slot(uint8_t id, uint8_t parent, LogSlotBehaviour behaviour, const char* label) {
    LogSlot slot;
    slot.id = id;
    slot.parent = parent;
    slot.behaviour = behaviour;
    std::snprintf(slot.label, LogSlot::labelSize, "%s", label);
    return slot;
  }

  /// Moods holding two feelings, and medication on its own, which is the shape the wearer is
  /// expected to build.
  bool PushExample(LogSlots& table, uint16_t revision = 7) {
    table.BeginUpdate(revision);
    table.AddSlot(Slot(1, LogSlot::noParent, LogSlotBehaviour::Group, "Moods"));
    table.AddSlot(Slot(2, 1, LogSlotBehaviour::Valued, "Sad"));
    table.AddSlot(Slot(3, 1, LogSlotBehaviour::Valued, "Joyful"));
    table.AddSlot(Slot(4, LogSlot::noParent, LogSlotBehaviour::Punctual, "Medication"));
    table.AddSlot(Slot(5, LogSlot::noParent, LogSlotBehaviour::Continuous, "Napping"));
    return table.CommitUpdate();
  }
}

int main() {
  printf("maxSlots=%u  maxDepth=%u  label=%u bytes\n", LogSlots::maxSlots, LogSlots::maxDepth, LogSlot::labelSize);

  {
    printf("a pushed table is shown, and its shape can be walked\n");
    FS fs;
    LogSlots table(fs);
    table.Init();
    CHECK(table.Count() == 0);
    CHECK(table.Revision() == 0);

    CHECK(PushExample(table));
    CHECK(table.Count() == 5);
    CHECK(table.Revision() == 7);
    CHECK(std::strcmp(table.At(0)->label, "Moods") == 0);
    CHECK(table.Find(3)->behaviour == LogSlotBehaviour::Valued);
    CHECK(table.Find(9) == nullptr);
    CHECK(!table.HasSingleGroup());  // there are things beside the group

    uint8_t children[LogSlots::maxSlots];
    CHECK(table.ChildrenOf(LogSlot::noParent, children, LogSlots::maxSlots) == 3);
    CHECK(table.ChildrenOf(1, children, LogSlots::maxSlots) == 2);
    CHECK(table.At(children[0])->id == 2);
    CHECK(table.At(children[1])->id == 3);
    CHECK(table.ChildrenOf(4, children, LogSlots::maxSlots) == 0);
  }

  {
    printf("a table survives a reboot without a phone in reach\n");
    FS fs;
    {
      LogSlots table(fs);
      table.Init();
      CHECK(PushExample(table, 12));
      table.Flush();  // the system task's pass, without which nothing is on flash
    }
    LogSlots reloaded(fs);
    reloaded.Init();
    CHECK(reloaded.Count() == 5);
    CHECK(reloaded.Revision() == 12);
    CHECK(std::strcmp(reloaded.Find(5)->label, "Napping") == 0);
    CHECK(reloaded.Find(5)->behaviour == LogSlotBehaviour::Continuous);
  }

  {
    printf("a table that does not hold together is refused, and the old one stays\n");
    FS fs;
    LogSlots table(fs);
    table.Init();
    CHECK(PushExample(table, 3));

    table.Flush();

    // A slot whose parent is not a group. The arriving slots go over the ones being shown, so a
    // refused table leaves nothing until the next flush reads the file back: the watch offers
    // nothing for a moment rather than half of two tables.
    table.BeginUpdate(4);
    table.AddSlot(Slot(1, LogSlot::noParent, LogSlotBehaviour::Punctual, "Medication"));
    table.AddSlot(Slot(2, 1, LogSlotBehaviour::Punctual, "Inside a punctual slot"));
    CHECK(!table.CommitUpdate());
    CHECK(table.Count() == 0);
    table.Flush();
    CHECK(table.Revision() == 3);
    CHECK(table.Count() == 5);

    // A parent that was never sent.
    table.BeginUpdate(5);
    table.AddSlot(Slot(1, 40, LogSlotBehaviour::Punctual, "Orphan"));
    CHECK(!table.CommitUpdate());
    table.Flush();
    CHECK(table.Revision() == 3);

    // Two slots with the same id, which an event could not tell apart.
    table.BeginUpdate(6);
    table.AddSlot(Slot(1, LogSlot::noParent, LogSlotBehaviour::Punctual, "One"));
    table.AddSlot(Slot(1, LogSlot::noParent, LogSlotBehaviour::Punctual, "Also one"));
    CHECK(!table.CommitUpdate());
    table.Flush();
    CHECK(table.Revision() == 3);

    // No label to print.
    table.BeginUpdate(7);
    table.AddSlot(Slot(1, LogSlot::noParent, LogSlotBehaviour::Punctual, ""));
    CHECK(!table.CommitUpdate());
    table.Flush();
    CHECK(table.Revision() == 3);

    // The id that means "no parent" cannot also be a slot.
    table.BeginUpdate(8);
    table.AddSlot(Slot(LogSlot::noParent, LogSlot::noParent, LogSlotBehaviour::Punctual, "Reserved"));
    CHECK(!table.CommitUpdate());
    table.Flush();
    CHECK(table.Revision() == 3);

    // And the table that was there all along is still readable.
    CHECK(std::strcmp(table.Find(2)->label, "Sad") == 0);
  }

  {
    printf("groups go two deep and no deeper\n");
    FS fs;
    LogSlots table(fs);
    table.Init();

    table.BeginUpdate(1);
    table.AddSlot(Slot(1, LogSlot::noParent, LogSlotBehaviour::Group, "Moods"));
    table.AddSlot(Slot(2, 1, LogSlotBehaviour::Group, "Bad ones"));
    table.AddSlot(Slot(3, 2, LogSlotBehaviour::Valued, "Sad"));
    CHECK(table.CommitUpdate());
    CHECK(table.Count() == 3);

    table.Flush();

    table.BeginUpdate(2);
    table.AddSlot(Slot(1, LogSlot::noParent, LogSlotBehaviour::Group, "Moods"));
    table.AddSlot(Slot(2, 1, LogSlotBehaviour::Group, "Bad ones"));
    table.AddSlot(Slot(3, 2, LogSlotBehaviour::Group, "Worse ones"));
    CHECK(!table.CommitUpdate());
    table.Flush();
    CHECK(table.Revision() == 1);
  }

  {
    printf("one group and nothing else means the watch can skip a screen\n");
    FS fs;
    LogSlots table(fs);
    table.Init();

    table.BeginUpdate(1);
    table.AddSlot(Slot(1, LogSlot::noParent, LogSlotBehaviour::Group, "Everything"));
    table.AddSlot(Slot(2, 1, LogSlotBehaviour::Punctual, "Medication"));
    CHECK(table.CommitUpdate());
    CHECK(table.HasSingleGroup());

    table.BeginUpdate(2);
    table.AddSlot(Slot(1, LogSlot::noParent, LogSlotBehaviour::Group, "Moods"));
    table.AddSlot(Slot(2, LogSlot::noParent, LogSlotBehaviour::Group, "Body"));
    table.AddSlot(Slot(3, 1, LogSlotBehaviour::Valued, "Sad"));
    CHECK(table.CommitUpdate());
    CHECK(!table.HasSingleGroup());
  }

  {
    printf("a table bigger than the watch can hold is refused whole\n");
    FS fs;
    LogSlots table(fs);
    table.Init();
    CHECK(PushExample(table, 2));
    table.Flush();

    table.BeginUpdate(3);
    bool refused = false;
    for (uint16_t i = 1; i <= LogSlots::maxSlots + 1u; i++) {
      char label[LogSlot::labelSize];
      std::snprintf(label, sizeof(label), "Slot %u", i);
      if (!table.AddSlot(Slot(static_cast<uint8_t>(i), LogSlot::noParent, LogSlotBehaviour::Punctual, label))) {
        refused = true;
        break;
      }
    }
    CHECK(refused);
    CHECK(!table.CommitUpdate());  // the update was abandoned, so there is nothing to commit
    table.Flush();
    CHECK(table.Revision() == 2);
    CHECK(table.Count() == 5);
  }

  {
    printf("a label longer than the screen is cut rather than run off the end\n");
    FS fs;
    LogSlots table(fs);
    table.Init();

    LogSlot slot;
    slot.id = 1;
    slot.parent = LogSlot::noParent;
    slot.behaviour = LogSlotBehaviour::Punctual;
    std::memset(slot.label, 'x', LogSlot::labelSize);  // no terminator at all
    table.BeginUpdate(1);
    CHECK(table.AddSlot(slot));
    CHECK(table.CommitUpdate());
    CHECK(std::strlen(table.At(0)->label) == LogSlot::labelSize - 1);
  }

  {
    printf("a slot file from an older firmware is discarded, not misread\n");
    FS fs;
    fs.exists = true;
    fs.contents = {99, 1, 0, 0};  // version 99
    LogSlots table(fs);
    table.Init();
    CHECK(table.Count() == 0);
  }

  {
    printf("clearing leaves nothing to show, on the watch or in the file\n");
    FS fs;
    {
      LogSlots table(fs);
      table.Init();
      CHECK(PushExample(table));
      table.Clear();
      CHECK(table.Count() == 0);
      CHECK(table.Revision() == 0);
      table.Flush();
    }
    LogSlots reloaded(fs);
    reloaded.Init();
    CHECK(reloaded.Count() == 0);
  }

  {
    printf("receiving a table touches no flash until the watch is awake\n");
    FS fs;
    LogSlots table(fs);
    table.Init();
    const int afterInit = fs.reads + fs.writes;

    // Every one of these runs on the BLE host task, where the external flash may be powered down
    // and the SPI peripheral disabled. A write there waits forever on a completion that never
    // comes, holding the display until the watchdog reboots the watch, which is what a phone
    // pushing a table on connecting used to do to a watch asleep on a wrist.
    CHECK(PushExample(table, 21));
    CHECK(fs.reads + fs.writes == afterInit);
    CHECK(table.IsDirty());

    // The system task's pass, which is the only place the flash is reached.
    table.Flush();
    CHECK(fs.writes > 0);
    CHECK(!table.IsDirty());

    // A refused table is the other way in, since giving up used to mean reading the file back on
    // the spot. It is put off in the same way, and the table that was there comes back with it.
    const int afterWrite = fs.reads + fs.writes;
    table.BeginUpdate(22);
    table.AddSlot(Slot(1, 40, LogSlotBehaviour::Punctual, "Orphan"));
    CHECK(!table.CommitUpdate());
    CHECK(fs.reads + fs.writes == afterWrite);
    CHECK(table.IsDirty());
    CHECK(table.Count() == 0);

    table.Flush();
    CHECK(fs.reads + fs.writes > afterWrite);
    CHECK(!table.IsDirty());
    CHECK(table.Revision() == 21);
    CHECK(table.Count() == 5);

    // Flushing again with nothing to say leaves the flash alone.
    const int afterReload = fs.reads + fs.writes;
    table.Flush();
    CHECK(fs.reads + fs.writes == afterReload);
  }

  if (failures == 0) {
    printf("\nall checks passed\n");
    return 0;
  }
  printf("\n%d checks failed\n", failures);
  return 1;
}
