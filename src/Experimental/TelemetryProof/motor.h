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
  /* The max value for pwm duty cycles */
  const static uint8_t MAX_PWM_VALUE = 255;

  /* The min value for pwm duty cycles */
  const static uint8_t MIN_PWM_VALUE = 0;

  /*
   * Possible motor states
   */
  enum Direction
  {
    COAST, // Motor is in an unpowered state allowing coasting
    STOP, // Motor is in a powered, stoped state. Will not allow movement
    FORWARD, // Motor is in a forward state
    BACKWARD // Motor is in a backwards state
  };

  /*
   * Encapsulates the details for running a 
   * DC motor connected to a typical HBridge such
   * as the L298N, with PWM motor control diabled
   * (motor is always fully on or fully off)
   */
  class HBridge
  {
    public:
      /* Default Constructor */
      HBridge() : HBridge(DD2, DD3) {};

      /* Constructor
       * @param input1 Pin that is used to control direction of motor
       * @param input2 Pin that is used to control direction of motor
       */
      HBridge(uint8_t input1, uint8_t input2)
      {
        this->INPUT1 = input1;
        this->INPUT2 = input2;
        pinMode(this->INPUT1, OUTPUT);
        pinMode(this->INPUT2, OUTPUT);
      };

      /*
       * Set the motor to go forward
       */
      virtual void forward();

      /*
       * Set the motor to go backward
       */
      virtual void backward();

      /*
       * Set the motors to hard stop
       */
      virtual void stop();

      /*
       * Turn the motors off (coast)
       */
      virtual void off();

    protected:
      /* Pin that controls motor direction */
      uint8_t INPUT1;

      /* Pin that controls motor direction */
      uint8_t INPUT2;

      /* Current state of the motor (COAST, STOP, FORWARD, BACKWARDS) */
      Direction state;
  };

  /*
   * Encapsulates the details for running a 
   * DC motor connected to a typical HBridge such
   * as the L298N, with PWM motor control enabled
   */
  class HBridgePWM : public HBridge
  {
    public:
      /* Default Contructor */
      HBridgePWM() : HBridgePWM(DD2, DD3, DD4) {};

      /* Constructor
       * @param input1 Pin that helps to control direction of motor
       * @param input2 Pin that helps to control direction of motor
       * @param pwmPin Pin that controls speed of motor
       */
      HBridgePWM(uint8_t input1, uint8_t input2, uint8_t pwmPin)
      {
        HBridge(input1, input2);
        this->PWM_PIN = pwmPin;
        pinMode(PWM_PIN, OUTPUT);
      };

      /*
       * Set speed and direction of motor
       * @param direction Sets direction of motor possible values are COAST, FORWARD, BACKWARD, STOP
       * @param pwm Set duty cycle (on portion) of pwm pulse, higher is faster, max of 255
       */
      void set(Direction direction, uint8_t pwm);

      /*
       * Set the motors speed
       * @param pwm The duty cycle (on portion) of pwm pulse, higher is faster, max of 255
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

      Direction getState();

    private:
      /* Pin that controls motor speed */
      uint8_t PWM_PIN;

      /* 
       * Current rotations per minute
       * Unused and inaccessible right now
       */
      int32_t rpm;

      /* Duty Cycle Motor is set */
      uint8_t pwmLevel;

      Direction state;
  };
}

#endif