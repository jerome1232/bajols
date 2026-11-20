#include "BajolsProtocol.h"

#include <string.h>

namespace bajols {
namespace bcp {
namespace {

// Reads one little-endian unsigned value without alignment assumptions.
uint32_t ReadUnsigned(const uint8_t* bytes, size_t width) {
  uint32_t value = 0;
  for (size_t i = 0; i < width; ++i) {
    value |= static_cast<uint32_t>(bytes[i]) << (8 * i);
  }
  return value;
}

// Writes one little-endian value without alignment assumptions.
void WriteUnsigned(uint8_t* bytes, uint32_t value, size_t width) {
  for (size_t i = 0; i < width; ++i) {
    bytes[i] = static_cast<uint8_t>(value >> (8 * i));
  }
}

// Converts unsigned wire bits without an out-of-range signed cast.
int16_t ReadI16(const uint8_t* bytes) {
  uint32_t value = ReadUnsigned(bytes, 2);
  return static_cast<int16_t>(value <= 32767
                                  ? static_cast<int32_t>(value)
                                  : static_cast<int32_t>(value) - 65536L);
}

// Converts all signed 32-bit values, including INT32_MIN, portably.
int32_t ReadI32(const uint8_t* bytes) {
  uint32_t value = ReadUnsigned(bytes, 4);
  if (value <= 2147483647UL) {
    return static_cast<int32_t>(value);
  }
  return -1 - static_cast<int32_t>(0xFFFFFFFFUL - value);
}

// Records a syntax error and provides a common failure return.
bool Fail(Error value, Error& error) {
  error = value;
  return false;
}

// Checks an assigned message ID and its permitted wire direction.
bool ValidateDirection(MessageType type, Direction direction, Error& error) {
  switch (type) {
    case MessageType::kHello:
      if (direction == Direction::kPcToVehicle) {
        return true;
      }
      return Fail(Error::kInvalidDirection, error);
    case MessageType::kHelloReply:
      if (direction == Direction::kVehicleToPc) {
        return true;
      }
      return Fail(Error::kInvalidDirection, error);
    case MessageType::kSessionConfirm:
      if (direction == Direction::kPcToVehicle) {
        return true;
      }
      return Fail(Error::kInvalidDirection, error);
    case MessageType::kSessionReady:
      if (direction == Direction::kVehicleToPc) {
        return true;
      }
      return Fail(Error::kInvalidDirection, error);
    case MessageType::kControlTicket:
      if (direction == Direction::kVehicleToPc) {
        return true;
      }
      return Fail(Error::kInvalidDirection, error);
    case MessageType::kHeartbeat:
      if (direction == Direction::kPcToVehicle ||
          direction == Direction::kVehicleToPc) {
        return true;
      }
      return Fail(Error::kInvalidDirection, error);
    case MessageType::kMovementCommand:
      if (direction == Direction::kPcToVehicle) {
        return true;
      }
      return Fail(Error::kInvalidDirection, error);
    case MessageType::kArm:
      if (direction == Direction::kPcToVehicle) {
        return true;
      }
      return Fail(Error::kInvalidDirection, error);
    case MessageType::kDisarm:
      if (direction == Direction::kPcToVehicle) {
        return true;
      }
      return Fail(Error::kInvalidDirection, error);
    case MessageType::kClearFailsafe:
      if (direction == Direction::kPcToVehicle) {
        return true;
      }
      return Fail(Error::kInvalidDirection, error);
    case MessageType::kEmergencyStop:
      if (direction == Direction::kPcToVehicle) {
        return true;
      }
      return Fail(Error::kInvalidDirection, error);
    case MessageType::kCommandResult:
      if (direction == Direction::kVehicleToPc) {
        return true;
      }
      return Fail(Error::kInvalidDirection, error);
    case MessageType::kBuoyancyCommand:
      if (direction == Direction::kPcToVehicle) {
        return true;
      }
      return Fail(Error::kInvalidDirection, error);
    case MessageType::kCapabilities:
      if (direction == Direction::kVehicleToPc) {
        return true;
      }
      return Fail(Error::kInvalidDirection, error);
    case MessageType::kGetCapabilities:
      if (direction == Direction::kPcToVehicle) {
        return true;
      }
      return Fail(Error::kInvalidDirection, error);
    case MessageType::kVehicleStatus:
      if (direction == Direction::kVehicleToPc) {
        return true;
      }
      return Fail(Error::kInvalidDirection, error);
    case MessageType::kTelemetry:
      if (direction == Direction::kVehicleToPc) {
        return true;
      }
      return Fail(Error::kInvalidDirection, error);
    case MessageType::kObjectReport:
    case MessageType::kFault:
      if (direction == Direction::kVehicleToPc) {
        return true;
      }
      return Fail(Error::kInvalidDirection, error);
    default:
      return Fail(Error::kUnsupportedMessage, error);
  }
}

// Validates every bounded observation before typed decoding or transmission.
bool ValidateObjects(const uint8_t* bytes, size_t length, Error& error) {
  if (length < 11 || bytes[10] > 4 || length != 11 + bytes[10] * 21U) {
    return Fail(Error::kInvalidPayloadLength, error);
  }
  if (bytes[0] != 1 || bytes[1] == 0 || bytes[1] == 255 || bytes[8] > 3 ||
      (bytes[9] & ~3U) != 0 ||
      (bytes[8] != 1 && (bytes[10] != 0 || (bytes[9] & 1U) != 0))) {
    return Fail(Error::kInvalidPayload, error);
  }
  for (uint8_t i = 0; i < bytes[10]; ++i) {
    const uint8_t* r = bytes + 11 + i * 21;
    uint32_t id = ReadUnsigned(r, 2);
    int16_t bearing = ReadI16(r + 10);
    int16_t elevation = ReadI16(r + 12);
    uint32_t bw = ReadUnsigned(r + 16, 2);
    uint32_t ew = ReadUnsigned(r + 18, 2);
    if (id == 0 || ReadUnsigned(r + 2, 4) == 0xFFFFFFFFUL ||
        ReadUnsigned(r + 6, 4) == 0xFFFFFFFFUL || bearing < -18000 ||
        bearing > 17999 ||
        (elevation != 32767 && (elevation < -9000 || elevation > 9000)) ||
        (bw != 65535 && bw > 18000) || (ew != 65535 && ew > 9000) ||
        (r[20] & ~15U) != 0 || (r[20] & 2U) != (bytes[9] & 2U)) {
      return Fail(Error::kInvalidPayload, error);
    }
    for (uint8_t j = 0; j < i; ++j) {
      if (id == ReadUnsigned(bytes + 11 + j * 21, 2)) {
        return Fail(Error::kInvalidPayload, error);
      }
    }
  }
  return true;
}

// Checks exact shape before any message-specific byte access.
bool ValidatePayload(MessageType type, const uint8_t* bytes, size_t length,
                     Error& error) {
  size_t expected = 0;
  switch (type) {
    case MessageType::kHello:
      expected = 17;
      break;
    case MessageType::kHelloReply:
      expected = 33;
      break;
    case MessageType::kSessionConfirm:
      expected = 16;
      break;
    case MessageType::kSessionReady:
      expected = 4;
      break;
    case MessageType::kControlTicket:
      expected = 10;
      break;
    case MessageType::kMovementCommand:
      expected = 14;
      break;
    case MessageType::kArm:
      expected = 12;
      break;
    case MessageType::kDisarm:
      expected = 4;
      break;
    case MessageType::kClearFailsafe:
      expected = 12;
      break;
    case MessageType::kEmergencyStop:
      expected = 4;
      break;
    case MessageType::kCommandResult:
      expected = 10;
      break;
    case MessageType::kBuoyancyCommand:
      expected = 9;
      break;
    case MessageType::kCapabilities:
      expected = 23;
      break;
    case MessageType::kVehicleStatus:
      expected = 34;
      break;
    case MessageType::kFault:
      expected = 8;
      break;
    case MessageType::kHeartbeat:
    case MessageType::kGetCapabilities:
      break;
    case MessageType::kObjectReport:
      return ValidateObjects(bytes, length, error);
    case MessageType::kTelemetry:
      if (length == 0 || bytes[0] > kMaxTelemetryRecords ||
          length != 1 + static_cast<size_t>(bytes[0]) * 8) {
        return Fail(Error::kInvalidPayloadLength, error);
      }
      for (uint8_t i = 0; i < bytes[0]; ++i) {
        for (uint8_t j = 0; j < i; ++j) {
          if (bytes[1 + i * 8] == bytes[1 + j * 8]) {
            return Fail(Error::kDuplicateMeasurement, error);
          }
        }
      }
      return true;
    default:
      return Fail(Error::kUnsupportedMessage, error);
  }
  if (length != expected) {
    return Fail(Error::kInvalidPayloadLength, error);
  }
  if (type == MessageType::kMovementCommand) {
    for (size_t offset = 8; offset < 14; offset += 2) {
      int16_t axis = ReadI16(bytes + offset);
      if (axis < -1000 || axis > 1000) {
        return Fail(Error::kInvalidPayload, error);
      }
    }
  }
  if ((type == MessageType::kBuoyancyCommand && bytes[8] > 2) ||
      (type == MessageType::kFault && bytes[3] > 1)) {
    return Fail(Error::kInvalidPayload, error);
  }
  return true;
}

// Enforces stricter sender rules while preserving future reporting values on
// receive.
bool ValidSenderValues(const Packet& packet) {
  const Payload& p = packet.message;
  switch (packet.header.type) {
    case MessageType::kVehicleStatus:
      return !HasUnknownControlValues(p.status);
    case MessageType::kCommandResult:
      return static_cast<uint8_t>(p.command_result.result) <= 11 &&
             (p.command_result.original_message_type == MessageType::kArm ||
              p.command_result.original_message_type == MessageType::kDisarm ||
              p.command_result.original_message_type ==
                  MessageType::kClearFailsafe ||
              p.command_result.original_message_type ==
                  MessageType::kEmergencyStop);
    case MessageType::kCapabilities:
      return (p.capabilities.supported_modes & ~1U) == 0 &&
             (p.capabilities.supported_axes & ~7U) == 0 &&
             (p.capabilities.supported_systems & ~31U) == 0 &&
             (p.capabilities.supported_telemetry & ~255UL) == 0;
    case MessageType::kFault:
      return static_cast<uint16_t>(p.fault.fault_code) >= 1 &&
             static_cast<uint16_t>(p.fault.fault_code) <= 7 &&
             static_cast<uint8_t>(p.fault.severity) <= 3;
    case MessageType::kObjectReport:
      return p.object_report.count <= 4;
    case MessageType::kTelemetry:
      if (p.telemetry.count > kMaxTelemetryRecords) {
        return false;
      }
      for (uint8_t i = 0; i < p.telemetry.count; ++i) {
        const TelemetryRecord& record = p.telemetry.records[i];
        uint8_t id = static_cast<uint8_t>(record.measurement_id);
        if (id < 1 || id > 8 || !HasKnownQualityFlags(record)) {
          return false;
        }
      }
      return true;
    default:
      return true;
  }
}
}  // namespace

uint16_t ComputeCrc(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = static_cast<uint16_t>((crc & 0x8000) != 0 ? (crc << 1) ^ 0x1021
                                                      : crc << 1);
    }
  }
  return crc;
}

