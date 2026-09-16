#include "components/ble/EventLogService.h"

#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_att.h>
#undef max
#undef min

#include <cstring>

#include "components/ble/NimbleController.h"
#include <nrf_log.h>

using namespace Pinetime::Controllers;

namespace {
  // 0007yyxx-78fc-48fe-8e23-433b3a1942d0, alongside the activity log's 0006 family.
  constexpr ble_uuid128_t CharUuid(uint8_t x, uint8_t y) {
    return ble_uuid128_t {.u = {.type = BLE_UUID_TYPE_128},
                          .value = {0xd0, 0x42, 0x19, 0x3a, 0x3b, 0x43, 0x23, 0x8e, 0xfe, 0x48, 0xfc, 0x78, x, y, 0x07, 0x00}};
  }

  constexpr ble_uuid128_t eventLogServiceUuid {CharUuid(0x00, 0x00)};
  constexpr ble_uuid128_t controlPointCharUuid {CharUuid(0x01, 0x00)};
  constexpr ble_uuid128_t dataCharUuid {CharUuid(0x02, 0x00)};

  // Commands written by the host to the control point.
  constexpr uint8_t commandGetStatus = 0x01;
  constexpr uint8_t commandRequestEvents = 0x02;
  constexpr uint8_t commandRelease = 0x03;
  constexpr uint8_t commandBeginSlots = 0x10;
  constexpr uint8_t commandAddSlot = 0x11;
  constexpr uint8_t commandCommitSlots = 0x12;

  // Responses notified by the watch on the control point.
  constexpr uint8_t responseStatus = 0x81;
  constexpr uint8_t responseBatchComplete = 0x82;
  constexpr uint8_t responseSlotsResult = 0x83;
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

  uint16_t ReadUint16(const uint8_t* buffer) {
    return static_cast<uint16_t>(buffer[0]) | (static_cast<uint16_t>(buffer[1]) << 8);
  }

  uint32_t ReadUint32(const uint8_t* buffer) {
    return static_cast<uint32_t>(buffer[0]) | (static_cast<uint32_t>(buffer[1]) << 8) | (static_cast<uint32_t>(buffer[2]) << 16) |
           (static_cast<uint32_t>(buffer[3]) << 24);
  }

  int EventLogServiceCallback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg) {
    auto* eventLogService = static_cast<EventLogService*>(arg);
    return eventLogService->OnCommand(conn_handle, attr_handle, ctxt);
  }
}

EventLogService::EventLogService(NimbleController& nimble, EventLogProvider& provider, LogSlots& slots)
  : nimble {nimble},
    provider {provider},
    slots {slots},
    characteristicDefinition {{.uuid = &controlPointCharUuid.u,
                               .access_cb = EventLogServiceCallback,
                               .arg = this,
                               .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                               .val_handle = &controlPointHandle},
                              {.uuid = &dataCharUuid.u,
                               .access_cb = EventLogServiceCallback,
                               .arg = this,
                               .flags = BLE_GATT_CHR_F_NOTIFY,
                               .val_handle = &dataHandle},
                              {0}},
    serviceDefinition {
      {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &eventLogServiceUuid.u, .characteristics = characteristicDefinition},
      {0},
    } {
}

void EventLogService::Init() {
  int res = 0;
  res = ble_gatts_count_cfg(serviceDefinition);
  ASSERT(res == 0);

  res = ble_gatts_add_svcs(serviceDefinition);
  ASSERT(res == 0);
}

int EventLogService::OnCommand(uint16_t connectionHandle, uint16_t attributeHandle, ble_gatt_access_ctxt* context) {
  if (attributeHandle != controlPointHandle || context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return 0;
  }

  // Flattened rather than read out of context->om->om_data directly: a write can in principle
  // arrive spread over several mbufs. A write longer than the longest command fails here, which is
  // the right answer for one.
  uint8_t command[maxCommandSize];
  uint16_t copied = 0;
  if (ble_hs_mbuf_to_flat(context->om, command, sizeof(command), &copied) != 0 || copied < 1) {
    NotifyError(connectionHandle, errorMalformedCommand);
    return 0;
  }

  switch (command[0]) {
    case commandGetStatus:
      NotifyStatus(connectionHandle);
      break;
    case commandRequestEvents:
      if (copied < 5) {
        NotifyError(connectionHandle, errorMalformedCommand);
        break;
      }
      HandleRequestEvents(connectionHandle, ReadUint32(&command[1]));
      break;
    case commandRelease:
      if (copied < 5) {
        NotifyError(connectionHandle, errorMalformedCommand);
        break;
      }
      HandleRelease(connectionHandle, ReadUint32(&command[1]));
      break;
    case commandBeginSlots:
      if (copied < 3) {
        NotifyError(connectionHandle, errorMalformedCommand);
        break;
      }
      slots.BeginUpdate(ReadUint16(&command[1]));
      break;
    case commandAddSlot:
      HandleAddSlot(connectionHandle, command, copied);
      break;
    case commandCommitSlots:
      HandleCommitSlots(connectionHandle);
      break;
    default:
      NotifyError(connectionHandle, errorUnknownCommand);
      break;
  }

  return 0;
}

