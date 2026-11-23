#ifndef BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_SESSION_H_
#define BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_SESSION_H_

#include "BajolsProtocol.h"

namespace bajols {
namespace bcp {

// Storage recovery outcome supplied by a board adapter after verification.
enum class StoredEmergency : uint8_t {
  // Both durable records establish a completed clear.
  kCleared,
  // Emergency stop survived the previous boot.
  kLatched,
  // Torn, corrupt, missing or otherwise ambiguous records; treated as latched.
  kAmbiguous
};

// Trusted startup evidence, not a request to create or advance durable
// identity.
struct VerifiedBootState {
  // Stable provisioned vehicle identity, already checked by the board adapter.
  OpaqueId128 vehicle_id;
  // Boot identity already advanced and read back; zero and UINT32_MAX are
  // refused.
  uint32_t boot_counter;
  // Whether the provisioned identity and new durable boot counter are verified.
  bool identity_verified;
  // Recovered emergency state; ambiguity never means cleared.
  StoredEmergency emergency;
  // Whether the emergency journal passed verification and is writable.
  bool emergency_storage_healthy;
};

// Immutable controller support and calibrated limits supplied before startup.
// There is deliberately no production default profile or actuator calibration.
struct ControllerProfile {
  // Fixed capabilities; timing fields must match the approved BCP v1 values.
  Capabilities capabilities;
  // Calibrated intervention depth in millimetres, positive and at most 5000.
  uint16_t intervention_depth_mm;
  // Release depth, strictly below intervention; release requires depth below
  // it.
  uint16_t release_depth_mm;
  // Maximum accepted depth-sample age, positive and less than 32768 ms.
  uint16_t depth_stale_ms;
  // Explicit reporting severity for each assigned fault code, indexed code - 1.
  FaultSeverity fault_severity[7];
  // Configured PC may monitor, command when selected, and stop regardless of
  // owner.
  bool pc_configured;
  // Configured local sources; adapters must supply fresh samples and action
  // edges.
  bool flysky_configured;
  // Whether the future local autonomy adapter is provisioned.
  bool autonomy_configured;
  // Dry bench only: PC movement with no depth or ballast authority.
  bool bench_control;
};

// Output intent only; neither these values nor codec packets drive actuators.
struct ControlOutputs {
  // Nonzero movement is possible only while armed with an unexpired lease.
  int16_t forward;
  // Starboard-positive turning intent under the same movement lease.
  int16_t yaw;
  // Down-positive intent after depth inhibition.
  int16_t dive;
  // Effective buoyancy target/procedure after safety overrides.
  BuoyancyAction buoyancy;
  // Controller feedback; never inferred successful from a target request.
  ActionStatus action_status;
};

// Asynchronous durable-write request. The adapter must never block the safety
// loop.
struct EmergencyWrite {
  // Monotonic transaction token; stale acknowledgements cannot clear a new
  // stop.
  uint32_t token;
  // True for a latch write, false for a requested durable local reset.
  bool latched;
  // Whether the board adapter owes a verified write/readback acknowledgement.
  bool pending;
};

// Synchronous bounded output callback; the caller owns encoding and transport.
class PacketSink {
 public:
  // Releases a sink through its interface.
  virtual ~PacketSink() {}
  // Consumes one packet before returning; must not reenter the manager or
  // block.
  virtual void Send(const Packet& packet) = 0;
};

// Portable BCP authorization and safety policy. No Arduino APIs or allocations.
// Call Advance frequently even when no wire traffic arrives. All calls must run
// serially on the control-loop thread; callbacks must not reenter the manager.
class SessionManager {
 public:
  // Copies an immutable profile and verified startup evidence; starts stopped.
  SessionManager(const ControllerProfile& profile,
                 const VerifiedBootState& boot);
  // Authorization identities and counters must never be cloned into another
  // peer.
  SessionManager(const SessionManager&) = delete;
  // Prevents replacing a live authorization domain through accidental
  // assignment.
  SessionManager& operator=(const SessionManager&) = delete;
  // Applies safety deadlines before parsing one complete delimited wire frame.
  // Returns syntax success only; true does not imply command authorization.
  bool Receive(const uint8_t* wire, size_t length, uint32_t now,
               PacketSink& sink);
  // Applies expiry, sensor interlocks and qualification without any incoming
  // data.
  void Advance(uint32_t now);
  // Advances safety and emits at most one ticket, status, heartbeat and seven
  // fault reports. No catch-up loops or internal outgoing queue are used.
  void Poll(uint32_t now, PacketSink& sink);
  // Validates a trusted driver snapshot and assigns the established session and
  // next packet sequence. Requires advertised support; grants no control.
  // Drivers own acquisition counters, calibrated geometry and publication rate.
  bool PublishObjects(const ObjectReport& report, uint32_t now,
                      PacketSink& sink);
  // Validates trusted instrument records against advertised telemetry support,
  // assigning the established session and next packet sequence. No control
  // authority.
  bool PublishTelemetry(const Telemetry& telemetry, uint32_t now,
                        PacketSink& sink);
  // Revokes pending/current PC sessions on a known disconnect or cancellation.
  // Active PC movement enters command-loss failsafe; local ownership is
  // retained.
  void RevokePcSession(uint32_t now);
  // Updates the trusted physical selector, preserving higher-priority latches.
  void SetOwner(MovementOwner owner, uint32_t now);
  // Supplies a newly acquired calibrated sample; invalid samples never renew
  // it.
  void SetDepth(int32_t millimetres, bool valid, uint32_t now);
  // Records a controller/storage fault with the profile's explicit severity.
  void SetFault(FaultCode code, bool active, uint32_t now);
  // Accepts a genuinely new local adapter sample, never cached receiver
  // channels. Sequence increases per source across ownership changes; zero is
  // invalid.
  bool LocalMovement(MovementOwner source, uint32_t sequence, int16_t forward,
                     int16_t yaw, int16_t dive, uint32_t now);
  // Independently renews a local buoyancy target with a fresh adapter sample.
  bool LocalBuoyancy(MovementOwner source, uint32_t sequence,
                     BuoyancyTarget target, uint32_t now);
  // Processes a new local action edge; adapters must enforce startup/reconnect
  // off-then-on qualification. PC actions must arrive through Receive instead.
  CommandResultCode LocalAction(MovementOwner source, MessageType action,
                                uint32_t now);
  // Asserts the trusted physical emergency input, immediately stopping intent.
  void SetEmergencyInput(bool asserted, uint32_t now);
  // Supplies observed procedure feedback; completion never clears a safety
  // latch.
  void SetBuoyancyFeedback(BuoyancyAction action, ActionStatus status,
                           EstimatedBuoyancy estimate, uint32_t now);
  // Requests local reset only after physical input release and procedure end.
  // Remains latched until the matching durable-clear acknowledgement succeeds.
  bool RequestLocalReset(uint32_t now);
  // Completes the current write/readback transaction; failure requires service.
  void CompleteEmergencyWrite(uint32_t token, bool verified, uint32_t now);
  // Returns the current bounded persistence request for a future board adapter.
  EmergencyWrite PendingEmergencyWrite() const;
  // Advances safety before exposing effective controller intent.
  ControlOutputs Outputs(uint32_t now);
  // Advances safety and builds authoritative status; no actuator success
  // implied.
  VehicleStatus Status(uint32_t now);
  // Returns whether the PC completed an explicit session handshake.
  bool HasSession() const;
  // Returns the established identity, or zero when no established session
  // exists.
  SessionId CurrentSession() const;

