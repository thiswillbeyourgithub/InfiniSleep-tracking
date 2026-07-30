#include "components/ble/ActivityLogService.h"

#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_att.h>
#undef max
#undef min

#include "components/ble/NimbleController.h"
#include <nrf_log.h>

using namespace Pinetime::Controllers;

namespace {
  // 0006yyxx-78fc-48fe-8e23-433b3a1942d0
  constexpr ble_uuid128_t CharUuid(uint8_t x, uint8_t y) {
    return ble_uuid128_t {.u = {.type = BLE_UUID_TYPE_128},
                          .value = {0xd0, 0x42, 0x19, 0x3a, 0x3b, 0x43, 0x23, 0x8e, 0xfe, 0x48, 0xfc, 0x78, x, y, 0x06, 0x00}};
  }

  // 00060000-78fc-48fe-8e23-433b3a1942d0
  constexpr ble_uuid128_t BaseUuid() {
    return CharUuid(0x00, 0x00);
  }

  constexpr ble_uuid128_t activityLogServiceUuid {BaseUuid()};
  constexpr ble_uuid128_t controlPointCharUuid {CharUuid(0x01, 0x00)};
  constexpr ble_uuid128_t dataCharUuid {CharUuid(0x02, 0x00)};

  // Commands written by the host to the control point.
  constexpr uint8_t commandGetStatus = 0x01;
  constexpr uint8_t commandRequestRecords = 0x02;
  constexpr uint8_t commandRelease = 0x03;

  // Responses notified by the watch on the control point.
  constexpr uint8_t responseStatus = 0x81;
  constexpr uint8_t responseBatchComplete = 0x82;
  constexpr uint8_t responseError = 0x8F;

  constexpr uint8_t errorUnknownCommand = 0x01;
  constexpr uint8_t errorMalformedCommand = 0x02;
  constexpr uint8_t errorNotSubscribed = 0x03;

  bool IsConnected(uint16_t connectionHandle) {
    return connectionHandle != 0 && connectionHandle != BLE_HS_CONN_HANDLE_NONE;
  }

  void WriteUint16(uint8_t* buffer, uint16_t value) {
    buffer[0] = value & 0xFF;
    buffer[1] = value >> 8;
  }

  void WriteUint32(uint8_t* buffer, uint32_t value) {
    buffer[0] = value & 0xFF;
    buffer[1] = (value >> 8) & 0xFF;
    buffer[2] = (value >> 16) & 0xFF;
    buffer[3] = (value >> 24) & 0xFF;
  }

  uint32_t ReadUint32(const uint8_t* buffer) {
    return static_cast<uint32_t>(buffer[0]) | (static_cast<uint32_t>(buffer[1]) << 8) | (static_cast<uint32_t>(buffer[2]) << 16) |
           (static_cast<uint32_t>(buffer[3]) << 24);
  }

  int ActivityLogServiceCallback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg) {
    auto* activityLogService = static_cast<ActivityLogService*>(arg);
    return activityLogService->OnCommand(conn_handle, attr_handle, ctxt);
  }
}

ActivityLogService::ActivityLogService(NimbleController& nimble, ActivityLogProvider& provider)
  : nimble {nimble},
    provider {provider},
    characteristicDefinition {{.uuid = &controlPointCharUuid.u,
                               .access_cb = ActivityLogServiceCallback,
                               .arg = this,
                               .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                               .val_handle = &controlPointHandle},
                              {.uuid = &dataCharUuid.u,
                               .access_cb = ActivityLogServiceCallback,
                               .arg = this,
                               .flags = BLE_GATT_CHR_F_NOTIFY,
                               .val_handle = &dataHandle},
                              {0}},
    serviceDefinition {
      {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &activityLogServiceUuid.u, .characteristics = characteristicDefinition},
      {0},
    } {
}

void ActivityLogService::Init() {
  int res = 0;
  res = ble_gatts_count_cfg(serviceDefinition);
  ASSERT(res == 0);

  res = ble_gatts_add_svcs(serviceDefinition);
  ASSERT(res == 0);
}

int ActivityLogService::OnCommand(uint16_t connectionHandle, uint16_t attributeHandle, ble_gatt_access_ctxt* context) {
  if (attributeHandle != controlPointHandle || context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return 0;
  }

  // Flatten rather than reading context->om->om_data directly: a write can in principle arrive
  // spread over several mbufs, and the commands here are only a few bytes. A write longer than
  // any command fails here, which is the right answer for one.
  uint8_t command[5];
  uint16_t copied = 0;
  if (ble_hs_mbuf_to_flat(context->om, command, sizeof(command), &copied) != 0 || copied < 1) {
    NotifyError(connectionHandle, errorMalformedCommand);
    return 0;
  }

  switch (command[0]) {
    case commandGetStatus:
      HandleGetStatus(connectionHandle);
      break;
    case commandRequestRecords:
      if (copied < 5) {
        NotifyError(connectionHandle, errorMalformedCommand);
        break;
      }
      HandleRequestRecords(connectionHandle, ReadUint32(&command[1]));
      break;
    case commandRelease:
      if (copied < 5) {
        NotifyError(connectionHandle, errorMalformedCommand);
        break;
      }
      HandleRelease(connectionHandle, ReadUint32(&command[1]));
      break;
    default:
      NotifyError(connectionHandle, errorUnknownCommand);
      break;
  }

  return 0;
}

