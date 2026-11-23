#if defined(BAJOLS_ENABLE_LEGACY_CONTROL) || defined(BAJOLS_BCP_ENABLE_BENCH_CONTROL)
/*
 * The TelemetryStreamTest application.
 *
 * Copyright (C) 2024 Jeremy D. Jones <j.jones1232@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "motor.h"

namespace bajols {
namespace legacy {

void HBridge::Begin() {
  HBridge::Off();
  pinMode(input_1_, OUTPUT);
  pinMode(input_2_, OUTPUT);
}

void HBridgePwm::Begin() {
  HBridge::Begin();
  if (!backend_.write) {
    digitalWrite(pwm_pin_, LOW);
    pinMode(pwm_pin_, OUTPUT);
    SetSpeed(0);
  }
}

void HBridge::Forward() {
  digitalWrite(input_1_, HIGH);
  digitalWrite(input_2_, LOW);
  direction_ = Direction::kForward;
}

void HBridge::Backward() {
  digitalWrite(input_1_, LOW);
  digitalWrite(input_2_, HIGH);
  direction_ = Direction::kBackward;
}

void HBridge::Stop() {
  digitalWrite(input_1_, HIGH);
  digitalWrite(input_2_, HIGH);
  direction_ = Direction::kStop;
}

void HBridge::Off() {
  digitalWrite(input_1_, LOW);
  digitalWrite(input_2_, LOW);
  direction_ = Direction::kCoast;
}

Direction HBridge::GetState() const {
  return direction_;
}

void HBridgePwm::Set(Direction direction, uint16_t pwm) {
  if (backend_.write && direction != direction_ && pwm_level_ != 0) {
    HBridge::Off();
    // Disable the bridge's enable/PWM input before changing direction.
    SetSpeed(0);
  }
  if (pwm_fault_) {
    return;
  }
  switch (direction) {
    case Direction::kForward:
      Forward();
      break;
    case Direction::kBackward:
      Backward();
      break;
    case Direction::kCoast:
      HBridge::Off();
      pwm = kMinPwmValue;
      break;
    case Direction::kStop:
      HBridge::Stop();
      pwm = backend_.full_level;
      break;
  }

  SetSpeed(pwm);
}

void HBridgePwm::SetSpeed(uint16_t pwm) {
  if (pwm_fault_) {
    return;
  }
  if (backend_.write) {
    if (!backend_.write(pwm)) {
      pwm_fault_ = true;
      pwm_level_ = 0;
      HBridge::Off();
      return;
    }
  } else {
    analogWrite(pwm_pin_, pwm);
  }
  pwm_level_ = pwm;
}

uint16_t HBridgePwm::GetSpeed() const {
  return pwm_level_;
}

void HBridgePwm::Stop() {
  HBridgePwm::Set(Direction::kStop, backend_.full_level);
}

void HBridgePwm::Off() {
  HBridge::Off();
  SetSpeed(kMinPwmValue);
}

#if defined(BAJOLS_ENABLE_LEGACY_CONTROL)
namespace {

constexpr float kMillisecondsPerMinute = 60000.0f;
constexpr uint32_t kRpmIntervalMs = 50;
constexpr int16_t kPulsesPerRevolution = 8400;

}  // namespace

void HBridgePwmEncoder::Read() {
  uint32_t now = millis();
  uint32_t elapsed_ms = now - last_rpm_at_;
  if (elapsed_ms == 0) {
    return;
  }

  if (elapsed_ms >= kRpmIntervalMs) {
    int32_t pulses = encoder_.readAndReset();
    if (pulses < 0) {
      pulses = pulses * -1;
    }
    rpm_ = (pulses / static_cast<double>(kPulsesPerRevolution)) *
           (kMillisecondsPerMinute / elapsed_ms);
    last_rpm_at_ = now;
  }
}

#endif  // BAJOLS_ENABLE_LEGACY_CONTROL
}  // namespace legacy
}  // namespace bajols

#endif  // Actuator-enabled build
