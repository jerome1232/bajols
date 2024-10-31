#include "HardwareSerial.h"
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
  state = FORWARD;
}


void Motor::HBridge::backward()
{
  digitalWrite(this->INPUT1, LOW);
  digitalWrite(this->INPUT2, HIGH);
  state = BACKWARD;
}

void Motor::HBridge::stop()
{
  digitalWrite(this->INPUT1, HIGH);
  digitalWrite(this->INPUT2, HIGH);
  state = STOP;
}

void Motor::HBridge::off()
{
  digitalWrite(this->INPUT1, LOW);
  digitalWrite(this->INPUT2, LOW);
  state = COAST;
}

Motor::Direction Motor::HBridge::getState()
{
  return state;
}

void Motor::HBridgePWM::set(Direction direction, uint8_t pwm)
{
  DEBUG_PRINTLN_INFO("Set called");
  switch (direction) 
  {
    case FORWARD:
      forward();
      break;
    case BACKWARD:
      backward();
      break;
    case COAST:
      off();
      break;
    case STOP:
      stop();
      break;
  }  

  setSpeed(pwm);
}

void Motor::HBridgePWM::setSpeed(uint8_t pwm)
{
  if (pwm > MAX_PWM_VALUE)
  {
    DEBUG_PRINTLN_ERROR("PWM value exceeds maximum, clamping to MAX_PWM_VALUE");
    pwm = MAX_PWM_VALUE;
  }

  if (pwm < MIN_PWM_VALUE)
  {
    DEBUG_PRINTLN_WARN("PWM value below minimum, clamping to MIN_PWM_VALUE");
    pwm = MIN_PWM_VALUE;
  }

  analogWrite(this->PWM_PIN, pwm);
  pwmLevel = pwm;
}

void Motor::HBridgePWM::stop()
{
  HBridge::stop();
  setSpeed(MAX_PWM_VALUE);
}

void Motor::HBridgePWM::off()
{
  HBridge::off();
  setSpeed(MIN_PWM_VALUE);
}


void Motor::HBridgePWMEnc::read()
{
  uint32_t currentTime = millis();
  uint32_t elapsedTime = currentTime - lastTime;
  if (elapsedTime == 0)
  {
    return;
  }

  if (elapsedTime >= UPDATE_INTERVAL)
  {
    int32_t pulses = this->encoder.readAndReset();
    if (pulses < 0)
    {
      pulses = pulses * -1;
    }
    rpm = (pulses / double(PULSES_PER_REVOLUTION)) * (MINUTE_MS / elapsedTime);
    lastTime = currentTime;
  }
}

void Motor::HBridgePWMEnc::set(Motor::Direction direction, uint8_t speed)
{
  HBridgePWM::set(direction, speed);
  read();
}

void Motor::HBridgePWMEnc::setSpeed(uint8_t speed)
{
  HBridgePWM::setSpeed(speed);
  read();
}

void Motor::HBridgePWMEnc::off()
{
  HBridgePWM::off();
  read();
}

void Motor::HBridgePWMEnc::stop()
{
  HBridgePWM::stop();
  rpm = 0;
}