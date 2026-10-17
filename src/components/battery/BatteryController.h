#pragma once
#include <cstdint>
#include <components/battery/BatteryCurve.h>
#include <components/battery/LearnedCurve.h>
#include <components/fs/FS.h>
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

      /// Reads back the curve learned before the last reboot. After FS::Init.
      void Init(FS& fs);

      /// Whether the learned curve or the termination changed since they were last written.
      bool IsDirty() const {
        return dirty;
      }

      /// Writes them to flash. Only while the flash is awake: see OnPowerChanged.
      void Flush();

      uint8_t PercentRemaining() const {
        return percentRemaining;
      }

      /// The voltage corrected for this watch's divider and ADC reference, the same one the
      /// percentage is read from, so that the two shown side by side agree. The raw reading stays
      /// private: only the termination it is compared against needs it.
      uint16_t Voltage() const {
        return BatteryCurve::Calibrated(voltage, observedTermination);
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
      /// Kept in flash beside the learned curve, since that curve is drawn on the calibrated scale:
      /// read with no calibration after a reboot it would be off by the watch's whole error until
      /// the next charge finished.
      uint16_t observedTermination = BatteryCurve::noTerminationSeen;

      /// The curve learned from this watch's own discharges, read in the ADC interrupt and folded
      /// into on plug-in from the system task, inside a critical section.
      LearnedCurve learnedCurve;
      TickType_t lastSampleTick = 0;
      FS* fs = nullptr;

      static constexpr const char* learnedCurvePath = "/battery_curve.dat";
      static constexpr uint8_t learnedCurveVersion = 1;
      struct LearnedCurveFile {
        uint8_t version;
        uint16_t observedTermination;
        LearnedCurve::Minutes minutes;
      };
      /// The termination last written, so that unplugging writes only when a charge changed it.
      uint16_t savedTermination = BatteryCurve::noTerminationSeen;

      bool dirty = false;

      void OnPowerChanged();

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
