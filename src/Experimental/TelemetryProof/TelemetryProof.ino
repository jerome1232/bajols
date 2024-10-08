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

#include "motor.h"
#include "input.h"
#include "output.h"
#include "debug.h"
#include "Servo.h"

/*
 * Version number
 */
const String VERSION = "0.0.3";

const int32_t TELEM_DELAY = 100;

const uint32_t BAUD_RATE = 115200;

/* Rx data received from controller */
Data::Input Rx;

/* Tx data we are sending to controller */
Data::Output Tx(TELEM_DELAY);

/* The main screw */
Motor::HBridgePWM engine;

Servo rudder;

Servo divePlane;

void setup()
{
  Serial.begin(BAUD_RATE);
  Rx.Begin();
  Tx.Begin();
  rudder.attach(DD5);
  divePlane.attach(DD6);
  delay(TELEM_DELAY);
}

void loop() {
  Rx.Read();
  Tx.SetSensors(Rx);

  DEBUG_PRINT_TRACE("Setting engine to ");
  DEBUG_PRINT_TRACE(int(Rx.swA));
  DEBUG_PRINT_TRACE(" with throttle at ");
  DEBUG_PRINTLN_TRACE(Rx.throttle);
  engine.set(Rx.swA == Data::SwitchPos::UP ? Motor::Direction::FORWARD : Motor::Direction::BACKWARD, Rx.throttle);
  rudder.write(Rx.rudder);
  divePlane.write(Rx.divePlane);
}
