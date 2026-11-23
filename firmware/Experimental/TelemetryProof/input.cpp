#if defined(BAJOLS_ENABLE_LEGACY_CONTROL)
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

#include "input.h"

#include <Arduino.h>

#include "debug.h"
#include "motor.h"

namespace bajols {
namespace legacy {
namespace {

constexpr uint8_t kRudderIndex = 0;
constexpr uint8_t kDivePlaneIndex = 1;
constexpr uint8_t kThrottleIndex = 2;
constexpr uint8_t kSwitchAIndex = 4;
constexpr uint8_t kSwitchCIndex = 6;

}  // namespace

Input::Input()
    : throttle(0),
      rudder_us(0),
      dive_plane_us(0),
      switch_a(SwitchPos::kUp),
      switch_b(SwitchPos::kUp),
      switch_c(ThreeWaySwitchPos::kUp),
      switch_d(SwitchPos::kUp),
      knob_a(kMinRawInput),
      knob_b(kMinRawInput) {
  for (uint8_t i = 0; i < kChannelCount; i++) {
    channel_data_[i] = kMinRawInput;
  }
}

void Input::Begin() {
  ibus_.begin(Serial2);
}

void Input::Read() {
  for (uint8_t i = 0; i < kChannelCount; i++) {
    channel_data_[i] = ibus_.readChannel(i);
    DEBUG_PRINT_TRACE("Read channel ");
    DEBUG_PRINT_TRACE(i);
    DEBUG_PRINT_TRACE(" : ");
    DEBUG_PRINTLN_TRACE(channel_data_[i]);
  }

  rudder_us = map(channel_data_[kRudderIndex], kMinRawInput, kMaxRawInput,
                  kRudderMinUs, kRudderMaxUs);
  dive_plane_us = map(channel_data_[kDivePlaneIndex], kMinRawInput,
                      kMaxRawInput, kDivePlaneMinUs, kDivePlaneMaxUs);
  throttle = map(channel_data_[kThrottleIndex], kMinRawInput, kMaxRawInput,
                 kMinPwmValue, kMaxPwmValue);

  switch (channel_data_[kSwitchAIndex]) {
    case kMinRawInput:
      switch_a = SwitchPos::kUp;
      break;
    case kMaxRawInput:
      switch_a = SwitchPos::kDown;
      break;
  }
  switch (channel_data_[kSwitchCIndex]) {
    case kMinRawInput:
      switch_c = ThreeWaySwitchPos::kUp;
      break;
    case kMidRawInput:
      switch_c = ThreeWaySwitchPos::kMiddle;
      break;
    case kMaxRawInput:
      switch_c = ThreeWaySwitchPos::kDown;
  }

  DEBUG_PRINTLN_TRACE("Raw to Mapped data values");
  DEBUG_PRINTLN_TRACE("-------------------------");
  DEBUG_PRINTLN_TRACE("Object\t\t|\tRaw\t|\tMapped");

  DEBUG_PRINT_TRACE("Rudder\t\t|\t");
  DEBUG_PRINT_TRACE(channel_data_[kRudderIndex]);
  DEBUG_PRINT_TRACE("\t|\t");
  DEBUG_PRINTLN_TRACE(rudder_us);

  DEBUG_PRINT_TRACE("Dive Plane\t|\t");
  DEBUG_PRINT_TRACE(channel_data_[kDivePlaneIndex]);
  DEBUG_PRINT_TRACE("\t|\t");
  DEBUG_PRINTLN_TRACE(dive_plane_us);

  DEBUG_PRINT_TRACE("Throttle\t|\t");
  DEBUG_PRINT_TRACE(channel_data_[kThrottleIndex]);
  DEBUG_PRINT_TRACE("\t|\t");
  DEBUG_PRINTLN_TRACE(throttle);

  DEBUG_PRINT_TRACE("switch_a\t\t|\t");
  DEBUG_PRINT_TRACE(channel_data_[kSwitchAIndex]);
  DEBUG_PRINT_TRACE("\t|\t");
  DEBUG_PRINTLN_TRACE(static_cast<int>(switch_a));

  DEBUG_PRINT_TRACE("switch_c\t\t|\t");
  DEBUG_PRINT_TRACE(channel_data_[kSwitchCIndex]);
  DEBUG_PRINT_TRACE("\t|\t");
  DEBUG_PRINTLN_TRACE(static_cast<int>(switch_c));
}
}  // namespace legacy
}  // namespace bajols

#endif  // BAJOLS_ENABLE_LEGACY_CONTROL
