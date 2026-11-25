#include <BajolsSession.h>

namespace {
namespace bcp = bajols::bcp;

// Toolchain fixture only; these depth limits are not hardware calibration.
bcp::ControllerProfile MakeFixtureProfile() {
  bcp::ControllerProfile profile = {};
  profile.capabilities = {1, 1, 7, 15, 255, bcp::kMovementIntervalMs,
                          bcp::kCommandTimeoutMs, bcp::kTicketIntervalMs,
                          bcp::kTicketLifetimeMs, bcp::kNeutralHoldMs,
                          bcp::kNeutralTolerance};
  profile.intervention_depth_mm = 4500;
  profile.release_depth_mm = 4000;
  profile.depth_stale_ms = 2000;
  profile.pc_configured = true;
  for (uint8_t i = 0; i < 7; ++i) {
    profile.fault_severity[i] = bcp::FaultSeverity::kBlocking;
  }
  return profile;
}

// This sketch has no boot-identity journal; arming remains unavailable.
bcp::VerifiedBootState UnverifiedBoot() {
  bcp::VerifiedBootState boot = {};
  boot.emergency = bcp::StoredEmergency::kAmbiguous;
  return boot;
}

// The manager owns fixed caches; no transport, EEPROM or actuator is
// initialized.
bcp::SessionManager manager(MakeFixtureProfile(), UnverifiedBoot());
// Visible only to a debugger; no claims about physical controller execution.
volatile bool self_test_passed = false;

// A bounded sink that verifies response syntax without transmitting anything.
class CheckSink : public bcp::PacketSink {
 public:
  // Checks one response in fixed temporary storage.
  void Send(const bcp::Packet& packet) override {
    uint8_t wire[bcp::kMaxWireSize];
    size_t written;
    bcp::Error error;
    self_test_passed = bcp::Encode(packet, bcp::Direction::kVehicleToPc, wire,
                                   sizeof(wire), written, error);
  }
};
}  // namespace

// Exercises the complete policy API without accepting unverified boot identity.
void setup() {
  CheckSink sink;
  bcp::Packet packet = {};
  packet.header.type = bcp::MessageType::kHello;
  packet.message.hello.requested_version = 1;
  uint8_t wire[bcp::kMaxWireSize];
  size_t written;
  bcp::Error error;
  if (bcp::Encode(packet, bcp::Direction::kPcToVehicle, wire, sizeof(wire),
                  written, error)) {
    manager.Receive(wire, written, 0, sink);
  }
  manager.Poll(1000, sink);
  bcp::ControlOutputs outputs = manager.Outputs(1000);
  self_test_passed = !manager.HasSession() && outputs.forward == 0 &&
                     manager.Status(1000).safety_state ==
                         bcp::SafetyState::kEmergencyStopLatched;
}

// Hardware integration and actuator work deliberately remain outside this
// sketch.
void loop() {}
