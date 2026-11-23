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

/*
 * Download IBusBM library here
 * https://github.com/bmellink/IBusBM
 *
 * Click "<> Code" -> Download zip
 * Add to sketch in Arduino ide
 * Sketch -> Include Library -> Add .zip library
 */

#if !defined(BAJOLS_ENABLE_LEGACY_CONTROL)

#if defined(BAJOLS_LEGACY_SHIELD_ENGINE_PWM)
#error "BAJOLS_LEGACY_SHIELD_ENGINE_PWM requires BAJOLS_ENABLE_LEGACY_CONTROL."
#endif

#include "bcpVehicle.h"

// Default integration path: BCP only, with no actuator or receiver setup.
void setup() {
  bajols::vehicle::Begin();
}

// Bounded serial, persistence and instrument publication; no blocking LED
// startup.
void loop() {
  bajols::vehicle::Step();
}

#else
#if defined(BAJOLS_BCP_ENABLE_BENCH_CONTROL)
#error "BCP bench control and legacy FlySky control are separate builds."
#endif
#if defined(BAJOLS_BCP_USE_SERIAL1)
#error "BAJOLS_BCP_USE_SERIAL1 requires the BCP firmware."
#endif
#if !defined(__AVR_ATmega2560__)
#error \
    "Legacy FlySky/actuator control is ATmega2560-only; use the default BCP build on ESP32."
#endif
// Explicit compatibility build. This legacy path directly drives real outputs
// and does not provide BCP command arbitration.

#include <Adafruit_PWMServoDriver.h>
#include <IBusBM.h>
#include <Wire.h>

#include "debug.h"
#include "input.h"
#include "motor.h"
#include "output.h"

namespace {
namespace legacy = bajols::legacy;

constexpr uint32_t kReadIntervalMs = 100;
constexpr uint32_t kBaudRate = 115200;

constexpr uint8_t kEngineInput1 = 22;
constexpr uint8_t kEngineInput2 = 23;
constexpr uint8_t kEngineEncoderTrigger1 = 2;
constexpr uint8_t kEngineEncoderTrigger2 = 3;

constexpr uint8_t kWaterPumpInput1 = 24;
constexpr uint8_t kWaterPumpInput2 = 25;

constexpr uint8_t kActivityLedPin = 8;
constexpr uint8_t kRudderChannel = 0;
constexpr uint8_t kDivePlaneChannel = 1;

Adafruit_PWMServoDriver pwm;

#if defined(BAJOLS_LEGACY_SHIELD_ENGINE_PWM)
constexpr uint8_t kEnginePwmChannel = 2;
constexpr uint8_t kPumpEnableChannel = 3;
bool shield_ready = false;

bool WriteEnginePwm(uint8_t duty) {
  if (duty == legacy::kMinPwmValue) {
    return pwm.setPWM(kEnginePwmChannel, 0, 4096) == 0;
  }
  if (duty == legacy::kMaxPwmValue) {
    return pwm.setPWM(kEnginePwmChannel, 4096, 0) == 0;
  }
  const uint16_t ticks = (static_cast<uint32_t>(duty) * 4096 + 127) / 255;
  return pwm.setPWM(kEnginePwmChannel, 0, ticks) == 0;
}

bool WritePumpEnable(bool enabled) {
  return pwm.setPWM(kPumpEnableChannel, enabled ? 4096 : 0,
                    enabled ? 0 : 4096) == 0;
}
#else
constexpr uint8_t kEnginePwm = 11;
#endif

// Commands from the FlySky receiver.
legacy::Input receiver;

// Telemetry sent back to the transmitter.
legacy::Output telemetry;

// Main propulsion motor.
#if defined(BAJOLS_LEGACY_SHIELD_ENGINE_PWM)
legacy::HBridgePwmEncoder engine(kEngineInput1, kEngineInput2, WriteEnginePwm,
                                 kEngineEncoderTrigger1,
                                 kEngineEncoderTrigger2);
#else
legacy::HBridgePwmEncoder engine(kEngineInput1, kEngineInput2, kEnginePwm,
                                 kEngineEncoderTrigger1,
                                 kEngineEncoderTrigger2);
#endif

legacy::HBridge water_pump(kWaterPumpInput1, kWaterPumpInput2);

void SetWaterPump(bool fill) {
  if (fill) {
    water_pump.Forward();
  } else {
    water_pump.Off();
  }
#if defined(BAJOLS_LEGACY_SHIELD_ENGINE_PWM)
  if (!WritePumpEnable(fill)) {
    water_pump.Off();
    engine.Off();
    shield_ready = false;
  }
#endif
}

// Cache previous commands to skip redundant actuator writes.
int32_t previous_rudder_us = legacy::kServoCenterUs;
int32_t previous_dive_plane_us = legacy::kServoCenterUs;
legacy::ThreeWaySwitchPos previous_switch_c = legacy::ThreeWaySwitchPos::kUp;
int32_t previous_throttle = legacy::kMinRawInput;
legacy::SwitchPos previous_switch_a = legacy::SwitchPos::kUp;

// Last receiver update time.
uint32_t previous_update_ms = 0;

uint8_t next_led_state = LOW;
}  // namespace

