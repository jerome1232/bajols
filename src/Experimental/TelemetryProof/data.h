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

#ifndef DATA_h
#define DATA_h

#include "Arduino.h"
#include "IBusBM.h"
#include "motor.h"

/* Holds classes for controlling motors */
namespace Data
{
  const static uint16_t MIN_RAW_INPUT = 1000;
  const static uint16_t MAX_RAW_INPUT = 2000;

  /* Possible switch positions */
  enum class SwitchPos
  {
    UP,
    DOWN
  };

  /* Possible 3 way switch postions */
  enum class ThreeWaySwitchPos
  {
    UP,
    MIDDLE,
    DOWN
  };

  /* Transformed controller inpus */
  class Input
  {
    public:
      /* Default constructor */
      Input();

      /* Starts serial communication */
      Begin();

      /* Read data */
      Read();

      /* Throttle value, between 0 and 255 */
      uint8_t throttle;

      /* Rudder angle, between -15 and 15 */
      int8_t rudder;

      /* Dive Plane angle, between -15 and 15 */
      int8_t divePlane;

      /* Throttle reverse switch */
      SwitchPos swA;

      /* Unknown use */
      SwitchPos swB;

      /* Ballast control */
      ThreeWaySwitchPos swC;

      /* Unknown use */
      SwitchPos swD;

      /* Unknown use */
      uint16_t vrA;

      /* Unknown use */
      uint16_t vrB;

      private:
        /* Number of channels to read form reciever */
        const static uint8_t NUM_CHANNELS = 10;

        /* Raw sensor data */
        uint16_t channelData[NUM_CHANNELS];

        /* iBus object */
        IBusBM ibus;
   };
   
   class Output
   {
    public:
      /* Default constructor */
      Output();

      /* Starts serial communication */
      Begin();

      /* updates sensor values */
      SetSensors(const Data::Input& input);

    private:

    /* Pressure sensor address */
    const uint8_t PRESSURE = 0x41;

    /* Size of pressure sensor data in bytes */
    const uint8_t PRESSURE_SIZE = 4;

    /* 
    * Heading sensor address, this doesn't seem to be an actual
    * FlySky address, it shows as '5' and is raw data
    */
    const uint8_t HEADING = 0x08;

    /* Stores the last time we sent telemetry data in milliseconds */
    int32_t previousMillis = 0;

    /* Fake sensor data */
    int32_t pres;
    int16_t rpm;
    int16_t voltage;
    int16_t heading;

    /* 
     * After adding sensors to iBus they get assigned indices
     * These store these indices for use when updating the sensor
     */
    uint8_t rpmSensor;
    uint8_t presSensor;
    uint8_t voltageSensor;
    uint8_t headingSensor;

    /* The iBus object */
    IBusBM ibus;
   };
}

#endif