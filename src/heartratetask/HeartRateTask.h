#pragma once
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <components/heartrate/Ppg.h>

namespace Pinetime {
  namespace Drivers {
    class Hrs3300;
  }

  namespace Controllers {
    class HeartRateController;
  }

  namespace Applications {
    class HeartRateTask {
    public:
      enum class Messages : uint8_t { GoToSleep, WakeUp, StartMeasurement, StopMeasurement };
      enum class States { Idle, Running };

      explicit HeartRateTask(Drivers::Hrs3300& heartRateSensor, Controllers::HeartRateController& controller);
      void Start();
      void Work();
      void PushMessage(Messages msg);

      /// Whether a measurement is running, meaning the sensor is powered and the loop is feeding
      /// samples to Ppg, or would be again on the next WakeUp.
      ///
      /// This is the only honest answer to "is the sensor already taken". HeartRateController's
      /// state cannot stand in for it: the task writes that state from its sample loop, so a
      /// measurement that has just been stopped can leave it reading Running for one more cycle,
      /// long after the flag below has gone false.
      bool IsMeasuring() const {
        return measurementStarted;
      }

    private:
      static void Process(void* instance);
      void StartMeasurement();
      void StopMeasurement();

      TaskHandle_t taskHandle;
      QueueHandle_t messageQueue;
      States state = States::Running;
      Drivers::Hrs3300& heartRateSensor;
      Controllers::HeartRateController& controller;
      Controllers::Ppg ppg;
      bool measurementStarted = false;
    };

  }
}
