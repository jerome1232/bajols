#include <BajolsProtocol.h>

namespace {
namespace bcp = bajols::bcp;

// Retained for debugger inspection; this sketch never initializes actuators.
volatile bool self_test_passed = false;
// Fixed packet storage keeps the compile check independent of heap allocation.
bcp::Packet packet = {};
// Fixed wire storage exercises the public Arduino library API.
uint8_t wire[bcp::kMaxWireSize];
// One stream parser for incoming ground-station syntax.
bcp::StreamDecoder decoder(bcp::Direction::kPcToVehicle);
}  // namespace

// Exercises envelope encoding, typed decoding and the stream parser on the
// Mega.
void setup() {
  packet.header.type = bcp::MessageType::kMovementCommand;
  packet.message.movement.ticket_id = 9;
  packet.message.movement.control_generation = 3;
  packet.message.movement.forward = -1000;
  packet.message.movement.dive = 1000;
  size_t written = 0;
  bcp::Error error;
  bool passed = bcp::Encode(packet, bcp::Direction::kPcToVehicle, wire,
                            sizeof(wire), written, error);
  if (passed) {
    passed =
        bcp::Decode(wire, written, bcp::Direction::kPcToVehicle, packet, error);
    for (size_t i = 0; i < written; ++i) {
      bcp::StreamResult result = decoder.Push(wire[i], packet, error);
      if (i + 1 == written) {
        passed = passed && result == bcp::StreamResult::kPacketReady;
      }
    }
  }
  self_test_passed = passed && packet.message.movement.forward == -1000 &&
                     packet.message.movement.dive == 1000;
}

// No serial, actuator or vehicle-control work is performed by this check.
void loop() {}
