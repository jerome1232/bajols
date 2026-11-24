#include <stdio.h>
#include <stdlib.h>

#include <deque>
#include <vector>

#include "BajolsEndpoint.h"

namespace {
namespace bcp = bajols::bcp;

// Fails with the stream behavior whose invariant was violated.
void Check(bool condition, const char* label) {
  if (!condition) {
    fprintf(stderr, "FAIL endpoint: %s\n", label);
    exit(1);
  }
}

// Deterministic clock, advanced explicitly or by a simulated slow read adapter.
class Clock : public bcp::MillisecondClock {
 public:
  // Virtual wrapping milliseconds, unrelated to wall time.
  uint32_t now = 0;
  // Returns the current virtual instant without advancing it.
  uint32_t Now() override { return now; }
};

// Host-only fake UART with partial writes, blocked TX and arbitrary RX
// fragments.
class Port : public bcp::BytePort {
 public:
  // Bytes waiting in the fake driver, outside the endpoint's fixed storage.
  std::deque<uint8_t> input;
  // Bytes actually accepted by the fake transmit driver.
  std::vector<uint8_t> output;
  // Advertised writable capacity, zero models indefinite backpressure.
  size_t writable = 64;
  // Actual short-write limit; may be less than the advertised capacity.
  size_t write_limit = 64;
  // Number of Read attempts in the current service iteration.
  size_t reads = 0;
  // Accepted byte count in the current service iteration.
  size_t writes = 0;
  // Optional virtual clock progressed by reads to exercise deadlines mid-burst.
  Clock* advancing_clock = nullptr;
  // Time consumed by one fake read when advancing_clock is supplied.
  uint32_t read_ms = 0;
  // Reads one byte immediately or reports no available data.
  int Read() override {
    ++reads;
    if (advancing_clock != nullptr) {
      advancing_clock->now += read_ms;
    }
    if (input.empty()) {
      return -1;
    }
    uint8_t value = input.front();
    input.pop_front();
    return value;
  }
  // Reports fake writable capacity without simulating physical transmission.
  size_t Writable() override { return writable; }
  // Accepts a bounded prefix, modeling a short or zero-length nonblocking
  // write.
  size_t Write(const uint8_t* bytes, size_t length) override {
    Check(length <= writable, "writes never exceed advertised capacity");
    if (length > write_limit) {
      length = write_limit;
    }
    output.insert(output.end(), bytes, bytes + length);
    writes += length;
    return length;
  }
};

// Simulation fixture; these limits and support bits are not hardware
// calibration.
bcp::ControllerProfile Profile() {
  bcp::ControllerProfile profile = {};
  profile.capabilities = {1, 1, 7, 31, 255, 50, 500, 50, 500, 1000, 50};
  profile.intervention_depth_mm = 4500;
  profile.release_depth_mm = 4000;
  profile.depth_stale_ms = 2000;
  profile.pc_configured = true;
  profile.flysky_configured = true;
  for (uint8_t i = 0; i < 7; ++i) {
    profile.fault_severity[i] = bcp::FaultSeverity::kBlocking;
  }
  return profile;
}

// Explicit in-memory startup evidence for a simulated peer, never the Mega
// sketch.
bcp::VerifiedBootState Boot() {
  bcp::VerifiedBootState boot = {};
  boot.vehicle_id.bytes[0] = 42;
  boot.boot_counter = 1;
  boot.identity_verified = true;
  boot.emergency = bcp::StoredEmergency::kCleared;
  boot.emergency_storage_healthy = true;
  return boot;
}

// Encoded PC peer connected through the byte endpoint rather than direct
// callbacks.
class Fixture {
 public:
  // Host-side explicit virtual clock.
  Clock clock;
  // Fake nonblocking serial driver.
  Port port;
  // Independent authorization policy.
  bcp::SessionManager manager;
  // Fixed-buffer transport implementation under test.
  bcp::SerialEndpoint endpoint;
  // Decoder for bytes delivered to the simulated PC, including fragmented
  // frames.
  bcp::StreamDecoder decoder;
  // Host-only decoded response history.
  std::vector<bcp::Packet> packets;
  // Byte cursor in the driver's delivered data.
  size_t decoded = 0;
  // Number of rejected partial frames, expected only during cancellation
  // recovery.
  size_t rejected = 0;
  // Established peer identity after handshake.
  bcp::SessionId session = {};
  // Last delivered ticket for freshly sampled PC intent.
  bcp::ControlTicket ticket = {};
  // PC sender sequence, increasing within the session.
  uint32_t sequence = 1;
  // PC discrete identity, increasing independently of wire fragmentation.
  uint32_t request = 0;