void EventLogService::HandleRequestEvents(uint16_t connectionHandle, uint32_t sinceSequence) {
  const uint8_t wanted = EventsPerNotification(connectionHandle);
  const uint8_t count = provider.ReadEvents(sinceSequence, eventBuffer, wanted);

  NRF_LOG_INFO("EventLog : %d events since %d", count, sinceSequence);

  if (count > 0) {
    // Only send the data notification when there is something to send. An empty batch is reported
    // by the 0x82 below, which is what tells the host to stop asking.
    if (!dataNotificationEnabled) {
      // Without a subscription the events would be dropped silently, and the host would then read
      // the 0x82 count as proof they arrived.
      NotifyError(connectionHandle, errorNotSubscribed);
      return;
    }

    for (uint8_t i = 0; i < count; i++) {
      uint8_t* out = &wireBuffer[i * bytesPerEvent];
      WriteUint32(&out[0], eventBuffer[i].sequence);
      WriteUint32(&out[4], eventBuffer[i].timestamp);
      out[8] = eventBuffer[i].slot;
      out[9] = static_cast<uint8_t>(static_cast<uint8_t>(eventBuffer[i].type) | ((eventBuffer[i].value & 0x0F) << 2) |
                                    (eventBuffer[i].flagged ? 0x40 : 0));
    }

    if (!IsConnected(connectionHandle)) {
      return;
    }
    auto* om = ble_hs_mbuf_from_flat(wireBuffer, count * bytesPerEvent);
    if (om == nullptr) {
      // Out of mbufs. Say nothing rather than closing the batch: the host times out and retries
      // with the same sequence, and no event is lost.
      return;
    }
    if (ble_gattc_notify_custom(connectionHandle, dataHandle, om) != 0) {
      return;
    }
  }

  const uint8_t response[2] = {responseBatchComplete, count};
  NotifyControl(connectionHandle, response, sizeof(response));
}

void EventLogService::HandleRelease(uint16_t connectionHandle, uint32_t upToSequence) {
  provider.ReleaseEvents(upToSequence);
  // Answer with the status so the host can see what is actually left rather than assume.
  NotifyStatus(connectionHandle);
}

void EventLogService::HandleAddSlot(uint16_t connectionHandle, const uint8_t* command, uint16_t size) {
  if (size < 4) {
    NotifyError(connectionHandle, errorMalformedCommand);
    return;
  }

  LogSlot slot;
  slot.id = command[1];
  slot.parent = command[2];
  slot.behaviour = static_cast<LogSlotBehaviour>(command[3]);

  const uint16_t labelBytes = size - 4;
  const uint16_t kept = labelBytes < LogSlot::labelSize - 1 ? labelBytes : LogSlot::labelSize - 1;
  std::memcpy(slot.label, &command[4], kept);
  slot.label[kept] = '\0';

  if (!slots.AddSlot(slot)) {
    // The table is full, or nothing had said it was starting one. Either way the update is over,
    // and saying so now saves the host sending the rest of it.
    NotifySlotsResult(connectionHandle, false);
  }
}

void EventLogService::HandleCommitSlots(uint16_t connectionHandle) {
  NotifySlotsResult(connectionHandle, slots.CommitUpdate());
}

void EventLogService::NotifyStatus(uint16_t connectionHandle) {
  uint8_t response[16];
  response[0] = responseStatus;
  response[1] = protocolVersion;
  response[2] = bytesPerEvent;
  WriteUint16(&response[3], provider.EventCount());
  WriteUint32(&response[5], provider.OldestSequence());
  WriteUint32(&response[9], provider.NewestSequence());
  WriteUint16(&response[13], slots.Revision());
  response[15] = slots.Count();
  NotifyControl(connectionHandle, response, sizeof(response));
}

void EventLogService::NotifySlotsResult(uint16_t connectionHandle, bool accepted) {
  uint8_t response[4];
  response[0] = responseSlotsResult;
  response[1] = accepted ? 1 : 0;
  WriteUint16(&response[2], slots.Revision());
  NotifyControl(connectionHandle, response, sizeof(response));
}

void EventLogService::NotifyError(uint16_t connectionHandle, uint8_t errorCode) {
  const uint8_t response[2] = {responseError, errorCode};
  NotifyControl(connectionHandle, response, sizeof(response));
}

void EventLogService::NotifyControl(uint16_t connectionHandle, const uint8_t* data, uint16_t size) {
  if (!IsConnected(connectionHandle)) {
    return;
  }
  auto* om = ble_hs_mbuf_from_flat(data, size);
  if (om == nullptr) {
    return;
  }
  ble_gattc_notify_custom(connectionHandle, controlPointHandle, om);
}

uint8_t EventLogService::EventsPerNotification(uint16_t connectionHandle) const {
  uint16_t mtu = IsConnected(connectionHandle) ? ble_att_mtu(connectionHandle) : 0;
  if (mtu < BLE_ATT_MTU_DFLT) {
    mtu = BLE_ATT_MTU_DFLT;
  }
  // Three bytes of the MTU go to the notification's own header.
  const uint16_t events = (mtu - 3) / bytesPerEvent;
  if (events > maxEventsPerBatch) {
    return maxEventsPerBatch;
  }
  return static_cast<uint8_t>(events);
}

void EventLogService::SubscribeNotification(uint16_t attributeHandle) {
  if (attributeHandle == dataHandle) {
    dataNotificationEnabled = true;
  }
}

void EventLogService::UnsubscribeNotification(uint16_t attributeHandle) {
  if (attributeHandle == dataHandle) {
    dataNotificationEnabled = false;
  }
}
