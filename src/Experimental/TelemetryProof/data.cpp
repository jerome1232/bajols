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

#include "data.h"

Data::Input::Input()
{
  this->throttle = 0;
  this->rudder = 0;
  this->divePlane = 0;
  this->swA = SwitchPos::UP;
  this->swB = SwitchPos::UP;
  this->swC = ThreeWaySwitchPos::UP;
  this->swD = SwitchPos::UP;
  this->vrA = MIN_RAW_INPUT;
  this->vrB = MIN_RAW_INPUT;

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

  this->rudder = map(channelData[0], MIN_RAW_INPUT, MAX_RAW_INPUT, MIN_RUDDER_ANGLE, MAX_RUDDER_ANGLE);
  this->divePlane = map(channelData[1], MIN_RAW_INPUT, MAX_RAW_INPUT, MIN_DIVE_PLANE_ANGLE, MAX_DIVE_PLANE_ANGLE);
  this->throttle = map(channelData[2], MIN_RAW_INPUT, MAX_RAW_INPUT, Motor::MIN_PWM_VALUE, Motor::MAX_PWM_VALUE);

  switch (channelData[4])
  {
    case MIN_RAW_INPUT:
      this->swA = SwitchPos::UP;
      break;
    case MAX_RAW_INPUT:
      this->swA = SwitchPos::DOWN;
      break;
  }
  switch (channelData[6]) 
  {
    case MIN_RAW_INPUT:
      this->swC = ThreeWaySwitchPos::UP;
      break;
    case MID_RAW_INPUT:
      this->swC = ThreeWaySwitchPos::MIDDLE;
      break;
    case MAX_RAW_INPUT:
      this->swC = ThreeWaySwitchPos::DOWN;
  }

  DEBUG_PRINTLN_TRACE("Raw to Mapped data values");
  DEBUG_PRINTLN_TRACE("-------------------------");
  DEBUG_PRINTLN_TRACE("Object\t\t|\tRaw\t|\tMapped");

  DEBUG_PRINT_TRACE("Rudder\t\t|\t");
  DEBUG_PRINT_TRACE(channelData[0]);
  DEBUG_PRINT_TRACE("\t|\t");
  DEBUG_PRINTLN_TRACE(this->rudder);

  DEBUG_PRINT_TRACE("Dive Plane\t|\t");
  DEBUG_PRINT_TRACE(channelData[1]);
  DEBUG_PRINT_TRACE("\t|\t");
  DEBUG_PRINTLN_TRACE(this->divePlane);

  DEBUG_PRINT_TRACE("Throttle\t|\t");
  DEBUG_PRINT_TRACE(channelData[2]);
  DEBUG_PRINT_TRACE("\t|\t");
  DEBUG_PRINTLN_TRACE(this->throttle);

  DEBUG_PRINT_TRACE("swA\t\t|\t");
  DEBUG_PRINT_TRACE(channelData[4]);
  DEBUG_PRINT_TRACE("\t|\t");
  DEBUG_PRINTLN_TRACE(int(this->swA));

  DEBUG_PRINT_TRACE("swC\t\t|\t");
  DEBUG_PRINT_TRACE(channelData[6]);
  DEBUG_PRINT_TRACE("\t|\t");
  DEBUG_PRINTLN_TRACE(int(this->swC));
};

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

Data::Output::SetSensors(const Data::Input& input)
{
  uint32_t currentMillis = millis();

  if (currentMillis - this->previousMillis <= TELM_DELAY)
  {
    return;
  }

  this->previousMillis = currentMillis;

  rpm = input.throttle * 10;
  switch (input.swC) 
  {
    case ThreeWaySwitchPos::UP:
      this->pres += 1;
      break;
    case ThreeWaySwitchPos::DOWN:
      this->pres -= 1;
      break;
  }

  if (count > 50 )
  {
    this->voltage--;
    count = 0;
  }
  count++;

  this->heading++;
  if (this->heading > 359)
  {
    this->heading = 0;
  }

  this->speed = rpm / 3;

  DEBUG_PRINTLN_INFO("Streaming Telemetry");
  this->ibus.setSensorMeasurement(this->rpmSensor, this->rpm);
  this->ibus.setSensorMeasurement(this->presSensor, this->pres);
  this->ibus.setSensorMeasurement(this->voltageSensor, this->voltage);
  this->ibus.setSensorMeasurement(this->headingSensor, this->heading);
  this->ibus.setSensorMeasurement(this->speedSensor, this->speed);

  DEBUG_PRINT_INFO("Pressure :\t");
  DEBUG_PRINTLN_INFO(this->pres);

  DEBUG_PRINT_INFO("Heading :\t");
  DEBUG_PRINTLN_INFO(this->heading);

  DEBUG_PRINT_INFO("RPM :\t\t");
  DEBUG_PRINTLN_INFO(this->rpm);

  DEBUG_PRINT_INFO("Speed :\t\t");
  DEBUG_PRINTLN_INFO(this->speed);

  DEBUG_PRINT_INFO("Volts :\t\t");
  DEBUG_PRINTLN_INFO(this->voltage);
}
