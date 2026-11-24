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

#ifndef BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_DEBUG_H_
#define BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_DEBUG_H_

#include <Arduino.h>

// Uncomment a level to enable serial logging across the legacy sketch.
// Each level also enables the less verbose levels.
// #define DEBUG_TRACE
// #define DEBUG_ERROR
// #define DEBUG_WARN
// #define DEBUG_INFO
#if defined(DEBUG_TRACE)
#define DEBUG_PRINT_TRACE(x) do { Serial.print(x); } while (0)
#define DEBUG_PRINTLN_TRACE(x) do { Serial.println(x); } while (0)
#else
#define DEBUG_PRINT_TRACE(x) do {} while (0)
#define DEBUG_PRINTLN_TRACE(x) do {} while (0)
#endif

#if defined(DEBUG_TRACE) || defined(DEBUG_ERROR)
#define DEBUG_PRINT_ERROR(x) do { Serial.print(x); } while (0)
#define DEBUG_PRINTLN_ERROR(x) do { Serial.println(x); } while (0)
#else
#define DEBUG_PRINT_ERROR(x) do {} while (0)
#define DEBUG_PRINTLN_ERROR(x) do {} while (0)
#endif

#if defined(DEBUG_TRACE) || defined(DEBUG_ERROR) || defined(DEBUG_WARN)
#define DEBUG_PRINT_WARN(x) do { Serial.print(x); } while (0)
#define DEBUG_PRINTLN_WARN(x) do { Serial.println(x); } while (0)
#else
#define DEBUG_PRINT_WARN(x) do {} while (0)
#define DEBUG_PRINTLN_WARN(x) do {} while (0)
#endif

#if defined(DEBUG_TRACE) || defined(DEBUG_ERROR) || defined(DEBUG_WARN) || \
    defined(DEBUG_INFO)
#define DEBUG_BEGIN(x) do { Serial.begin(x); } while (0)
#define DEBUG_PRINT_INFO(x) do { Serial.print(x); } while (0)
#define DEBUG_PRINTLN_INFO(x) do { Serial.println(x); } while (0)
#else
#define DEBUG_BEGIN(x) do {} while (0)
#define DEBUG_PRINT_INFO(x) do {} while (0)
#define DEBUG_PRINTLN_INFO(x) do {} while (0)
#endif

#endif  // BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_DEBUG_H_
