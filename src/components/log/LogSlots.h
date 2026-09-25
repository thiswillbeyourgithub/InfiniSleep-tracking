#pragma once

#include <cstdint>

#include <FreeRTOS.h>
#include <semphr.h>

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

      /// A slot the phone chose no colour for, which the app then draws in its own.
      ///
      /// Black is the one colour that cannot be asked for, since it doubles as "none asked for".
      /// That costs nothing in practice: the app draws on a black background, so a black icon is an
      /// invisible one, and nobody picks it on purpose.
      static constexpr uint32_t noColour = 0;

      /// The phone's own id for this slot, which is what an event carries. Never reused, so that an
      /// event logged against a slot the wearer has since retired still means what it meant.
      uint8_t id = 0;
      uint8_t parent = noParent;
      LogSlotBehaviour behaviour = LogSlotBehaviour::Punctual;

      /// What the wearer asked this slot to be drawn in, as 0x00RRGGBB, or noColour.
      ///
      /// The phone works out inheritance before sending, so a slot that takes its group's colour
      /// arrives already carrying it. That is deliberate: the watch draws one slot at a time and
      /// would otherwise have to walk up the table for every row it paints.
      uint32_t colour = noColour;

      char label[labelSize] = {};
    };

    /// How a colour travels, on the wire and in the file: red, green, blue, one byte each.
    ///
    /// Written out here rather than at each end so the two cannot drift apart, and kept as three
    /// separate bytes rather than a packed integer so that no end has to agree about byte order for
    /// a value whose natural reading order is R, G, B.
    static constexpr uint8_t colourBytes = 3;

    inline uint32_t ColourFromBytes(const uint8_t* bytes) {
      return (static_cast<uint32_t>(bytes[0]) << 16) | (static_cast<uint32_t>(bytes[1]) << 8) | static_cast<uint32_t>(bytes[2]);
    }

    inline void ColourToBytes(uint32_t colour, uint8_t* bytes) {
      bytes[0] = static_cast<uint8_t>((colour >> 16) & 0xFF);
      bytes[1] = static_cast<uint8_t>((colour >> 8) & 0xFF);
      bytes[2] = static_cast<uint8_t>(colour & 0xFF);
    }

    /// Whether writing on top of this colour has to be black rather than white to be read.
    ///
    /// The usual weighting of the three channels by how bright the eye finds them, cut at the middle.
    /// The phone makes the same call with the same numbers (LogSlots.wantsDarkText), so that a row
    /// reads the same way on both ends.
    inline bool WantsDarkText(uint32_t colour) {
      const uint32_t red = (colour >> 16) & 0xFF;
      const uint32_t green = (colour >> 8) & 0xFF;
      const uint32_t blue = colour & 0xFF;
      return red * 299 + green * 587 + blue * 114 >= 128 * 1000;
    }

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
      /// second table's worth of RAM is a second table's worth LVGL does not get. The table is
      /// therefore emptied here rather than at the end: the logging app reads it from another task,
      /// and an empty table is something it already knows how to draw, whereas a table half
      /// overwritten by a push in flight is not. A push that does not hold together, or stops half
      /// way, leaves it empty until the next Flush() reads the file back.
      void BeginUpdate(uint16_t revision);

      /// Appends one slot to the table being received. False when there is no room, which abandons
      /// the update rather than committing half a table.
      bool AddSlot(const LogSlot& slot);

      /// Checks the table over and, if it holds together, starts showing it. False leaves the
      /// watch showing nothing until Flush() puts the previous table back.
      ///
      /// Nothing is written to flash here: see Flush() for why the task this runs on must not.
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

      /// True when the file no longer matches what the watch is showing, either way round.
      bool IsDirty() const {
        return dirty || pendingReload;
      }

      /// Brings the file and the table back into step, writing one out or reading the other back.
      ///
      /// Split from the methods that change the table because those run on the BLE host task, which
      /// must not touch flash: the PineTime powers the external flash down and disables the SPI
      /// peripheral while the watch sleeps, and a write in that state blocks forever on a DMA
      /// completion that never comes, taking the SPI mutex and therefore the display with it, until
      /// the watchdog reboots the watch. A table arrives exactly when that is most likely, since
      /// the phone pushes it on connecting and the watch is usually asleep on a wrist at the time.
      /// So the table in RAM is the authority and this is its mirror, taken by the system task
      /// whenever the watch is awake anyway.
      ///
      /// Losing a committed table to a flat battery before the next wake costs nothing: the file
      /// still holds the older revision, and the phone pushes again as soon as it sees one it did
      /// not send.
      void Flush();

    private:
      /// Bumped to 2 when slots gained a colour. A file written by the older version is discarded
      /// rather than read short, which costs nothing: the watch then reports revision 0, the phone
      /// sees a revision it did not send, and pushes the table again.
      static constexpr uint8_t fileFormatVersion = 2;
      static constexpr const char* filePath = "/.system/logslots.dat";

      /// Whether the table being received holds together: ids unique, parents present and groups,
      /// groups no deeper than maxDepth, labels not empty.
      bool IsValid(uint8_t entries) const;

      /// What AbandonUpdate does once the lock is held, so that the mutators can give up part way
      /// through without taking a mutex they are already holding.
      void AbandonUpdateLocked();

      bool Lock() const;
      void Unlock() const;

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

      /// Set by a commit, cleared by the Flush() that writes the table out.
      bool dirty = false;

      /// Set when a push was given up on, which leaves the table empty because the slots arriving
      /// went over the ones being shown. Cleared by the Flush() that reads the file back.
      bool pendingReload = false;

      /// The table is written by the BLE host task, as the phone pushes it, and read by the display
      /// task, as the logging app draws it, so everything that walks the slots takes this. Count()
      /// and Revision() do not: a single aligned scalar needs no help.
      ///
      /// It does not extend to the slots At() and Find() hand out either, and does not need to: a
      /// push empties the table before it touches a single slot, so a caller comes away either with
      /// a pointer to a slot nobody is writing or with nullptr.
      mutable SemaphoreHandle_t mutex = nullptr;
    };
  }
}