  // Starts with healthy simulated depth and selected PC ownership, without
  // arming.
  Fixture()
      : manager(Profile(), Boot()),
        endpoint(manager),
        decoder(bcp::Direction::kVehicleToPc) {
    manager.SetDepth(2000, true, 0);
    manager.SetOwner(bcp::MovementOwner::kPc, 0);
  }
  // Executes one service step and verifies both byte-work budgets every time.
  void Step() {
    port.reads = 0;
    port.writes = 0;
    endpoint.Step(port, clock);
    Check(port.reads <= bcp::SerialEndpoint::kReceiveBudget, "RX work bounded");
    Check(port.writes <= bcp::SerialEndpoint::kTransmitBudget,
          "TX work bounded");
    Check(endpoint.QueuedFrames() <= bcp::SerialEndpoint::kQueueCapacity,
          "queue bounded");
    while (decoded < port.output.size()) {
      bcp::Packet packet;
      bcp::Error error;
      bcp::StreamResult result =
          decoder.Push(port.output[decoded++], packet, error);
      if (result == bcp::StreamResult::kPacketReady) {
        packets.push_back(packet);
      }
      if (result == bcp::StreamResult::kRejected) {
        ++rejected;
      }
    }
  }
  // Runs enough bounded iterations to consume input and ordinary writable
  // output.
  void Pump() {
    for (size_t i = 0; i < 1000; ++i) {
      Step();
      if (port.input.empty() && (endpoint.QueuedFrames() == 0 ||
                                 port.writable == 0 || port.write_limit == 0)) {
        return;
      }
    }
    Check(false, "finite pump converges");
  }
  // Returns the most recent message of a known type from delivered responses.
  bcp::Packet Last(bcp::MessageType type) const {
    for (size_t i = packets.size(); i != 0; --i) {
      if (packets[i - 1].header.type == type) {
        return packets[i - 1];
      }
    }
    Check(false, "expected endpoint response");
    return bcp::Packet();
  }
  // Independently encodes a PC packet into the fake driver's pending bytes.
  void Queue(const bcp::Packet& packet) {
    uint8_t wire[bcp::kMaxWireSize];
    size_t length;
    bcp::Error error;
    Check(bcp::Encode(packet, bcp::Direction::kPcToVehicle, wire, sizeof(wire),
                      length, error),
          "test packet encodes");
    port.input.insert(port.input.end(), wire, wire + length);
  }
  // Makes the reserved initial HELLO without establishing authority.
  bcp::Packet Hello(uint8_t nonce = 1) const {
    bcp::Packet packet = {};
    packet.header.type = bcp::MessageType::kHello;
    packet.message.hello.requested_version = 1;
    packet.message.hello.nonce.bytes[0] = nonce;
    return packet;
  }
  // Performs the four-message handshake through actual encoded byte streams.
  void Connect(uint8_t nonce = 1) {
    Queue(Hello(nonce));
    Pump();
    session = Last(bcp::MessageType::kHelloReply).header.session;
    bcp::Packet confirm = {};
    confirm.header = {bcp::MessageType::kSessionConfirm, session, 1};
    confirm.message.session_confirm.nonce.bytes[0] = nonce;
    Queue(confirm);
    Pump();
    Check(manager.HasSession(), "encoded serial handshake");
    sequence = 1;
    request = 0;
    ticket = Last(bcp::MessageType::kControlTicket).message.control_ticket;
  }
  // Advances virtual time and delivers any newly scheduled ticket.
  void At(uint32_t now) {
    clock.now = now;
    Pump();
    ticket = Last(bcp::MessageType::kControlTicket).message.control_ticket;
  }
  // Samples and sends one movement intent without renewing its ticket
  // artificially.
  void Move(int16_t forward = 0) {
    bcp::Packet packet = {};
    packet.header = {bcp::MessageType::kMovementCommand, session, ++sequence};
    packet.message.movement = {ticket.ticket_id, ticket.control_generation,
                               forward, 0, 0};
    Queue(packet);
    Pump();
  }
  // Sends a new explicit action; retries may reuse the returned complete
  // packet.
  bcp::Packet Action(bcp::MessageType type) {
    bcp::Packet packet = {};
    packet.header = {type, session, ++sequence};
    ++request;
    if (type == bcp::MessageType::kArm) {
      packet.message.arm = {request, ticket.ticket_id,
                            ticket.control_generation};
    }
    if (type == bcp::MessageType::kDisarm) {
      packet.message.disarm.request_id = request;
    }
    if (type == bcp::MessageType::kEmergencyStop) {
      packet.message.emergency_stop.request_id = request;
    }
    Queue(packet);
    Pump();
    return packet;
  }
  // Qualifies, explicitly arms and obtains a new-generation ticket.
  void Arm() {
    Connect();
    for (uint32_t time = 0; time <= 1000; time += 50) {
      At(time);
      Move();
    }
    Action(bcp::MessageType::kArm);
    Check(
        Last(bcp::MessageType::kCommandResult).message.command_result.result ==
            bcp::CommandResultCode::kAccepted,
        "serial peer arm");
    At(1050);
    Move(600);
    Check(manager.Outputs(clock.now).forward == 600,
          "authorized encoded movement");
  }
};

// Verifies every receive split and short writes never create malformed output.
void TestFragmentation() {
  for (size_t split = 1; split < 37; ++split) {
    Fixture fixture;
    fixture.port.writable = 1;
    fixture.port.write_limit = 1;
    fixture.Queue(fixture.Hello());
    std::deque<uint8_t> rest;
    while (fixture.port.input.size() > split) {
      rest.push_front(fixture.port.input.back());
      fixture.port.input.pop_back();
    }
    fixture.Step();
    if (!rest.empty()) {
      Check(fixture.packets.empty(), "partial HELLO cannot reply");
    }
    fixture.port.input.insert(fixture.port.input.end(), rest.begin(),
                              rest.end());
    fixture.Pump();
    Check(fixture.Last(bcp::MessageType::kHelloReply)
                  .message.hello_reply.nonce.bytes[0] == 1,
          "fragmented HELLO round trip");
    Check(fixture.rejected == 0, "short writes preserve frame boundaries");
  }
  Fixture fixture;
  fixture.port.write_limit = 0;
  fixture.Queue(fixture.Hello());
  fixture.Step();
  Check(fixture.endpoint.QueuedFrames() == 1 && fixture.port.output.empty(),
        "zero write never spins");
  fixture.port.write_limit = 3;
  fixture.Pump();
  Check(fixture.rejected == 0 && !fixture.packets.empty(),
        "partial output eventually completes");
}

// Verifies framing recovery and budgets under continuous malformed/oversized
// RX.
void TestReceiveBounds() {
  Fixture fixture;
  fixture.port.input.insert(fixture.port.input.end(), 10000, 1);
  fixture.port.input.push_back(0);
  fixture.Queue(fixture.Hello());
  fixture.Pump();
  Check(fixture.endpoint.Diagnostics().oversized_frames == 1 &&
            !fixture.packets.empty(),
        "oversize discard and next-frame recovery");
  fixture.port.input.push_back(3);
  fixture.port.input.push_back(0);
  fixture.Pump();
  Check(fixture.endpoint.Diagnostics().malformed_frames == 1,
        "malformed framing diagnosed");
  fixture.port.input.insert(fixture.port.input.end(), 100, 0);
  fixture.Pump();
  Check(fixture.endpoint.Diagnostics().malformed_frames == 1,
        "empty delimiters ignored");
}

// Checks that silence, output blockage and malicious traffic never suspend
// safety.
void TestSafetyUnderLoad() {
  Fixture fixture;
  fixture.Arm();
  fixture.port.writable = 0;
  fixture.clock.now = 1550;
  fixture.Step();
  Check(fixture.manager.Status(1550).safety_state ==
                bcp::SafetyState::kCommandLossFailsafe &&
            fixture.manager.Outputs(1550).forward == 0,
        "TX backpressure cannot delay movement expiry");
  Fixture flooded;
  flooded.Arm();
  flooded.clock.now = 1490;
  flooded.port.advancing_clock = &flooded.clock;
  flooded.port.read_ms = 1;
  flooded.port.input.insert(flooded.port.input.end(), 10000, 1);
  flooded.Step();
  Check(flooded.clock.now == 1554 &&
            flooded.manager.Outputs(flooded.clock.now).forward == 0,
        "RX flood bounded; safety uses fresh clock after burst");
  Fixture stop;
  stop.Arm();
  stop.port.writable = 0;
  for (uint8_t i = 0; i < 12; ++i) {
    stop.Action(bcp::MessageType::kDisarm);
  }
  Check(stop.endpoint.QueuedFrames() == bcp::SerialEndpoint::kQueueCapacity,
        "fixed queue saturation");
  stop.Action(bcp::MessageType::kEmergencyStop);
  Check(stop.manager.Status(stop.clock.now).safety_state ==
                bcp::SafetyState::kEmergencyStopLatched &&
            stop.manager.Outputs(stop.clock.now).forward == 0 &&
            stop.endpoint.Diagnostics().dropped_frames > 0,
        "emergency applies even when reply cannot be queued");
  stop.port.writable = 64;
  stop.Pump();
  Check(
      stop.Last(bcp::MessageType::kVehicleStatus).message.status.safety_state ==
          bcp::SafetyState::kEmergencyStopLatched,
      "authoritative emergency status survives a full queue of cached replies");
}

// Verifies coalescing drops only unsent reports and never reorders wire
// sequences.
void TestQueueOrdering() {
  Fixture fixture;
  fixture.Connect();
  fixture.port.writable = 0;
  for (uint32_t now = 50; now <= 1000; now += 50) {
    fixture.clock.now = now;
    fixture.Step();
  }
  Check(fixture.endpoint.Diagnostics().coalesced_frames > 0,
        "queued reports coalesce under backpressure");
  fixture.Action(bcp::MessageType::kDisarm);
  fixture.port.writable = 64;
  fixture.port.write_limit = 7;
  fixture.Pump();
  Check(fixture.rejected == 0, "coalescing preserves encoded frames");
  uint32_t previous = 0;
  for (size_t i = 0; i < fixture.packets.size(); ++i) {
    const bcp::Packet& packet = fixture.packets[i];
    Check(packet.header.sequence > previous,
          "outgoing assigned sequences retain FIFO order");
    previous = packet.header.sequence;
  }
  Check(fixture.Last(bcp::MessageType::kControlTicket)
                .message.control_ticket.ticket_id > 10,
        "latest queued ticket replaces old tickets");
}

// Coalescing replaces only unsent snapshots from the same session and source.
void TestObjectCoalescing() {
  Fixture fixture;
  bcp::ObjectReport unavailable = {};
  Check(!fixture.endpoint.PublishObjects(unavailable, 0),
        "no object publication before session");
  fixture.Connect();
  Check(!fixture.endpoint.PublishObjects(unavailable, 0),
        "invalid report rejected before queueing");
  fixture.port.writable = 0;
  bcp::ObjectReport report = {};
  report.format_revision = 1;
  report.sensor_id = 1;
  report.scan_sequence = 1;
  report.sensor_state = 1;
  Check(fixture.endpoint.PublishObjects(report, 0), "publish first sensor");
  report.sensor_id = 2;
  Check(fixture.endpoint.PublishObjects(report, 0), "publish second sensor");
  Check(fixture.endpoint.QueuedFrames() == 2,
        "independent object sources retained");
  report.sensor_id = 1;
  report.scan_sequence = 2;
  Check(fixture.endpoint.PublishObjects(report, 0), "replace first sensor");
  Check(fixture.endpoint.QueuedFrames() == 2,
        "same source snapshot replaces queued scan");
  fixture.port.writable = 64;
  fixture.Pump();
  size_t objects = 0;
  for (size_t i = 0; i < fixture.packets.size(); ++i) {
    if (fixture.packets[i].header.type == bcp::MessageType::kObjectReport) {
      ++objects;
      const bcp::ObjectReport& report =
          fixture.packets[i].message.object_report;
      Check(report.sensor_id == 2 || report.scan_sequence == 2,
            "old scan removed");
    }
  }
  Check(objects == 2, "both sensor snapshots transmitted");
}

// Verifies cancel revokes PC control and safely terminates an interrupted TX
// frame.
void TestCancellation() {
  Fixture fixture;
  fixture.Arm();
  fixture.port.writable = 1;
  fixture.port.write_limit = 1;
  fixture.clock.now += 50;
  fixture.Step();
  Check(fixture.endpoint.QueuedFrames() != 0,
        "partial frame exists before reset");
  fixture.endpoint.Reset(fixture.clock.now);
  Check(!fixture.manager.HasSession() && fixture.endpoint.QueuedFrames() == 0 &&
            fixture.manager.Status(fixture.clock.now).safety_state ==
                bcp::SafetyState::kCommandLossFailsafe,
        "disconnect immediately revokes active PC session");
  fixture.port.writable = 64;
  fixture.port.write_limit = 64;
  fixture.Connect(2);
  Check(fixture.manager.Status(fixture.clock.now).safety_state ==
                bcp::SafetyState::kCommandLossFailsafe &&
            fixture.manager.Outputs(fixture.clock.now).forward == 0,
        "reconnect never clears or resumes movement");
  Check(fixture.rejected == 1,
        "cancellation delimiter discards exactly one partial TX frame");
  Fixture partial_rx;
  partial_rx.Queue(partial_rx.Hello());
  partial_rx.port.input
      .pop_back();  // Hold back the delimiter of a partial HELLO.
  partial_rx.Step();
  partial_rx.endpoint.Reset(0);
  partial_rx.port.input.push_back(0);
  partial_rx.Step();
  Check(partial_rx.packets.empty(), "cancellation drops partial RX");
  Fixture local;
  local.manager.SetOwner(bcp::MovementOwner::kFlySky, 0);
  for (uint32_t i = 1; i <= 21; ++i) {
    local.clock.now = (i - 1) * 50;
    local.manager.LocalMovement(bcp::MovementOwner::kFlySky, i, 0, 0, 0,
                                local.clock.now);
  }
  Check(local.manager.LocalAction(bcp::MovementOwner::kFlySky,
                                  bcp::MessageType::kArm,
                                  1000) == bcp::CommandResultCode::kAccepted,
        "local qualification");
  local.manager.LocalMovement(bcp::MovementOwner::kFlySky, 22, 500, 0, 0, 1000);
  local.endpoint.Reset(1000);
  Check(local.manager.Outputs(1000).forward == 500,
        "PC transport cancellation preserves local owner");
}
}  // namespace

// Runs deterministic serial, backpressure, cancellation and scheduling
// scenarios.
int main() {
  TestFragmentation();
  TestReceiveBounds();
  TestSafetyUnderLoad();
  TestQueueOrdering();
  TestObjectCoalescing();
  TestCancellation();
  printf(
      "PASS: bounded endpoint, encoded peer, backpressure, cancellation and "
      "safety scheduling\n");
  printf("Host endpoint storage: %zu bytes\n", sizeof(bcp::SerialEndpoint));
  return 0;
}
