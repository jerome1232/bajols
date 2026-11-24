#include "benchVehicle.h"

#if defined(BAJOLS_BCP_ENABLE_BENCH_CONTROL)
#include <Arduino.h>
namespace bajols {
namespace vehicle {
namespace {

constexpr uint8_t kOwnerPin = 28;
constexpr uint8_t kStopPin = 29;
constexpr uint8_t kResetPin = 30;

constexpr uint16_t kMaxMotorTicks =
    819;  // At most 20% of the 4096-count period.
constexpr int16_t kFullEffort = 1000;
constexpr uint32_t kResetDebounceMs = 25;
constexpr uint32_t kShieldCheckIntervalMs = 50;

uint16_t ServoPulse(int16_t effort, uint16_t minimum_us,
                    uint16_t maximum_us) {
  const int32_t span = effort < 0 ? legacy::kServoCenterUs - minimum_us
                                  : maximum_us - legacy::kServoCenterUs;
  return legacy::kServoCenterUs + span * effort / kFullEffort;
}

}  // namespace

void BenchHardware::Begin() {
  pinMode(kOwnerPin, INPUT_PULLUP);
  pinMode(kStopPin, INPUT_PULLUP);
  pinMode(kResetPin, INPUT_PULLUP);
  stop_during_boot_ = digitalRead(kStopPin) == HIGH;

  actuators_.Begin();
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
  if (stop_open || !pc_selected || actuators_.Failed()) {
    actuators_.DisableMotor();
  }

  const bool stop_asserted = stop_open || stop_during_boot_ || actuators_.Failed();
  const bool reset = ReadReset(!stop_asserted, now);
  if (runtime.SetBenchInputs(pc_selected, stop_asserted, reset, now)) {
    stop_during_boot_ = false;
  } else {
    stop_during_boot_ |= stop_open;
    reset_ready_ = false;
    reset_changed_at_ = now;
  }
}

void BenchHardware::Apply(const bcp::ControlOutputs& outputs) {
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - next_shield_check_at_) >= 0) {
    next_shield_check_at_ = now + kShieldCheckIntervalMs;
    actuators_.CheckShield();
  }

  const uint16_t magnitude = outputs.forward < 0 ? -outputs.forward
                                                  : outputs.forward;
  const uint16_t ticks =
      static_cast<uint32_t>(magnitude) * kMaxMotorTicks / kFullEffort;
  if (ticks == 0) {
    actuators_.DisableMotor();
  } else {
    actuators_.SetMotor(outputs.forward > 0 ? legacy::Direction::kForward
                                           : legacy::Direction::kBackward,
                        ticks);
  }
  actuators_.SetSurfaces(
      ServoPulse(outputs.yaw, legacy::kRudderMinUs, legacy::kRudderMaxUs),
      ServoPulse(outputs.dive, legacy::kDivePlaneMinUs, legacy::kDivePlaneMaxUs));
}

}  // namespace vehicle
}  // namespace bajols

#endif  // BAJOLS_BCP_ENABLE_BENCH_CONTROL
