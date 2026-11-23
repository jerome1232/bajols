#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "BajolsSession.h"

namespace bajols {
namespace bcp {
// Native-only access to authorization exhaustion boundaries; no production API.
class SessionTestAccess {
 public:
  // Injects a generation which cannot safely advance.
  static void ExhaustGeneration(SessionManager& manager) {
    manager.generation_ = 0xFFFFFFFEUL;
  }
  // Injects an outgoing sequence boundary in a live connection.
  static void ExhaustSequence(SessionManager& manager) {
    manager.current_.outbound = 0xFFFFFFFEUL;
  }
  // Injects a ticket boundary without billions of scheduled transmissions.
  static void ExhaustTicket(SessionManager& manager) {
    manager.ticket_counter_ = 0xFFFFFFFEUL;
  }
  // Injects a session identity boundary before a new handshake.
  static void ExhaustSession(SessionManager& manager) {
    manager.session_counter_ = 0xFFFFFFFEUL;
  }
  // Verifies bounded ticket eviction even when test scheduling fills all slots.
  static void FillTicketRing(SessionManager& manager, uint32_t now) {
    for (uint8_t i = 0; i < 17; ++i) {
      SessionManager::TicketEntry& entry = manager.tickets_[i % 16];
      entry.id = ++manager.ticket_counter_;
      entry.issued_at = now;
    }
  }
  // Reads one independent retained recurrence counter.
  static uint32_t Occurrences(const SessionManager& manager, uint8_t code) {
    return manager.faults_[code - 1].occurrences;
  }
  // Places a fault just before its saturating recurrence boundary.
  static void SaturateFault(SessionManager& manager, uint8_t code) {
    manager.faults_[code - 1].occurrences = 0xFFFFFFFFUL;
  }
};
}  // namespace bcp
}  // namespace bajols

