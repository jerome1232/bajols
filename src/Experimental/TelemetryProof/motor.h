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
#include "Encoder.h"
#include "debug.h"

/* Holds classes for controlling motors */
namespace Motor
{
  // Constants
  static constexpr float MINUTE_MS = 60000.0f;
  static constexpr uint16_t MAX_PWM_VALUE = 255;
  static constexpr uint8_t MIN_PWM_VALUE = 0;
  static constexpr uint32_t UPDATE_INTERVAL = 50;
  static constexpr int16_t PULSES_PER_REVOLUTION = 8400;

  // pins
  static constexpr uint8_t DEFAULT_INPUT_1_PIN = 22;
  static constexpr uint8_t DEFAULT_INPUT_2_PIN = 23;
  static constexpr uint8_t DEFAULT_PWM_PIN = 2;
  static constexpr uint8_t DEFAULT_ENCODER_SIGNAL_A_PIN = 21;
  static constexpr uint8_t DEFAULT_ENCODER_SIGNAL_B_PIN = 20;

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
   * HBridge class - Controls basic motor direction without speed control.
   */
  class HBridge
  {
    public:
      /* Default Constructor */
      HBridge() 
        : HBridge(DEFAULT_INPUT_1_PIN, DEFAULT_INPUT_2_PIN) {};

      /* Parametized Constructor
       * @param input1 Pin that is used to control direction of motor
       * @param input2 Pin that is used to control direction of motor
       */
      HBridge(const uint8_t input1, const uint8_t input2): INPUT1(input1), INPUT2(input2)
      {
        pinMode(INPUT1, OUTPUT);
        pinMode(INPUT2, OUTPUT);
        off();
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

      /*
       * Get current motor state
       */
      Direction getState();

    protected:
      /* Pin that controls motor direction */
      const uint8_t INPUT1;

      /* Pin that controls motor direction */
      const uint8_t INPUT2;

      /* Current state of the motor */
      Direction state;
  };

  /*
   * HBridgePWM class - Controls motor direction and speed using PWM.
   */
  class HBridgePWM : public HBridge
  {
    public:
      /* Default Contructor */
      HBridgePWM() 
        : HBridgePWM(DEFAULT_INPUT_1_PIN, DEFAULT_INPUT_2_PIN, DEFAULT_PWM_PIN) {};

      /* Parametized Constructor
       * @param input1 Pin that helps to control direction of motor
       * @param input2 Pin that helps to control direction of motor
       * @param pwmPin Pin that controls speed of motor
       */
      HBridgePWM(const uint8_t input1, const uint8_t input2, const uint8_t pwmPin) 
        : HBridge(input1, input2), PWM_PIN(pwmPin), pwmLevel(0)
      {
        pinMode(PWM_PIN, OUTPUT);
      };

      /*
       * Set motor direction and speed.
       * @param direction Direction to set the motor (FORWARD, BACKWARD, COAST, STOP)
       * @param pwm PWM duty cycle (0-255)
       */
      virtual void set(Direction direction, uint8_t pwm);

      /*
      * Set motor speed.
      * @param pwm PWM duty cycle (0-255)
      */
      virtual void setSpeed(uint8_t pwm);

      /*
       * Set the motors to hard stop
       */
      virtual void stop() override;

      /* Turn the motors off (coast) */
      virtual void off() override;

      /* Get the current duty cycle of the motor */
      uint8_t getSpeed() const;

    private:
      /* Pin that controls motor speed */
      uint8_t PWM_PIN;

      /* Duty Cycle Motor is currently set to */
      uint16_t pwmLevel;
  };

  class HBridgePWMEnc : public HBridgePWM
  {
    public:
      HBridgePWMEnc() 
        : HBridgePWMEnc(DEFAULT_INPUT_1_PIN, DEFAULT_INPUT_2_PIN, DEFAULT_PWM_PIN, DEFAULT_ENCODER_SIGNAL_A_PIN, DEFAULT_ENCODER_SIGNAL_B_PIN) {};
      
      HBridgePWMEnc(uint8_t input1, uint8_t input2, uint8_t pwmPin, uint8_t interrupt1, uint8_t interrupt2)
        : HBridgePWM(input1, input2, pwmPin), encoder(interrupt1, interrupt2), rpm(0) {};

      int32_t getRpm()
      {
        return rpm;
      }

      void set(Direction direction, uint8_t speed) override;

      void setSpeed(uint8_t speed) override;

      void off() override;

      void stop() override;

      /*
        * Reads encoder
        */
      void read();

      private:
        /* 
        * Current rotations per minute
        * Unused and inaccessible right now
        */
        volatile int32_t rpm;

        /*
         * Last time in milliseconds
         * pulse count was checked
         */
        uint32_t lastTime = millis();

        /*
         * Encoder for reading motor speed
         */
        Encoder encoder;
  };
}

#endif