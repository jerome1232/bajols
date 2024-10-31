#include "Arduino.h"
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

namespace data
{
  Data::Output::Begin()
  {
    this->ibus.begin(Serial2);
    this->speedSensor = ibus.addSensor(SPEED);
    this->rpmSensor = ibus.addSensor(IBUSS_RPM);
    this->presSensor = ibus.addSensor(PRESSURE, PRESSURE_SIZE);
    this->voltageSensor = ibus.addSensor(IBUSS_EXTV);
    this->headingSensor = ibus.addSensor(HEADING);

    DEBUG_PRINT_INFO("Speed sensor index: ");
    DEBUG_PRINTLN_INFO(this->speedSensor);
    DEBUG_PRINT_INFO("RPM sensor index: ");
    DEBUG_PRINTLN_INFO(this->rpmSensor);
    DEBUG_PRINT_INFO("Pressure sensor index: ");
    DEBUG_PRINTLN_INFO(this->presSensor);
    DEBUG_PRINT_INFO("Voltage sensor index: ");
    DEBUG_PRINTLN_INFO(this->voltageSensor);
    DEBUG_PRINT_INFO("Heading sensor index: ");
    DEBUG_PRINTLN_INFO(this->headingSensor);
  }

  Data::Output::SetSensors(const Data::Input& input, int16_t _rpm)
  {
    rpm = _rpm;
    switch (input.swC) 
    {
      case ThreeWaySwitchPos::UP:
        pres += 1;
        break;
      case ThreeWaySwitchPos::DOWN:
        pres -= 1;
        break;
    }

    voltage = analogRead(A1);

    if (++heading > 359)
    {
      heading = 0;
    }

    speed = _rpm / 3;

    DEBUG_PRINTLN_INFO("Streaming Telemetry");
    ibus.setSensorMeasurement(this->rpmSensor, this->rpm);
    ibus.setSensorMeasurement(this->presSensor, this->pres);
    ibus.setSensorMeasurement(this->voltageSensor, this->voltage);
    ibus.setSensorMeasurement(this->headingSensor, this->heading);
    ibus.setSensorMeasurement(this->speedSensor, this->speed);

    DEBUG_PRINT_INFO("Pressure :\t");
    DEBUG_PRINTLN_INFO(pres);

    DEBUG_PRINT_INFO("Heading :\t");
    DEBUG_PRINTLN_INFO(heading);

    DEBUG_PRINT_INFO("RPM :\t\t");
    DEBUG_PRINTLN_INFO(rpm);

    DEBUG_PRINT_INFO("Speed :\t\t");
    DEBUG_PRINTLN_INFO(speed);

    DEBUG_PRINT_INFO("Volts :\t\t");
    DEBUG_PRINTLN_INFO(voltage);
  };
}