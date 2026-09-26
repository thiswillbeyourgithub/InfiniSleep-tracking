#pragma once

#include <cstdint>

// Stands in for the BLE heart rate service, which HeartRateController notifies on every new value.
// Only counts the notifications, since the controller's own bookkeeping is what is under test.
namespace Pinetime {
  namespace Controllers {
    class HeartRateService {
    public:
      void OnNewHeartRateValue(uint8_t /*heartRateValue*/) {
        notifications++;
      }

      int notifications = 0;
    };
  }
}
