#pragma once

#include <cstdint>

// Stands in for the heart rate task, which the controller only ever pushes messages to. The test
// plays the task's part itself by calling HeartRateController::Update, as the real one does from
// its sample loop.
namespace Pinetime {
  namespace Applications {
    class HeartRateTask {
    public:
      enum class Messages : uint8_t { GoToSleep, WakeUp, StartMeasurement, StopMeasurement };

      void PushMessage(Messages msg) {
        lastMessage = msg;
      }

      Messages lastMessage = Messages::GoToSleep;
    };
  }
}