bool EncodeFrame(const Frame& frame, uint8_t* destination, size_t capacity,
                 size_t& written, Error& error) {
  written = 0;
  error = Error::kNone;
  if (frame.payload_length > kMaxPayloadSize) {
    return Fail(Error::kFrameTooLarge, error);
  }
  uint8_t decoded[kMaxDecodedSize] = {};
  decoded[0] = kVersion;
  decoded[1] = static_cast<uint8_t>(frame.header.type);
  WriteUnsigned(decoded + 3, frame.header.session.boot_counter, 4);
  WriteUnsigned(decoded + 7, frame.header.session.session_counter, 4);
  WriteUnsigned(decoded + 11, frame.header.sequence, 4);
  decoded[15] = frame.payload_length;
  memcpy(decoded + kHeaderSize, frame.payload, frame.payload_length);
  size_t size = kHeaderSize + frame.payload_length;
  WriteUnsigned(decoded + size, ComputeCrc(decoded, size), 2);
  size += 2;
  // A decoded packet is shorter than 254 bytes, so no COBS 0xFF block is
  // needed.
  uint8_t encoded[kMaxWireSize];
  size_t code_index = 0;
  size_t next = 1;
  uint8_t code = 1;
  for (size_t i = 0; i < size; ++i) {
    if (decoded[i] == 0) {
      encoded[code_index] = code;
      code_index = next++;
      code = 1;
    } else {
      encoded[next++] = decoded[i];
      ++code;
    }
  }
  encoded[code_index] = code;
  encoded[next++] = 0;
  if (capacity < next) {
    return Fail(Error::kDestinationTooSmall, error);
  }
  if (destination == nullptr) {
    return Fail(Error::kInvalidArgument, error);
  }
  memcpy(destination, encoded, next);
  written = next;
  return true;
}

