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

#ifndef OUTPUT_h
#define OUTPUT_h

#include "dataUtils.h"
#include "Arduino.h"
#include "IBusBM.h"
#include "motor.h"
#include "debug.h"
#include "input.h"

// #define DEBUG_TRACE
// #define DEBUG_WARN
// #define DEBUG_ERROR
// #define DEBUG_INFO

/* Holds classes for working with Rx/Tx */
namespace Data
{
  class Output
  {
    public:
      /* Constructor */
      Output(uint32_t telemDelay)
        : TELM_DELAY(telemDelay) 
      {
        this->rpm = 0;
        this->pres = 101300;  // Sea level
        this->voltage = 960;  // 9.6 volts
        this->heading = 90;   // Due east?
        this->speed = 0;
      };

      /* Starts serial communication */
      Begin();

      /* updates sensor values */
      SetSensors(const Data::Input& input);

    private:
      /* Delay between telemetry updates in ms */
      const uint32_t TELM_DELAY;

      /* Pressure sensor address */
      const uint8_t PRESSURE = 0x41;

      /* Size of pressure sensor data in bytes */
      const uint8_t PRESSURE_SIZE = 4;

      /* 
      * Heading sensor address, this doesn't seem to be an actual
      * FlySky address, it shows as '5' and is raw data
      */
      const uint8_t HEADING = 0x08;

      /* Speed sensor address */
      const uint8_t SPEED = 0x7E;

      /* Stores the last time we sent telemetry data in milliseconds */
      int32_t previousMillis = 0;

      int16_t count = 0;

      /* Fake sensor data */
      int32_t pres;
      int16_t rpm;
      int16_t voltage;
      int16_t heading;
      int16_t speed;

      /* 
      * After adding sensors to iBus they get assigned indices
      * These store these indices for use when updating the sensor
      */
      uint8_t rpmSensor;
      uint8_t presSensor;
      uint8_t voltageSensor;
      uint8_t headingSensor;
      uint8_t speedSensor;

      /* The iBus object */
      IBusBM ibus;
  };
}

#endif