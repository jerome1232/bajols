#include "BajolsSession.h"

#include <string.h>

namespace bajols {
namespace bcp {
namespace {
// Computes unsigned elapsed milliseconds across a single clock wrap.
uint32_t Age(uint32_t now, uint32_t then) { return now - then; }
// Compares session identities without comparing padding.
bool SameSession(const SessionId& a, const SessionId& b) {
  return a.boot_counter == b.boot_counter &&
         a.session_counter == b.session_counter;
}
// Compares opaque IDs in exact transmitted byte order.
bool SameId(const OpaqueId128& a, const OpaqueId128& b) {
  return memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0;
}
// Bounds a ticking status value independently of its no-value sentinel.
uint16_t Saturate(uint32_t value, uint16_t limit) {
  return static_cast<uint16_t>(value > limit ? limit : value);
}
// Computes a lease's smaller remaining independent lifetime.
uint16_t Remaining(uint32_t now, uint32_t accepted, uint32_t issued) {
  uint32_t a = Age(now, accepted);
  uint32_t b = Age(now, issued);
  if (a >= 500 || b >= 500) {
    return 0;
  }
  return static_cast<uint16_t>(500 - (a > b ? a : b));
}
// Compares only status fields which advance the change indicator.
bool SameStatusShape(const VehicleStatus& a, const VehicleStatus& b) {
  return a.safety_state == b.safety_state &&
         a.movement_owner == b.movement_owner &&
         a.control_mode == b.control_mode &&
         a.control_generation == b.control_generation &&
         a.arming_blockers == b.arming_blockers &&
         a.buoyancy_action == b.buoyancy_action &&
         a.buoyancy_action_status == b.buoyancy_action_status &&
         a.safety_cause == b.safety_cause &&
         a.estimated_buoyancy == b.estimated_buoyancy &&
         a.authorized_normal_target == b.authorized_normal_target &&
         a.active_restrictions == b.active_restrictions;
}
}  // namespace

SessionManager::SessionManager(const ControllerProfile& profile,
                               const VerifiedBootState& boot)
    : profile_(profile),
      boot_(boot),
      pending_(),
      current_(),
      tickets_(),
      requests_(),
      faults_(),
      movement_(),
      buoyancy_(),
      emergency_write_(),
      previous_status_(),
      session_counter_(),
      ticket_counter_(),
      generation_(1),
      highest_request_(),
      movement_sequence_(),
      buoyancy_sequence_(),
      local_movement_sequence_(),
      local_buoyancy_sequence_(),
      revision_(1),
      neutral_since_(),
      release_since_(),
      armed_at_(),
      depth_at_(),
      depth_mm_(),
      ticket_at_(),
      status_at_(),
      heartbeat_at_(),
      ticket_slot_(),
      request_slot_(),
      state_(SafetyState::kDisarmed),
      owner_(MovementOwner::kNone),
      cause_(SafetyCause::kStartup),
      forward_(),
      yaw_(),
      dive_(),
      target_(BuoyancyTarget::kNeutral),
      feedback_action_(BuoyancyAction::kNone),
      last_action_(BuoyancyAction::kNone),
      feedback_status_(ActionStatus::kInactive),
      estimate_(EstimatedBuoyancy::kUnknown),
      service_required_(),
      depth_valid_(),
      neutral_holding_(),
      release_holding_(),
      restricted_(),
      emergency_latched_(),
      command_loss_latched_(),
      fault_latched_(),
      emergency_input_(),
      reset_pending_(),
      accepted_in_generation_(),
      has_previous_status_() {
  service_required_ = !ValidProfile(profile_) || !boot.identity_verified ||
                      boot.boot_counter == 0 ||
                      boot.boot_counter == 0xFFFFFFFFUL ||
                      !boot.emergency_storage_healthy;
  emergency_latched_ = boot.emergency != StoredEmergency::kCleared;
  if (emergency_latched_) {
    state_ = SafetyState::kEmergencyStopLatched;
    cause_ = SafetyCause::kEmergencyStopAsserted;
  }
  RecordFault(FaultCode::kIdentityStorageFailed,
              !boot.identity_verified || boot.boot_counter == 0 ||
                  boot.boot_counter == 0xFFFFFFFFUL);
  RecordFault(FaultCode::kEmergencyLatchStorageFailed,
              !boot.emergency_storage_healthy);
}

bool SessionManager::ValidProfile(const ControllerProfile& p) {
  const Capabilities& c = p.capabilities;
  if ((c.supported_modes & ~1U) != 0 || (c.supported_axes & ~7U) != 0 ||
      (c.supported_systems & ~31U) != 0 ||
      (c.supported_telemetry & ~255UL) != 0 || c.movement_interval_ms != 50 ||
      c.command_timeout_ms != 500 || c.ticket_interval_ms != 50 ||
      c.ticket_lifetime_ms != 500 || c.neutral_hold_ms != 1000 ||
      c.neutral_tolerance != 50 || p.intervention_depth_mm == 0 ||
      p.intervention_depth_mm > 5000 ||
      p.release_depth_mm >= p.intervention_depth_mm || p.depth_stale_ms == 0 ||
      p.depth_stale_ms >= 32768) {
    return false;
  }
  for (uint8_t i = 0; i < 7; ++i) {
    if (static_cast<uint8_t>(p.fault_severity[i]) > 3) {
      return false;
    }
  }
  return true;
}

bool SessionManager::Fresh(const Lease& lease, uint32_t now) {
  return lease.active && Age(now, lease.accepted_at) < 500 &&
         Age(now, lease.ticket_at) < 500;
}

bool SessionManager::Configured(MovementOwner source) const {
  switch (source) {
    case MovementOwner::kPc:
      return profile_.pc_configured;
    case MovementOwner::kFlySky:
      return profile_.flysky_configured;
    case MovementOwner::kAutonomy:
      return profile_.autonomy_configured;
    default:
      return false;
  }
}

bool SessionManager::DepthHealthy(uint32_t now) const {
  return depth_valid_ && Age(now, depth_at_) < profile_.depth_stale_ms;
}

bool SessionManager::BlockingFault() const {
  if (service_required_ || profile_.capabilities.supported_modes != 1 ||
      profile_.capabilities.supported_axes != 7 ||
      (profile_.capabilities.supported_systems & 3U) != 3U) {
    return true;
  }
  for (uint8_t i = 0; i < 7; ++i) {
    if (faults_[i].active) {
      return true;
    }
  }
  return false;
}

bool SessionManager::Neutral() const {
  return forward_ >= -50 && forward_ <= 50 && yaw_ >= -50 && yaw_ <= 50 &&
         dive_ >= -50 && dive_ <= 50;
}

void SessionManager::Changed() { ++revision_; }

void SessionManager::ClearIntent(bool preserve_movement) {
  neutral_holding_ = false;
  release_holding_ = false;
  buoyancy_.active = false;
  accepted_in_generation_ = false;
  if (!preserve_movement) {
    movement_.active = false;
    forward_ = 0;
    yaw_ = 0;
    dive_ = 0;
  }
}

void SessionManager::CounterFailure(bool permanent) {
  service_required_ = service_required_ || permanent;
  if (generation_ < 0xFFFFFFFEUL) {
    ++generation_;
  } else {
    service_required_ = true;
  }
  memset(tickets_, 0, sizeof(tickets_));
  current_.active = false;
  pending_.active = false;
  ClearIntent();
  fault_latched_ = true;
  if (!emergency_latched_) {
    state_ = SafetyState::kFaultFailsafe;
    cause_ = SafetyCause::kOtherBlockingFault;
  }
  Changed();
}

bool SessionManager::ChangeGeneration(bool preserve_movement) {
  ClearIntent(preserve_movement);
  memset(tickets_, 0, sizeof(tickets_));
  if (generation_ >= 0xFFFFFFFEUL) {
    CounterFailure(true);
    return false;
  }
  ++generation_;
  Changed();
  return true;
}

void SessionManager::Enter(SafetyState state, SafetyCause cause) {
  if (state == SafetyState::kCommandLossFailsafe) {
    command_loss_latched_ = true;
  }
  if (state == SafetyState::kFaultFailsafe) {
    fault_latched_ = true;
  }
  if (state == SafetyState::kEmergencyStopLatched) {
    emergency_latched_ = true;
  }
  if (state_ == SafetyState::kEmergencyStopLatched || state_ == state ||
      (state_ == SafetyState::kFaultFailsafe &&
       state == SafetyState::kCommandLossFailsafe)) {
    return;
  }
  state_ = state;
  cause_ = cause;
  ChangeGeneration();
}

void SessionManager::RecordFault(FaultCode code, bool active) {
  uint16_t index = static_cast<uint16_t>(code);
  if (index < 1 || index > 7) {
    return;
  }
  FaultState& fault = faults_[index - 1];
  if (fault.active != active) {
    fault.active = active;
    if (active && fault.occurrences != 0xFFFFFFFFUL) {
      ++fault.occurrences;
    }
    Changed();
  }
}

void SessionManager::Advance(uint32_t now) {
  if (pending_.active && Age(now, pending_.created_at) >= 2000) {
    pending_.active = false;
  }
  bool depth_healthy = DepthHealthy(now);
  RecordFault(FaultCode::kDepthUnavailable, !depth_healthy);
  bool fresh = Fresh(movement_, now);
  bool expired = state_ == SafetyState::kArmed &&
                 ((movement_.active && !fresh) ||
                  (!movement_.active && Age(now, armed_at_) >= 500));
  if (expired) {
    command_loss_latched_ = true;
  }
  if (BlockingFault() && (state_ == SafetyState::kArmed ||
                          state_ == SafetyState::kCommandLossFailsafe)) {
    Enter(SafetyState::kFaultFailsafe, !depth_healthy
                                           ? SafetyCause::kDepthUnavailable
                                           : SafetyCause::kOtherBlockingFault);
  } else if (expired) {
    Enter(SafetyState::kCommandLossFailsafe, SafetyCause::kMovementExpired);
  }
  if (!Fresh(movement_, now)) {
    movement_.active = false;
    neutral_holding_ = false;
  }
  if (!Fresh(buoyancy_, now)) {
    buoyancy_.active = false;
  }
  bool release = restricted_ && state_ == SafetyState::kArmed &&
                 depth_healthy && depth_mm_ < profile_.release_depth_mm &&
                 Fresh(movement_, now) && Fresh(buoyancy_, now) &&
                 target_ == BuoyancyTarget::kNeutral && dive_ <= 0;
  if (!release) {
    release_holding_ = false;
  } else if (!release_holding_) {
    release_holding_ = true;
    release_since_ = now;
  } else if (Age(now, release_since_) >= 1000) {
    restricted_ = false;
    ChangeGeneration(true);
  }
  BuoyancyAction action = EffectiveBuoyancy();
  if (action != last_action_) {
    last_action_ = action;
    feedback_action_ = action;
    feedback_status_ = action == BuoyancyAction::kNone ? ActionStatus::kInactive
                                                       : ActionStatus::kRunning;
  }
}

void SessionManager::RevokePcSession(uint32_t now) {
  Advance(now);
  current_.active = false;
  pending_.active = false;
  memset(tickets_, 0, sizeof(tickets_));
  if (owner_ == MovementOwner::kPc) {
    if (state_ == SafetyState::kArmed) {
      Enter(SafetyState::kCommandLossFailsafe,
            SafetyCause::kActivePcSessionRevoked);
    } else {
      ClearIntent();
    }
  }
  Advance(now);
}

void SessionManager::SetOwner(MovementOwner owner, uint32_t now) {
  Advance(now);
  if (static_cast<uint8_t>(owner) > 3 || owner == owner_) {
    return;
  }
  owner_ = owner;
  if (state_ == SafetyState::kArmed) {
    state_ = SafetyState::kDisarmed;
    cause_ = SafetyCause::kOwnerSelectorChanged;
  }
  ChangeGeneration();
}

void SessionManager::SetDepth(int32_t millimetres, bool valid, uint32_t now) {
  Advance(now);
  depth_valid_ = valid && millimetres >= 0;
  if (depth_valid_) {
    depth_mm_ = millimetres;
    depth_at_ = now;
  }
  if (depth_valid_ && millimetres >= profile_.intervention_depth_mm &&
      !restricted_) {
    restricted_ = true;
    ChangeGeneration(true);
  }
  Advance(now);
}

void SessionManager::SetFault(FaultCode code, bool active, uint32_t now) {
  Advance(now);
  RecordFault(code, active);
  if (active && (code == FaultCode::kIdentityStorageFailed ||
                 code == FaultCode::kEmergencyLatchStorageFailed)) {
    service_required_ = true;
    if (code == FaultCode::kIdentityStorageFailed) {
      current_.active = false;
      pending_.active = false;
    }
  }
  Advance(now);
}

BuoyancyAction SessionManager::EffectiveBuoyancy() const {
  if (emergency_latched_) {
    return BuoyancyAction::kEmergencyEvacuation;
  }
  if (state_ == SafetyState::kCommandLossFailsafe ||
      state_ == SafetyState::kFaultFailsafe) {
    return BuoyancyAction::kSeekNeutral;
  }
  if (state_ != SafetyState::kArmed) {
    return BuoyancyAction::kNone;
  }
  if (!buoyancy_.active) {
    return BuoyancyAction::kSeekNeutral;
  }
  if (target_ == BuoyancyTarget::kPositive) {
    return BuoyancyAction::kSeekPositive;
  }
  if (target_ == BuoyancyTarget::kNegative && !restricted_) {
    return BuoyancyAction::kSeekNegative;
  }
  return BuoyancyAction::kSeekNeutral;
}

bool SessionManager::BeginEmergencyWrite(bool latched) {
  if (emergency_write_.token == 0xFFFFFFFFUL) {
    CounterFailure(true);
    return false;
  }
  ++emergency_write_.token;
  emergency_write_.latched = latched;
  emergency_write_.pending = true;
  return true;
}

void SessionManager::SetEmergencyInput(bool asserted, uint32_t now) {
  Advance(now);
  emergency_input_ = asserted;
  if (asserted) {
    // A reassertion during a pending local clear revokes that transaction.
    bool needs_write = !emergency_latched_ || reset_pending_;
    reset_pending_ = false;
    Enter(SafetyState::kEmergencyStopLatched,
          SafetyCause::kEmergencyStopAsserted);
    if (needs_write) {
      BeginEmergencyWrite(true);
    }
  }
}

void SessionManager::SetBuoyancyFeedback(BuoyancyAction action,
                                         ActionStatus status,
                                         EstimatedBuoyancy estimate,
                                         uint32_t now) {
  Advance(now);
  if (static_cast<uint8_t>(action) > 4 || static_cast<uint8_t>(status) > 4 ||
      static_cast<uint8_t>(estimate) > 3) {
    return;
  }
  feedback_action_ = action;
  feedback_status_ = status;
  estimate_ = estimate;
}

bool SessionManager::RequestLocalReset(uint32_t now) {
  Advance(now);
  bool ended = feedback_action_ == BuoyancyAction::kEmergencyEvacuation &&
               (feedback_status_ == ActionStatus::kCompleted ||
                feedback_status_ == ActionStatus::kFailed ||
                feedback_status_ == ActionStatus::kUnavailable);
  if (!emergency_latched_ || emergency_input_ || !ended || reset_pending_ ||
      service_required_ || emergency_write_.pending) {
    return false;
  }
  reset_pending_ = BeginEmergencyWrite(false);
  return reset_pending_;
}

void SessionManager::CompleteEmergencyWrite(uint32_t token, bool verified,
                                            uint32_t now) {
  Advance(now);
  if (!emergency_write_.pending || token != emergency_write_.token) {
    return;
  }
  emergency_write_.pending = false;
  if (!verified) {
    reset_pending_ = false;
    service_required_ = true;
    RecordFault(FaultCode::kEmergencyLatchStorageFailed, true);
    Advance(now);
    return;
  }
  if (reset_pending_ && !emergency_write_.latched && !emergency_input_) {
    reset_pending_ = false;
    emergency_latched_ = false;
    // A local reset returns D, but any unresolved faults still block arming.
    command_loss_latched_ = false;
    fault_latched_ = false;
    state_ = SafetyState::kDisarmed;
    cause_ = SafetyCause::kLocalEmergencyStopReset;
    ChangeGeneration();
  }
}

EmergencyWrite SessionManager::PendingEmergencyWrite() const {
  return emergency_write_;
}
bool SessionManager::HasSession() const { return current_.active; }
SessionId SessionManager::CurrentSession() const {
  SessionId empty = {};
  return current_.active ? current_.id : empty;
}

CommandResultCode SessionManager::CheckTicket(uint32_t id, uint32_t generation,
                                              uint32_t now,
                                              uint32_t& issued_at) const {
  if (generation != generation_) {
    return CommandResultCode::kWrongControlGeneration;
  }
  for (uint8_t i = 0; i < 16; ++i) {
    if (id != 0 && tickets_[i].id == id &&
        Age(now, tickets_[i].issued_at) < 500) {
      issued_at = tickets_[i].issued_at;
      return CommandResultCode::kAccepted;
    }
  }
  return CommandResultCode::kUnknownOrExpiredTicket;
}

bool SessionManager::AcceptMovement(uint32_t sequence, uint32_t issued_at,
                                    int16_t forward, int16_t yaw, int16_t dive,
                                    uint32_t now) {
  if (sequence == 0 || emergency_latched_ || forward < -1000 ||
      forward > 1000 || yaw < -1000 || yaw > 1000 || dive < -1000 ||
      dive > 1000) {
    return false;
  }
  if (restricted_ && dive > 0) {
    release_holding_ = false;
    return false;
  }
  movement_.active = true;
  movement_.accepted_at = now;
  movement_.ticket_at = issued_at;
  accepted_in_generation_ = true;
  forward_ = forward;
  yaw_ = yaw;
  dive_ = dive;
  if (!Neutral()) {
    neutral_holding_ = false;
  } else if (!neutral_holding_) {
    neutral_holding_ = true;
    neutral_since_ = now;
  }
  return true;
}

bool SessionManager::AcceptBuoyancy(uint32_t sequence, uint32_t issued_at,
                                    BuoyancyTarget target, uint32_t now) {
  if (sequence == 0 || state_ != SafetyState::kArmed ||
      static_cast<uint8_t>(target) > 2 ||
      (profile_.capabilities.supported_systems &
       (2U << static_cast<uint8_t>(target))) == 0) {
    return false;
  }
  if (restricted_ && target == BuoyancyTarget::kNegative) {
    release_holding_ = false;
    return false;
  }
  target_ = target;
  buoyancy_.active = true;
  buoyancy_.accepted_at = now;
  buoyancy_.ticket_at = issued_at;
  return true;
}

bool SessionManager::LocalMovement(MovementOwner source, uint32_t sequence,
                                   int16_t forward, int16_t yaw, int16_t dive,
                                   uint32_t now) {
  Advance(now);
  if (source == MovementOwner::kPc || !Configured(source) || source != owner_) {
    return false;
  }
  uint8_t i = static_cast<uint8_t>(source);
  if (sequence <= local_movement_sequence_[i] ||
      !AcceptMovement(sequence, now, forward, yaw, dive, now)) {
    return false;
  }
  local_movement_sequence_[i] = sequence;
  Advance(now);
  return true;
}

bool SessionManager::LocalBuoyancy(MovementOwner source, uint32_t sequence,
                                   BuoyancyTarget target, uint32_t now) {
  Advance(now);
  if (source == MovementOwner::kPc || !Configured(source) || source != owner_) {
    return false;
  }
  uint8_t i = static_cast<uint8_t>(source);
  if (sequence <= local_buoyancy_sequence_[i] ||
      !AcceptBuoyancy(sequence, now, target, now)) {
    return false;
  }
  local_buoyancy_sequence_[i] = sequence;
  Advance(now);
  return true;
}

CommandResultCode SessionManager::ApplyAction(MessageType type, uint32_t now) {
  if (type == MessageType::kEmergencyStop) {
    bool needs_write = !emergency_latched_ || reset_pending_;
    reset_pending_ = false;
    Enter(SafetyState::kEmergencyStopLatched,
          SafetyCause::kEmergencyStopAsserted);
    if (needs_write) {
      BeginEmergencyWrite(true);
    }
    return CommandResultCode::kAccepted;
  }
  if (type == MessageType::kDisarm) {
    if (state_ == SafetyState::kArmed) {
      state_ = SafetyState::kDisarmed;
      cause_ = SafetyCause::kExplicitDisarm;
      ChangeGeneration();
    } else if (state_ == SafetyState::kDisarmed) {
      ClearIntent();
    }
    return CommandResultCode::kAccepted;
  }
  if (type != MessageType::kArm && type != MessageType::kClearFailsafe) {
    return CommandResultCode::kUnsupportedMessage;
  }
  if (emergency_latched_) {
    return CommandResultCode::kLocalResetRequired;
  }
  if (BlockingFault()) {
    return CommandResultCode::kBlockingFault;
  }
  if ((type == MessageType::kArm && state_ != SafetyState::kDisarmed) ||
      (type == MessageType::kClearFailsafe &&
       state_ != SafetyState::kCommandLossFailsafe &&
       state_ != SafetyState::kFaultFailsafe)) {
    return CommandResultCode::kInvalidSafetyState;
  }
  if (!Fresh(movement_, now) ||
      (type == MessageType::kArm &&
       (!neutral_holding_ || !Neutral() || Age(now, neutral_since_) < 1000))) {
    return CommandResultCode::kNeutralHoldIncomplete;
  }
  if (type == MessageType::kArm) {
    state_ = SafetyState::kArmed;
    cause_ = SafetyCause::kExplicitArm;
    armed_at_ = now;
  } else {
    cause_ = state_ == SafetyState::kFaultFailsafe
                 ? SafetyCause::kFaultFailsafeCleared
                 : SafetyCause::kCommandLossFailsafeCleared;
    state_ = SafetyState::kDisarmed;
    command_loss_latched_ = false;
    fault_latched_ = false;
  }
  return ChangeGeneration() ? CommandResultCode::kAccepted
                            : CommandResultCode::kBlockingFault;
}

CommandResultCode SessionManager::LocalAction(MovementOwner source,
                                              MessageType action,
                                              uint32_t now) {
  Advance(now);
  if (source == MovementOwner::kPc || !Configured(source)) {
    return CommandResultCode::kNotMovementOwner;
  }
  if (action != MessageType::kDisarm && action != MessageType::kEmergencyStop &&
      source != owner_) {
    return CommandResultCode::kNotMovementOwner;
  }
  CommandResultCode result = ApplyAction(action, now);
  Advance(now);
  return result;
}

bool SessionManager::Emit(Packet& packet, Connection& connection,
                          PacketSink& sink) {
  if (connection.outbound >= 0xFFFFFFFEUL) {
    CounterFailure();
    return false;
  }
  packet.header.session = connection.id;
  packet.header.sequence = ++connection.outbound;
  sink.Send(packet);
  return true;
}

void SessionManager::HelloReply(Connection& connection, PacketSink& sink) {
  Packet reply = {};
  reply.header.type = MessageType::kHelloReply;
  reply.message.hello_reply.nonce = connection.nonce;
  reply.message.hello_reply.selected_version = kVersion;
  reply.message.hello_reply.vehicle_id = boot_.vehicle_id;
  Emit(reply, connection, sink);
}

void SessionManager::Discrete(const Packet& packet, uint32_t now,
                              PacketSink& sink) {
  RequestEntry request = {};
  request.type = packet.header.type;
  switch (request.type) {
    case MessageType::kArm:
      request.id = packet.message.arm.request_id;
      request.ticket = packet.message.arm.ticket_id;
      request.generation = packet.message.arm.control_generation;
      break;
    case MessageType::kClearFailsafe:
      request.id = packet.message.clear_failsafe.request_id;
      request.ticket = packet.message.clear_failsafe.ticket_id;
      request.generation = packet.message.clear_failsafe.control_generation;
      break;
    case MessageType::kDisarm:
      request.id = packet.message.disarm.request_id;
      break;
    case MessageType::kEmergencyStop:
      request.id = packet.message.emergency_stop.request_id;
      break;
    default:
      return;
  }
  CommandResult result = {request.id, request.type,
                          CommandResultCode::kAccepted, generation_};
  bool cached = false;
  for (uint8_t i = 0; i < 16; ++i) {
    const RequestEntry& old = requests_[i];
    if (request.id != 0 && old.id == request.id) {
      cached = true;
      if (old.type == request.type && old.ticket == request.ticket &&
          old.generation == request.generation) {
        result = old.result;
      } else {
        result.result = CommandResultCode::kRequestIdReused;
      }
      break;
    }
  }
  if (!cached) {
    if (request.id == 0 || request.id <= highest_request_) {
      result.result = CommandResultCode::kRequestTooOld;
    } else {
      highest_request_ = request.id;
      if (request.id >= 0xFFFFFFFEUL &&
          request.type != MessageType::kEmergencyStop &&
          request.type != MessageType::kDisarm) {
        CounterFailure();
        return;
      }
      uint32_t issued_at = 0;
      if (request.type == MessageType::kArm ||
          request.type == MessageType::kClearFailsafe) {
        if (emergency_latched_) {
          result.result = CommandResultCode::kLocalResetRequired;
        } else if (owner_ != MovementOwner::kPc) {
          result.result = CommandResultCode::kNotMovementOwner;
        } else {
          result.result =
              CheckTicket(request.ticket, request.generation, now, issued_at);
        }
      }
      if (result.result == CommandResultCode::kAccepted) {
        result.result = ApplyAction(request.type, now);
      }
      result.control_generation = generation_;
      request.result = result;
      requests_[request_slot_] = request;
      request_slot_ = (request_slot_ + 1) % 16;
    }
  }
  Packet response = {};
  response.header.type = MessageType::kCommandResult;
  response.message.command_result = result;
  Emit(response, current_, sink);
  if (highest_request_ >= 0xFFFFFFFEUL) {
    CounterFailure();
  }
}

void SessionManager::Process(const Packet& packet, uint32_t now,
                             PacketSink& sink) {
  if (!profile_.pc_configured) {
    return;
  }
  if (packet.header.type == MessageType::kHello) {
    if (!ValidProfile(profile_) || !boot_.identity_verified ||
        boot_.boot_counter == 0 || boot_.boot_counter == 0xFFFFFFFFUL ||
        faults_[4].active) {
      return;
    }
    const Hello& hello = packet.message.hello;
    if (packet.header.session.boot_counter != 0 ||
        packet.header.session.session_counter != 0 ||
        packet.header.sequence != 0 || hello.requested_version != kVersion) {
      return;
    }
    if (current_.active && SameId(hello.nonce, current_.nonce)) {
      HelloReply(current_, sink);
      return;
    }
    if (pending_.active) {
      if (SameId(hello.nonce, pending_.nonce)) {
        HelloReply(pending_, sink);
      }
      return;
    }
    if (state_ == SafetyState::kArmed && owner_ == MovementOwner::kPc) {
      return;
    }
    if (session_counter_ >= 0xFFFFFFFEUL) {
      CounterFailure(true);
      return;
    }
    pending_ = Connection();
    pending_.id = {boot_.boot_counter, ++session_counter_};
    pending_.nonce = hello.nonce;
    pending_.created_at = now;
    pending_.active = true;
    HelloReply(pending_, sink);
    return;
  }
  if (packet.header.type == MessageType::kSessionConfirm) {
    if (!ValidProfile(profile_) || !boot_.identity_verified ||
        boot_.boot_counter == 0 || boot_.boot_counter == 0xFFFFFFFFUL ||
        faults_[4].active) {
      return;
    }
    if (packet.header.sequence == 0) {
      return;
    }
    const OpaqueId128& nonce = packet.message.session_confirm.nonce;
    if (current_.active && SameSession(packet.header.session, current_.id) &&
        SameId(nonce, current_.nonce)) {
      if (packet.header.sequence <= current_.confirmation_sequence) {
        return;
      }
      current_.confirmation_sequence = packet.header.sequence;
    } else {
      if (!pending_.active ||
          !SameSession(packet.header.session, pending_.id) ||
          !SameId(nonce, pending_.nonce) ||
          (state_ == SafetyState::kArmed && owner_ == MovementOwner::kPc)) {
        return;
      }
      current_ = pending_;
      current_.confirmation_sequence = packet.header.sequence;
      pending_.active = false;
      ticket_counter_ = 0;
      highest_request_ = 0;
      movement_sequence_ = 0;
      buoyancy_sequence_ = 0;
      request_slot_ = 0;
      ticket_slot_ = 0;
      memset(tickets_, 0, sizeof(tickets_));
      memset(requests_, 0, sizeof(requests_));
      if (owner_ == MovementOwner::kPc) {
        ClearIntent();
      }
      ticket_at_ = now - 50;
      heartbeat_at_ = now;
    }
    Packet reply = {};
    reply.header.type = MessageType::kSessionReady;
    reply.message.session_ready.control_generation = generation_;
    if (!Emit(reply, current_, sink)) {
      return;
    }
    reply.header.type = MessageType::kCapabilities;
    reply.message.capabilities = profile_.capabilities;
    if (!Emit(reply, current_, sink)) {
      return;
    }
    SendStatus(now, sink);
    return;
  }
  if (!current_.active || !SameSession(packet.header.session, current_.id) ||
      packet.header.sequence == 0) {
    return;
  }
  if (packet.header.sequence >= 0xFFFFFFFEUL) {
    if (packet.header.type == MessageType::kEmergencyStop ||
        packet.header.type == MessageType::kDisarm) {
      Discrete(packet, now, sink);
    }
    CounterFailure();
    return;
  }
  uint32_t issued_at;
  switch (packet.header.type) {
    case MessageType::kMovementCommand: {
      const MovementCommand& m = packet.message.movement;
      if (owner_ == MovementOwner::kPc &&
          packet.header.sequence > movement_sequence_ &&
          CheckTicket(m.ticket_id, m.control_generation, now, issued_at) ==
              CommandResultCode::kAccepted &&
          AcceptMovement(packet.header.sequence, issued_at, m.forward, m.yaw,
                         m.dive, now)) {
        movement_sequence_ = packet.header.sequence;
      }
      break;
    }
    case MessageType::kBuoyancyCommand: {
      const BuoyancyCommand& b = packet.message.buoyancy;
      if (owner_ == MovementOwner::kPc &&
          packet.header.sequence > buoyancy_sequence_ &&
          CheckTicket(b.ticket_id, b.control_generation, now, issued_at) ==
              CommandResultCode::kAccepted &&
          AcceptBuoyancy(packet.header.sequence, issued_at, b.target, now)) {
        buoyancy_sequence_ = packet.header.sequence;
      }
      break;
    }
    case MessageType::kArm:
    case MessageType::kDisarm:
    case MessageType::kClearFailsafe:
    case MessageType::kEmergencyStop:
      Discrete(packet, now, sink);
      break;
    case MessageType::kGetCapabilities: {
      Packet reply = {};
      reply.header.type = MessageType::kCapabilities;
      reply.message.capabilities = profile_.capabilities;
      Emit(reply, current_, sink);
      break;
    }
    default:
      break;  // Diagnostic heartbeat deliberately has no control effect.
  }
}

bool SessionManager::Receive(const uint8_t* wire, size_t length, uint32_t now,
                             PacketSink& sink) {
  Advance(now);
  Packet packet;
  Error error;
  if (!Decode(wire, length, Direction::kPcToVehicle, packet, error)) {
    return false;
  }
  Process(packet, now, sink);
  Advance(now);
  return true;
}

ControlOutputs SessionManager::Outputs(uint32_t now) {
  Advance(now);
  ControlOutputs output = {};
  if (state_ == SafetyState::kArmed && Fresh(movement_, now)) {
    output.forward = forward_;
    output.yaw = yaw_;
    output.dive = restricted_ && dive_ > 0 ? 0 : dive_;
  }
  output.buoyancy = EffectiveBuoyancy();
  if (output.buoyancy == BuoyancyAction::kNone) {
    output.action_status = ActionStatus::kInactive;
  } else if (output.buoyancy == feedback_action_) {
    output.action_status = feedback_status_;
  } else {
    output.action_status = ActionStatus::kRunning;
  }
  bool neutral_unavailable =
      output.buoyancy == BuoyancyAction::kSeekNeutral &&
      (!DepthHealthy(now) || faults_[1].active || faults_[2].active ||
       faults_[5].active ||
       (profile_.capabilities.supported_systems & 2U) == 0);
  if (output.buoyancy == BuoyancyAction::kEmergencyEvacuation &&
      faults_[3].active) {
    output.action_status = ActionStatus::kFailed;
  }
  if (neutral_unavailable ||
      (output.buoyancy == BuoyancyAction::kEmergencyEvacuation &&
       (profile_.capabilities.supported_systems & 1U) == 0)) {
    output.action_status = ActionStatus::kUnavailable;
  }
  return output;
}

VehicleStatus SessionManager::Status(uint32_t now) {
  ControlOutputs output = Outputs(now);
  VehicleStatus status = {};
  status.vehicle_uptime_ms = now;
  status.safety_state = state_;
  status.movement_owner = owner_;
  status.control_mode = profile_.capabilities.supported_modes == 1
                            ? ControlMode::kEffort
                            : ControlMode::kNone;
  status.control_generation = generation_;
  if (owner_ == MovementOwner::kNone || !Configured(owner_)) {
    status.arming_blockers |= 1U;
  }
  if (!Fresh(movement_, now)) {
    status.arming_blockers |= 2U;
  }
  if (movement_.active && !Neutral()) {
    status.arming_blockers |= 4U;
  }
  status.neutral_hold_progress_ms =
      neutral_holding_ && Fresh(movement_, now)
          ? Saturate(Age(now, neutral_since_), 1000)
          : 0;
  if (status.neutral_hold_progress_ms < 1000) {
    status.arming_blockers |= 8U;
  }
  if (command_loss_latched_) {
    status.arming_blockers |= 16U;
  }
  if (emergency_latched_) {
    status.arming_blockers |= 32U;
  }
  if (BlockingFault()) {
    status.arming_blockers |= 64U;
  }
  if (!DepthHealthy(now) || faults_[1].active || faults_[2].active ||
      (profile_.capabilities.supported_systems & 3U) != 3U ||
      service_required_) {
    status.arming_blockers |= 128U;
  }
  if (fault_latched_) {
    status.arming_blockers |= 512U;
  }
  status.movement_age_ms =
      accepted_in_generation_ ? Saturate(Age(now, movement_.accepted_at), 65534)
                              : 65535;
  status.movement_time_remaining_ms =
      movement_.active
          ? Remaining(now, movement_.accepted_at, movement_.ticket_at)
          : 0;
  status.buoyancy_action = output.buoyancy;
  status.buoyancy_action_status = output.action_status;
  status.safety_cause = cause_;
  status.estimated_buoyancy = estimate_;
  status.authorized_normal_target = buoyancy_.active
                                        ? static_cast<AuthorizedBuoyancyTarget>(
                                              1 + static_cast<uint8_t>(target_))
                                        : AuthorizedBuoyancyTarget::kNone;
  status.buoyancy_command_time_remaining_ms =
      buoyancy_.active
          ? Remaining(now, buoyancy_.accepted_at, buoyancy_.ticket_at)
          : 0;
  if (restricted_) {
    status.active_restrictions |= 5U;
  }
  if (!DepthHealthy(now)) {
    status.active_restrictions |= 6U;
  }
  if (!has_previous_status_ || !SameStatusShape(status, previous_status_)) {
    Changed();
    previous_status_ = status;
    has_previous_status_ = true;
  }
  status.status_revision = revision_;
  return status;
}

void SessionManager::SendStatus(uint32_t now, PacketSink& sink) {
  Packet packet = {};
  packet.header.type = MessageType::kVehicleStatus;
  packet.message.status = Status(now);
  Emit(packet, current_, sink);
  status_at_ = now;
}

bool SessionManager::PublishObjects(const ObjectReport& report, uint32_t now,
                                    PacketSink& sink) {
  Advance(now);
  if (!current_.active ||
      (profile_.capabilities.supported_systems & 16U) == 0) {
    return false;
  }
  Packet packet = {};
  packet.header.type = MessageType::kObjectReport;
  packet.message.object_report = report;
  uint8_t wire[kMaxWireSize];
  size_t written;
  Error error;
  if (!Encode(packet, Direction::kVehicleToPc, wire, sizeof(wire), written,
              error)) {
    return false;
  }
  return Emit(packet, current_, sink);
}

bool SessionManager::PublishTelemetry(const Telemetry& telemetry, uint32_t now,
                                      PacketSink& sink) {
  Advance(now);
  if (!current_.active || telemetry.count > kMaxTelemetryRecords) {
    return false;
  }
  for (uint8_t i = 0; i < telemetry.count; ++i) {
    uint8_t id = static_cast<uint8_t>(telemetry.records[i].measurement_id);
    if (id < 1 || id > 8 ||
        (profile_.capabilities.supported_telemetry & (1UL << (id - 1))) == 0) {
      return false;
    }
  }
  Packet packet = {};
  packet.header.type = MessageType::kTelemetry;
  packet.message.telemetry = telemetry;
  uint8_t wire[kMaxWireSize];
  size_t written;
  Error error;
  if (!Encode(packet, Direction::kVehicleToPc, wire, sizeof(wire), written,
              error)) {
    return false;
  }
  return Emit(packet, current_, sink);
}

void SessionManager::Poll(uint32_t now, PacketSink& sink) {
  Advance(now);
  if (!current_.active) {
    return;
  }
  if (Age(now, ticket_at_) >= 50) {
    if (ticket_counter_ >= 0xFFFFFFFEUL) {
      CounterFailure();
      return;
    }
    TicketEntry& entry = tickets_[ticket_slot_];
    entry.id = ++ticket_counter_;
    entry.issued_at = now;
    ticket_slot_ = (ticket_slot_ + 1) % 16;
    ticket_at_ = now;
    Packet packet = {};
    packet.header.type = MessageType::kControlTicket;
    packet.message.control_ticket = {entry.id, generation_, 500};
    if (!Emit(packet, current_, sink)) {
      return;
    }
  }
  VehicleStatus status = Status(now);
  if (Age(now, status_at_) >= 200 ||
      status.status_revision != previous_status_.status_revision) {
    SendStatus(now, sink);
    previous_status_.status_revision = revision_;
  }
  if (!current_.active) {
    return;
  }
  for (uint8_t i = 0; i < 7; ++i) {
    FaultState& fault = faults_[i];
    if (fault.active != fault.reported ||
        (fault.active && Age(now, fault.reported_at) >= 1000)) {
      Packet packet = {};
      packet.header.type = MessageType::kFault;
      packet.message.fault = {static_cast<FaultCode>(i + 1),
                              profile_.fault_severity[i], fault.active,
                              fault.occurrences};
      if (!Emit(packet, current_, sink)) {
        return;
      }
      fault.reported = fault.active;
      fault.reported_at = now;
    }
  }
  if (Age(now, heartbeat_at_) >= 1000) {
    Packet packet = {};
    packet.header.type = MessageType::kHeartbeat;
    Emit(packet, current_, sink);
    heartbeat_at_ = now;
  }
}
}  // namespace bcp
}  // namespace bajols
