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
  static constexpr int8_t MIN_RUDDER_ANGLE = -15;
  static constexpr int8_t MAX_RUDDER_ANGLE = 15;
  static constexpr int8_t MIN_DIVE_PLANE_ANGLE = -15;
  static constexpr int8_t MAX_DIVE_PLANE_ANGLE = 15;

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