void setup() {
  DEBUG_BEGIN(kBaudRate);
#if defined(BAJOLS_LEGACY_SHIELD_ENGINE_PWM)
  Wire.setWireTimeout(2500, true);
  if (!pwm.begin()) {
    return;
  }
#else
  pwm.begin();
#endif
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(50);
#if defined(BAJOLS_LEGACY_SHIELD_ENGINE_PWM)
  const bool engine_disabled = WriteEnginePwm(0);
  const bool pump_disabled = WritePumpEnable(false);
  shield_ready = engine_disabled && pump_disabled;
  if (!shield_ready) {
    return;
  }
#endif
  pwm.writeMicroseconds(kRudderChannel, previous_rudder_us);
  pwm.writeMicroseconds(kDivePlaneChannel, previous_dive_plane_us);
  receiver.Begin();
  telemetry.Begin();

  pinMode(kActivityLedPin, OUTPUT);
  digitalWrite(kActivityLedPin, LOW);

  DEBUG_PRINTLN_INFO("Starting");
  uint32_t current = millis();
  while (current < 5000) {
    digitalWrite(kActivityLedPin, HIGH);
    delay(100);
    digitalWrite(kActivityLedPin, LOW);
    delay(50);
    current = millis();
  }
}

void loop() {
#if defined(BAJOLS_LEGACY_SHIELD_ENGINE_PWM)
  if (!shield_ready) {
    return;
  }
#endif

  uint32_t current = millis();
  bool update_due = (current - previous_update_ms) > kReadIntervalMs;

  if (update_due) {
    digitalWrite(kActivityLedPin, next_led_state);
    next_led_state = next_led_state == LOW ? HIGH : LOW;
    previous_update_ms = current;
    receiver.Read();

    if (previous_throttle != receiver.throttle ||
        previous_switch_a != receiver.switch_a) {
      previous_throttle = receiver.throttle;
      previous_switch_a = receiver.switch_a;
      engine.Set(receiver.switch_a == legacy::SwitchPos::kUp
                     ? legacy::Direction::kForward
                     : legacy::Direction::kBackward,
                 receiver.throttle);
    }

    uint16_t rpm = engine.GetRpm();

    if (receiver.rudder_us != previous_rudder_us) {
      pwm.writeMicroseconds(kRudderChannel, receiver.rudder_us);
      previous_rudder_us = receiver.rudder_us;
    }

    if (receiver.dive_plane_us != previous_dive_plane_us) {
      pwm.writeMicroseconds(kDivePlaneChannel, receiver.dive_plane_us);
      previous_dive_plane_us = receiver.dive_plane_us;
    }

    if (receiver.switch_c != previous_switch_c) {
      previous_switch_c = receiver.switch_c;
      SetWaterPump(receiver.switch_c == legacy::ThreeWaySwitchPos::kMiddle);
    }

    telemetry.SetSensors(receiver, rpm);
    digitalWrite(kActivityLedPin, LOW);
  }
}

#endif  // BAJOLS_ENABLE_LEGACY_CONTROL