 private:
  // Native conformance tests inject otherwise impractical UINT32_MAX
  // boundaries.
  friend class SessionTestAccess;
  // One fixed outstanding ticket, all invalidated together on generation
  // change.
  struct TicketEntry {
    // Zero marks an unused slot.
    uint32_t id;
    // Onboard issuance timestamp.
    uint32_t issued_at;
  };
  // Canonical discrete identity, independent of packet padding and sequence.
  struct RequestEntry {
    // Nonzero request ID; zero marks unused.
    uint32_t id;
    // Request type participates in identity.
    MessageType type;
    // Ticket or zero for unconditional stop requests.
    uint32_t ticket;
    // Generation or zero for unconditional stop requests.
    uint32_t generation;
    // Historical response, including its original generation.
    CommandResult result;
  };
  // Continuous stream lease with independent acceptance and ticket deadlines.
  struct Lease {
    // Whether the stream currently has usable input.
    bool active;
    // Time of acceptance, or new local acquisition.
    uint32_t accepted_at;
    // Issuance time of the authorizing ticket, or local acquisition time.
    uint32_t ticket_at;
  };
  // One pending/current handshake with an independent outgoing sequence.
  struct Connection {
    // Nonreused assigned identity.
    SessionId id;
    // Opaque connection correlation value.
    OpaqueId128 nonce;
    // Last outgoing sequence in this identity.
    uint32_t outbound;
    // Last accepted confirmation sequence, independent of continuous streams.
    uint32_t confirmation_sequence;
    // Creation time, immutable through retries.
    uint32_t created_at;
    // Whether this slot contains a live connection.
    bool active;
  };
  // One fault's independent cause and occurrence state.
  struct FaultState {
    // Current active condition, independent of the dominant safety state.
    bool active;
    // Last transmitted active condition.
    bool reported;
    // Saturating inactive-to-active occurrence count within this boot.
    uint32_t occurrences;
    // Last repeat publication time.
    uint32_t reported_at;
  };

