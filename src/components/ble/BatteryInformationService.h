#pragma once
#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#undef max
#undef min

namespace Pinetime {
  namespace System {
    class SystemTask;
  }

  namespace Controllers {
    class Battery;

    class BatteryInformationService {
    public:
      BatteryInformationService(Controllers::Battery& batteryController);
      void Init();
      /// Registers the voltage service. Called after every other service rather than from Init():
      /// a phone bonded to the watch keeps the attribute handles it discovered and the watch never
      /// tells it they moved, so a characteristic slotted in mid-table shifts every service after it
      /// and the phone's writes land on the wrong attributes. Added last, nothing that existed moves.
      void InitVoltageService();

      int OnBatteryServiceRequested(uint16_t attributeHandle, ble_gatt_access_ctxt* context);
      /// Sends the voltage first and the percentage after it, so a phone that keeps the last voltage
      /// it heard and stores it with each percentage stores the two from the same measurement.
      void NotifyBatteryLevel(uint16_t connectionHandle, uint8_t level, uint16_t voltage);

    private:
      Controllers::Battery& batteryController;
      static constexpr uint16_t batteryInformationServiceId {0x180F};
      static constexpr uint16_t batteryLevelId {0x2A19};

      static constexpr ble_uuid16_t batteryInformationServiceUuid {.u {.type = BLE_UUID_TYPE_16}, .value = batteryInformationServiceId};

      static constexpr ble_uuid16_t batteryLevelUuid {.u {.type = BLE_UUID_TYPE_16}, .value = batteryLevelId};

      /// 00080000-78fc-48fe-8e23-433b3a1942d0 and its one characteristic, 00080001, the measured voltage
      /// in millivolts, uint16 little endian. The standard service has no voltage, and the percentage
      /// alone hides what the battery is doing: the curve that turns one into the other is flat for
      /// most of a charge.
      static constexpr ble_uuid128_t batteryVoltageServiceUuid {
        .u {.type = BLE_UUID_TYPE_128},
        .value = {0xd0, 0x42, 0x19, 0x3a, 0x3b, 0x43, 0x23, 0x8e, 0xfe, 0x48, 0xfc, 0x78, 0x00, 0x00, 0x08, 0x00}};
      static constexpr ble_uuid128_t batteryVoltageUuid {
        .u {.type = BLE_UUID_TYPE_128},
        .value = {0xd0, 0x42, 0x19, 0x3a, 0x3b, 0x43, 0x23, 0x8e, 0xfe, 0x48, 0xfc, 0x78, 0x01, 0x00, 0x08, 0x00}};

      struct ble_gatt_chr_def characteristicDefinition[2];
      struct ble_gatt_svc_def serviceDefinition[2];
      struct ble_gatt_chr_def voltageCharacteristicDefinition[2];
      struct ble_gatt_svc_def voltageServiceDefinition[2];

      uint16_t batteryLevelHandle;
      uint16_t batteryVoltageHandle;
    };
  }
}
