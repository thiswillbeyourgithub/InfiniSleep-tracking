#pragma once

#include <cstdint>

#include "components/fs/FS.h"

namespace Pinetime {
  namespace Controllers {

    /// What a slot is for, which is also what the logging app does when it is tapped.
    ///
    /// A group is not loggable: it is a screen holding other slots. Everything else is, and says
    /// which of the event types it produces.
    enum class LogSlotBehaviour : uint8_t {
      Group = 0,
      /// "I took my medication", logged the moment it is tapped.
      Punctual = 1,
      /// Started and stopped, as two events.
      Continuous = 2,
      /// Asks for a reading, 0 to 10, and logs that.
      Valued = 3,
    };

    /// One entry of the table the phone pushes to the watch.
    ///
    /// The description the wearer writes stays on the phone, along with the order slots are listed
    /// in and everything else that does not fit on a watch.
    struct LogSlot {
      /// Fifteen characters and a terminator, which is about what fits across a 240 pixel screen at
      /// a readable size.
      static constexpr uint8_t labelSize = 16;

      /// What a slot at the top of the table has for a parent.
      static constexpr uint8_t noParent = 0xFF;

      /// The phone's own id for this slot, which is what an event carries. Never reused, so that an
      /// event logged against a slot the wearer has since retired still means what it meant.
      uint8_t id = 0;
      uint8_t parent = noParent;
      LogSlotBehaviour behaviour = LogSlotBehaviour::Punctual;
      char label[labelSize] = {};
    };

    /// The table of slots the watch is showing, as the phone last pushed it.
    ///
    /// Held in RAM because the logging app reads it on every screen it builds, and mirrored to a
    /// file so the watch still knows what to offer after a reboot with no phone in reach. The caps
    /// are what keep that affordable: a byte here is a byte LVGL does not get for building screens.
    ///
    /// The watch never invents a slot. It shows what it was given and logs ids, which is what makes
    /// renaming, reordering and regrouping on the phone free.
    class LogSlots {
    public:
      /// Two levels of groups, so at most a group inside a group, and events inside either.
      static constexpr uint8_t maxDepth = 2;
      static constexpr uint8_t maxSlots = 32;

      explicit LogSlots(Controllers::FS& fs);

      /// Must be called once the filesystem is up, before anything else.
      void Init();

      /// Starts receiving a table.
      ///
      /// The slots arriving are written over the ones being shown rather than beside them, since a
      /// second table's worth of RAM is a second table's worth LVGL does not get. A push that does
      /// not hold together, or stops half way, is followed by reading the file back, so the watch
      /// ends up showing what it last wrote down rather than half a table.
      void BeginUpdate(uint16_t revision);

      /// Appends one slot to the table being received. False when there is no room, which abandons
      /// the update rather than committing half a table.
      bool AddSlot(const LogSlot& slot);

      /// Checks the table over and, if it holds together, starts showing it and writes it out.
      /// False leaves the watch showing what it had.
      bool CommitUpdate();

      void AbandonUpdate();

      /// Which table the watch is showing, so the phone can tell whether it is the current one.
      /// Zero means the watch has never been given one.
      uint16_t Revision() const {
        return revision;
      }

      uint8_t Count() const {
        return count;
      }

      /// By position in the table, which is the order the phone listed them in.
      const LogSlot* At(uint8_t index) const;

      const LogSlot* Find(uint8_t id) const;

      /// The positions of the slots whose parent this is, in table order. Pass LogSlot::noParent
      /// for the top of the table.
      uint8_t ChildrenOf(uint8_t parentId, uint8_t* out, uint8_t max) const;

      /// True when the table is one group and nothing else, which the app uses to skip a screen
      /// that would only ever have one thing on it.
      bool HasSingleGroup() const;

      void Clear();

    private:
      static constexpr uint8_t fileFormatVersion = 1;
      static constexpr const char* filePath = "/.system/logslots.dat";

      /// Whether the table being received holds together: ids unique, parents present and groups,
      /// groups no deeper than maxDepth, labels not empty.
      bool IsValid(uint8_t entries) const;

      void LoadFromFile();
      void SaveToFile() const;

      Controllers::FS& fs;

      LogSlot slots[maxSlots];
      uint8_t count = 0;
      uint16_t revision = 0;

      /// What is being received, if anything. It goes over the table being shown, which is why
      /// giving up on an update means reading the file back.
      bool updating = false;
      uint8_t incoming = 0;
      uint16_t incomingRevision = 0;
    };
  }
}
