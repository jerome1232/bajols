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

#include "output.h"

#include <Arduino.h>

#include "debug.h"

namespace bajols {
namespace legacy {
namespace {

constexpr uint8_t kPressure = 0x41;
constexpr uint8_t kPressureSize = 4;
constexpr uint8_t kHeading = 0x08;
constexpr uint8_t kSpeed = 0x7E;

}  // namespace

void Output::Begin() {
  ibus_.begin(Serial3);
  speed_sensor_ = ibus_.addSensor(kSpeed);
  rpm_sensor_ = ibus_.addSensor(IBUSS_RPM);
  pressure_sensor_ = ibus_.addSensor(kPressure, kPressureSize);
  voltage_sensor_ = ibus_.addSensor(IBUSS_EXTV);
  heading_sensor_ = ibus_.addSensor(kHeading);

  DEBUG_PRINT_INFO("Speed sensor index: ");
  DEBUG_PRINTLN_INFO(speed_sensor_);
  DEBUG_PRINT_INFO("RPM sensor index: ");
  DEBUG_PRINTLN_INFO(rpm_sensor_);
  DEBUG_PRINT_INFO("Pressure sensor index: ");
  DEBUG_PRINTLN_INFO(pressure_sensor_);
  DEBUG_PRINT_INFO("Voltage sensor index: ");
  DEBUG_PRINTLN_INFO(voltage_sensor_);
  DEBUG_PRINT_INFO("Heading sensor index: ");
  DEBUG_PRINTLN_INFO(heading_sensor_);
}

void Output::SetSensors(const Input& input, int16_t rpm) {
  rpm_ = rpm;
  switch (input.switch_c) {
    case ThreeWaySwitchPos::kUp:
      pressure_ += 1;
      break;
    case ThreeWaySwitchPos::kDown:
      pressure_ -= 1;
      break;
    case ThreeWaySwitchPos::kMiddle:
      break;
  }

  voltage_ = analogRead(A1);

  if (++heading_ > 359) {
    heading_ = 0;
  }

  speed_ = rpm / 3;

  DEBUG_PRINTLN_INFO("Streaming Telemetry");
  ibus_.setSensorMeasurement(rpm_sensor_, rpm_);
  ibus_.setSensorMeasurement(pressure_sensor_, pressure_);
  ibus_.setSensorMeasurement(voltage_sensor_, voltage_);
  ibus_.setSensorMeasurement(heading_sensor_, heading_);
  ibus_.setSensorMeasurement(speed_sensor_, speed_);

  DEBUG_PRINT_INFO("Pressure :\t");
  DEBUG_PRINTLN_INFO(pressure_);

  DEBUG_PRINT_INFO("Heading :\t");
  DEBUG_PRINTLN_INFO(heading_);

  DEBUG_PRINT_INFO("RPM :\t\t");
  DEBUG_PRINTLN_INFO(rpm_);

  DEBUG_PRINT_INFO("Speed :\t\t");
  DEBUG_PRINTLN_INFO(speed_);

  DEBUG_PRINT_INFO("Volts :\t\t");
  DEBUG_PRINTLN_INFO(voltage_);
}
}  // namespace legacy
}  // namespace bajols

#endif  // BAJOLS_ENABLE_LEGACY_CONTROL
