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

#ifndef BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_DATA_UTILS_H_
#define BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_DATA_UTILS_H_

#include <stdint.h>

namespace bajols {
namespace legacy {
constexpr uint16_t kMinRawInput = 1000;
constexpr uint16_t kMidRawInput = 1500;
constexpr uint16_t kMaxRawInput = 2000;
// Servo travel and pulse widths from the HD-3001HB datasheet.
// https://www.pololu.com/file/0J728/HD-3001HB.pdf
constexpr uint32_t kServoMaxUs = 2200;
constexpr uint32_t kServoMinUs = 800;
constexpr uint32_t kServoCenterUs = (kServoMinUs + kServoMaxUs) / 2;
constexpr uint32_t kDegreesOfTravel = 165;
constexpr uint32_t kRudderMinUs =
    (-15 / static_cast<double>(kDegreesOfTravel)) *
        (kServoMaxUs - kServoMinUs) +
    kServoCenterUs;
constexpr uint32_t kRudderMaxUs =
    (15 / static_cast<double>(kDegreesOfTravel)) * (kServoMaxUs - kServoMinUs) +
    kServoCenterUs;
constexpr uint32_t kDivePlaneMinUs =
    (-15 / static_cast<double>(kDegreesOfTravel)) *
        (kServoMaxUs - kServoMinUs) +
    kServoCenterUs;
constexpr uint32_t kDivePlaneMaxUs =
    (15 / static_cast<double>(kDegreesOfTravel)) * (kServoMaxUs - kServoMinUs) +
    kServoCenterUs;

// Two-position FlySky switch.
enum class SwitchPos { kUp, kDown };

// Three-position FlySky switch.
enum class ThreeWaySwitchPos { kUp, kMiddle, kDown };
}  // namespace legacy
}  // namespace bajols

#endif  // BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_DATA_UTILS_H_
