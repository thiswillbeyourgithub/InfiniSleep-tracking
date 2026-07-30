#include "drivers/Bma421.h"
#include <libraries/delay/nrf_delay.h>
#include <libraries/log/nrf_log.h>
#include "drivers/TwiMaster.h"
#include <drivers/Bma421_C/bma423.h>

using namespace Pinetime::Drivers;

namespace {
  int8_t user_i2c_read(uint8_t reg_addr, uint8_t* reg_data, uint32_t length, void* intf_ptr) {
    auto bma421 = static_cast<Bma421*>(intf_ptr);
    bma421->Read(reg_addr, reg_data, length);
    return 0;
  }

  int8_t user_i2c_write(uint8_t reg_addr, const uint8_t* reg_data, uint32_t length, void* intf_ptr) {
    auto bma421 = static_cast<Bma421*>(intf_ptr);
    bma421->Write(reg_addr, reg_data, length);
    return 0;
  }

  void user_delay(uint32_t period_us, void* /*intf_ptr*/) {
    nrf_delay_us(period_us);
  }

  // Scale factors to convert accelerometer counts to milli-g
  // from datasheet: https://files.pine64.org/doc/datasheet/pinetime/BST-BMA421-FL000.pdf
  // The array index to use is stored in accel_conf.range
  constexpr int16_t accelScaleFactors[] = {
    [BMA4_ACCEL_RANGE_2G] = 1024, // LSB/g +/- 2g range
    [BMA4_ACCEL_RANGE_4G] = 512,  // LSB/g +/- 4g range
    [BMA4_ACCEL_RANGE_8G] = 256,  // LSB/g +/- 8g range
    [BMA4_ACCEL_RANGE_16G] = 128  // LSB/g +/- 16g range
  };

  // The datasheet asks for a few milliseconds after the 0xB6 soft reset before the chip id
  // register reads back reliably. One millisecond was not always enough.
  constexpr uint32_t softResetDelayMs = 5;

  // Reading the chip id straight after the soft reset, with the TWI peripheral having just been
  // disabled and re-enabled, can return a corrupted byte: watches in this state report 0x01
  // rather than the 0x11 of a BMA421. bma4_init() already throws away one read for the same
  // reason when running over SPI, but does nothing equivalent for I2C, so retry here instead of
  // condemning a working sensor on the strength of a single bad transaction.
  constexpr uint8_t maxInitAttempts = 5;
  constexpr uint32_t initRetryDelayMs = 5;
}

Bma421::Bma421(TwiMaster& twiMaster, uint8_t twiAddress) : twiMaster {twiMaster}, deviceAddress {twiAddress} {
  bma.intf = BMA4_I2C_INTF;
  bma.bus_read = user_i2c_read;
  bma.bus_write = user_i2c_write;
  bma.variant = BMA42X_VARIANT;
  bma.intf_ptr = this;
  bma.delay_us = user_delay;
  bma.read_write_len = 16;
}

