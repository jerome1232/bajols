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
#include <Encoder.h>
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
      : input_1_(input_1), input_2_(input_2) {
    pinMode(input_1_, OUTPUT);
    pinMode(input_2_, OUTPUT);
    Off();
  }

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
  Direction direction_;
};

// HBridgePwm class - Controls motor direction and speed using PWM.
class HBridgePwm : public HBridge {
 public:
  // Optional PWM backend. It returns false when the output write fails.
  using PwmWriter = bool (*)(uint8_t);

  // Uses the default Arduino PWM and direction pins.
  HBridgePwm()
      : HBridgePwm(kDefaultInput1Pin, kDefaultInput2Pin, kDefaultPwmPin) {}

  HBridgePwm(uint8_t input_1, uint8_t input_2, uint8_t pwm_pin)
      : HBridge(input_1, input_2),
        pwm_pin_(pwm_pin),
        pwm_writer_(nullptr),
        pwm_level_(0),
        pwm_fault_(false) {
    pinMode(pwm_pin_, OUTPUT);
  }

  HBridgePwm(uint8_t input_1, uint8_t input_2, PwmWriter pwm_writer)
      : HBridge(input_1, input_2),
        pwm_pin_(0),
        pwm_writer_(pwm_writer),
        pwm_level_(0),
        pwm_fault_(false) {}

  /*
   * Set motor direction and speed.
   * @param direction Direction to set the motor (kForward, kBackward, kCoast,
   * kStop)
   * @param pwm PWM duty cycle (0-255)
   */
  virtual void Set(Direction direction, uint8_t pwm);

  /*
   * Set motor speed.
   * @param pwm PWM duty cycle (0-255)
   */
  virtual void SetSpeed(uint8_t pwm);

  // Selects the bridge braking state.
  void Stop() override;

  // Disables PWM and sets both direction inputs low.
  void Off() override;

  /* Get the current duty cycle of the motor */
  uint8_t GetSpeed() const;

 private:
  /* Pin that controls motor speed */
  const uint8_t pwm_pin_;
  const PwmWriter pwm_writer_;

  // Last successful duty cycle, or zero after a PWM failure.
  uint8_t pwm_level_;
  bool pwm_fault_;
};

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

  HBridgePwmEncoder(uint8_t input_1, uint8_t input_2, PwmWriter pwm_writer,
                    uint8_t encoder_pin_a, uint8_t encoder_pin_b)
      : HBridgePwm(input_1, input_2, pwm_writer),
        encoder_(encoder_pin_a, encoder_pin_b) {}

  // Returns the most recent shaft RPM.
  int32_t GetRpm() const {
    return rpm_;
  }

  void Set(Direction direction, uint8_t speed) override;

  void SetSpeed(uint8_t speed) override;

  void Off() override;

  void Stop() override;

  // Updates RPM once the encoder sampling interval has elapsed.
  void Read();

 private:
  // Latest shaft speed in revolutions per minute.
  volatile int32_t rpm_ = 0;

  // Timestamp of the last encoder sample.
  uint32_t last_rpm_at_ = millis();

  Encoder encoder_;
};
}  // namespace legacy
}  // namespace bajols

#endif  // BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_MOTOR_H_
