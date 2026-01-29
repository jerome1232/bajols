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
 * Download IBusBM library here
 * https://github.com/bmellink/IBusBM
 *
 * Click "<> Code" -> Download zip
 * Add to sketch in Arduino ide
 * Sketch -> Include Library -> Add .zip library
 */

/*
 * Uncomment one of these to enable debug logging to
 * the serial monitor. INFO is the least verbose to
 * TRACE should positivly overwhelm you with logging.
 */
// #define DEBUG_TRACE
// #define DEBUG_WARN
// #define DEBUG_ERROR
// #define DEBUG_INFO

#include <IBusBM.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

#include "motor.h"
#include "input.h"
#include "output.h"
#include "debug.h"
#include "Servo.h"

/*
 * Version number
 */
const String VERSION = "0.0.3";
constexpr uint32_t READ_DELAY = 100;
constexpr uint32_t BAUD_RATE = 115200;

constexpr uint8_t WATER_SOLENOID_PIN = 27;
constexpr uint8_t ENGINE_INPUT_1 = 22;
constexpr uint8_t ENGINE_INPUT_2 = 23;
constexpr uint8_t ENGINE_PWM = 11;
constexpr uint8_t ENGINE_ENCODER_TRIGGER_1 = 2;
constexpr uint8_t ENGINE_ENCODER_TRIGGER_2 = 3;

constexpr uint8_t WATER_PUMP_INPUT_1 = 24;
constexpr uint8_t WATER_PUMP_INPUT_2 = 25;

constexpr uint8_t RUDDER = 0;
constexpr uint8_t DIVE_PLANE = 1;

/* Rx data received from controller */
Data::Input Rx;

/* Tx data we are sending to controller */
Data::Output Tx;

/* The main screw */
Motor::HBridgePWMEnc engine(ENGINE_INPUT_1, ENGINE_INPUT_2, ENGINE_PWM, ENGINE_ENCODER_TRIGGER_1, ENGINE_ENCODER_TRIGGER_2);

Motor::HBridge waterPump(WATER_PUMP_INPUT_1, WATER_PUMP_INPUT_2);

/* Rudder for steering */
Servo rudder;

/* Diveplane for changing depth */
Servo divePlane;

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

/* Stores previous values of rudder/diveplane so we aren't rewriting same value to servo over and over */
int32_t oldRudder = Data::MID_POINT;
int32_t oldDivePlane = Data::MID_POINT;
Data::ThreeWaySwitchPos oldswC = Data::ThreeWaySwitchPos::UP;
int32_t oldThrottle = Data::MIN_RAW_INPUT;
Data::SwitchPos oldswA = Data::SwitchPos::UP;

/* Used for timing with no delay */
uint32_t previousMillis = 0;

uint8_t nextLedState = LOW;

void setup()
{
  DEBUG_BEGIN(BAUD_RATE);
  // Serial.begin(BAUD_RATE);
  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(50);
  pwm.writeMicroseconds(RUDDER, oldRudder);
  pwm.writeMicroseconds(DIVE_PLANE, oldDivePlane);
  Rx.Begin();
  Tx.Begin();

  // Normally I'd write a small class to work water pump
  // but it's literally an on off relay so meh
  pinMode(WATER_SOLENOID_PIN, OUTPUT);
  digitalWrite(WATER_SOLENOID_PIN, LOW);

  pinMode(8, OUTPUT);
  digitalWrite(8, LOW);

  DEBUG_PRINTLN_INFO("Starting");
  uint32_t current = millis();
  while (current < 5000)
  {
    digitalWrite(8, HIGH);
    delay(100);
    digitalWrite(8, LOW);
    delay(50);
    current = millis();
  }
}

void loop() {

  uint32_t current = millis();
  bool takeAction = (current - previousMillis) > READ_DELAY;

  if (takeAction)
  {
    digitalWrite(8, nextLedState);
    nextLedState = nextLedState == LOW ? HIGH : LOW;
    previousMillis = current;
    Rx.Read();

    if (oldThrottle != Rx.throttle || oldswA != Rx.swA)
    {
      oldThrottle = Rx.throttle;
      oldswA = Rx.swA;
      engine.set(Rx.swA == Data::SwitchPos::UP ? Motor::Direction::FORWARD : Motor::Direction::BACKWARD, Rx.throttle);
    }

    uint16_t rpm = engine.getRpm();

    if (Rx.rudder != oldRudder)
    {
      pwm.writeMicroseconds(RUDDER, Rx.rudder);
      oldRudder = Rx.rudder;
    }

    if (Rx.divePlane != oldDivePlane)
    {
      pwm.writeMicroseconds(DIVE_PLANE, Rx.divePlane);
      oldDivePlane = Rx.divePlane;
    }

    if (Rx.swC != oldswC)
    {
      oldswC = Rx.swC;
      switch(Rx.swC)
      {
        case Data::ThreeWaySwitchPos::UP:
          // Let water out, solenoid open, pump off
          digitalWrite(WATER_SOLENOID_PIN, LOW);
          waterPump.off();
          break;
        case Data::ThreeWaySwitchPos::MIDDLE:
          // Pull water in, solenoid open, pump on
          digitalWrite(WATER_SOLENOID_PIN, LOW);
          waterPump.forward();
          break;
        case Data::ThreeWaySwitchPos::DOWN:
          // Hold water, solenoid closed, pump off
          digitalWrite(WATER_SOLENOID_PIN, HIGH);
          waterPump.off();
          break;
      }

    }


    Tx.SetSensors(Rx, rpm);
    digitalWrite(8, LOW);
  }
}