void Bma421::Init() {
  if (not isResetOk) {
    diagnostics.status = InitStatus::NotReset;
    return; // Call SoftReset (and reset TWI device) first!
  }

  // Probe first: a sensor that never acknowledges its address is a different problem from one
  // that answers with a chip id nobody recognises, and the chip id read alone cannot tell them
  // apart. The alternate address covers a board that pulled SDO high.
  diagnostics.addressAcked = twiMaster.Probe(deviceAddress);
  diagnostics.altAddressAcked = twiMaster.Probe(deviceAddress + 1);

  int8_t ret = BMA4_E_INVALID_SENSOR;
  for (uint8_t attempt = 1; attempt <= maxInitAttempts; attempt++) {
    diagnostics.attempts = attempt;
    ret = bma423_init(&bma);
    if (ret == BMA4_OK) {
      break;
    }
    nrf_delay_ms(initRetryDelayMs);
  }

  // Keep the chip id even when the driver rejected it: it is the only clue that tells an
  // unsupported sensor apart from nothing answering on the bus at all.
  diagnostics.chipId = bma.chip_id;
  if (ret != BMA4_OK) {
    diagnostics.status = InitStatus::DriverInit;
    return;
  }

  switch (bma.chip_id) {
    case BMA423_CHIP_ID:
      deviceType = DeviceTypes::BMA421;
      break;
    case BMA425_CHIP_ID:
      deviceType = DeviceTypes::BMA425;
      break;
    default:
      deviceType = DeviceTypes::Unknown;
      break;
  }

  ret = bma423_write_config_file(&bma);
  if (ret != BMA4_OK) {
    diagnostics.status = InitStatus::ConfigFile;
    return;
  }

  ret = bma4_set_interrupt_mode(BMA4_LATCH_MODE, &bma);
  if (ret != BMA4_OK) {
    diagnostics.status = InitStatus::InterruptMode;
    return;
  }

  ret = bma423_feature_enable(BMA423_STEP_CNTR, 1, &bma);
  if (ret != BMA4_OK) {
    diagnostics.status = InitStatus::FeatureEnable;
    return;
  }

  ret = bma423_step_detector_enable(0, &bma);
  if (ret != BMA4_OK) {
    diagnostics.status = InitStatus::StepDetector;
    return;
  }

  ret = bma4_set_accel_enable(1, &bma);
  if (ret != BMA4_OK) {
    diagnostics.status = InitStatus::AccelEnable;
    return;
  }

  accel_conf.odr = BMA4_OUTPUT_DATA_RATE_100HZ;
  accel_conf.range = BMA4_ACCEL_RANGE_2G;
  accel_conf.bandwidth = BMA4_ACCEL_NORMAL_AVG4;
  accel_conf.perf_mode = BMA4_CIC_AVG_MODE;
  ret = bma4_set_accel_config(&accel_conf, &bma);
  if (ret != BMA4_OK) {
    diagnostics.status = InitStatus::AccelConfig;
    return;
  }

  diagnostics.status = InitStatus::Ok;
  isOk = true;
}

void Bma421::Reset() {
  uint8_t data = 0xb6;
  twiMaster.Write(deviceAddress, 0x7E, &data, 1);
}

void Bma421::Read(uint8_t registerAddress, uint8_t* buffer, size_t size) {
  twiMaster.Read(deviceAddress, registerAddress, buffer, size);
}

void Bma421::Write(uint8_t registerAddress, const uint8_t* data, size_t size) {
  twiMaster.Write(deviceAddress, registerAddress, data, size);
}

Bma421::Values Bma421::Process() {
  if (not isOk)
    return {};
  struct bma4_accel rawData;
  struct bma4_accel data;
  bma4_read_accel_xyz(&rawData, &bma);

  // Scale the measured ADC counts to units of 'binary milli-g'
  // where 1g = 1024 'binary milli-g' units.
  // See https://github.com/InfiniTimeOrg/InfiniTime/pull/1950 for
  // discussion of why we opted for scaling to 1024 rather than 1000.
  data.x = 1024 * rawData.x / accelScaleFactors[accel_conf.range];
  data.y = 1024 * rawData.y / accelScaleFactors[accel_conf.range];
  data.z = 1024 * rawData.z / accelScaleFactors[accel_conf.range];

  uint32_t steps = 0;
  bma423_step_counter_output(&steps, &bma);

  // X and Y axis are swapped because of the way the sensor is mounted in the PineTime
  return {steps, data.y, data.x, data.z};
}

bool Bma421::IsOk() const {
  return isOk;
}

void Bma421::ResetStepCounter() {
  bma423_reset_step_counter(&bma);
}

void Bma421::SoftReset() {
  auto ret = bma4_soft_reset(&bma);
  if (ret == BMA4_OK) {
    isResetOk = true;
    nrf_delay_ms(softResetDelayMs);
  }
}

Bma421::DeviceTypes Bma421::DeviceType() const {
  return deviceType;
}

const Bma421::Diagnostics& Bma421::GetDiagnostics() const {
  return diagnostics;
}
