#pragma once
#include <cstdint>
#include <components/battery/BatteryCurve.h>
#include <drivers/include/nrfx_saadc.h>
#include <systemtask/SystemTask.h>

namespace Pinetime {
  namespace Controllers {

    class Battery {
    public:
      Battery();

      void ReadPowerState();
      void MeasureVoltage();
      void Register(System::SystemTask* systemTask);

      uint8_t PercentRemaining() const {
        return percentRemaining;
      }

      uint16_t Voltage() const {
        return voltage;
      }

      bool IsCharging() const {
        // isCharging will go up and down when fully charged
        // isFull makes sure this returns false while fully charged.
        return isCharging && !isFull;
      }

      bool IsPowerPresent() const {
        return isPowerPresent;
      }

    private:
      /// The highest voltage seen while charging was finished, which is this watch's own reading of
      /// a voltage the curve knows the real value of.
      ///
      /// Deliberately not written to flash. It is relearned the first time the watch is charged,
      /// which is every few days, so persisting it would buy one cycle of accuracy after a reboot
      /// in exchange for a flash write the watch otherwise never makes.
      uint16_t observedTermination = BatteryCurve::noTerminationSeen;

      static Battery* instance;
      nrf_saadc_value_t saadc_value;

      static constexpr nrf_saadc_input_t batteryVoltageAdcInput = NRF_SAADC_INPUT_AIN7;
      uint16_t voltage = 0;
      uint8_t percentRemaining = 0;

      bool isFull = false;
      bool isCharging = false;
      bool isPowerPresent = false;
      bool firstMeasurement = true;

      void SaadcInit();

      void SaadcEventHandler(nrfx_saadc_evt_t const* p_event);
      static void AdcCallbackStatic(nrfx_saadc_evt_t const* event);

      bool isReading = false;

      Pinetime::System::SystemTask* systemTask = nullptr;
    };
  }
}
