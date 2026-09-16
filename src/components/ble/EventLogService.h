#pragma once
#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#include <atomic>
#undef max
#undef min

#include <cstdint>

#include "components/log/EventLog.h"
#include "components/log/LogSlots.h"

namespace Pinetime {
  namespace Controllers {
    class NimbleController;

    /// Lets a companion application push the table of what can be logged, and pull back what was.
    ///
    /// Both halves live in one service because they are two ends of the same conversation: the host
    /// says what the slots are, the watch logs their ids, and the host reads the ids back. A host
    /// that has never pushed a table gets nothing to read, which is correct rather than a special
    /// case.
    ///
    /// As with the activity log, the pull is stateless on the watch side. A request carries the
    /// sequence the host already has and the watch answers with the next batch after it, so a
    /// dropped connection costs at most one batch. Events are numbered rather than dated because a
    /// wearer logging three things at once produces three events in the same minute.
    ///
    /// Wire format, all little endian.
    ///
    /// Control point, host writes:
    ///   0x01                      GetStatus
    ///   0x02 <uint32 since>       RequestEvents, events numbered above since
    ///   0x03 <uint32 upTo>        Release, host has durably stored everything up to and including
    ///                             upTo, the watch may reclaim the space
    ///   0x10 <uint16 revision>    BeginSlots, start pushing a table
    ///   0x11 <uint8 id> <uint8 parent> <uint8 behaviour> <label>
    ///                             AddSlot, label is up to 15 bytes of UTF-8, no terminator needed.
    ///                             parent 0xFF means the top of the table. behaviour is 0 group,
    ///                             1 punctual, 2 continuous, 3 asks for a value.
    ///   0x12                      CommitSlots, show the table if it holds together
    ///
    /// Control point, watch notifies:
    ///   0x81 <uint8 protocolVersion> <uint8 eventSize> <uint16 eventCount>
    ///        <uint32 oldestSequence> <uint32 newestSequence>
    ///        <uint16 slotRevision> <uint8 slotCount>
    ///   0x82 <uint8 eventsInBatch>   batch finished, 0 means nothing left to send
    ///   0x83 <uint8 accepted> <uint16 revision>  the table was taken, or refused and the revision
    ///        is the one still being shown
    ///   0x8F <uint8 errorCode>       1 unknown command, 2 malformed command,
    ///                                3 data characteristic not subscribed
    ///
    /// Data, watch notifies:
    ///   eventsInBatch events back to back, no header. Each event is
    ///   <uint32 sequence> <uint32 timestamp> <uint8 slot> <uint8 packed>, where packed holds the
    ///   type in bits 0 and 1, the value in bits 2 to 5 (15 meaning the slot does not ask for one)
    ///   and whether the wearer flagged it in bit 6.
    ///
    /// The data notification is always sent before the 0x82 that closes the batch, so a host that
    /// has seen the 0x82 has already seen the events it counts.
    class EventLogService {
    public:
      EventLogService(NimbleController& nimble, EventLogProvider& provider, LogSlots& slots);
      void Init();

      int OnCommand(uint16_t connectionHandle, uint16_t attributeHandle, ble_gatt_access_ctxt* context);

      void SubscribeNotification(uint16_t attributeHandle);
      void UnsubscribeNotification(uint16_t attributeHandle);

      static constexpr uint8_t protocolVersion = 1;

    private:
      /// Bounds the buffer below. Twelve events is 120 bytes on the wire, which a host that
      /// negotiated a larger MTU can take in one notification.
      static constexpr uint8_t maxEventsPerBatch = 12;
      static constexpr uint8_t bytesPerEvent = 10;

      /// The longest command is a slot: three bytes of its own and a label.
      static constexpr uint8_t maxCommandSize = 4 + LogSlot::labelSize;

      void HandleRequestEvents(uint16_t connectionHandle, uint32_t sinceSequence);
      void HandleRelease(uint16_t connectionHandle, uint32_t upToSequence);
      void HandleAddSlot(uint16_t connectionHandle, const uint8_t* command, uint16_t size);
      void HandleCommitSlots(uint16_t connectionHandle);

      void NotifyStatus(uint16_t connectionHandle);
      void NotifySlotsResult(uint16_t connectionHandle, bool accepted);
      void NotifyError(uint16_t connectionHandle, uint8_t errorCode);
      void NotifyControl(uint16_t connectionHandle, const uint8_t* data, uint16_t size);

      /// How many events fit in one notification on this connection.
      uint8_t EventsPerNotification(uint16_t connectionHandle) const;

      NimbleController& nimble;
      EventLogProvider& provider;
      LogSlots& slots;

      struct ble_gatt_chr_def characteristicDefinition[3];
      struct ble_gatt_svc_def serviceDefinition[2];

      uint16_t controlPointHandle {};
      uint16_t dataHandle {};
      std::atomic_bool dataNotificationEnabled {false};

      /// Members rather than locals: the BLE host task's stack is not the place for a few hundred
      /// bytes of scratch, and access is serialised by the host task anyway.
      LoggedEvent eventBuffer[maxEventsPerBatch];
      uint8_t wireBuffer[maxEventsPerBatch * bytesPerEvent];
    };
  }
}
