#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "BajolsProtocol.h"

namespace {
namespace bcp = bajols::bcp;
using bcp::Direction;
using bcp::Error;
using bcp::Frame;
using bcp::MeasurementId;
using bcp::MessageType;
using bcp::Packet;
// Shared test field expressions also use the reporting enum types.
using bcp::ActionStatus;
using bcp::AuthorizedBuoyancyTarget;
using bcp::BuoyancyAction;
using bcp::BuoyancyTarget;
using bcp::CommandResultCode;
using bcp::ControlMode;
using bcp::EstimatedBuoyancy;
using bcp::FaultCode;
using bcp::FaultSeverity;
using bcp::MovementOwner;
using bcp::SafetyCause;
using bcp::SafetyState;

// Number of authoritative JSON vectors executed.
size_t vector_count = 0;

// Stops on the first failed invariant with its fixture or boundary label.
void Check(bool condition, const char* label) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", label);
    exit(1);
  }
}

// Converts fixture hex into host-owned bytes without using the codec.
std::vector<uint8_t> Hex(const char* hex) {
  std::vector<uint8_t> bytes;
  for (size_t i = 0; hex[i] != '\0'; i += 2) {
    char pair[3] = {hex[i], hex[i + 1], '\0'};
    bytes.push_back(static_cast<uint8_t>(strtoul(pair, nullptr, 16)));
  }
  return bytes;
}

// Maps shared rejection categories to codec errors independently of wire bytes.
Error ExpectedError(const std::string& category) {
  if (category == "reject_direction") {
    return Error::kInvalidDirection;
  }
  if (category == "reject_payload") {
    return Error::kInvalidPayload;
  }
  if (category == "reject_payload_length") {
    return Error::kInvalidPayloadLength;
  }
  if (category == "reject_version") {
    return Error::kUnsupportedVersion;
  }
  if (category == "reject_reserved_flags") {
    return Error::kReservedFlags;
  }
  if (category == "reject_telemetry_duplicate_ids") {
    return Error::kDuplicateMeasurement;
  }
  if (category == "reject_crc") {
    return Error::kInvalidCrc;
  }
  if (category == "reject_frame_length") {
    return Error::kInvalidFrameLength;
  }
  if (category == "reject_cobs") {
    return Error::kInvalidCobs;
  }
  if (category == "discard_through_delimiter") {
    return Error::kFrameTooLarge;
  }
  Check(false, "unrecognized shared vector category");
  return Error::kNone;
}

// Verifies raw framing, typed syntax, atomic failure and outgoing golden bytes.
Packet TestVector(const char* name, const char* wire_hex, Direction direction,
                  const char* category_text, const Packet& expected,
                  const char* payload_hex, const char* crc_hex, uint16_t crc) {
  ++vector_count;
  std::vector<uint8_t> wire = Hex(wire_hex);
  std::string category(category_text);
  Packet actual = {};
  actual.header.sequence = 0xDEADBEEFUL;
  Packet before = actual;
  Error error;
  bool success =
      bcp::Decode(wire.data(), wire.size(), direction, actual, error);
  bool valid =
      category == "wire_valid" || category == "skip_unknown_measurement";
  Check(success == valid, name);
  if (!valid) {
    Check(memcmp(&actual, &before, sizeof(actual)) == 0,
          "failed decode preserves output");
    Check(error == (category == "framing_valid_unknown_message"
                        ? Error::kUnsupportedMessage
                        : ExpectedError(category)),
          name);
  }
  if (category == "wire_valid" || category == "framing_valid_unknown_message") {
    Frame frame;
    Check(bcp::DecodeFrame(wire.data(), wire.size(), frame, error), name);
    std::vector<uint8_t> payload = Hex(payload_hex);
    Check(payload.size() == frame.payload_length, name);
    Check(payload.empty() ||
              memcmp(payload.data(), frame.payload, payload.size()) == 0,
          name);
    std::vector<uint8_t> crc_input = Hex(crc_hex);
    Check(bcp::ComputeCrc(crc_input.data(), crc_input.size()) == crc, name);
    uint8_t encoded[bcp::kMaxWireSize];
    size_t written;
    Check(bcp::EncodeFrame(frame, encoded, sizeof(encoded), written, error),
          name);
    Check(written == wire.size() && memcmp(encoded, wire.data(), written) == 0,
          name);
    if (category == "wire_valid") {
      Check(bcp::Encode(expected, direction, encoded, sizeof(encoded), written,
                        error),
            name);
      Check(
          written == wire.size() && memcmp(encoded, wire.data(), written) == 0,
          name);
      memset(encoded, 0xA5, sizeof(encoded));
      Check(!bcp::Encode(expected, direction, encoded, wire.size() - 1, written,
                         error),
            name);
      Check(written == 0 && error == Error::kDestinationTooSmall, name);
      for (size_t i = 0; i < sizeof(encoded); ++i) {
        Check(encoded[i] == 0xA5, "failed encode preserves destination");
      }
      if (expected.header.type != MessageType::kHeartbeat) {
        Direction opposite = direction == Direction::kPcToVehicle
                                 ? Direction::kVehicleToPc
                                 : Direction::kPcToVehicle;
        Check(!bcp::Decode(wire.data(), wire.size(), opposite, actual, error),
              name);
        Check(error == Error::kInvalidDirection, name);
        // Restore the decoded value for the independently generated field
        // assertions.
        Check(bcp::Decode(wire.data(), wire.size(), direction, actual, error),
              name);
      }
    }
  }
  if (category == "skip_unknown_measurement") {
    Check(actual.message.telemetry.count == 0, "unknown measurement filtered");
  }
  // Byte-at-a-time parsing must make the same decision as complete-frame
  // parsing.
  bcp::StreamDecoder stream(direction);
  bcp::StreamResult result = bcp::StreamResult::kNeedMoreData;
  Packet streamed = {};
  for (size_t i = 0; i < wire.size(); ++i) {
    result = stream.Push(wire[i], streamed, error);
    if (i + 1 < wire.size()) {
      Check(result == bcp::StreamResult::kNeedMoreData, name);
    }
  }
  Check(result == (valid ? bcp::StreamResult::kPacketReady
                         : bcp::StreamResult::kRejected),
        name);
  return actual;
}

