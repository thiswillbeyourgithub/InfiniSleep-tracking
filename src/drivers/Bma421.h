#pragma once
#include <drivers/Bma421_C/bma4_defs.h>

namespace Pinetime {
  namespace Drivers {
    class TwiMaster;

    class Bma421 {
    public:
      enum class DeviceTypes : uint8_t { Unknown, BMA421, BMA425 };

      /// Which step of Init() failed, if any. Every step is silently fatal, so without this
      /// a watch whose accelerometer never starts looks identical to one that simply never moves.
      enum class InitStatus : uint8_t {
        Ok,
        NotReset,      // SoftReset() was not called before Init()
        DriverInit,    // bma423_init(), which also rejects an unrecognised chip id
        ConfigFile,    // bma423_write_config_file()
        InterruptMode, // bma4_set_interrupt_mode()
        FeatureEnable, // bma423_feature_enable()
        StepDetector,  // bma423_step_detector_enable()
        AccelEnable,   // bma4_set_accel_enable()
        AccelConfig,   // bma4_set_accel_config()
      };

      /// Everything known about why the sensor did or did not start. Grouped so the diagnostics
      /// can grow without threading another argument through MotionController::Init().
      struct Diagnostics {
        /// Raw chip id byte read back. When the device type is Unknown this is the value
        /// bma423_init() refused to accept.
        uint8_t chipId = 0;
        InitStatus status = InitStatus::NotReset;
        /// How many attempts the chip id read needed. Above 1 means it only answered after a
        /// retry, which is worth knowing before trusting the sensor.
        uint8_t attempts = 0;
        /// Whether the sensor acknowledged its address on the bus at all, and whether anything
        /// answered on the alternate address used when SDO is pulled high.
        bool addressAcked = false;
        bool altAddressAcked = false;
      };

      struct Values {
        uint32_t steps;
        int16_t x;
        int16_t y;
        int16_t z;
      };

      Bma421(TwiMaster& twiMaster, uint8_t twiAddress);
      Bma421(const Bma421&) = delete;
      Bma421& operator=(const Bma421&) = delete;
      Bma421(Bma421&&) = delete;
      Bma421& operator=(Bma421&&) = delete;

      /// The chip freezes the TWI bus after the softreset operation. Softreset is separated from the
      /// Init() method to allow the caller to uninit and then reinit the TWI device after the softreset.
      void SoftReset();
      void Init();
      Values Process();
      void ResetStepCounter();

      void Read(uint8_t registerAddress, uint8_t* buffer, size_t size);
      void Write(uint8_t registerAddress, const uint8_t* data, size_t size);

      bool IsOk() const;
      DeviceTypes DeviceType() const;

      const Diagnostics& GetDiagnostics() const;

    private:
      void Reset();

      TwiMaster& twiMaster;
      uint8_t deviceAddress = 0x18;
      struct bma4_dev bma;
      struct bma4_accel_config accel_conf; // Store the device configuration for later reference.
      bool isOk = false;
      bool isResetOk = false;
      DeviceTypes deviceType = DeviceTypes::Unknown;
      Diagnostics diagnostics;
    };
  }
}