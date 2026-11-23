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

#ifndef BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_OUTPUT_H_
#define BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_OUTPUT_H_

#include <Arduino.h>
#include <IBusBM.h>
#include <stdint.h>

#include "input.h"

namespace bajols {
namespace legacy {

// Sends the legacy FlySky instrument feed.
class Output {
 public:
  // Registers the iBus telemetry sensors on Serial3.
  void Begin();

  // Updates the legacy telemetry values from the receiver and shaft encoder.
  void SetSensors(const Input& input, int16_t rpm);

 private:
  int32_t pressure_ = 101300;
  int16_t rpm_ = 0;
  int16_t voltage_ = 960;
  int16_t heading_ = 90;
  int16_t speed_ = 0;

  // iBus assigns these indices during sensor registration.
  uint8_t rpm_sensor_;
  uint8_t pressure_sensor_;
  uint8_t voltage_sensor_;
  uint8_t heading_sensor_;
  uint8_t speed_sensor_;

  IBusBM ibus_;
};

}  // namespace legacy
}  // namespace bajols

#endif  // BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_OUTPUT_H_