bool DecodeFrame(const uint8_t* wire, size_t length, Frame& output,
                 Error& error) {
  error = Error::kNone;
  if (length > kMaxWireSize) {
    return Fail(Error::kFrameTooLarge, error);
  }
  if (wire == nullptr) {
    return Fail(Error::kInvalidArgument, error);
  }
  if (length < 2 || wire[length - 1] != 0) {
    return Fail(Error::kInvalidDelimiter, error);
  }
  uint8_t decoded[kMaxDecodedSize];
  size_t used = 0;
  size_t position = 0;
  const size_t end = length - 1;
  while (position < end) {
    uint8_t code = wire[position++];
    if (code == 0 || static_cast<size_t>(code - 1) > end - position) {
      return Fail(Error::kInvalidCobs, error);
    }
    for (uint8_t i = 1; i < code; ++i) {
      if (wire[position] == 0) {
        return Fail(Error::kInvalidCobs, error);
      }
      if (used >= kMaxDecodedSize) {
        return Fail(Error::kFrameTooLarge, error);
      }
      decoded[used++] = wire[position++];
    }
    if (code != 0xFF && position < end) {
      if (used >= kMaxDecodedSize) {
        return Fail(Error::kFrameTooLarge, error);
      }
      decoded[used++] = 0;
    }
  }
  if (used < kHeaderSize + 2 || decoded[15] > kMaxPayloadSize ||
      used != kHeaderSize + static_cast<size_t>(decoded[15]) + 2) {
    return Fail(Error::kInvalidFrameLength, error);
  }
  if (ReadUnsigned(decoded + used - 2, 2) != ComputeCrc(decoded, used - 2)) {
    return Fail(Error::kInvalidCrc, error);
  }
  if (decoded[0] != kVersion) {
    return Fail(Error::kUnsupportedVersion, error);
  }
  if (decoded[2] != 0) {
    return Fail(Error::kReservedFlags, error);
  }
  Frame candidate = {};
  candidate.header.type = static_cast<MessageType>(decoded[1]);
  candidate.header.session.boot_counter = ReadUnsigned(decoded + 3, 4);
  candidate.header.session.session_counter = ReadUnsigned(decoded + 7, 4);
  candidate.header.sequence = ReadUnsigned(decoded + 11, 4);
  candidate.payload_length = decoded[15];
  memcpy(candidate.payload, decoded + kHeaderSize, candidate.payload_length);
  output = candidate;
  return true;
}

