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
  }

  this->rudder = map(channelData[0], MIN_RAW_INPUT, MAX_RAW_INPUT, -15, 15);
  this->divePlane = map(channelData[1], MIN_RAW_INPUT, MAX_RAW_INPUT, -15, 15);
  this->throttle = map(channelData[2], MIN_RAW_INPUT, MAX_RAW_INPUT, Motor::MIN_PWM_VALUE, Motor::MAX_PWM_VALUE);
  switch (channelData[4])
  {
    case 1000:
      this->swA = SwitchPos::UP;
      break;
    case 2000:
      this->swA = SwitchPos::DOWN;
      break;
  }
  switch (channelData[6]) 
  {
    case 1000:
      this->swC = ThreeWaySwitchPos::UP;
      break;
    case 1500:
      this->swC = ThreeWaySwitchPos::MIDDLE;
      break;
    case 2000:
      this->swC = ThreeWaySwitchPos::DOWN;
  }
};

Data::Output::Output()
{
  this->rpm = 0;
  this->pres = 101300;
  this->voltage = 960;
  this->heading = 90;
  this->speedSensor = ibus.addSensor(0x7E);
  this->rpmSensor = ibus.addSensor(IBUSS_RPM);
  this->presSensor = ibus.addSensor(PRESSURE, PRESSURE_SIZE);
  this->voltageSensor = ibus.addSensor(IBUSS_EXTV);
  this->headingSensor = ibus.addSensor(HEADING);
}

Data::Output::Begin()
{
  this->ibus.begin(Serial2);
}

Data::Output::SetSensors(const Data::Input& input)
{
  uint32_t currentMillis = millis();

  if (currentMillis - this->previousMillis <= 100)
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

  this->voltage--;
  this->heading++;
  if (this->heading > 360)
  {
    this->heading = 0;
  }

  this->ibus.setSensorMeasurement(this->rpmSensor, this->rpm);
  this->ibus.setSensorMeasurement(this->presSensor, this->pres);
  this->ibus.setSensorMeasurement(this->voltageSensor, this->voltage);
  this->ibus.setSensorMeasurement(this->headingSensor, this->heading);
  this->ibus.setSensorMeasurement(this->speedSensor, this->rpm / 3);
}