#include "shared_vectors.h"

// Verifies receiver forward compatibility without relaxing outgoing validation.
void TestReportingValues() {
  Frame frame = {};
  frame.header.type = MessageType::kVehicleStatus;
  frame.payload_length = 34;
  frame.payload[8] =
      255;  // Future safety state remains reportable, never usable.
  frame.payload[15] = 0x80;  // Unknown blocker bit must also remain visible.
  uint8_t wire[bcp::kMaxWireSize];
  size_t length;
  Error error;
  Packet packet;
  Check(bcp::EncodeFrame(frame, wire, sizeof(wire), length, error),
        "future raw status");
  Check(bcp::Decode(wire, length, Direction::kVehicleToPc, packet, error),
        "future status accepted");
  Check(static_cast<uint8_t>(packet.message.status.safety_state) == 255 &&
            packet.message.status.arming_blockers == 128 &&
            bcp::HasUnknownControlValues(packet.message.status),
        "future status preserved");
  Check(!bcp::Encode(packet, Direction::kVehicleToPc, wire, sizeof(wire),
                     length, error),
        "sender rejects unknown status");
  frame.header.type = MessageType::kTelemetry;
  frame.payload_length = 25;
  memset(frame.payload, 0, sizeof(frame.payload));
  frame.payload[0] = 3;
  frame.payload[1] = 254;
  frame.payload[9] = 1;
  frame.payload[10] = 0x80;
  frame.payload[17] = 8;
  Check(bcp::EncodeFrame(frame, wire, sizeof(wire), length, error),
        "mixed telemetry frame");
  Check(bcp::Decode(wire, length, Direction::kVehicleToPc, packet, error),
        "mixed telemetry accepted");
  Check(packet.message.telemetry.count == 2 &&
            packet.message.telemetry.records[0].measurement_id ==
                MeasurementId::kDepth &&
            packet.message.telemetry.records[1].measurement_id ==
                MeasurementId::kWaterTemperature &&
            packet.message.telemetry.records[0].quality_flags == 0x80 &&
            !bcp::HasKnownQualityFlags(packet.message.telemetry.records[0]),
        "unknown IDs skipped and reserved quality preserved");
  Check(!bcp::Encode(packet, Direction::kVehicleToPc, wire, sizeof(wire),
                     length, error),
        "sender rejects reserved quality");
  frame.payload[17] = 254;
  Check(bcp::EncodeFrame(frame, wire, sizeof(wire), length, error),
        "duplicate unknown frame");
  Check(!bcp::Decode(wire, length, Direction::kVehicleToPc, packet, error) &&
            error == Error::kDuplicateMeasurement,
        "unknown duplicates rejected before filtering");
  frame.header.type = MessageType::kFault;
  frame.payload_length = 8;
  memset(frame.payload, 0, sizeof(frame.payload));
  frame.payload[0] = 255;
  frame.payload[2] = 255;
  frame.payload[3] = 1;
  Check(bcp::EncodeFrame(frame, wire, sizeof(wire), length, error),
        "future fault frame");
  Check(bcp::Decode(wire, length, Direction::kVehicleToPc, packet, error) &&
            static_cast<uint16_t>(packet.message.fault.fault_code) == 255 &&
            static_cast<uint8_t>(packet.message.fault.severity) == 255,
        "future fault values preserved");
  frame.payload[3] = 2;
  Check(bcp::EncodeFrame(frame, wire, sizeof(wire), length, error),
        "invalid fault bool frame");
  Check(!bcp::Decode(wire, length, Direction::kVehicleToPc, packet, error) &&
            error == Error::kInvalidPayload,
        "fault active must be boolean");
}

