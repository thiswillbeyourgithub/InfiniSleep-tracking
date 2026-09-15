#pragma once
// Stands in for the PWM driver so a vibration pattern can be asserted on instead of felt. Every
// pulse is recorded, which is how the tests check that an alert vibrates exactly as many times as
// the vibration count says and that a burst stays inside its window.
#include "task.h"

#include <cstdint>
#include <vector>

namespace Pinetime {
  namespace Controllers {
    class MotorController {
    public:
      struct Pulse {
        unsigned long tick;
        uint8_t strength;
        uint16_t durationMs;
      };

      std::vector<Pulse> pulses;

      void SetMotorStrength(uint8_t strength) {
        pendingStrength = strength;
      }

      // Records the pulse with the tick it started at, which is what lets a test reconstruct the
      // timing of a burst rather than only its length.
      void RunForDuration(uint16_t motorDuration) {
        pulses.push_back({xTaskGetTickCount(), pendingStrength, motorDuration});
      }

    private:
      uint8_t pendingStrength = 0;
    };
  }
}
