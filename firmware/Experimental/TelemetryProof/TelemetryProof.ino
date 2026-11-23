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

#include <IBusBM.h>

#include "actuatorHardware.h"
#include "debug.h"
#include "input.h"
#include "output.h"

namespace {
namespace legacy = bajols::legacy;

constexpr uint32_t kReadIntervalMs = 100;
constexpr uint32_t kBaudRate = 115200;

constexpr uint8_t kActivityLedPin = 8;

legacy::Input receiver;
legacy::Output telemetry;
bajols::vehicle::ActuatorHardware actuators;

// Last receiver update time.
uint32_t previous_update_ms = 0;

uint8_t next_led_state = LOW;
}  // namespace

void setup() {
  DEBUG_BEGIN(kBaudRate);
  actuators.Begin();
  if (actuators.Failed()) {
    return;
  }
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
  if (actuators.Failed()) {
    return;
  }

  uint32_t current = millis();
  bool update_due = (current - previous_update_ms) > kReadIntervalMs;

  if (update_due) {
    digitalWrite(kActivityLedPin, next_led_state);
    next_led_state = next_led_state == LOW ? HIGH : LOW;
    previous_update_ms = current;
    receiver.Read();

    actuators.SetThrottle(receiver.switch_a == legacy::SwitchPos::kUp
                              ? legacy::Direction::kForward
                              : legacy::Direction::kBackward,
                          receiver.throttle);
    const uint16_t rpm = actuators.ReadRpm();
    actuators.SetSurfaces(receiver.rudder_us, receiver.dive_plane_us);
    actuators.SetPump(receiver.switch_c == legacy::ThreeWaySwitchPos::kMiddle);

    telemetry.SetSensors(receiver, rpm);
    digitalWrite(kActivityLedPin, LOW);
  }
}

#endif  // BAJOLS_ENABLE_LEGACY_CONTROL
