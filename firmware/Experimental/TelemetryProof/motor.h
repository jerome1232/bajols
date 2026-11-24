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

#ifndef BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_MOTOR_H_
#define BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_MOTOR_H_

#include <Arduino.h>
#if defined(BAJOLS_ENABLE_LEGACY_CONTROL)
#include <Encoder.h>
#endif
#include <stdint.h>

namespace bajols {
namespace legacy {
constexpr uint8_t kMaxPwmValue = 255;
constexpr uint8_t kMinPwmValue = 0;

// Default Arduino pin assignments.
constexpr uint8_t kDefaultInput1Pin = 22;
constexpr uint8_t kDefaultInput2Pin = 23;
constexpr uint8_t kDefaultPwmPin = 2;
constexpr uint8_t kDefaultEncoderSignalAPin = 21;
constexpr uint8_t kDefaultEncoderSignalBPin = 20;

enum class Direction : uint8_t {
  kCoast,
  kStop,
  kForward,
  kBackward,
};

// HBridge class - Controls basic motor direction without speed control.
class HBridge {
 public:
  HBridge() : HBridge(kDefaultInput1Pin, kDefaultInput2Pin) {}

  HBridge(uint8_t input_1, uint8_t input_2)
      : input_1_(input_1), input_2_(input_2) {}

  void Begin();

  // Set the motor to go forward
  virtual void Forward();

  // Set the motor to go backward
  virtual void Backward();

  // Selects the bridge braking state.
  virtual void Stop();

  // Sets both direction inputs low.
  virtual void Off();

  // Returns the last selected direction.
  Direction GetState() const;

 protected:
  /* Pin that controls motor direction */
  const uint8_t input_1_;

  /* Pin that controls motor direction */
  const uint8_t input_2_;

  // Last direction selected by this driver.
  Direction direction_ = Direction::kCoast;
};

// HBridgePwm class - Controls motor direction and speed using PWM.
class HBridgePwm : public HBridge {
 public:
  // Optional PWM backend. It returns false when the output write fails.
  struct PwmBackend {
    bool (*write)(uint16_t);
    uint16_t full_level;
  };

  // Uses the default Arduino PWM and direction pins.
  HBridgePwm()
      : HBridgePwm(kDefaultInput1Pin, kDefaultInput2Pin, kDefaultPwmPin) {}

  HBridgePwm(uint8_t input_1, uint8_t input_2, uint8_t pwm_pin)
      : HBridge(input_1, input_2),
        pwm_pin_(pwm_pin),
        backend_{nullptr, kMaxPwmValue} {}

  HBridgePwm(uint8_t input_1, uint8_t input_2, PwmBackend backend)
      : HBridge(input_1, input_2),
        pwm_pin_(0),
        backend_(backend) {}

  void Begin();

  /*
   * Set motor direction and speed.
   * @param direction Direction to set the motor (kForward, kBackward, kCoast,
   * kStop)
   * @param pwm PWM level in units of the selected backend
   */
  virtual void Set(Direction direction, uint16_t pwm);

  /*
   * Set motor speed.
   * @param pwm PWM level in units of the selected backend
   */
  virtual void SetSpeed(uint16_t pwm);

  // Selects the bridge braking state.
  void Stop() override;

  // Disables PWM and sets both direction inputs low.
  void Off() override;

  /* Get the current duty cycle of the motor */
  uint16_t GetSpeed() const;
  bool Failed() const { return pwm_fault_; }

 private:
  /* Pin that controls motor speed */
  const uint8_t pwm_pin_;
  const PwmBackend backend_;

  // Last successful duty cycle, or zero after a PWM failure.
  uint16_t pwm_level_ = 0;
  bool pwm_fault_ = false;
};

#if defined(BAJOLS_ENABLE_LEGACY_CONTROL)
class HBridgePwmEncoder : public HBridgePwm {
 public:
  HBridgePwmEncoder()
      : HBridgePwmEncoder(kDefaultInput1Pin, kDefaultInput2Pin, kDefaultPwmPin,
                          kDefaultEncoderSignalAPin,
                          kDefaultEncoderSignalBPin) {}

  HBridgePwmEncoder(uint8_t input_1, uint8_t input_2, uint8_t pwm_pin,
                    uint8_t encoder_pin_a, uint8_t encoder_pin_b)
      : HBridgePwm(input_1, input_2, pwm_pin),
        encoder_(encoder_pin_a, encoder_pin_b) {}

  HBridgePwmEncoder(uint8_t input_1, uint8_t input_2, PwmBackend backend,
                    uint8_t encoder_pin_a, uint8_t encoder_pin_b)
      : HBridgePwm(input_1, input_2, backend),
        encoder_(encoder_pin_a, encoder_pin_b) {}

  // Returns the most recent shaft RPM.
  int32_t GetRpm() const {
    return rpm_;
  }

  // Updates RPM once the encoder sampling interval has elapsed.
  void Read();

 private:
  // Latest shaft speed in revolutions per minute.
  int32_t rpm_ = 0;

  // Timestamp of the last encoder sample.
  uint32_t last_rpm_at_ = 0;

  Encoder encoder_;
};
#endif  // BAJOLS_ENABLE_LEGACY_CONTROL
}  // namespace legacy
}  // namespace bajols

#endif  // BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_MOTOR_H_
