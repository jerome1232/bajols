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

#include "motor.h"

void Motor::HBridge::forward()
{
  digitalWrite(this->INPUT1, HIGH);
  digitalWrite(this->INPUT2, LOW);
  this->state = FORWARD;
}


void Motor::HBridge::backward()
{
  digitalWrite(this->INPUT1, LOW);
  digitalWrite(this->INPUT2, HIGH);
  this->state = BACKWARD;
}

void Motor::HBridge::stop()
{
  digitalWrite(this->INPUT1, HIGH);
  digitalWrite(this->INPUT2, HIGH);
  this->state = STOP;
}

void Motor::HBridge::off()
{
  digitalWrite(this->INPUT1, LOW);
  digitalWrite(this->INPUT2, LOW);
  this->state = COAST;
}

void Motor::HBridgePWM::set(Direction direction, uint8_t pwm)
{
  switch (direction) 
  {
    case FORWARD:
      this->forward();
      break;
    case BACKWARD:
      this->backward();
      break;
    case COAST:
      this->off();
      break;
    case STOP:
      this->stop();
      break;
  }  

  this->setSpeed(pwm);
}

void Motor::HBridgePWM::setSpeed(uint8_t pwm)
{
  if (pwm > MAX_PWM_VALUE)
  {
    pwm = MAX_PWM_VALUE;
  }
  else if (pwm < MIN_PWM_VALUE)
  {
    pwm = MIN_PWM_VALUE;
  }
  
  analogWrite(this->PWM_PIN, pwm);
  this->pwmLevel = pwm;
}

void Motor::HBridgePWM::stop()
{
  HBridge::stop();
  this->setSpeed(MAX_PWM_VALUE);
}

void Motor::HBridgePWM::off()
{
  HBridge::off();
  this->setSpeed(MIN_PWM_VALUE);
}

Motor::Direction Motor::HBridgePWM::getState()
{
  return this->state;
}