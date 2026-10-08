#include "components/ble/BatteryInformationService.h"
#include <nrf_log.h>
#include "components/battery/BatteryController.h"

using namespace Pinetime::Controllers;

constexpr ble_uuid16_t BatteryInformationService::batteryInformationServiceUuid;
constexpr ble_uuid16_t BatteryInformationService::batteryLevelUuid;
constexpr ble_uuid128_t BatteryInformationService::batteryVoltageServiceUuid;
constexpr ble_uuid128_t BatteryInformationService::batteryVoltageUuid;

namespace {
  struct VoltageBytes {
    uint8_t value[2];
  };

  /// Little endian, the order every multi-byte value in the standard GATT characteristics uses.
  VoltageBytes ToBytes(uint16_t voltage) {
    return {{static_cast<uint8_t>(voltage & 0xff), static_cast<uint8_t>(voltage >> 8)}};
  }
}

int BatteryInformationServiceCallback(uint16_t /*conn_handle*/, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  auto* batteryInformationService = static_cast<BatteryInformationService*>(arg);
  return batteryInformationService->OnBatteryServiceRequested(attr_handle, ctxt);
}

BatteryInformationService::BatteryInformationService(Controllers::Battery& batteryController)
  : batteryController {batteryController},
    characteristicDefinition {{.uuid = &batteryLevelUuid.u,
                               .access_cb = BatteryInformationServiceCallback,
                               .arg = this,
                               .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                               .val_handle = &batteryLevelHandle},
                              {0}},
    serviceDefinition {
      {/* Device Information Service */
       .type = BLE_GATT_SVC_TYPE_PRIMARY,
       .uuid = &batteryInformationServiceUuid.u,
       .characteristics = characteristicDefinition},
      {0},
    },
    voltageCharacteristicDefinition {{.uuid = &batteryVoltageUuid.u,
                                      .access_cb = BatteryInformationServiceCallback,
                                      .arg = this,
                                      .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                                      .val_handle = &batteryVoltageHandle},
                                     {0}},
    voltageServiceDefinition {
      {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &batteryVoltageServiceUuid.u, .characteristics = voltageCharacteristicDefinition},
      {0},
    } {
}

void BatteryInformationService::Init() {
  int res = 0;
  res = ble_gatts_count_cfg(serviceDefinition);
  ASSERT(res == 0);

  res = ble_gatts_add_svcs(serviceDefinition);
  ASSERT(res == 0);
}

void BatteryInformationService::InitVoltageService() {
  int res = ble_gatts_count_cfg(voltageServiceDefinition);
  ASSERT(res == 0);

  res = ble_gatts_add_svcs(voltageServiceDefinition);
  ASSERT(res == 0);
}

int BatteryInformationService::OnBatteryServiceRequested(uint16_t attributeHandle, ble_gatt_access_ctxt* context) {
  if (attributeHandle == batteryLevelHandle) {
    NRF_LOG_INFO("BATTERY : handle = %d", batteryLevelHandle);
    uint8_t batteryValue = batteryController.PercentRemaining();
    int res = os_mbuf_append(context->om, &batteryValue, 1);
    return (res == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }
  if (attributeHandle == batteryVoltageHandle) {
    const VoltageBytes bytes = ToBytes(batteryController.Voltage());
    int res = os_mbuf_append(context->om, bytes.value, sizeof(bytes.value));
    return (res == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }
  return 0;
}

void BatteryInformationService::NotifyBatteryLevel(uint16_t connectionHandle, uint8_t level, uint16_t voltage) {
  const VoltageBytes bytes = ToBytes(voltage);
  ble_gattc_notify_custom(connectionHandle, batteryVoltageHandle, ble_hs_mbuf_from_flat(bytes.value, sizeof(bytes.value)));
  auto* om = ble_hs_mbuf_from_flat(&level, 1);
  ble_gattc_notify_custom(connectionHandle, batteryLevelHandle, om);
}
