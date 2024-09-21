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

/*
 * Motor.h - Library for interacting with DC motors.
 */
#ifndef Motor_h
#define Motor_h

#include "Arduino.h"

/* Holds classes for controlling motors */
namespace Motor
{
  enum Direction
  {
    COAST,
    STOP,
    FORWARD,
    BACKWARD
  };
  
  /* Encapsulates controlling a DC motor via a motor driver such as the L298N */
  class HBridgePWM
  {
    public:
      /* Represents the max for pwm duty cycles */
      const static uint8_t MAX_PWM_VALUE = 255;

      /* Represents the min for pwm duty cycles */
      const static uint8_t MIN_PWM_VALUE = 0;

      /* Default Contructor */
      HBridgePWM() : HBridgePWM(DD2, DD3, DD4) {}

      /* Constructor
       * @param input1 Pin that helps to control direction of motor
       * @param input2 Pin that helps to control direction of motor
       * @param pwmPin Pin that controls speed of motor
       */
      HBridgePWM(uint8_t input1, uint8_t input2, uint8_t pwmPin) : INPUT1(input1), INPUT2(input2), PWM_PIN(pwmPin)
      {
        pinMode(this->INPUT1, OUTPUT);
        pinMode(this->INPUT2, OUTPUT);
        pinMode(this->PWM_PIN, OUTPUT);
      };

      /*
       * Set speed and direction of motor
       * @param forward True to go forwards, else backwards
       * @param pwm Set duty cycle (on portion) of pwm pulse, higher is faster, max of 255.
       */
      void control(Direction direction, uint8_t pwm);

      /*
       * Set the motor to go forward
       */
      void forward();

      /*
       * Set the motor to go backward
       */
      void backward();

      /*
       * Set the motors speed
       * @param pwm The duty cycle (on portion) of pwm pulse, higher is faster, max of 255.
       */
      void setSpeed(uint8_t pwm);

      /*
       * Set the motors to hard stop
       */
      void stop();

      /*
       * Turn the motors off (coast)
       */
      void off();

    private:
      /* Pin that controls motor direction */
      const uint8_t INPUT1;

      /* Pin that controls motor direction */
      const uint8_t INPUT2;

      /* Pin that controls motor speed */
      const uint8_t PWM_PIN;

      /* Current rotations per minute */
      int32_t rpm;

      /* Duty Cycle Motor is set */
      uint8_t pwmLevel;

      /* Current state of the motor (COAST, STOP, FORWARD, BACKWARDS) */
      Direction state;
  };
}

#endif