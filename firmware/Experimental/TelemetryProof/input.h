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

#ifndef BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_INPUT_H_
#define BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_INPUT_H_

#include <Arduino.h>
#include <IBusBM.h>
#include <stdint.h>

#include "data_utils.h"

namespace bajols {
namespace legacy {
// Reads FlySky receiver channels and maps them onto actuator inputs.
class Input {
 public:
  Input();

  // Starts receiving iBus channel data on Serial2.
  void Begin();
  // Samples and converts the current receiver channels.
  void Read();

  uint8_t throttle;            // Motor duty from 0 to 255.
  uint32_t rudder_us;          // Rudder pulse width in microseconds.
  uint32_t dive_plane_us;      // Dive plane pulse width in microseconds.
  SwitchPos switch_a;          // Selects forward or reverse.
  SwitchPos switch_b;          // Reserved receiver switch.
  ThreeWaySwitchPos switch_c;  // Selects the ballast action.
  SwitchPos switch_d;          // Reserved receiver switch.
  uint16_t knob_a;             // Reserved receiver knob.
  uint16_t knob_b;             // Reserved receiver knob.

 private:
  static constexpr uint8_t kChannelCount = 10;
  uint16_t channel_data_[kChannelCount];
  IBusBM ibus_;
};

}  // namespace legacy
}  // namespace bajols

#endif  // BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_INPUT_H_
