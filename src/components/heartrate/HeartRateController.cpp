#include "components/heartrate/HeartRateController.h"
#include <heartratetask/HeartRateTask.h>
#include <systemtask/SystemTask.h>

using namespace Pinetime::Controllers;

void HeartRateController::Update(HeartRateController::States newState, uint8_t heartRate, uint8_t uncertainty) {
  this->state = newState;
  this->uncertainty = uncertainty;
  if (this->heartRate != heartRate) {
    this->heartRate = heartRate;
    if (bleNotificationsEnabled) {
      service->OnNewHeartRateValue(heartRate);
    }
  }
}

void HeartRateController::Start() {
  if (task != nullptr) {
    state = States::NotEnoughData;
    // Whatever the last run left behind is not this run's reading, and must not be taken for one
    // before the sensor has said anything.
    uncertainty = 0;
    task->PushMessage(Pinetime::Applications::HeartRateTask::Messages::StartMeasurement);
  }
}

void HeartRateController::Stop() {
  if (task != nullptr) {
    state = States::Stopped;
    task->PushMessage(Pinetime::Applications::HeartRateTask::Messages::StopMeasurement);
  }
}

void HeartRateController::SetHeartRateTask(Pinetime::Applications::HeartRateTask* task) {
  this->task = task;
}

void HeartRateController::SetService(Pinetime::Controllers::HeartRateService* service) {
  this->service = service;
}