namespace {
namespace bcp = bajols::bcp;

// Fails immediately with a scenario label that identifies the violated
// contract.
void Check(bool condition, const char* label) {
  if (!condition) {
    fprintf(stderr, "FAIL session: %s\n", label);
    exit(1);
  }
}

// Captures replies and checks every outgoing packet with the actual wire codec.
class Sink : public bcp::PacketSink {
 public:
  // Host-only message history; the embedded manager never allocates storage.
  std::vector<bcp::Packet> packets;
  // Encodes each response before retaining its typed representation.
  void Send(const bcp::Packet& packet) override {
    uint8_t wire[bcp::kMaxWireSize];
    size_t written;
    bcp::Error error;
    Check(bcp::Encode(packet, bcp::Direction::kVehicleToPc, wire, sizeof(wire),
                      written, error),
          "all manager responses satisfy wire contract");
    packets.push_back(packet);
  }
  // Returns the latest reply of the requested type, failing if absent.
  bcp::Packet Last(bcp::MessageType type) const {
    for (size_t i = packets.size(); i != 0; --i) {
      if (packets[i - 1].header.type == type) {
        return packets[i - 1];
      }
    }
    Check(false, "expected reply missing");
    return bcp::Packet();
  }
};

// Builds a simulation-only profile, explicitly unrelated to hardware
// calibration.
bcp::ControllerProfile Profile() {
  bcp::ControllerProfile profile = {};
  profile.capabilities = {1, 1, 7, 15, 255, 50, 500, 50, 500, 1000, 50};
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

// Supplies explicit verified startup evidence from an in-memory test fixture.
bcp::VerifiedBootState Boot() {
  bcp::VerifiedBootState boot = {};
  boot.vehicle_id.bytes[0] = 42;
  boot.boot_counter = 1;
  boot.identity_verified = true;
  boot.emergency = bcp::StoredEmergency::kCleared;
  boot.emergency_storage_healthy = true;
  return boot;
}

// Encoded simulated PC with a virtual clock and independently sampled intent.
class Peer {
 public:
  // Deterministic manager under test; no wall clock or physical devices.
  bcp::SessionManager manager;
  // Response capture, including historical results.
  Sink sink;
  // Explicit established connection identity.
  bcp::SessionId session;
  // Last accepted ticket delivered to the PC.
  bcp::ControlTicket ticket;
  // PC sender sequence, never reset within a session.
  uint32_t sequence;
  // Increasing discrete request identity.
  uint32_t request;
  // Wrap-safe virtual onboard time.
  uint32_t now;

  // Starts stopped, injects healthy depth and performs a real encoded
  // handshake.
  explicit Peer(uint32_t start = 0,
                const bcp::ControllerProfile& profile = Profile(),
                const bcp::VerifiedBootState& boot = Boot())
      : manager(profile, boot),
        session(),
        ticket(),
        sequence(1),
        request(0),
        now(start) {
    manager.SetDepth(2000, true, now);
    manager.SetOwner(bcp::MovementOwner::kPc, now);
    Connect(1);
  }
  // Sends one syntax-valid packet through the wire-only authorization boundary.
  void Send(bcp::Packet packet) {
    uint8_t wire[bcp::kMaxWireSize];
    size_t written;
    bcp::Error error;
    Check(bcp::Encode(packet, bcp::Direction::kPcToVehicle, wire, sizeof(wire),
                      written, error),
          "PC packet encodes");
    Check(manager.Receive(wire, written, now, sink), "PC packet decodes");
  }
  // Generates HELLO with an opaque nonce distinct from other test connections.
  bcp::Packet Hello(uint8_t nonce) {
    bcp::Packet hello = {};
    hello.header.type = bcp::MessageType::kHello;
    hello.message.hello.requested_version = 1;
    hello.message.hello.nonce.bytes[0] = nonce;
    return hello;
  }
  // Establishes a new connection without implicitly arming or clearing latches.
  void Connect(uint8_t nonce) {
    Send(Hello(nonce));
    bcp::Packet reply = sink.Last(bcp::MessageType::kHelloReply);
    session = reply.header.session;
    bcp::Packet confirm = {};
    confirm.header = {bcp::MessageType::kSessionConfirm, session, 1};
    confirm.message.session_confirm.nonce.bytes[0] = nonce;
    Send(confirm);
    sequence = 1;
    request = 0;
    Check(manager.HasSession(), "session established");
    GetTicket();
  }
  // Advances time without implicitly delivering commands or tickets.
  void At(uint32_t time) {
    now = time;
    manager.Advance(now);
  }
  // Polls and explicitly delivers the latest ticket in the simulated downlink.
  void GetTicket() {
    manager.Poll(now, sink);
    ticket = sink.Last(bcp::MessageType::kControlTicket).message.control_ticket;
  }
  // Makes a freshly sampled continuous movement packet without sending it.
  bcp::Packet Movement(int16_t forward = 0, int16_t yaw = 0, int16_t dive = 0) {
    bcp::Packet packet = {};
    packet.header = {bcp::MessageType::kMovementCommand, session, ++sequence};
    packet.message.movement = {ticket.ticket_id, ticket.control_generation,
                               forward, yaw, dive};
    return packet;
  }
  // Sends a new independent buoyancy target using the last delivered ticket.
  void Buoyancy(bcp::BuoyancyTarget target) {
    bcp::Packet packet = {};
    packet.header = {bcp::MessageType::kBuoyancyCommand, session, ++sequence};
    packet.message.buoyancy = {ticket.ticket_id, ticket.control_generation,
                               target};
    Send(packet);
  }
  // Creates a new discrete identity, retaining ticket and generation for
  // retries.
  bcp::Packet ActionPacket(bcp::MessageType type) {
    bcp::Packet packet = {};
    packet.header = {type, session, ++sequence};
    ++request;
    switch (type) {
      case bcp::MessageType::kArm:
        packet.message.arm = {request, ticket.ticket_id,
                              ticket.control_generation};
        break;
      case bcp::MessageType::kClearFailsafe:
        packet.message.clear_failsafe = {request, ticket.ticket_id,
                                         ticket.control_generation};
        break;
      case bcp::MessageType::kDisarm:
        packet.message.disarm.request_id = request;
        break;
      case bcp::MessageType::kEmergencyStop:
        packet.message.emergency_stop.request_id = request;
        break;
      default:
        Check(false, "test action type");
    }
    return packet;
  }
  // Applies a new action and returns the wire-visible acceptance/rejection
  // result.
  bcp::CommandResultCode Action(bcp::MessageType type) {
    Send(ActionPacket(type));
    return sink.Last(bcp::MessageType::kCommandResult)
        .message.command_result.result;
  }
  // Samples neutral controls at 20 Hz for exactly the approved one-second hold.
  void Qualify(int16_t forward = 0, int16_t yaw = 0, int16_t dive = 0) {
    uint32_t start = now;
    for (uint32_t elapsed = 0; elapsed <= 1000; elapsed += 50) {
      At(start + elapsed);
      manager.SetDepth(2000, true, now);
      GetTicket();
      Send(Movement(forward, yaw, dive));
    }
  }
  // Explicitly arms after qualification; no movement may survive the
  // transition.
  void Arm() {
    Qualify();
    Check(Action(bcp::MessageType::kArm) == bcp::CommandResultCode::kAccepted,
          "qualified arm");
    Check(manager.Outputs(now).forward == 0, "arm clears old thrust");
    At(now + 50);
    GetTicket();
  }
};

// T01/T02/T08: neutral timing, tolerance and generation separation.
void TestQualification() {
  Peer peer;
  peer.Qualify(50, -50, 50);
  Check(peer.manager.Status(peer.now).neutral_hold_progress_ms == 1000,
        "T02 inclusive tolerance");
  uint32_t generation = peer.ticket.control_generation;
  Check(
      peer.Action(bcp::MessageType::kArm) == bcp::CommandResultCode::kAccepted,
      "T01 one-second hold");
  Check(peer.manager.Status(peer.now).control_generation == generation + 1,
        "T08 arm advances once");
  peer.Send(peer.Movement(900));
  Check(peer.manager.Outputs(peer.now).forward == 0,
        "T08 old ticket cannot move");
  Peer early;
  early.Send(early.Movement());
  for (uint32_t t = 50; t <= 950; t += 50) {
    early.At(t);
    early.GetTicket();
    early.Send(early.Movement());
  }
  early.At(999);
  Check(early.Action(bcp::MessageType::kArm) ==
            bcp::CommandResultCode::kNeutralHoldIncomplete,
        "T01 999 ms incomplete");
  early.At(1000);
  Check(
      early.Action(bcp::MessageType::kArm) == bcp::CommandResultCode::kAccepted,
      "T01 1000 ms eligible");
  Peer excursion;
  excursion.Qualify();
  excursion.Send(excursion.Movement(51));
  Check(excursion.manager.Status(excursion.now).neutral_hold_progress_ms == 0,
        "T02 51 resets");
  Check(excursion.Action(bcp::MessageType::kArm) ==
            bcp::CommandResultCode::kNeutralHoldIncomplete,
        "T02 held nonneutral refuses arm");
}

// T03-T07/T09/T25: exact ticket bounds, one-way loss, replay and timer
// wrapping.
void TestFreshness() {
  Peer peer;
  peer.Arm();
  uint32_t issued = peer.now;
  peer.At(issued + 400);
  bcp::Packet movement = peer.Movement(700);
  peer.Send(movement);
  Check(peer.manager.Status(peer.now).movement_time_remaining_ms == 100,
        "T03 ticket bounds lease");
  peer.At(issued + 499);
  peer.Send(movement);
  Check(peer.manager.Status(peer.now).movement_time_remaining_ms == 1,
        "T07 replay does not renew");
  peer.At(issued + 500);
  Check(peer.manager.Status(peer.now).safety_state ==
            bcp::SafetyState::kCommandLossFailsafe,
        "T04 exact expiry");
  Check(peer.manager.Outputs(peer.now).forward == 0 &&
            peer.manager.Outputs(peer.now).buoyancy ==
                bcp::BuoyancyAction::kSeekNeutral,
        "T05 thrust off neutral attempt");
  Peer heartbeat;
  heartbeat.Arm();
  uint32_t armed = heartbeat.now - 50;
  for (uint32_t t = heartbeat.now; t <= armed + 500; t += 50) {
    heartbeat.At(t);
    heartbeat.GetTicket();
    bcp::Packet packet = {};
    packet.header = {bcp::MessageType::kHeartbeat, heartbeat.session,
                     ++heartbeat.sequence};
    heartbeat.Send(packet);
  }
  Check(heartbeat.manager.Status(heartbeat.now).safety_state ==
            bcp::SafetyState::kCommandLossFailsafe,
        "T09 heartbeat/tickets no post-arm grace");
  Peer one_way;
  one_way.Arm();
  uint32_t origin = one_way.now;
  for (uint32_t t = origin; t <= origin + 500; t += 50) {
    one_way.At(t);
    one_way.manager.Poll(
        t, one_way.sink);  // Deliberately never deliver newer tickets.
    one_way.Send(one_way.Movement(600));
  }
  Check(one_way.manager.Status(one_way.now).safety_state ==
            bcp::SafetyState::kCommandLossFailsafe,
        "T06 one-way loss");
  Peer wrap(0xFFFFFF00UL);
  wrap.At(0x90);
  wrap.Send(wrap.Movement());
  Check(wrap.manager.Status(wrap.now).movement_time_remaining_ms == 100,
        "T25 clock wrap gives 400 ms age");
  bcp::SessionTestAccess::FillTicketRing(wrap.manager, wrap.now);
  wrap.Send(wrap.Movement(20));
  Check(wrap.manager.Status(wrap.now).movement_time_remaining_ms == 100,
        "ticket eviction does not renew old input");
}

// T10/T11: independent renewable streams and neutral fallback.
void TestBuoyancyExpiry() {
  Peer peer;
  peer.Arm();
  peer.Send(peer.Movement(300));
  peer.Buoyancy(bcp::BuoyancyTarget::kNegative);
  uint32_t origin = peer.now;
  for (uint32_t elapsed = 50; elapsed <= 500; elapsed += 50) {
    peer.At(origin + elapsed);
    peer.GetTicket();
    peer.Send(peer.Movement(300));
  }
  Check(
      peer.manager.Status(peer.now).safety_state == bcp::SafetyState::kArmed &&
          peer.manager.Outputs(peer.now).buoyancy ==
              bcp::BuoyancyAction::kSeekNeutral &&
          peer.manager.Status(peer.now).authorized_normal_target ==
              bcp::AuthorizedBuoyancyTarget::kNone,
      "T10 independent buoyancy expiry");
  uint32_t movement_origin = peer.now;
  for (uint32_t elapsed = 50; elapsed <= 500; elapsed += 50) {
    peer.At(movement_origin + elapsed);
    peer.GetTicket();
    peer.Buoyancy(bcp::BuoyancyTarget::kPositive);
  }
  Check(peer.manager.Status(peer.now).safety_state ==
            bcp::SafetyState::kCommandLossFailsafe,
        "T11 buoyancy cannot renew movement");
}

// T12-T16: safety priority, exactly-once cached actions and fixed-cache
// eviction.
void TestRequestsAndPriority() {
  Peer peer;
  peer.Qualify();
  bcp::Packet arm = peer.ActionPacket(bcp::MessageType::kArm);
  peer.Send(arm);
  peer.Action(bcp::MessageType::kDisarm);
  arm.header.sequence = ++peer.sequence;
  peer.Send(arm);
  Check(peer.sink.Last(bcp::MessageType::kCommandResult)
                    .message.command_result.result ==
                bcp::CommandResultCode::kAccepted &&
            peer.manager.Status(peer.now).safety_state ==
                bcp::SafetyState::kDisarmed,
        "T14 historical ARM result cannot rearm");
  arm.message.arm.ticket_id++;
  arm.header.sequence = ++peer.sequence;
  peer.Send(arm);
  Check(peer.sink.Last(bcp::MessageType::kCommandResult)
                .message.command_result.result ==
            bcp::CommandResultCode::kRequestIdReused,
        "T16 same ID different payload");
  for (uint8_t i = 0; i < 17; ++i) {
    peer.Action(bcp::MessageType::kDisarm);
  }
  arm.header.sequence = ++peer.sequence;
  peer.Send(arm);
  Check(peer.sink.Last(bcp::MessageType::kCommandResult)
                .message.command_result.result ==
            bcp::CommandResultCode::kRequestTooOld,
        "T15 evicted ID too old");
  Peer priority;
  priority.Arm();
  priority.Send(priority.Movement(900));
  priority.now += 500;
  priority.manager.SetDepth(2000, false, priority.now);
  priority.Action(bcp::MessageType::kEmergencyStop);
  bcp::VehicleStatus status = priority.manager.Status(priority.now);
  Check(status.safety_state == bcp::SafetyState::kEmergencyStopLatched &&
            (status.arming_blockers & (16U | 32U | 512U)) == (16U | 32U | 512U),
        "T12 retains all three latch causes");
  uint32_t generation = status.control_generation;
  bcp::EmergencyWrite write = priority.manager.PendingEmergencyWrite();
  priority.Action(bcp::MessageType::kEmergencyStop);
  Check(
      priority.manager.Status(priority.now).control_generation == generation &&
          priority.manager.PendingEmergencyWrite().token == write.token,
      "T13 repeated emergency does not restart");
  Check(priority.Action(bcp::MessageType::kClearFailsafe) ==
            bcp::CommandResultCode::kLocalResetRequired,
        "T23 remote clear rejected");
  priority.Action(bcp::MessageType::kDisarm);
  Check(priority.manager.Outputs(priority.now).buoyancy ==
            bcp::BuoyancyAction::kEmergencyEvacuation,
        "disarm preserves emergency procedure");
}

// T21-T24: sensor fault, explicit clear, local reset and ambiguous reboot
// state.
void TestRecoveryAndPersistence() {
  Peer peer;
  peer.Arm();
  peer.Send(peer.Movement(500));
  peer.manager.SetDepth(2000, false, peer.now);
  Check(peer.manager.Status(peer.now).safety_state ==
                bcp::SafetyState::kFaultFailsafe &&
            peer.manager.Outputs(peer.now).action_status ==
                bcp::ActionStatus::kUnavailable,
        "T21 depth fault inhibits thrust/recovery claim");
  peer.manager.SetDepth(2000, true, peer.now);
  Check(peer.manager.Status(peer.now).safety_state ==
            bcp::SafetyState::kFaultFailsafe,
        "sensor recovery never clears latch");
  peer.At(peer.now + 50);
  peer.GetTicket();
  peer.Send(peer.Movement(100));
  Check(
      peer.Action(bcp::MessageType::kClearFailsafe) ==
              bcp::CommandResultCode::kAccepted &&
          peer.manager.Status(peer.now).safety_state ==
              bcp::SafetyState::kDisarmed &&
          peer.manager.Outputs(peer.now).buoyancy == bcp::BuoyancyAction::kNone,
      "T22 clear ends recovery in D");
  peer.Action(bcp::MessageType::kEmergencyStop);
  bcp::EmergencyWrite write = peer.manager.PendingEmergencyWrite();
  Check(write.pending && write.latched &&
            peer.manager.Outputs(peer.now).forward == 0,
        "stop precedes durable acknowledgement");
  Check(!peer.manager.RequestLocalReset(peer.now),
        "running procedure cannot reset");
  peer.manager.CompleteEmergencyWrite(write.token, true, peer.now);
  peer.manager.SetBuoyancyFeedback(bcp::BuoyancyAction::kEmergencyEvacuation,
                                   bcp::ActionStatus::kCompleted,
                                   bcp::EstimatedBuoyancy::kUnknown, peer.now);
  peer.manager.SetEmergencyInput(true, peer.now);
  Check(!peer.manager.RequestLocalReset(peer.now),
        "held physical stop cannot reset");
  peer.manager.SetEmergencyInput(false, peer.now);
  Check(peer.manager.RequestLocalReset(peer.now),
        "T23 local ended procedure reset eligible");
  bcp::EmergencyWrite clear = peer.manager.PendingEmergencyWrite();
  Check(!clear.latched && clear.token > write.token &&
            peer.manager.Status(peer.now).safety_state ==
                bcp::SafetyState::kEmergencyStopLatched,
        "reset waits for durable clear");
  peer.manager.CompleteEmergencyWrite(write.token, true, peer.now);
  Check(peer.manager.Status(peer.now).safety_state ==
            bcp::SafetyState::kEmergencyStopLatched,
        "stale write acknowledgement cannot reset");
  peer.manager.CompleteEmergencyWrite(clear.token, true, peer.now);
  Check(peer.manager.Status(peer.now).safety_state ==
                bcp::SafetyState::kDisarmed &&
            peer.manager.Status(peer.now).neutral_hold_progress_ms == 0,
        "T23 verified local reset requires requalification");
  bcp::VerifiedBootState boot = Boot();
  boot.emergency = bcp::StoredEmergency::kAmbiguous;
  bcp::SessionManager reboot(Profile(), boot);
  Check(
      reboot.Status(0).safety_state == bcp::SafetyState::kEmergencyStopLatched,
      "T24 ambiguous/torn storage remains E");
  boot.emergency = bcp::StoredEmergency::kCleared;
  boot.boot_counter = 2;
  bcp::SessionManager cleared_reboot(Profile(), boot);
  Check(cleared_reboot.Status(0).safety_state == bcp::SafetyState::kDisarmed &&
            !cleared_reboot.HasSession(),
        "verified cleared reboot never resumes");
}

// T17/T30: selector changes and monitoring PC must not disturb local ownership.
void TestLocalOwner() {
  Peer peer;
  peer.manager.SetOwner(bcp::MovementOwner::kFlySky, peer.now);
  for (uint32_t i = 1; i <= 21; ++i) {
    peer.At((i - 1) * 50);
    peer.manager.LocalMovement(bcp::MovementOwner::kFlySky, i, 0, 0, 0,
                               peer.now);
  }
  Check(peer.manager.LocalAction(bcp::MovementOwner::kFlySky,
                                 bcp::MessageType::kArm,
                                 peer.now) == bcp::CommandResultCode::kAccepted,
        "local owner qualified arm");
  peer.manager.LocalMovement(bcp::MovementOwner::kFlySky, 22, 800, 0, 0,
                             peer.now);
  peer.Connect(2);
  peer.Send(peer.Movement(-900));
  Check(peer.manager.Outputs(peer.now).forward == 800,
        "T30 monitoring reconnect and PC input preserve local thrust");
  peer.manager.SetOwner(bcp::MovementOwner::kPc, peer.now);
  Check(peer.manager.Status(peer.now).safety_state ==
                bcp::SafetyState::kDisarmed &&
            peer.manager.Outputs(peer.now).forward == 0,
        "T17 selector change disarms");
  peer.manager.SetOwner(bcp::MovementOwner::kFlySky, peer.now);
  Check(!peer.manager.LocalMovement(bcp::MovementOwner::kFlySky, 22, 800, 0, 0,
                                    peer.now),
        "cached local sample cannot requalify");
}

// T26-T29: bounded pending lifetime, retry replay and replacement isolation.
void TestHandshakes() {
  Peer peer;
  bcp::SessionId old = peer.session;
  peer.Send(peer.Hello(2));
  bcp::Packet reply = peer.sink.Last(bcp::MessageType::kHelloReply);
  Check(peer.manager.CurrentSession().session_counter == old.session_counter,
        "T28 pending retains current session");
  peer.At(1900);
  peer.Send(peer.Hello(2));
  Check(peer.sink.Last(bcp::MessageType::kHelloReply)
                .header.session.session_counter ==
            reply.header.session.session_counter,
        "T26 retry retains pending identity");
  peer.At(2000);
  bcp::Packet confirm = {};
  confirm.header = {bcp::MessageType::kSessionConfirm, reply.header.session, 1};
  confirm.message.session_confirm.nonce.bytes[0] = 2;
  peer.Send(confirm);
  Check(peer.manager.CurrentSession().session_counter == old.session_counter,
        "T26 confirmation expired at 2000");
  peer.Send(peer.Hello(2));
  bcp::Packet replacement = peer.sink.Last(bcp::MessageType::kHelloReply);
  Check(replacement.header.session.session_counter >
            reply.header.session.session_counter,
        "T29 expired identity never recycled");
  peer.Send(confirm);
  Check(peer.manager.CurrentSession().session_counter == old.session_counter,
        "T29 old confirmation cannot complete replacement");
  confirm.header.session = replacement.header.session;
  peer.Send(confirm);
  peer.session = replacement.header.session;
  uint32_t generation = peer.manager.Status(peer.now).control_generation;
  confirm.header.sequence = 2;
  peer.Send(confirm);
  Check(peer.manager.Status(peer.now).control_generation == generation,
        "T27 confirmation retry no generation reset");
  bcp::Packet previous_command = peer.Movement(900);
  previous_command.header.session = old;
  peer.Send(previous_command);
  Check(peer.manager.Status(peer.now).movement_age_ms == 65535,
        "T07 previous session cannot qualify");
}

// T18-T20/T36-T40: whole-command rejection and continuous depth-release hold.
void TestDepthRestriction() {
  Peer peer;
  peer.Arm();
  peer.Send(peer.Movement(400, 100, 0));
  peer.Buoyancy(bcp::BuoyancyTarget::kNegative);
  uint32_t deadline_origin = peer.now;
  uint32_t generation = peer.manager.Status(peer.now).control_generation;
  peer.manager.SetDepth(4500, true, peer.now);
  Check(peer.manager.Status(peer.now).control_generation == generation + 1 &&
            peer.manager.Status(peer.now).authorized_normal_target ==
                bcp::AuthorizedBuoyancyTarget::kNone &&
            peer.manager.Outputs(peer.now).buoyancy ==
                bcp::BuoyancyAction::kSeekNeutral,
        "T18 restriction invalidates negative authorization");
  peer.At(peer.now + 50);
  peer.GetTicket();
  bcp::Packet downward = peer.Movement(900, 900, 500);
  uint16_t age = peer.manager.Status(peer.now).movement_age_ms;
  peer.Send(downward);
  Check(peer.manager.Outputs(peer.now).forward == 400 &&
            peer.manager.Outputs(peer.now).yaw == 100 &&
            peer.manager.Status(peer.now).movement_age_ms == age,
        "T36 whole command rejected");
  // Reusing the rejected sequence with permitted intent proves it was not
  // accepted.
  downward.message.movement.dive = 0;
  downward.message.movement.forward = 500;
  peer.Send(downward);
  Check(peer.manager.Outputs(peer.now).forward == 500,
        "T37 rejected sequence was not consumed");
  peer.Buoyancy(bcp::BuoyancyTarget::kNegative);
  Check(peer.manager.Status(peer.now).authorized_normal_target ==
            bcp::AuthorizedBuoyancyTarget::kNone,
        "T39 negative never queued");
  peer.Buoyancy(bcp::BuoyancyTarget::kPositive);
  Check(peer.manager.Outputs(peer.now).buoyancy ==
            bcp::BuoyancyAction::kSeekPositive,
        "T40 positive eligible without renewing movement");
  peer.manager.SetDepth(3900, true, peer.now);
  uint32_t start = peer.now;
  for (uint32_t elapsed = 50; elapsed <= 1100; elapsed += 50) {
    peer.At(start + elapsed);
    peer.GetTicket();
    peer.manager.SetDepth(3900, true, peer.now);
    peer.Send(peer.Movement());
    peer.Send(peer.Movement(0, 0, 20));
    peer.Buoyancy(bcp::BuoyancyTarget::kNeutral);
  }
  Check(peer.manager.Status(peer.now).active_restrictions == 5,
        "T19 held descent prevents release");
  start = peer.now;
  for (uint32_t elapsed = 50; elapsed <= 1050; elapsed += 50) {
    peer.At(start + elapsed);
    peer.GetTicket();
    peer.manager.SetDepth(3900, true, peer.now);
    peer.Send(peer.Movement());
    peer.Buoyancy(bcp::BuoyancyTarget::kNeutral);
  }
  Check(peer.manager.Status(peer.now).active_restrictions == 0,
        "T20 fresh two-stream one-second release");
  Check(peer.manager.Status(peer.now).authorized_normal_target !=
            bcp::AuthorizedBuoyancyTarget::kNegative,
        "release never resumes negative target");
  (void)deadline_origin;
  Peer rejected;
  rejected.Arm();
  rejected.Send(rejected.Movement(300));
  rejected.manager.SetDepth(4500, true, rejected.now);
  start = rejected.now;
  for (uint32_t elapsed = 50; elapsed <= 500; elapsed += 50) {
    rejected.At(start + elapsed);
    rejected.GetTicket();
    rejected.Send(rejected.Movement(900, 900, 900));
  }
  Check(rejected.manager.Status(rejected.now).safety_state ==
            bcp::SafetyState::kCommandLossFailsafe,
        "T38 downward rejections do not grant grace");
}

// Establishes a matrix precondition using public events and resolved sensor
// faults.
void EnterFixtureState(Peer& peer, bcp::SafetyState state) {
  if (state == bcp::SafetyState::kDisarmed) {
    peer.Qualify();
    return;
  }
  peer.Arm();
  peer.Send(peer.Movement());
  if (state == bcp::SafetyState::kCommandLossFailsafe) {
    peer.At(peer.now + 500);
  }
  if (state == bcp::SafetyState::kFaultFailsafe) {
    peer.manager.SetDepth(2000, false, peer.now);
    peer.manager.SetDepth(2000, true, peer.now);
  }
  if (state == bcp::SafetyState::kEmergencyStopLatched) {
    peer.Action(bcp::MessageType::kEmergencyStop);
  }
  peer.At(peer.now + 50);
  peer.GetTicket();
  peer.Send(peer.Movement());
  Check(peer.manager.Status(peer.now).safety_state == state,
        "matrix precondition");
}

// Verifies stop/source/arm/clear rules independently across every safety state.
void TestStateMatrix() {
  const bcp::SafetyState states[] = {
      bcp::SafetyState::kDisarmed, bcp::SafetyState::kArmed,
      bcp::SafetyState::kCommandLossFailsafe, bcp::SafetyState::kFaultFailsafe,
      bcp::SafetyState::kEmergencyStopLatched};
  for (uint8_t i = 0; i < 5; ++i) {
    bcp::SafetyState state = states[i];
    Peer disarm;
    EnterFixtureState(disarm, state);
    disarm.Action(bcp::MessageType::kDisarm);
    Check(disarm.manager.Status(disarm.now).safety_state ==
              (state == bcp::SafetyState::kArmed ? bcp::SafetyState::kDisarmed
                                                 : state),
          "matrix disarm preserves higher-priority latch");
    Peer selector;
    EnterFixtureState(selector, state);
    selector.manager.SetOwner(bcp::MovementOwner::kFlySky, selector.now);
    Check(selector.manager.Status(selector.now).safety_state ==
              (state == bcp::SafetyState::kArmed ? bcp::SafetyState::kDisarmed
                                                 : state),
          "matrix selector preserves safety latch");
    Peer arm;
    EnterFixtureState(arm, state);
    Check(arm.Action(bcp::MessageType::kArm) ==
              (state == bcp::SafetyState::kDisarmed
                   ? bcp::CommandResultCode::kAccepted
               : state == bcp::SafetyState::kEmergencyStopLatched
                   ? bcp::CommandResultCode::kLocalResetRequired
                   : bcp::CommandResultCode::kInvalidSafetyState),
          "matrix ARM only from qualified D");
    Peer clear;
    EnterFixtureState(clear, state);
    bool recoverable = state == bcp::SafetyState::kCommandLossFailsafe ||
                       state == bcp::SafetyState::kFaultFailsafe;
    Check(clear.Action(bcp::MessageType::kClearFailsafe) ==
              (recoverable ? bcp::CommandResultCode::kAccepted
               : state == bcp::SafetyState::kEmergencyStopLatched
                   ? bcp::CommandResultCode::kLocalResetRequired
                   : bcp::CommandResultCode::kInvalidSafetyState),
          "matrix CLEAR only non-emergency failsafe");
    Peer emergency;
    EnterFixtureState(emergency, state);
    emergency.Action(bcp::MessageType::kEmergencyStop);
    Check(emergency.manager.Status(emergency.now).safety_state ==
                  bcp::SafetyState::kEmergencyStopLatched &&
              emergency.manager.Outputs(emergency.now).forward == 0 &&
              emergency.manager.Outputs(emergency.now).buoyancy ==
                  bcp::BuoyancyAction::kEmergencyEvacuation,
          "matrix emergency dominates every state");
  }
}

// Exercises old-stop replay after rearm, malformed frames and persistence
// faults.
void TestStopReplayAndFailures() {
  Peer peer;
  bcp::Packet stop = peer.ActionPacket(bcp::MessageType::kDisarm);
  peer.Send(stop);
  peer.Arm();
  peer.Send(peer.Movement(800));
  stop.header.sequence = ++peer.sequence;
  peer.Send(stop);
  Check(peer.manager.Outputs(peer.now).forward == 800,
        "cached old stop does not execute after rearm");
  peer.Action(bcp::MessageType::kDisarm);
  Check(peer.manager.Outputs(peer.now).forward == 0, "new stop does execute");
  peer.Action(bcp::MessageType::kEmergencyStop);
  bcp::EmergencyWrite latch = peer.manager.PendingEmergencyWrite();
  peer.manager.CompleteEmergencyWrite(latch.token, true, peer.now);
  peer.manager.SetBuoyancyFeedback(bcp::BuoyancyAction::kEmergencyEvacuation,
                                   bcp::ActionStatus::kCompleted,
                                   bcp::EstimatedBuoyancy::kUnknown, peer.now);
  Check(peer.manager.RequestLocalReset(peer.now),
        "local reset starts clear transaction");
  bcp::EmergencyWrite clear = peer.manager.PendingEmergencyWrite();
  peer.manager.SetEmergencyInput(true, peer.now);
  bcp::EmergencyWrite renewed = peer.manager.PendingEmergencyWrite();
  Check(renewed.latched && renewed.pending && renewed.token > clear.token,
        "new stop supersedes in-flight durable clear");
  peer.manager.CompleteEmergencyWrite(clear.token, true, peer.now);
  Check(peer.manager.Status(peer.now).safety_state ==
            bcp::SafetyState::kEmergencyStopLatched,
        "superseded clear cannot reset new stop");
  peer.manager.CompleteEmergencyWrite(renewed.token, false, peer.now);
  Check((peer.manager.Status(peer.now).arming_blockers & 64U) != 0 &&
            !peer.manager.RequestLocalReset(peer.now),
        "failed journal write requires service");
  peer.Connect(2);
  Check(peer.manager.Status(peer.now).safety_state ==
            bcp::SafetyState::kEmergencyStopLatched,
        "journal fault permits monitoring but never clears latch");
  Peer failed_clear;
  failed_clear.Action(bcp::MessageType::kEmergencyStop);
  latch = failed_clear.manager.PendingEmergencyWrite();
  failed_clear.manager.CompleteEmergencyWrite(latch.token, true,
                                              failed_clear.now);
  failed_clear.manager.SetBuoyancyFeedback(
      bcp::BuoyancyAction::kEmergencyEvacuation, bcp::ActionStatus::kFailed,
      bcp::EstimatedBuoyancy::kUnknown, failed_clear.now);
  Check(failed_clear.manager.RequestLocalReset(failed_clear.now),
        "ended failed procedure may request local reset");
  clear = failed_clear.manager.PendingEmergencyWrite();
  failed_clear.manager.CompleteEmergencyWrite(clear.token, false,
                                              failed_clear.now);
  Check(failed_clear.manager.Status(failed_clear.now).safety_state ==
            bcp::SafetyState::kEmergencyStopLatched,
        "failed durable clear remains latched");
  Peer malformed;
  malformed.Arm();
  malformed.Send(malformed.Movement(700));
  uint32_t origin = malformed.now;
  uint8_t noise[140];
  memset(noise, 0xFF, sizeof(noise));
  for (uint32_t elapsed = 50; elapsed <= 500; elapsed += 50) {
    malformed.now = origin + elapsed;
    Check(!malformed.manager.Receive(noise, sizeof(noise), malformed.now,
                                     malformed.sink),
          "oversized wire frame rejects syntax");
  }
  Check(malformed.manager.Status(malformed.now).safety_state ==
            bcp::SafetyState::kCommandLossFailsafe,
        "malformed traffic never postpones deadline processing");
  bcp::ControllerProfile invalid = Profile();
  invalid.release_depth_mm = invalid.intervention_depth_mm;
  bcp::SessionManager unsafe(invalid, Boot());
  unsafe.SetDepth(1000, true, 0);
  unsafe.SetOwner(bcp::MovementOwner::kFlySky, 0);
  Check(unsafe.LocalAction(bcp::MovementOwner::kFlySky, bcp::MessageType::kArm,
                           0) == bcp::CommandResultCode::kBlockingFault,
        "unsafe profile never arms");
  bcp::VerifiedBootState missing = Boot();
  missing.identity_verified = false;
  bcp::SessionManager no_identity(Profile(), missing);
  Check(!no_identity.HasSession() &&
            (no_identity.Status(0).arming_blockers & 64U) != 0,
        "missing verified boot evidence blocks control");
}

// T35 and implementation prerequisites: occurrence saturation and counter
// safety.
void TestFaultsAndExhaustion() {
  Peer peer;
  uint32_t baseline = bcp::SessionTestAccess::Occurrences(peer.manager, 3);
  peer.manager.SetFault(bcp::FaultCode::kBuoyancyControllerFailed, true,
                        peer.now);
  peer.manager.SetFault(bcp::FaultCode::kBuoyancyControllerFailed, true,
                        peer.now);
  peer.manager.SetFault(bcp::FaultCode::kBuoyancyControllerFailed, false,
                        peer.now);
  peer.manager.SetFault(bcp::FaultCode::kBuoyancyControllerFailed, true,
                        peer.now);
  Check(bcp::SessionTestAccess::Occurrences(peer.manager, 3) == baseline + 2,
        "T35 counts activations only");
  bcp::SessionTestAccess::SaturateFault(peer.manager, 3);
  peer.manager.SetFault(bcp::FaultCode::kBuoyancyControllerFailed, false,
                        peer.now);
  peer.manager.SetFault(bcp::FaultCode::kBuoyancyControllerFailed, true,
                        peer.now);
  Check(bcp::SessionTestAccess::Occurrences(peer.manager, 3) == 0xFFFFFFFFUL,
        "T35 saturates");
  Peer sequence;
  sequence.Arm();
  sequence.Send(sequence.Movement(800));
  bcp::SessionTestAccess::ExhaustSequence(sequence.manager);
  sequence.At(sequence.now + 50);
  sequence.manager.Poll(sequence.now, sequence.sink);
  Check(!sequence.manager.HasSession() &&
            sequence.manager.Outputs(sequence.now).forward == 0 &&
            sequence.manager.Status(sequence.now).safety_state ==
                bcp::SafetyState::kFaultFailsafe,
        "outgoing exhaustion stops before wrap");
  sequence.Connect(2);
  Check(sequence.manager.Status(sequence.now).safety_state ==
            bcp::SafetyState::kFaultFailsafe,
        "new session after exhaustion does not clear failsafe");
  Peer ticket;
  ticket.Arm();
  ticket.Send(ticket.Movement(800));
  bcp::SessionTestAccess::ExhaustTicket(ticket.manager);
  ticket.At(ticket.now + 50);
  ticket.manager.Poll(ticket.now, ticket.sink);
  Check(!ticket.manager.HasSession() &&
            ticket.manager.Outputs(ticket.now).forward == 0,
        "ticket exhaustion stops");
  Peer generation;
  generation.Qualify();
  bcp::SessionTestAccess::ExhaustGeneration(generation.manager);
  generation.manager.SetOwner(bcp::MovementOwner::kFlySky, generation.now);
  Check(generation.manager.Status(generation.now).safety_state ==
                bcp::SafetyState::kFaultFailsafe &&
            generation.manager.Status(generation.now).control_generation ==
                0xFFFFFFFEUL,
        "generation never wraps");
  Peer session;
  bcp::SessionTestAccess::ExhaustSession(session.manager);
  session.Send(session.Hello(2));
  Check(!session.manager.HasSession(),
        "session identity exhaustion refuses reuse");
}
// Missing implementation capabilities remain monitorable but never authorize
// ARM.
void TestMonitorCapabilities() {
  bcp::ControllerProfile profile = Profile();
  profile.capabilities.supported_modes = 0;
  profile.capabilities.supported_axes = 0;
  profile.capabilities.supported_systems = 0;
  profile.capabilities.supported_telemetry = 0;
  Peer peer(0, profile);
  peer.Qualify();
  Check(
      peer.manager.HasSession() &&
          peer.manager.Status(peer.now).control_mode == bcp::ControlMode::kNone,
      "zero-capability monitoring profile establishes a session");
  Check(peer.Action(bcp::MessageType::kArm) ==
            bcp::CommandResultCode::kBlockingFault,
        "even selected owner and qualified input cannot arm unimplemented "
        "control");
}

void TestBenchControl() {
  bcp::ControllerProfile profile = Profile();
  profile.capabilities.supported_systems = 0;
  profile.flysky_configured = false;
  Peer ordinary(0, profile);
  ordinary.Qualify();
  Check(ordinary.Action(bcp::MessageType::kArm) ==
            bcp::CommandResultCode::kBlockingFault,
        "ordinary control still requires ballast capabilities");

  profile.bench_control = true;
  bcp::VerifiedBootState boot = Boot();
  boot.emergency = bcp::StoredEmergency::kLatched;
  Peer bench(0, profile, boot);
  bench.manager.SetEmergencyInput(true, bench.now);
  Check(!bench.manager.RequestLocalReset(bench.now),
        "open bench stop loop prevents reset");
  bench.manager.SetEmergencyInput(false, bench.now);
  Check(bench.manager.RequestLocalReset(bench.now),
        "bench reset can proceed without evacuation hardware");
  const bcp::EmergencyWrite clear = bench.manager.PendingEmergencyWrite();
  Check(clear.pending && !clear.latched, "bench reset requires durable clear");
  Check(bench.manager.Status(bench.now).safety_state ==
            bcp::SafetyState::kEmergencyStopLatched,
        "bench remains latched while the clear is pending");
  bench.manager.CompleteEmergencyWrite(clear.token, true, bench.now);
  bench.At(bench.now + 50);
  bench.GetTicket();
  Check(bench.Action(bcp::MessageType::kArm) ==
            bcp::CommandResultCode::kNeutralHoldIncomplete,
        "bench still requires fresh neutral qualification");
  bench.Arm();
  bench.manager.SetDepth(6000, true, bench.now);
  bench.Send(bench.Movement(800, -250, 300));
  bench.Buoyancy(bcp::BuoyancyTarget::kNegative);
  const bcp::ControlOutputs armed = bench.manager.Outputs(bench.now);
  Check(armed.forward == 800 && armed.yaw == -250 && armed.dive == 300 &&
            armed.buoyancy == bcp::BuoyancyAction::kNone,
        "bench grants three movement axes and no ballast authority");
  bench.At(bench.now + 500);
  const bcp::ControlOutputs expired = bench.manager.Outputs(bench.now);
  Check(expired.forward == 0 && expired.yaw == 0 && expired.dive == 0 &&
            expired.buoyancy == bcp::BuoyancyAction::kNone,
        "expired bench input returns all outputs to neutral");
  bench.manager.SetEmergencyInput(true, bench.now);
  Check(bench.manager.Status(bench.now).safety_state ==
            bcp::SafetyState::kEmergencyStopLatched &&
            bench.manager.PendingEmergencyWrite().latched,
        "bench physical stop is latched and persisted");
}

}  // namespace

// Runs portable stateful scenarios independently of the stateless codec suite.
int main() {
  TestQualification();
  TestFreshness();
  TestBuoyancyExpiry();
  TestRequestsAndPriority();
  TestRecoveryAndPersistence();
  TestLocalOwner();
  TestHandshakes();
  TestDepthRestriction();
  TestFaultsAndExhaustion();
  TestStopReplayAndFailures();
  TestStateMatrix();
  TestMonitorCapabilities();
  TestBenchControl();
  printf(
      "PASS: portable session/authorization scenario groups (T01-T30, "
      "T35-T40)\n");
  printf("Host SessionManager storage: %zu bytes; no embedded allocation\n",
         sizeof(bcp::SessionManager));
  return 0;
}