bool HasKnownQualityFlags(const TelemetryRecord& record) {
  return (record.quality_flags & ~15U) == 0;
}

bool HasUnknownControlValues(const VehicleStatus& s) {
  return static_cast<uint8_t>(s.safety_state) > 4 ||
         static_cast<uint8_t>(s.movement_owner) > 3 ||
         static_cast<uint8_t>(s.control_mode) > 1 ||
         static_cast<uint8_t>(s.buoyancy_action) > 4 ||
         static_cast<uint8_t>(s.buoyancy_action_status) > 4 ||
         static_cast<uint8_t>(s.safety_cause) > 11 ||
         static_cast<uint8_t>(s.estimated_buoyancy) > 3 ||
         static_cast<uint8_t>(s.authorized_normal_target) > 3 ||
         (s.arming_blockers & ~1023UL) != 0 ||
         (s.active_restrictions & ~7U) != 0;
}

bool Encode(const Packet& packet, Direction direction, uint8_t* destination,
            size_t capacity, size_t& written, Error& error) {
  written = 0;
  error = Error::kNone;
  if (!ValidateDirection(packet.header.type, direction, error)) {
    return false;
  }
  if (!ValidSenderValues(packet)) {
    return Fail(Error::kInvalidPayload, error);
  }
  Frame frame = {};
  frame.header = packet.header;
  uint8_t* bytes = frame.payload;
  switch (packet.header.type) {
    case MessageType::kHello: {
      const Hello& value = packet.message.hello;
      memcpy(bytes + 0, value.nonce.bytes, 16);
      WriteUnsigned(bytes + 16, static_cast<uint32_t>(value.requested_version),
                    1);
      frame.payload_length = 17;
      break;
    }
    case MessageType::kHelloReply: {
      const HelloReply& value = packet.message.hello_reply;
      memcpy(bytes + 0, value.nonce.bytes, 16);
      WriteUnsigned(bytes + 16, static_cast<uint32_t>(value.selected_version),
                    1);
      memcpy(bytes + 17, value.vehicle_id.bytes, 16);
      frame.payload_length = 33;
      break;
    }
    case MessageType::kSessionConfirm: {
      const SessionConfirm& value = packet.message.session_confirm;
      memcpy(bytes + 0, value.nonce.bytes, 16);
      frame.payload_length = 16;
      break;
    }
    case MessageType::kSessionReady: {
      const SessionReady& value = packet.message.session_ready;
      WriteUnsigned(bytes + 0, static_cast<uint32_t>(value.control_generation),
                    4);
      frame.payload_length = 4;
      break;
    }
    case MessageType::kControlTicket: {
      const ControlTicket& value = packet.message.control_ticket;
      WriteUnsigned(bytes + 0, static_cast<uint32_t>(value.ticket_id), 4);
      WriteUnsigned(bytes + 4, static_cast<uint32_t>(value.control_generation),
                    4);
      WriteUnsigned(bytes + 8, static_cast<uint32_t>(value.validity_ms), 2);
      frame.payload_length = 10;
      break;
    }
    case MessageType::kMovementCommand: {
      const MovementCommand& value = packet.message.movement;
      WriteUnsigned(bytes + 0, static_cast<uint32_t>(value.ticket_id), 4);
      WriteUnsigned(bytes + 4, static_cast<uint32_t>(value.control_generation),
                    4);
      WriteUnsigned(bytes + 8, static_cast<uint32_t>(value.forward), 2);
      WriteUnsigned(bytes + 10, static_cast<uint32_t>(value.yaw), 2);
      WriteUnsigned(bytes + 12, static_cast<uint32_t>(value.dive), 2);
      frame.payload_length = 14;
      break;
    }
    case MessageType::kArm: {
      const Arm& value = packet.message.arm;
      WriteUnsigned(bytes + 0, static_cast<uint32_t>(value.request_id), 4);
      WriteUnsigned(bytes + 4, static_cast<uint32_t>(value.ticket_id), 4);
      WriteUnsigned(bytes + 8, static_cast<uint32_t>(value.control_generation),
                    4);
      frame.payload_length = 12;
      break;
    }
    case MessageType::kDisarm: {
      const Disarm& value = packet.message.disarm;
      WriteUnsigned(bytes + 0, static_cast<uint32_t>(value.request_id), 4);
      frame.payload_length = 4;
      break;
    }
    case MessageType::kClearFailsafe: {
      const ClearFailsafe& value = packet.message.clear_failsafe;
      WriteUnsigned(bytes + 0, static_cast<uint32_t>(value.request_id), 4);
      WriteUnsigned(bytes + 4, static_cast<uint32_t>(value.ticket_id), 4);
      WriteUnsigned(bytes + 8, static_cast<uint32_t>(value.control_generation),
                    4);
      frame.payload_length = 12;
      break;
    }
    case MessageType::kEmergencyStop: {
      const EmergencyStop& value = packet.message.emergency_stop;
      WriteUnsigned(bytes + 0, static_cast<uint32_t>(value.request_id), 4);
      frame.payload_length = 4;
      break;
    }
    case MessageType::kCommandResult: {
      const CommandResult& value = packet.message.command_result;
      WriteUnsigned(bytes + 0, static_cast<uint32_t>(value.request_id), 4);
      WriteUnsigned(bytes + 4,
                    static_cast<uint32_t>(value.original_message_type), 1);
      WriteUnsigned(bytes + 5, static_cast<uint32_t>(value.result), 1);
      WriteUnsigned(bytes + 6, static_cast<uint32_t>(value.control_generation),
                    4);
      frame.payload_length = 10;
      break;
    }
    case MessageType::kBuoyancyCommand: {
      const BuoyancyCommand& value = packet.message.buoyancy;
      WriteUnsigned(bytes + 0, static_cast<uint32_t>(value.ticket_id), 4);
      WriteUnsigned(bytes + 4, static_cast<uint32_t>(value.control_generation),
                    4);
      WriteUnsigned(bytes + 8, static_cast<uint32_t>(value.target), 1);
      frame.payload_length = 9;
      break;
    }
    case MessageType::kCapabilities: {
      const Capabilities& value = packet.message.capabilities;
      WriteUnsigned(bytes + 0, static_cast<uint32_t>(value.capability_revision),
                    2);
      WriteUnsigned(bytes + 2, static_cast<uint32_t>(value.supported_modes), 2);
      WriteUnsigned(bytes + 4, static_cast<uint32_t>(value.supported_axes), 1);
      WriteUnsigned(bytes + 5, static_cast<uint32_t>(value.supported_systems),
                    2);
      WriteUnsigned(bytes + 7, static_cast<uint32_t>(value.supported_telemetry),
                    4);
      WriteUnsigned(bytes + 11,
                    static_cast<uint32_t>(value.movement_interval_ms), 2);
      WriteUnsigned(bytes + 13, static_cast<uint32_t>(value.command_timeout_ms),
                    2);
      WriteUnsigned(bytes + 15, static_cast<uint32_t>(value.ticket_interval_ms),
                    2);
      WriteUnsigned(bytes + 17, static_cast<uint32_t>(value.ticket_lifetime_ms),
                    2);
      WriteUnsigned(bytes + 19, static_cast<uint32_t>(value.neutral_hold_ms),
                    2);
      WriteUnsigned(bytes + 21, static_cast<uint32_t>(value.neutral_tolerance),
                    2);
      frame.payload_length = 23;
      break;
    }
    case MessageType::kVehicleStatus: {
      const VehicleStatus& value = packet.message.status;
      WriteUnsigned(bytes + 0, static_cast<uint32_t>(value.status_revision), 4);
      WriteUnsigned(bytes + 4, static_cast<uint32_t>(value.vehicle_uptime_ms),
                    4);
      WriteUnsigned(bytes + 8, static_cast<uint32_t>(value.safety_state), 1);
      WriteUnsigned(bytes + 9, static_cast<uint32_t>(value.movement_owner), 1);
      WriteUnsigned(bytes + 10, static_cast<uint32_t>(value.control_mode), 1);
      WriteUnsigned(bytes + 11, static_cast<uint32_t>(value.control_generation),
                    4);
      WriteUnsigned(bytes + 15, static_cast<uint32_t>(value.arming_blockers),
                    4);
      WriteUnsigned(bytes + 19,
                    static_cast<uint32_t>(value.neutral_hold_progress_ms), 2);
      WriteUnsigned(bytes + 21, static_cast<uint32_t>(value.movement_age_ms),
                    2);
      WriteUnsigned(bytes + 23,
                    static_cast<uint32_t>(value.movement_time_remaining_ms), 2);
      WriteUnsigned(bytes + 25, static_cast<uint32_t>(value.buoyancy_action),
                    1);
      WriteUnsigned(bytes + 26,
                    static_cast<uint32_t>(value.buoyancy_action_status), 1);
      WriteUnsigned(bytes + 27, static_cast<uint32_t>(value.safety_cause), 1);
      WriteUnsigned(bytes + 28, static_cast<uint32_t>(value.estimated_buoyancy),
                    1);
      WriteUnsigned(bytes + 29,
                    static_cast<uint32_t>(value.authorized_normal_target), 1);
      WriteUnsigned(
          bytes + 30,
          static_cast<uint32_t>(value.buoyancy_command_time_remaining_ms), 2);
      WriteUnsigned(bytes + 32,
                    static_cast<uint32_t>(value.active_restrictions), 2);
      frame.payload_length = 34;
      break;
    }
    case MessageType::kObjectReport: {
      const ObjectReport& value = packet.message.object_report;
      WriteUnsigned(bytes + 0, value.format_revision, 1);
      WriteUnsigned(bytes + 1, value.sensor_id, 1);
      WriteUnsigned(bytes + 2, value.scan_sequence, 4);
      WriteUnsigned(bytes + 6, value.sample_age_ms, 2);
      WriteUnsigned(bytes + 8, value.sensor_state, 1);
      WriteUnsigned(bytes + 9, value.report_flags, 1);
      WriteUnsigned(bytes + 10, value.count, 1);
      for (uint8_t i = 0; i < value.count; ++i) {
        uint8_t* r = bytes + 11 + i * 21;
        const ObjectObservation& observation = value.observations[i];
        WriteUnsigned(r + 0, static_cast<uint32_t>(observation.observation_id),
                      2);
        WriteUnsigned(r + 2, static_cast<uint32_t>(observation.sensor_range_mm),
                      4);
        WriteUnsigned(r + 6,
                      static_cast<uint32_t>(observation.vehicle_range_mm), 4);
        WriteUnsigned(r + 10, static_cast<uint32_t>(observation.bearing_cdeg),
                      2);
        WriteUnsigned(r + 12, static_cast<uint32_t>(observation.elevation_cdeg),
                      2);
        WriteUnsigned(
            r + 14, static_cast<uint32_t>(observation.range_half_width_mm), 2);
        WriteUnsigned(
            r + 16, static_cast<uint32_t>(observation.bearing_half_width_cdeg),
            2);
        WriteUnsigned(
            r + 18,
            static_cast<uint32_t>(observation.elevation_half_width_cdeg), 2);
        WriteUnsigned(r + 20, static_cast<uint32_t>(observation.quality_flags),
                      1);
      }
      frame.payload_length = 11 + value.count * 21;
      break;
    }
    case MessageType::kFault: {
      const Fault& value = packet.message.fault;
      WriteUnsigned(bytes + 0, static_cast<uint32_t>(value.fault_code), 2);
      WriteUnsigned(bytes + 2, static_cast<uint32_t>(value.severity), 1);
      WriteUnsigned(bytes + 3, static_cast<uint32_t>(value.active), 1);
      WriteUnsigned(bytes + 4, static_cast<uint32_t>(value.occurrence_counter),
                    4);
      frame.payload_length = 8;
      break;
    }
    case MessageType::kHeartbeat:
    case MessageType::kGetCapabilities:
      break;
    case MessageType::kTelemetry:
      bytes[0] = packet.message.telemetry.count;
      for (uint8_t i = 0; i < bytes[0]; ++i) {
        const TelemetryRecord& record = packet.message.telemetry.records[i];
        size_t offset = 1 + i * 8;
        bytes[offset] = static_cast<uint8_t>(record.measurement_id);
        bytes[offset + 1] = record.quality_flags;
        WriteUnsigned(bytes + offset + 2, record.sample_age_ms, 2);
        WriteUnsigned(bytes + offset + 4, static_cast<uint32_t>(record.value),
                      4);
      }
      frame.payload_length = 1 + bytes[0] * 8;
      break;
    default:
      return Fail(Error::kUnsupportedMessage, error);
  }
  if (!ValidatePayload(frame.header.type, bytes, frame.payload_length, error)) {
    return false;
  }
  return EncodeFrame(frame, destination, capacity, written, error);
}

