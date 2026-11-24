#include "actuatorHardware.h"

#if defined(BAJOLS_ENABLE_LEGACY_CONTROL) || defined(BAJOLS_BCP_ENABLE_BENCH_CONTROL)
#include <Wire.h>

namespace bajols {
namespace vehicle {
namespace {

constexpr uint8_t kEngineIn1 = 22;
constexpr uint8_t kEngineIn2 = 23;
constexpr uint8_t kPumpIn3 = 24;
constexpr uint8_t kPumpIn4 = 25;

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
constexpr uint16_t kPwmFull = 4096;

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

bool WritePwm(uint8_t channel, uint16_t on, uint16_t off) {
  Wire.beginTransmission(kShieldAddress);
  Wire.write(static_cast<uint8_t>(kChannel0Register + 4 * channel));
  Wire.write(static_cast<uint8_t>(on));
  Wire.write(static_cast<uint8_t>(on >> 8));
  Wire.write(static_cast<uint8_t>(off));
  Wire.write(static_cast<uint8_t>(off >> 8));
  return Wire.endTransmission() == 0;
}

bool WriteServo(uint8_t channel, uint16_t pulse_us) {
  const uint16_t ticks =
      (static_cast<uint32_t>(pulse_us) * 27 + (kServoPrescale + 1) / 2) /
      (kServoPrescale + 1);
  return WritePwm(channel, 0, ticks);
}

#if defined(BAJOLS_BCP_ENABLE_BENCH_CONTROL) || defined(BAJOLS_LEGACY_SHIELD_ENGINE_PWM)
bool WriteEnginePwm(uint16_t level) {
  if (level == kPwmFull) {
    return WritePwm(kThrottleChannel, kPwmFull, 0);
  }
  return WritePwm(kThrottleChannel, 0, level == 0 ? kPwmFull : level);
}
constexpr legacy::HBridgePwm::PwmBackend kEnginePwm{WriteEnginePwm, kPwmFull};
#else
constexpr uint8_t kEnginePwm = 11;
#endif

}  // namespace

ActuatorHardware::ActuatorHardware()
#if defined(BAJOLS_ENABLE_LEGACY_CONTROL)
    : engine_(kEngineIn1, kEngineIn2, kEnginePwm, 2, 3),
#else
    : engine_(kEngineIn1, kEngineIn2, kEnginePwm),
#endif
      pump_(kPumpIn3, kPumpIn4) {}

void ActuatorHardware::Begin() {
  engine_.Begin();
  pump_.Begin();
  Wire.begin();
  Wire.setWireTimeout(2500, true);
  if (!WriteRegister(kMode1Register, kAutoIncrement | kSleep) ||
      !WriteRegister(kMode2Register, kPushPull) ||
      !WriteRegister(kPrescaleRegister, kServoPrescale) ||
      !WritePwm(kThrottleChannel, 0, kPwmFull) ||
      !WritePwm(kPumpEnableChannel, 0, kPwmFull) ||
      !WriteServo(kRudderChannel, rudder_us_) ||
      !WriteServo(kDivePlaneChannel, dive_plane_us_) ||
      !WriteRegister(kMode1Register, kAutoIncrement)) {
    Fail();
    return;
  }
  delayMicroseconds(500);
  if (!WriteRegister(kMode1Register, kAutoIncrement | kRestart)) {
    Fail();
  }
}

void ActuatorHardware::CheckShield() {
  if (!failed_ &&
      (!CheckRegister(kMode1Register, kAutoIncrement, 0x7F) ||
       !CheckRegister(kMode2Register, kPushPull) ||
       !CheckRegister(kPrescaleRegister, kServoPrescale))) {
    Fail();
  }
}

void ActuatorHardware::DisableMotor() {
  SetMotor(legacy::Direction::kCoast, 0);
}

void ActuatorHardware::SetMotor(legacy::Direction direction, uint16_t level) {
  if (failed_ || (engine_.GetState() == direction &&
                  engine_.GetSpeed() == level)) {
    return;
  }
  if (direction == legacy::Direction::kCoast) {
    engine_.Off();
  } else {
    engine_.Set(direction, level);
  }
  if (engine_.Failed()) {
    Fail();
  }
}

void ActuatorHardware::SetSurfaces(uint16_t rudder_us, uint16_t dive_plane_us) {
  if (failed_) {
    return;
  }
  if ((rudder_us != rudder_us_ && !WriteServo(kRudderChannel, rudder_us)) ||
      (dive_plane_us != dive_plane_us_ &&
       !WriteServo(kDivePlaneChannel, dive_plane_us))) {
    Fail();
    return;
  }
  rudder_us_ = rudder_us;
  dive_plane_us_ = dive_plane_us;
}

void ActuatorHardware::Fail() {
  // Remove drive at the GPIOs even when the shield cannot be reached.
  engine_.HBridge::Off();
  pump_.Off();
  failed_ = true;
}

#if defined(BAJOLS_ENABLE_LEGACY_CONTROL)
void ActuatorHardware::SetThrottle(legacy::Direction direction,
                                   uint8_t throttle) {
#if defined(BAJOLS_LEGACY_SHIELD_ENGINE_PWM)
  SetMotor(direction, (static_cast<uint32_t>(throttle) * kPwmFull + legacy::kMaxPwmValue / 2) /
      legacy::kMaxPwmValue);
#else
  SetMotor(direction, throttle);
#endif
}

void ActuatorHardware::SetPump(bool fill) {
  if (failed_ || fill == (pump_.GetState() == legacy::Direction::kForward)) {
    return;
  }
  if (fill) {
    pump_.Forward();
  } else {
    pump_.Off();
  }
#if defined(BAJOLS_LEGACY_SHIELD_ENGINE_PWM)
  if (!WritePwm(kPumpEnableChannel, fill ? kPwmFull : 0, fill ? 0 : kPwmFull)) {
    Fail();
  }
#endif
}

int32_t ActuatorHardware::ReadRpm() {
  engine_.Read();
  return engine_.GetRpm();
}
#endif

}  // namespace vehicle
}  // namespace bajols

#endif  // Actuator-enabled build