  // Checks wrap-safe elapsed lease lifetimes, each strictly below 2^31 ms.
  static bool Fresh(const Lease& lease, uint32_t now);
  // Checks the immutable profile, refusing unsupported masks or unsafe limits.
  static bool ValidProfile(const ControllerProfile& profile);
  // Checks configured trusted input sources.
  bool Configured(MovementOwner source) const;
  // Checks calibrated sample validity and its independent stale deadline.
  bool DepthHealthy(uint32_t now) const;
  // Checks all active arming blockers that require engineering/service
  // recovery.
  bool BlockingFault() const;
  // Reports whether all three movement axes are within the inclusive tolerance.
  bool Neutral() const;
  // Invalidates authorization once; optionally retains the current movement
  // lease.
  bool ChangeGeneration(bool preserve_movement = false);
  // Stops before counter exhaustion without wrapping or downgrading emergency.
  void CounterFailure(bool permanent = false);
  // Clears intent and hold intervals without changing stream replay counters.
  void ClearIntent(bool preserve_movement = false);
  // Enters a higher-priority latch and records its cause once.
  void Enter(SafetyState state, SafetyCause cause);
  // Updates a fault occurrence without recursively invoking Advance.
  void RecordFault(FaultCode code, bool active);
  // Builds and returns the effective buoyancy action from policy state.
  BuoyancyAction EffectiveBuoyancy() const;
  // Allocates a new asynchronous emergency transaction, never wrapping its
  // token.
  bool BeginEmergencyWrite(bool latched);
  // Validates a ticket against this generation and original issuance time.
  CommandResultCode CheckTicket(uint32_t id, uint32_t generation, uint32_t now,
                                uint32_t& issued_at) const;
  // Validates and records an owner movement sample after transport checks.
  bool AcceptMovement(uint32_t sequence, uint32_t issued_at, int16_t forward,
                      int16_t yaw, int16_t dive, uint32_t now);
  // Validates and records an independent normal buoyancy sample.
  bool AcceptBuoyancy(uint32_t sequence, uint32_t issued_at,
                      BuoyancyTarget target, uint32_t now);
  // Applies a new arm/clear/disarm/stop action after identity checks.
  CommandResultCode ApplyAction(MessageType type, uint32_t now);
  // Handles fixed-cache request identity and exactly-once action application.
  void Discrete(const Packet& packet, uint32_t now, PacketSink& sink);
  // Processes a syntax-checked PC packet after safety deadlines run.
  void Process(const Packet& packet, uint32_t now, PacketSink& sink);
  // Builds a handshake reply from the retained connection without renewing it.
  void HelloReply(Connection& connection, PacketSink& sink);
  // Assigns a checked sequence and synchronously sends one packet.
  bool Emit(Packet& packet, Connection& connection, PacketSink& sink);
  // Sends current status in the established session.
  void SendStatus(uint32_t now, PacketSink& sink);
  // Marks a discrete externally visible status change (revision may wrap).
  void Changed();

