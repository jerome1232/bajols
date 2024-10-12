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

#include "input.h"

Data::Input::Input()
  : throttle(0), rudder(0), divePlane(0), swA(SwitchPos::UP), swB(SwitchPos::UP), swC(ThreeWaySwitchPos::UP), swD(SwitchPos::UP), vrA(MIN_RAW_INPUT), vrB(MIN_RAW_INPUT)
{
  for (uint8_t i = 0; i < NUM_CHANNELS; i++)
  {
    channelData[i] = MIN_RAW_INPUT;
  }
};

Data::Input::Begin()
{
  ibus.begin(Serial1);
}

Data::Input::Read()
{
  for (uint8_t i = 0; i < NUM_CHANNELS; i++)
  {
    channelData[i] = ibus.readChannel(i);
    DEBUG_PRINT_TRACE("Read channel ");
    DEBUG_PRINT_TRACE(i);
    DEBUG_PRINT_TRACE(" : ");
    DEBUG_PRINTLN_TRACE(channelData[i]);
  }

  rudder = map(channelData[RUDDER_INDEX], MIN_RAW_INPUT, MAX_RAW_INPUT, MIN_RUDDER_ANGLE, MAX_RUDDER_ANGLE);
  divePlane = map(channelData[DIVE_PLANE_INDEX], MIN_RAW_INPUT, MAX_RAW_INPUT, MIN_DIVE_PLANE_ANGLE, MAX_DIVE_PLANE_ANGLE);
  throttle = map(channelData[THROTTLE_INDEX], MIN_RAW_INPUT, MAX_RAW_INPUT, Motor::MIN_PWM_VALUE, Motor::MAX_PWM_VALUE);

  switch (channelData[SWA_INDEX])
  {
    case MIN_RAW_INPUT:
      swA = SwitchPos::UP;
      break;
    case MAX_RAW_INPUT:
      swA = SwitchPos::DOWN;
      break;
  }
  switch (channelData[SWC_INDEX]) 
  {
    case MIN_RAW_INPUT:
      swC = ThreeWaySwitchPos::UP;
      break;
    case MID_RAW_INPUT:
      swC = ThreeWaySwitchPos::MIDDLE;
      break;
    case MAX_RAW_INPUT:
      swC = ThreeWaySwitchPos::DOWN;
  }

  DEBUG_PRINTLN_TRACE("Raw to Mapped data values");
  DEBUG_PRINTLN_TRACE("-------------------------");
  DEBUG_PRINTLN_TRACE("Object\t\t|\tRaw\t|\tMapped");

  DEBUG_PRINT_TRACE("Rudder\t\t|\t");
  DEBUG_PRINT_TRACE(channelData[RUDDER_INDEX]);
  DEBUG_PRINT_TRACE("\t|\t");
  DEBUG_PRINTLN_TRACE(rudder);

  DEBUG_PRINT_TRACE("Dive Plane\t|\t");
  DEBUG_PRINT_TRACE(channelData[DIVE_PLANE_INDEX]);
  DEBUG_PRINT_TRACE("\t|\t");
  DEBUG_PRINTLN_TRACE(divePlane);

  DEBUG_PRINT_TRACE("Throttle\t|\t");
  DEBUG_PRINT_TRACE(channelData[THROTTLE_INDEX]);
  DEBUG_PRINT_TRACE("\t|\t");
  DEBUG_PRINTLN_TRACE(throttle);

  DEBUG_PRINT_TRACE("swA\t\t|\t");
  DEBUG_PRINT_TRACE(channelData[SWA_INDEX]);
  DEBUG_PRINT_TRACE("\t|\t");
  DEBUG_PRINTLN_TRACE(int(swA));

  DEBUG_PRINT_TRACE("swC\t\t|\t");
  DEBUG_PRINT_TRACE(channelData[SWC_INDEX]);
  DEBUG_PRINT_TRACE("\t|\t");
  DEBUG_PRINTLN_TRACE(int(swC));
};