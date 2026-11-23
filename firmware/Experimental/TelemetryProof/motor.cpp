#if defined(BAJOLS_ENABLE_LEGACY_CONTROL)
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

#include "debug.h"

namespace bajols {
namespace legacy {
namespace {

constexpr float kMillisecondsPerMinute = 60000.0f;
constexpr uint32_t kRpmIntervalMs = 50;
constexpr int16_t kPulsesPerRevolution = 8400;

}  // namespace

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

void HBridgePwm::Set(Direction direction, uint8_t pwm) {
  DEBUG_PRINTLN_INFO("Set called");
  if (pwm_writer_ && direction != direction_ && pwm_level_ != 0) {
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
      pwm = kMaxPwmValue;
      break;
  }

  SetSpeed(pwm);
}

void HBridgePwm::SetSpeed(uint8_t pwm) {
  if (pwm_fault_) {
    return;
  }
  if (pwm_writer_) {
    if (!pwm_writer_(pwm)) {
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

uint8_t HBridgePwm::GetSpeed() const {
  return pwm_level_;
}

void HBridgePwm::Stop() {
  HBridgePwm::Set(Direction::kStop, kMaxPwmValue);
}

void HBridgePwm::Off() {
  HBridge::Off();
  SetSpeed(kMinPwmValue);
}

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

void HBridgePwmEncoder::Set(Direction direction, uint8_t speed) {
  HBridgePwm::Set(direction, speed);
  Read();
}

void HBridgePwmEncoder::SetSpeed(uint8_t speed) {
  HBridgePwm::SetSpeed(speed);
  Read();
}

void HBridgePwmEncoder::Off() {
  HBridgePwm::Off();
  Read();
}

void HBridgePwmEncoder::Stop() {
  HBridgePwm::Stop();
  rpm_ = 0;
}
}  // namespace legacy
}  // namespace bajols

#endif  // BAJOLS_ENABLE_LEGACY_CONTROL