  // Immutable capabilities, engineering profile and source provisioning.
  ControllerProfile profile_;
  // Verified startup evidence retained for handshake identity.
  VerifiedBootState boot_;
  // Pending and established identities; creating pending never revokes current.
  Connection pending_;
  // Established PC session and its outgoing sequence.
  Connection current_;
  // Fixed ticket ring and next insertion slot.
  TicketEntry tickets_[16];
  // Fixed historical result cache.
  RequestEntry requests_[16];
  // Independent fault causes and saturating recurrence counts.
  FaultState faults_[7];
  // Current movement freshness, independent of buoyancy.
  Lease movement_;
  // Current renewable normal buoyancy authorization.
  Lease buoyancy_;
  // Next durable emergency transaction to be acknowledged.
  EmergencyWrite emergency_write_;
  // Previously projected status shape, excluding ticking times and hold
  // progress.
  VehicleStatus previous_status_;
  // Nonreused session counter within this verified boot.
  uint32_t session_counter_;
  // Monotonic ticket counter within the current session.
  uint32_t ticket_counter_;
  // Nonwrapping authorization generation, initially one.
  uint32_t generation_;
  // Highest processed discrete ID, including evicted results.
  uint32_t highest_request_;
  // Last accepted continuous PC movement sequence.
  uint32_t movement_sequence_;
  // Last accepted independent PC buoyancy sequence.
  uint32_t buoyancy_sequence_;
  // Trusted local movement replay counters indexed by owner enum.
  uint32_t local_movement_sequence_[4];
  // Trusted local buoyancy replay counters indexed by owner enum.
  uint32_t local_buoyancy_sequence_[4];
  // Wrapping discrete status change indicator.
  uint32_t revision_;
  // Start of the uninterrupted neutral interval.
  uint32_t neutral_since_;
  // Start of the independent depth-release qualification interval.
  uint32_t release_since_;
  // Deadline origin for the first movement after arming.
  uint32_t armed_at_;
  // Last valid depth acquisition time.
  uint32_t depth_at_;
  // Latest calibrated depth in millimetres.
  int32_t depth_mm_;
  // Last ticket scheduling time; skipped intervals are never replayed.
  uint32_t ticket_at_;
  // Last status scheduling time.
  uint32_t status_at_;
  // Last diagnostic heartbeat time.
  uint32_t heartbeat_at_;
  // Index of the next ticket ring slot.
  uint8_t ticket_slot_;
  // Index of the next result-cache slot.
  uint8_t request_slot_;
  // Dominant safety state; independent latch booleans retain subordinate
  // causes.
  SafetyState state_;
  // Trusted physical movement selector.
  MovementOwner owner_;
  // Entry cause of the current dominant state.
  SafetyCause cause_;
  // Accepted forward intent, never applied while disarmed.
  int16_t forward_;
  // Accepted yaw intent.
  int16_t yaw_;
  // Accepted dive intent.
  int16_t dive_;
  // Last accepted independently renewable normal target.
  BuoyancyTarget target_;
  // Controller-reported action whose feedback may be used.
  BuoyancyAction feedback_action_;
  // Last effective action; a changed action invalidates old completion
  // feedback.
  BuoyancyAction last_action_;
  // Explicit controller procedure feedback; no implied completion.
  ActionStatus feedback_status_;
  // Explicit controller estimate, initially unknown.
  EstimatedBuoyancy estimate_;
  // Profile/boot validation failure cannot be cleared by remote requests.
  bool service_required_;
  // Calibrated depth sample validity, independent of its age.
  bool depth_valid_;
  // Whether a neutral interval is currently uninterrupted.
  bool neutral_holding_;
  // Whether both input streams currently qualify for depth release.
  bool release_holding_;
  // Whether depth restriction is latched pending explicit fresh-input release.
  bool restricted_;
  // Retained emergency latch, independent of lower-priority failures.
  bool emergency_latched_;
  // Retained command-loss cause, even when fault/emergency has priority.
  bool command_loss_latched_;
  // Retained fault-failsafe cause, even while emergency has priority.
  bool fault_latched_;
  // State of the physical emergency input, initially released.
  bool emergency_input_;
  // Whether reset is awaiting verified durable clear.
  bool reset_pending_;
  // Movement age is meaningful only after acceptance in the current generation.
  bool accepted_in_generation_;
  // Whether previous_status_ has been initialized.
  bool has_previous_status_;
};
}  // namespace bcp
}  // namespace bajols
#endif