// Exercises noise, disconnect reset, overrun recovery and signed extrema.
void TestBoundaries() {
  const uint8_t reference[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  Check(bcp::ComputeCrc(reference, sizeof(reference)) == 0x29B1,
        "CRC reference");
  Packet packet = {};
  packet.header.type = MessageType::kHeartbeat;
  uint8_t wire[bcp::kMaxWireSize];
  size_t length;
  Error error;
  Check(bcp::Encode(packet, Direction::kPcToVehicle, wire, sizeof(wire), length,
                    error),
        "heartbeat");
  bcp::StreamDecoder stream(Direction::kPcToVehicle);
  Check(stream.Push(0, packet, error) == bcp::StreamResult::kNeedMoreData,
        "empty delimiter");
  for (size_t i = 0; i < 10000; ++i) {
    Check(stream.Push(1, packet, error) == bcp::StreamResult::kNeedMoreData,
          "bounded overrun");
  }
  Check(stream.Push(0, packet, error) == bcp::StreamResult::kRejected &&
            error == Error::kFrameTooLarge,
        "overrun delimiter");
  for (size_t i = 0; i < length; ++i) {
    bcp::StreamResult result = stream.Push(wire[i], packet, error);
    Check(result == (i + 1 == length ? bcp::StreamResult::kPacketReady
                                     : bcp::StreamResult::kNeedMoreData),
          "stream recovers");
  }
  stream.Push(wire[0], packet, error);
  stream.Reset();
  Check(stream.Push(0, packet, error) == bcp::StreamResult::kNeedMoreData,
        "reset drops partial");
  Check(!bcp::Decode(nullptr, 0, Direction::kPcToVehicle, packet, error),
        "null input");
  Check(!bcp::Encode(packet, Direction::kPcToVehicle, nullptr, sizeof(wire),
                     length, error),
        "null output");
  packet.header.type = MessageType::kTelemetry;
  packet.message.telemetry = {};
  packet.message.telemetry.count = 2;
  packet.message.telemetry.records[0] = {MeasurementId::kDepth, 15, 65535,
                                         (-2147483647 - 1)};
  packet.message.telemetry.records[1] = {MeasurementId::kHeading, 1, 0,
                                         2147483647};
  Check(bcp::Encode(packet, Direction::kVehicleToPc, wire, sizeof(wire), length,
                    error),
        "signed extremes encode");
  Packet decoded;
  Check(bcp::Decode(wire, length, Direction::kVehicleToPc, decoded, error),
        "signed extremes decode");
  Check(decoded.message.telemetry.records[0].value == (-2147483647 - 1) &&
            decoded.message.telemetry.records[1].value == 2147483647,
        "signed extremes preserved");
  packet.message.telemetry.records[1].measurement_id = MeasurementId::kDepth;
  Check(!bcp::Encode(packet, Direction::kVehicleToPc, wire, sizeof(wire),
                     length, error) &&
            error == Error::kDuplicateMeasurement,
        "sender rejects duplicate telemetry");
  packet.message.telemetry.count = 255;
  Check(!bcp::Encode(packet, Direction::kVehicleToPc, wire, sizeof(wire),
                     length, error),
        "sender bounds count");
  // Deterministic malformed-input coverage under ASan/UBSan, independent of the
  // fixtures.
  uint32_t random = 123456789;
  for (size_t iteration = 0; iteration < 25000; ++iteration) {
    uint8_t noise[140];
    size_t size = iteration % sizeof(noise);
    for (size_t i = 0; i < size; ++i) {
      random ^= random << 13;
      random ^= random >> 17;
      random ^= random << 5;
      noise[i] = static_cast<uint8_t>(random);
      stream.Push(noise[i], decoded, error);
    }
    bcp::Decode(noise, size, Direction::kPcToVehicle, decoded, error);
    stream.Push(0, decoded, error);
  }
}
}  // namespace

// Runs all shared wire fixtures and native safety-boundary checks.
int main() {
  RunSharedVectors();
  TestBoundaries();
  TestReportingValues();
  printf("PASS: %zu shared vectors, boundary checks, 25000 malformed inputs\n",
         vector_count);
  printf("Host storage: Packet=%zu Frame=%zu StreamDecoder=%zu bytes\n",
         sizeof(Packet), sizeof(Frame), sizeof(bcp::StreamDecoder));
  return 0;
}
