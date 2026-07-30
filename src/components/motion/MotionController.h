#pragma once

#include <atomic>
#include <cstdint>

#include <FreeRTOS.h>

#include "drivers/Bma421.h"
#include "components/ble/MotionService.h"
#include "utility/CircularBuffer.h"

namespace Pinetime {
  namespace Controllers {
    class MotionController {
    public:
      enum class DeviceTypes {
        Unknown,
        BMA421,
        BMA425,
      };

      void Update(int16_t x, int16_t y, int16_t z, uint32_t nbSteps);

      int16_t X() const {
        return xHistory[0];
      }

      int16_t Y() const {
        return yHistory[0];
      }

      int16_t Z() const {
        return zHistory[0];
      }

      uint32_t NbSteps() const {
        return nbSteps;
      }

      void ResetTrip() {
        currentTripSteps = 0;
      }

      uint32_t GetTripSteps() const {
        return currentTripSteps;
      }

      bool ShouldShakeWake(uint16_t thresh);
      bool ShouldRaiseWake() const;
      bool ShouldLowerSleep() const;

      int32_t CurrentShakeSpeed() const {
        return accumulatedSpeed;
      }

      /// Motion accumulated since the last call, and resets the accumulator.
      ///
      /// This is an actigraphy count: the total distance the acceleration vector travelled
      /// between samples, which is what a wrist actigraph reports and what sleep software
      /// expects. It is a magnitude of a difference rather than a difference of magnitudes,
      /// because the latter cancels out under exactly the back and forth movement that is the
      /// signal of interest.
      ///
      /// Scaled down by activityCountShift and saturating, so a long epoch of vigorous
      /// movement still fits the caller's 16 bits instead of wrapping to a small number.
      uint16_t TakeActivityCounts();

      DeviceTypes DeviceType() const {
        return deviceType;
      }

      /// Why the sensor did or did not start. Mostly of interest when DeviceType() is Unknown,
      /// in which case the whole controller reports zeros forever.
      const Pinetime::Drivers::Bma421::Diagnostics& GetDiagnostics() const {
        return diagnostics;
      }

      void Init(Pinetime::Drivers::Bma421::DeviceTypes types, const Pinetime::Drivers::Bma421::Diagnostics& diagnostics);

      void SetService(Pinetime::Controllers::MotionService* service) {
        this->service = service;
      }

      Pinetime::Controllers::MotionService* GetService() const {
        return service;
      }

    private:
      uint32_t nbSteps = 0;
      uint32_t currentTripSteps = 0;

      TickType_t lastTime = 0;
      TickType_t time = 0;

      struct AccelStats {
        static constexpr uint8_t numHistory = 2;

        int16_t xMean = 0;
        int16_t yMean = 0;
        int16_t zMean = 0;
        int16_t prevXMean = 0;
        int16_t prevYMean = 0;
        int16_t prevZMean = 0;

        uint32_t xVariance = 0;
        uint32_t yVariance = 0;
        uint32_t zVariance = 0;
      };

      AccelStats GetAccelStats() const;

      AccelStats stats = {};

      static constexpr uint8_t histSize = 8;
      Utility::CircularBuffer<int16_t, histSize> xHistory = {};
      Utility::CircularBuffer<int16_t, histSize> yHistory = {};
      Utility::CircularBuffer<int16_t, histSize> zHistory = {};
      int32_t accumulatedSpeed = 0;

      /// Raw units are roughly 1024 per g and Update() runs at 10 Hz, so a quarter hour of
      /// ordinary sensor noise alone reaches six figures. Shifting by 6 keeps a still night
      /// and a restless one both inside 16 bits while staying well clear of quantising the
      /// still night down to nothing.
      static constexpr uint8_t activityCountShift = 6;
      /// Written by whichever task polls the sensor and read by whichever task closes an
      /// epoch, which are not required to be the same one.
      std::atomic<uint32_t> activityCounts {0};

      DeviceTypes deviceType = DeviceTypes::Unknown;
      Pinetime::Drivers::Bma421::Diagnostics diagnostics;
      Pinetime::Controllers::MotionService* service = nullptr;
    };
  }
}