bool Decode(const uint8_t* wire, size_t length, Direction direction,
            Packet& output, Error& error) {
  Frame frame;
  if (!DecodeFrame(wire, length, frame, error) ||
      !ValidateDirection(frame.header.type, direction, error) ||
      !ValidatePayload(frame.header.type, frame.payload, frame.payload_length,
                       error)) {
    return false;
  }
  Packet candidate = {};
  candidate.header = frame.header;
  const uint8_t* bytes = frame.payload;
  switch (frame.header.type) {
    case MessageType::kHello: {
      Hello value = {};
      memcpy(value.nonce.bytes, bytes + 0, 16);
      value.requested_version =
          static_cast<uint8_t>(ReadUnsigned(bytes + 16, 1));
      candidate.message.hello = value;
      break;
    }
    case MessageType::kHelloReply: {
      HelloReply value = {};
      memcpy(value.nonce.bytes, bytes + 0, 16);
      value.selected_version =
          static_cast<uint8_t>(ReadUnsigned(bytes + 16, 1));
      memcpy(value.vehicle_id.bytes, bytes + 17, 16);
      candidate.message.hello_reply = value;
      break;
    }
    case MessageType::kSessionConfirm: {
      SessionConfirm value = {};
      memcpy(value.nonce.bytes, bytes + 0, 16);
      candidate.message.session_confirm = value;
      break;
    }
    case MessageType::kSessionReady: {
      SessionReady value = {};
      value.control_generation =
          static_cast<uint32_t>(ReadUnsigned(bytes + 0, 4));
      candidate.message.session_ready = value;
      break;
    }
    case MessageType::kControlTicket: {
      ControlTicket value = {};
      value.ticket_id = static_cast<uint32_t>(ReadUnsigned(bytes + 0, 4));
      value.control_generation =
          static_cast<uint32_t>(ReadUnsigned(bytes + 4, 4));
      value.validity_ms = static_cast<uint16_t>(ReadUnsigned(bytes + 8, 2));
      candidate.message.control_ticket = value;
      break;
    }
    case MessageType::kMovementCommand: {
      MovementCommand value = {};
      value.ticket_id = static_cast<uint32_t>(ReadUnsigned(bytes + 0, 4));
      value.control_generation =
          static_cast<uint32_t>(ReadUnsigned(bytes + 4, 4));
      value.forward = ReadI16(bytes + 8);
      value.yaw = ReadI16(bytes + 10);
      value.dive = ReadI16(bytes + 12);
      candidate.message.movement = value;
      break;
    }
    case MessageType::kArm: {
      Arm value = {};
      value.request_id = static_cast<uint32_t>(ReadUnsigned(bytes + 0, 4));
      value.ticket_id = static_cast<uint32_t>(ReadUnsigned(bytes + 4, 4));
      value.control_generation =
          static_cast<uint32_t>(ReadUnsigned(bytes + 8, 4));
      candidate.message.arm = value;
      break;
    }
    case MessageType::kDisarm: {
      Disarm value = {};
      value.request_id = static_cast<uint32_t>(ReadUnsigned(bytes + 0, 4));
      candidate.message.disarm = value;
      break;
    }
    case MessageType::kClearFailsafe: {
      ClearFailsafe value = {};
      value.request_id = static_cast<uint32_t>(ReadUnsigned(bytes + 0, 4));
      value.ticket_id = static_cast<uint32_t>(ReadUnsigned(bytes + 4, 4));
      value.control_generation =
          static_cast<uint32_t>(ReadUnsigned(bytes + 8, 4));
      candidate.message.clear_failsafe = value;
      break;
    }
    case MessageType::kEmergencyStop: {
      EmergencyStop value = {};
      value.request_id = static_cast<uint32_t>(ReadUnsigned(bytes + 0, 4));
      candidate.message.emergency_stop = value;
      break;
    }
    case MessageType::kCommandResult: {
      CommandResult value = {};
      value.request_id = static_cast<uint32_t>(ReadUnsigned(bytes + 0, 4));
      value.original_message_type =
          static_cast<MessageType>(ReadUnsigned(bytes + 4, 1));
      value.result = static_cast<CommandResultCode>(ReadUnsigned(bytes + 5, 1));
      value.control_generation =
          static_cast<uint32_t>(ReadUnsigned(bytes + 6, 4));
      candidate.message.command_result = value;
      break;
    }
    case MessageType::kBuoyancyCommand: {
      BuoyancyCommand value = {};
      value.ticket_id = static_cast<uint32_t>(ReadUnsigned(bytes + 0, 4));
      value.control_generation =
          static_cast<uint32_t>(ReadUnsigned(bytes + 4, 4));
      value.target = static_cast<BuoyancyTarget>(ReadUnsigned(bytes + 8, 1));
      candidate.message.buoyancy = value;
      break;
    }
    case MessageType::kCapabilities: {
      Capabilities value = {};
      value.capability_revision =
          static_cast<uint16_t>(ReadUnsigned(bytes + 0, 2));
      value.supported_modes = static_cast<uint16_t>(ReadUnsigned(bytes + 2, 2));
      value.supported_axes = static_cast<uint8_t>(ReadUnsigned(bytes + 4, 1));
      value.supported_systems =
          static_cast<uint16_t>(ReadUnsigned(bytes + 5, 2));
      value.supported_telemetry =
          static_cast<uint32_t>(ReadUnsigned(bytes + 7, 4));
      value.movement_interval_ms =
          static_cast<uint16_t>(ReadUnsigned(bytes + 11, 2));
      value.command_timeout_ms =
          static_cast<uint16_t>(ReadUnsigned(bytes + 13, 2));
      value.ticket_interval_ms =
          static_cast<uint16_t>(ReadUnsigned(bytes + 15, 2));
      value.ticket_lifetime_ms =
          static_cast<uint16_t>(ReadUnsigned(bytes + 17, 2));
      value.neutral_hold_ms =
          static_cast<uint16_t>(ReadUnsigned(bytes + 19, 2));
      value.neutral_tolerance =
          static_cast<uint16_t>(ReadUnsigned(bytes + 21, 2));
      candidate.message.capabilities = value;
      break;
    }
    case MessageType::kVehicleStatus: {
      VehicleStatus value = {};
      value.status_revision = static_cast<uint32_t>(ReadUnsigned(bytes + 0, 4));
      value.vehicle_uptime_ms =
          static_cast<uint32_t>(ReadUnsigned(bytes + 4, 4));
      value.safety_state = static_cast<SafetyState>(ReadUnsigned(bytes + 8, 1));
      value.movement_owner =
          static_cast<MovementOwner>(ReadUnsigned(bytes + 9, 1));
      value.control_mode =
          static_cast<ControlMode>(ReadUnsigned(bytes + 10, 1));
      value.control_generation =
          static_cast<uint32_t>(ReadUnsigned(bytes + 11, 4));
      value.arming_blockers =
          static_cast<uint32_t>(ReadUnsigned(bytes + 15, 4));
      value.neutral_hold_progress_ms =
          static_cast<uint16_t>(ReadUnsigned(bytes + 19, 2));
      value.movement_age_ms =
          static_cast<uint16_t>(ReadUnsigned(bytes + 21, 2));
      value.movement_time_remaining_ms =
          static_cast<uint16_t>(ReadUnsigned(bytes + 23, 2));
      value.buoyancy_action =
          static_cast<BuoyancyAction>(ReadUnsigned(bytes + 25, 1));
      value.buoyancy_action_status =
          static_cast<ActionStatus>(ReadUnsigned(bytes + 26, 1));
      value.safety_cause =
          static_cast<SafetyCause>(ReadUnsigned(bytes + 27, 1));
      value.estimated_buoyancy =
          static_cast<EstimatedBuoyancy>(ReadUnsigned(bytes + 28, 1));
      value.authorized_normal_target =
          static_cast<AuthorizedBuoyancyTarget>(ReadUnsigned(bytes + 29, 1));
      value.buoyancy_command_time_remaining_ms =
          static_cast<uint16_t>(ReadUnsigned(bytes + 30, 2));
      value.active_restrictions =
          static_cast<uint16_t>(ReadUnsigned(bytes + 32, 2));
      candidate.message.status = value;
      break;
    }
    case MessageType::kObjectReport: {
      ObjectReport value = {};
      value.format_revision = static_cast<uint8_t>(ReadUnsigned(bytes + 0, 1));
      value.sensor_id = static_cast<uint8_t>(ReadUnsigned(bytes + 1, 1));
      value.scan_sequence = static_cast<uint32_t>(ReadUnsigned(bytes + 2, 4));
      value.sample_age_ms = static_cast<uint16_t>(ReadUnsigned(bytes + 6, 2));
      value.sensor_state = static_cast<uint8_t>(ReadUnsigned(bytes + 8, 1));
      value.report_flags = static_cast<uint8_t>(ReadUnsigned(bytes + 9, 1));
      value.count = static_cast<uint8_t>(ReadUnsigned(bytes + 10, 1));
      for (uint8_t i = 0; i < value.count; ++i) {
        const uint8_t* r = bytes + 11 + i * 21;
        ObjectObservation& observation = value.observations[i];
        observation.observation_id =
            static_cast<uint16_t>(ReadUnsigned(r + 0, 2));
        observation.sensor_range_mm =
            static_cast<uint32_t>(ReadUnsigned(r + 2, 4));
        observation.vehicle_range_mm =
            static_cast<uint32_t>(ReadUnsigned(r + 6, 4));
        observation.bearing_cdeg = ReadI16(r + 10);
        observation.elevation_cdeg = ReadI16(r + 12);
        observation.range_half_width_mm =
            static_cast<uint16_t>(ReadUnsigned(r + 14, 2));
        observation.bearing_half_width_cdeg =
            static_cast<uint16_t>(ReadUnsigned(r + 16, 2));
        observation.elevation_half_width_cdeg =
            static_cast<uint16_t>(ReadUnsigned(r + 18, 2));
        observation.quality_flags =
            static_cast<uint8_t>(ReadUnsigned(r + 20, 1));
      }
      candidate.message.object_report = value;
      break;
    }
    case MessageType::kFault: {
      Fault value = {};
      value.fault_code = static_cast<FaultCode>(ReadUnsigned(bytes + 0, 2));
      value.severity = static_cast<FaultSeverity>(ReadUnsigned(bytes + 2, 1));
      value.active = bytes[3] != 0;
      value.occurrence_counter =
          static_cast<uint32_t>(ReadUnsigned(bytes + 4, 4));
      candidate.message.fault = value;
      break;
    }
    case MessageType::kHeartbeat:
    case MessageType::kGetCapabilities:
      break;
    case MessageType::kTelemetry: {
      Telemetry value = {};
      for (uint8_t i = 0; i < bytes[0]; ++i) {
        size_t offset = 1 + i * 8;
        uint8_t id = bytes[offset];
        if (id >= 1 && id <= 8) {
          TelemetryRecord& record = value.records[value.count++];
          record.measurement_id = static_cast<MeasurementId>(id);
          record.quality_flags = bytes[offset + 1];
          record.sample_age_ms =
              static_cast<uint16_t>(ReadUnsigned(bytes + offset + 2, 2));
          record.value = ReadI32(bytes + offset + 4);
        }
      }
      candidate.message.telemetry = value;
      break;
    }
    default:
      return Fail(Error::kUnsupportedMessage, error);
  }
  output = candidate;
  error = Error::kNone;
  return true;
}

StreamDecoder::StreamDecoder(Direction direction)
    : direction_(direction), count_(0), discarding_(false) {}

void StreamDecoder::Reset() {
  count_ = 0;
  discarding_ = false;
}

StreamResult StreamDecoder::Push(uint8_t value, Packet& output, Error& error) {
  error = Error::kNone;
  if (value == 0) {
    if (discarding_) {
      Reset();
      error = Error::kFrameTooLarge;
      return StreamResult::kRejected;
    }
    if (count_ == 0) {
      return StreamResult::kNeedMoreData;
    }
    buffer_[count_++] = 0;
    size_t length = count_;
    count_ = 0;
    return Decode(buffer_, length, direction_, output, error)
               ? StreamResult::kPacketReady
               : StreamResult::kRejected;
  }
  if (!discarding_) {
    if (count_ >= kMaxWireSize - 1) {
      count_ = 0;
      discarding_ = true;
    } else {
      buffer_[count_++] = value;
    }
  }
  return StreamResult::kNeedMoreData;
}
}  // namespace bcp
}  // namespace bajols
