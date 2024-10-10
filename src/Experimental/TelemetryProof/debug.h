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

#ifndef DEBUG_h
#define DEBUG_h

#if defined(DEBUG_TRACE)
  #define DEBUG_PRINT_TRACE(x)    Serial.print(x)
  #define DEBUG_PRINTLN_TRACE(x)  Serial.println(x)
  #define DEBUG_ERROR
#else
  #define DEBUG_PRINT_TRACE(x)
  #define DEBUG_PRINTLN_TRACE(x)
#endif

#if defined(DEBUG_ERROR)
  #define DEBUG_PRINT_ERROR(x)    Serial.print(x)
  #define DEBUG_PRINTLN_ERROR(x)  Serial.println(x)
  #define DEBUG_WARN
#else
  #define DEBUG_PRINT_ERROR(x)
  #define DEBUG_PRINTLN_ERROR(x)
#endif

#if defined(DEBUG_WARN)
  #define DEBUG_PRINT_WARN(x)     Serial.print(x)
  #define DEBUG_PRINTLN_WARN(x)   Serial.println(x)
  #define DEBUG_INFO
#else
  #define DEBUG_PRINT_WARN(x)
  #define DEBUG_PRINTLN_WARN(x)
#endif

#if defined(DEBUG_INFO)
  #define DEBUG_PRINT_INFO(x)     Serial.print(x)
  #define DEBUG_PRINTLN_INFO(x)   Serial.println(x)
#else
  #define DEBUG_PRINT_INFO(x)
  #define DEBUG_PRINTLN_INFO(x)
#endif

#endif
