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

 void Motor::HBridgePWM::control(Direction direction, uint8_t pwm)
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

 void Motor::HBridgePWM::forward()
 {
  digitalWrite(this->INPUT1, HIGH);
  digitalWrite(this->INPUT2, LOW);
  this->state = FORWARD;
 }

 void Motor::HBridgePWM::backward()
 {
  digitalWrite(this->INPUT1, LOW);
  digitalWrite(this->INPUT2, HIGH);
  this->state = BACKWARD;
 }

 void Motor::HBridgePWM::setSpeed(uint8_t pwm)
 {
  analogWrite(this->PWM_PIN, pwm);
  this->pwmLevel = pwm;
 }

 void Motor::HBridgePWM::stop()
 {
  digitalWrite(this->INPUT1, HIGH);
  digitalWrite(this->INPUT2, HIGH);
  this->setSpeed(MAX_PWM_VALUE);
  this->state = STOP;
 }

 void Motor::HBridgePWM::off()
 {
  digitalWrite(this->INPUT1, LOW);
  digitalWrite(this->INPUT2, LOW);
  this->setSpeed(MIN_PWM_VALUE);
  this->state = COAST;
 }