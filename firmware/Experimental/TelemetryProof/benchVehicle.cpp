#include "benchVehicle.h"

#if defined(BAJOLS_BCP_ENABLE_BENCH_CONTROL)
#include <Arduino.h>
#include <Wire.h>

#include "data_utils.h"

namespace bajols {
namespace vehicle {
namespace {

constexpr uint8_t kEngineIn1 = 22;
constexpr uint8_t kEngineIn2 = 23;
constexpr uint8_t kPumpIn3 = 24;
constexpr uint8_t kPumpIn4 = 25;
constexpr uint8_t kOwnerPin = 28;
constexpr uint8_t kStopPin = 29;
constexpr uint8_t kResetPin = 30;

constexpr uint8_t kShieldAddress = 0x40;
constexpr uint8_t kMode1Register = 0x00;
constexpr uint8_t kMode2Register = 0x01;
constexpr uint8_t kChannel0Register = 0x06;
constexpr uint8_t kPrescaleRegister = 0xFE;
constexpr uint8_t kAutoIncrement = 0x20;
constexpr uint8_t kSleep = 0x10;
constexpr uint8_t kRestart = 0x80;
constexpr uint8_t kPushPull = 0x04;
constexpr uint8_t kServoPrescale = 131;  // Nominal 50 Hz with a 27 MHz clock.
constexpr uint8_t kRudderChannel = 0;
constexpr uint8_t kDivePlaneChannel = 1;
constexpr uint8_t kThrottleChannel = 2;
constexpr uint8_t kPumpEnableChannel = 3;
constexpr uint16_t kPwmFullOff = 4096;
constexpr uint16_t kMaxMotorTicks =
    819;  // At most 20% of the 4096-count period.
constexpr int16_t kFullEffort = 1000;
constexpr uint32_t kResetDebounceMs = 25;
constexpr uint32_t kShieldCheckIntervalMs = 50;

bool WriteRegister(uint8_t address, uint8_t value) {
  Wire.beginTransmission(kShieldAddress);
  Wire.write(address);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool CheckRegister(uint8_t address, uint8_t value, uint8_t mask = 0xFF) {
  Wire.beginTransmission(kShieldAddress);
  Wire.write(address);
  return Wire.endTransmission(false) == 0 &&
         Wire.requestFrom(kShieldAddress, static_cast<uint8_t>(1)) == 1 &&
         (Wire.read() & mask) == value;
}

bool WritePwm(uint8_t channel, uint16_t ticks) {
  Wire.beginTransmission(kShieldAddress);
  Wire.write(static_cast<uint8_t>(kChannel0Register + 4 * channel));
  Wire.write(static_cast<uint8_t>(0));  // Start each pulse at count zero.
  Wire.write(static_cast<uint8_t>(0));
  Wire.write(static_cast<uint8_t>(ticks));
  Wire.write(static_cast<uint8_t>(ticks >> 8));
  return Wire.endTransmission() == 0;
}

bool WriteServo(uint8_t channel, int16_t effort, uint16_t minimum_us,
                uint16_t maximum_us) {
  const int32_t span = effort < 0 ? legacy::kServoCenterUs - minimum_us
                                  : maximum_us - legacy::kServoCenterUs;
  const uint32_t pulse_us = static_cast<int32_t>(legacy::kServoCenterUs) +
                            span * effort / kFullEffort;
  const uint16_t ticks =
      (pulse_us * 27 + (kServoPrescale + 1) / 2) / (kServoPrescale + 1);
  return WritePwm(channel, ticks);
}

void CoastMotor() {
  digitalWrite(kEngineIn1, LOW);
  digitalWrite(kEngineIn2, LOW);
}

}  // namespace

void BenchHardware::Begin() {
  CoastMotor();
  pinMode(kEngineIn1, OUTPUT);
  pinMode(kEngineIn2, OUTPUT);
  digitalWrite(kPumpIn3, LOW);
  digitalWrite(kPumpIn4, LOW);
  pinMode(kPumpIn3, OUTPUT);
  pinMode(kPumpIn4, OUTPUT);
  pinMode(kOwnerPin, INPUT_PULLUP);
  pinMode(kStopPin, INPUT_PULLUP);
  pinMode(kResetPin, INPUT_PULLUP);
  stop_during_boot_ = digitalRead(kStopPin) == HIGH;

  Wire.begin();
  Wire.setWireTimeout(2500, true);
  if (!WriteRegister(kMode1Register, kAutoIncrement | kSleep) ||
      !WriteRegister(kMode2Register, kPushPull) ||
      !WriteRegister(kPrescaleRegister, kServoPrescale) ||
      !WritePwm(kThrottleChannel, kPwmFullOff) ||
      !WritePwm(kPumpEnableChannel, kPwmFullOff) ||
      !WriteServo(kRudderChannel, 0, legacy::kRudderMinUs,
                  legacy::kRudderMaxUs) ||
      !WriteServo(kDivePlaneChannel, 0, legacy::kDivePlaneMinUs,
                  legacy::kDivePlaneMaxUs) ||
      !WriteRegister(kMode1Register, kAutoIncrement)) {
    Fail();
    return;
  }
  delayMicroseconds(500);
  if (!WriteRegister(kMode1Register, kAutoIncrement | kRestart)) {
    Fail();
    return;
  }
  next_shield_check_at_ = millis();
}

bool BenchHardware::ReadReset(bool permitted, uint32_t now) {
  const bool released = digitalRead(kResetPin) == HIGH;
  if (!permitted || released != reset_level_) {
    reset_changed_at_ = now;
    reset_level_ = released;
  }
  if (!permitted) {
    reset_ready_ = false;
    return false;
  }
  if (now - reset_changed_at_ < kResetDebounceMs) {
    return false;
  }
  if (released) {
    reset_ready_ = true;
    return false;
  }
  const bool pressed = reset_ready_;
  reset_ready_ = false;
  return pressed;
}

void BenchHardware::ReadInputs(bcp::MonitoringRuntime& runtime, uint32_t now) {
  const bool stop_open = digitalRead(kStopPin) == HIGH;
  const bool pc_selected = digitalRead(kOwnerPin) == LOW;
  if (stop_open || !pc_selected || failed_) {
    DisableMotor();
  }

  const bool stop_asserted = stop_open || stop_during_boot_ || failed_;
  const bool reset = ReadReset(!stop_asserted, now);
  if (runtime.SetBenchInputs(pc_selected, stop_asserted, reset, now)) {
    stop_during_boot_ = false;
  } else {
    stop_during_boot_ |= stop_open;
    reset_ready_ = false;
    reset_changed_at_ = now;
  }
}

bool BenchHardware::SetMotor(int16_t effort) {
  if (effort == forward_) {
    return true;
  }
  if (effort == 0 || (effort < 0) != (forward_ < 0) || forward_ == 0) {
    CoastMotor();
    if (!WritePwm(kThrottleChannel, kPwmFullOff)) {
      return false;
    }
    if (effort != 0) {
      digitalWrite(effort > 0 ? kEngineIn1 : kEngineIn2, HIGH);
    }
  }
  const uint16_t magnitude = effort < 0 ? -effort : effort;
  const uint16_t ticks =
      static_cast<uint32_t>(magnitude) * kMaxMotorTicks / kFullEffort;
  if (effort != 0 &&
      !WritePwm(kThrottleChannel, ticks == 0 ? kPwmFullOff : ticks)) {
    return false;
  }
  forward_ = effort;
  return true;
}

void BenchHardware::DisableMotor() {
  CoastMotor();
  if (!failed_ && !SetMotor(0)) {
    Fail();
  }
}

void BenchHardware::Fail() {
  CoastMotor();
  failed_ = true;
}

void BenchHardware::Apply(const bcp::ControlOutputs& outputs) {
  if (failed_) {
    return;
  }
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - next_shield_check_at_) >= 0) {
    next_shield_check_at_ = now + kShieldCheckIntervalMs;
    if (!CheckRegister(kMode1Register, kAutoIncrement, 0x7F) ||
        !CheckRegister(kMode2Register, kPushPull) ||
        !CheckRegister(kPrescaleRegister, kServoPrescale)) {
      Fail();
      return;
    }
  }
  if (!SetMotor(outputs.forward) ||
      (outputs.yaw != yaw_ &&
       !WriteServo(kRudderChannel, outputs.yaw, legacy::kRudderMinUs,
                   legacy::kRudderMaxUs)) ||
      (outputs.dive != dive_ &&
       !WriteServo(kDivePlaneChannel, outputs.dive, legacy::kDivePlaneMinUs,
                   legacy::kDivePlaneMaxUs))) {
    Fail();
    return;
  }
  yaw_ = outputs.yaw;
  dive_ = outputs.dive;
}

}  // namespace vehicle
}  // namespace bajols

#endif  // BAJOLS_BCP_ENABLE_BENCH_CONTROL
