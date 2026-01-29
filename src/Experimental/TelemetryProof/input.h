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

#ifndef INPUT_h
#define INPUT_h

#include "dataUtils.h"
#include "debug.h"
#include "Arduino.h"
#include "IBusBM.h"
#include "motor.h"

// #define DEBUG_TRACE
// #define DEBUG_WARN
// #define DEBUG_ERROR
// #define DEBUG_INFO

namespace Data
{
  static constexpr uint8_t RUDDER_INDEX = 0;
  static constexpr uint8_t DIVE_PLANE_INDEX = 1;
  static constexpr uint8_t THROTTLE_INDEX = 2;
  static constexpr uint8_t SWA_INDEX = 4; 
  static constexpr uint8_t SWB_INDEX = 5; 
  static constexpr uint8_t SWC_INDEX = 6; 
  static constexpr uint8_t SWD_INDEX = 7; 
  static constexpr uint8_t VRA_INDEX = 9; 
  static constexpr uint8_t VRB_INDEX = 10
  ; 
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
      uint32_t rudder;

      /* Dive Plane angle, between -15 and 15 */
      uint32_t divePlane;

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
        /* Number of channels to read from reciever */
        static constexpr uint8_t NUM_CHANNELS = 10;

        /* Raw sensor data */
        uint16_t channelData[NUM_CHANNELS];

        /* iBus object */
        IBusBM ibus;
  }; 
}

#endif