void ActivityLogService::HandleGetStatus(uint16_t connectionHandle) {
  NotifyStatus(connectionHandle);
}

void ActivityLogService::HandleRequestRecords(uint16_t connectionHandle, uint32_t sinceTimestamp) {
  const uint8_t wanted = RecordsPerNotification(connectionHandle);
  const uint8_t count = provider.ReadRecords(sinceTimestamp, recordBuffer, wanted);

  NRF_LOG_INFO("ActivityLog : %d records since %d", count, sinceTimestamp);

  if (count > 0) {
    // Only send the data notification when there is something to send. An empty batch is
    // reported by the 0x82 below, which is what tells the host to stop asking.
    if (!dataNotificationEnabled) {
      // Without a subscription the records would be dropped silently, and the host would then
      // read the 0x82 count as proof they arrived.
      NotifyError(connectionHandle, errorNotSubscribed);
      return;
    }

    for (uint8_t i = 0; i < count; i++) {
      uint8_t* out = &wireBuffer[i * bytesPerRecord];
      WriteUint32(&out[0], recordBuffer[i].timestamp);
      WriteUint16(&out[4], recordBuffer[i].motion);
      out[6] = recordBuffer[i].heartRate;
      out[7] = static_cast<uint8_t>(recordBuffer[i].kind);
    }

    if (!IsConnected(connectionHandle)) {
      return;
    }
    auto* om = ble_hs_mbuf_from_flat(wireBuffer, count * bytesPerRecord);
    if (om == nullptr) {
      // Out of mbufs. Say nothing rather than closing the batch: the host times out and
      // retries with the same timestamp, and no record is lost.
      return;
    }
    if (ble_gattc_notify_custom(connectionHandle, dataHandle, om) != 0) {
      return;
    }
  }

  const uint8_t response[2] = {responseBatchComplete, count};
  NotifyControl(connectionHandle, response, sizeof(response));
}

void ActivityLogService::HandleRelease(uint16_t connectionHandle, uint32_t upToTimestamp) {
  provider.Release(upToTimestamp);
  // Answer with the status so the host can see what is actually left rather than assume.
  NotifyStatus(connectionHandle);
}

void ActivityLogService::NotifyStatus(uint16_t connectionHandle) {
  uint8_t response[13];
  response[0] = responseStatus;
  response[1] = protocolVersion;
  response[2] = bytesPerRecord;
  WriteUint16(&response[3], provider.RecordCount());
  WriteUint32(&response[5], provider.OldestTimestamp());
  WriteUint32(&response[9], provider.NewestTimestamp());
  NotifyControl(connectionHandle, response, sizeof(response));
}

void ActivityLogService::NotifyError(uint16_t connectionHandle, uint8_t errorCode) {
  const uint8_t response[2] = {responseError, errorCode};
  NotifyControl(connectionHandle, response, sizeof(response));
}

void ActivityLogService::NotifyControl(uint16_t connectionHandle, const uint8_t* data, uint16_t size) {
  if (!IsConnected(connectionHandle)) {
    return;
  }
  auto* om = ble_hs_mbuf_from_flat(data, size);
  if (om == nullptr) {
    return;
  }
  ble_gattc_notify_custom(connectionHandle, controlPointHandle, om);
}

uint8_t ActivityLogService::RecordsPerNotification(uint16_t connectionHandle) const {
  uint16_t mtu = IsConnected(connectionHandle) ? ble_att_mtu(connectionHandle) : 0;
  if (mtu < BLE_ATT_MTU_DFLT) {
    mtu = BLE_ATT_MTU_DFLT;
  }
  // Three bytes of the MTU go to the notification's own header.
  const uint16_t records = (mtu - 3) / bytesPerRecord;
  if (records > maxRecordsPerBatch) {
    return maxRecordsPerBatch;
  }
  return static_cast<uint8_t>(records);
}

void ActivityLogService::SubscribeNotification(uint16_t attributeHandle) {
  if (attributeHandle == dataHandle) {
    dataNotificationEnabled = true;
  }
}

void ActivityLogService::UnsubscribeNotification(uint16_t attributeHandle) {
  if (attributeHandle == dataHandle) {
    dataNotificationEnabled = false;
  }
}
