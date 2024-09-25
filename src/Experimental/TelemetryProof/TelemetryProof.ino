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

#define ENABLE_DEBUG

#define DEBUG_SERIAL_PORT     Serial
#define DEBUG_SER_BAUD_RATE   115200

#include <IBusBM.h>
#include <MacroDebugger.h>

#include "motor.h"
#include "data.h"

/*
 * Version number
 */
const String VERSION = "0.0.3";

Data::Input Rx;
Data::Output Tx;

/* The main screw */
Motor::HBridgePWM engine;

void setup()
{
  DEBUG_BEGIN();

  DEBUGLN("~");
  DEBUGLN("Initializing TelemetryStreamTest v%s", VERSION);
  DEBUGLN("~");
  DEBUGLN("");

  Rx.Begin();

  Tx.Begin();

  DEBUGLN("Sensors initialized");
}

void loop() {
  Rx.Read();
  Tx.SetSensors(Rx);

  DEBUGLN("Throttle: %d", Rx.throttle);
  DEBUGLN("Rudder: %d", Rx.rudder);
  DEBUGLN("DivePlane: %d", Rx.divePlane);

  /* Act on main screw */
  engine.set(Rx.swA == Data::SwitchPos::UP ? Motor::Direction::FORWARD : Motor::Direction::BACKWARD, Rx.throttle);
}