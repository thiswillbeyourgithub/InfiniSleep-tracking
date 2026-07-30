#pragma once
#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#include <atomic>
#undef max
#undef min

#include <cstdint>

#include "components/activity/ActivityLog.h"

namespace Pinetime {
  namespace Controllers {
    class NimbleController;

    /// Lets a companion application pull recorded activity off the watch.
    ///
    /// The protocol is deliberately stateless on the watch side. A request carries the
    /// timestamp the host already has, and the watch answers with the next batch of records
    /// after it. The host repeats with the newest timestamp it received until it gets an empty
    /// batch. Nothing has to be remembered between requests, so a dropped connection costs at
    /// most one batch and a resumed transfer needs no special case.
    ///
    /// Wire format, all little endian.
    ///
    /// Control point, host writes:
    ///   0x01                    GetStatus
    ///   0x02 <uint32 since>     RequestRecords, records strictly newer than since
    ///   0x03 <uint32 upTo>      Release, host has durably stored everything up to and
    ///                           including upTo, the watch may reclaim the space
    ///
    /// Control point, watch notifies:
    ///   0x81 <uint8 protocolVersion> <uint8 recordSize> <uint16 recordCount>
    ///        <uint32 oldestTimestamp> <uint32 newestTimestamp>
    ///   0x82 <uint8 recordsInBatch>  batch finished, 0 means nothing left to send
    ///   0x8F <uint8 errorCode>       1 unknown command, 2 malformed command,
    ///                                3 data characteristic not subscribed
    ///
    /// Data, watch notifies:
    ///   recordsInBatch records back to back, no header. Each record is
    ///   <uint32 timestamp> <uint16 motion> <uint8 heartRate> <uint8 kind>.
    ///   motion 0xFFFF means not measured, heartRate 0 means not measured.
    ///
    /// The data notification is always sent before the 0x82 that closes the batch, so a host
    /// that has seen the 0x82 has already seen the records it counts.
    class ActivityLogService {
    public:
      ActivityLogService(NimbleController& nimble, ActivityLogProvider& provider);
      void Init();

      int OnCommand(uint16_t connectionHandle, uint16_t attributeHandle, ble_gatt_access_ctxt* context);

      void SubscribeNotification(uint16_t attributeHandle);
      void UnsubscribeNotification(uint16_t attributeHandle);

      static constexpr uint8_t protocolVersion = 1;

    private:
      /// Bounds the buffer below. Twenty records is 160 bytes on the wire, which a host that
      /// negotiated a larger MTU can take in one notification.
      static constexpr uint8_t maxRecordsPerBatch = 20;
      static constexpr uint8_t bytesPerRecord = 8;

      void HandleGetStatus(uint16_t connectionHandle);
      void HandleRequestRecords(uint16_t connectionHandle, uint32_t sinceTimestamp);
      void HandleRelease(uint16_t connectionHandle, uint32_t upToTimestamp);

      void NotifyStatus(uint16_t connectionHandle);
      void NotifyError(uint16_t connectionHandle, uint8_t errorCode);
      void NotifyControl(uint16_t connectionHandle, const uint8_t* data, uint16_t size);

      /// How many records fit in one notification on this connection.
      uint8_t RecordsPerNotification(uint16_t connectionHandle) const;

      NimbleController& nimble;
      ActivityLogProvider& provider;

      struct ble_gatt_chr_def characteristicDefinition[3];
      struct ble_gatt_svc_def serviceDefinition[2];

      uint16_t controlPointHandle {};
      uint16_t dataHandle {};
      std::atomic_bool dataNotificationEnabled {false};

      /// Members rather than locals: the BLE host task's stack is not the place for a few
      /// hundred bytes of scratch, and access is serialised by the host task anyway.
      ActivityRecord recordBuffer[maxRecordsPerBatch];
      uint8_t wireBuffer[maxRecordsPerBatch * bytesPerRecord];
    };
  }
}
