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

#ifndef DATA_UTILS_h
#define DATA_UTILS_h

#include "Arduino.h"

namespace Data
{
  static constexpr uint16_t MIN_RAW_INPUT = 1000;
  static constexpr uint16_t MID_RAW_INPUT = 1500;
  static constexpr uint16_t MAX_RAW_INPUT = 2000;
  // See https://www.pololu.com/file/0J728/HD-3001HB.pdf for where this data came from
  static constexpr uint32_t MAX_MICROSECONDS = 2200;
  static constexpr uint32_t MIN_MICROSECONDS = 800;
  static constexpr uint32_t MID_POINT = (MIN_MICROSECONDS + MAX_MICROSECONDS) / 2;
  static constexpr uint32_t DEGREES_OF_TRAVEL = 165;
  static constexpr uint32_t MIN_RUDDER_ANGLE = (-15 / (double)DEGREES_OF_TRAVEL) * (MAX_MICROSECONDS - MIN_MICROSECONDS) + MID_POINT;
  static constexpr uint32_t MAX_RUDDER_ANGLE = (15 / (double)DEGREES_OF_TRAVEL) * (MAX_MICROSECONDS - MIN_MICROSECONDS) + MID_POINT;
  static constexpr uint32_t MIN_DIVE_PLANE_ANGLE = (-15 / (double)DEGREES_OF_TRAVEL) * (MAX_MICROSECONDS - MIN_MICROSECONDS) + MID_POINT;
  static constexpr uint32_t MAX_DIVE_PLANE_ANGLE = (15 / (double)DEGREES_OF_TRAVEL) * (MAX_MICROSECONDS - MIN_MICROSECONDS) + MID_POINT;

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
}

#endif