#ifndef BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_PROTOCOL_H_
#define BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_PROTOCOL_H_

#include <stddef.h>
#include <stdint.h>

/// Portable BCP v1 syntax codec. Successful decoding never authorizes movement.
namespace bajols {
namespace bcp {
/// Wire-envelope version supported by this library.
constexpr uint8_t kVersion = 1;
/// Decoded header length before payload bytes.
constexpr size_t kHeaderSize = 16;
/// Maximum payload length, independent of message type.
constexpr size_t kMaxPayloadSize = 96;
/// Maximum decoded header, payload and CRC length.
constexpr size_t kMaxDecodedSize = 114;
/// Maximum COBS frame length including the zero delimiter.
constexpr size_t kMaxWireSize = 116;
/// Maximum telemetry records in one bounded payload.
constexpr uint8_t kMaxTelemetryRecords = 11;

/// Permitted direction of one typed message.
enum class Direction : uint8_t {
  /// PcToVehicle numeric value.
  kPcToVehicle = 0,
  /// VehicleToPc numeric value.
  kVehicleToPc = 1,
};

/// Assigned BCP v1 message IDs. Unknown IDs are rejected by the typed codec.
enum class MessageType : uint8_t {
  /// Hello numeric value.
  kHello = 1,
  /// HelloReply numeric value.
  kHelloReply = 2,
  /// SessionConfirm numeric value.
  kSessionConfirm = 3,
  /// SessionReady numeric value.
  kSessionReady = 4,
  /// ControlTicket numeric value.
  kControlTicket = 5,
  /// Heartbeat numeric value.
  kHeartbeat = 6,
  /// MovementCommand numeric value.
  kMovementCommand = 16,
  /// Arm numeric value.
  kArm = 17,
  /// Disarm numeric value.
  kDisarm = 18,
  /// ClearFailsafe numeric value.
  kClearFailsafe = 19,
  /// EmergencyStop numeric value.
  kEmergencyStop = 20,
  /// CommandResult numeric value.
  kCommandResult = 21,
  /// BuoyancyCommand numeric value.
  kBuoyancyCommand = 22,
  /// Capabilities numeric value.
  kCapabilities = 32,
  /// GetCapabilities numeric value.
  kGetCapabilities = 33,
  /// VehicleStatus numeric value.
  kVehicleStatus = 48,
  /// Telemetry numeric value.
  kTelemetry = 49,
  /// Fault numeric value.
  kFault = 50,
  /// Per-sensor observation snapshot.
  kObjectReport = 51,
};

/// Assigned SafetyState values; unknown receive-side reporting values are
/// preserved.
enum class SafetyState : uint8_t {
  /// Disarmed numeric value.
  kDisarmed = 0,
  /// Armed numeric value.
  kArmed = 1,
  /// CommandLossFailsafe numeric value.
  kCommandLossFailsafe = 2,
  /// EmergencyStopLatched numeric value.
  kEmergencyStopLatched = 3,
  /// FaultFailsafe numeric value.
  kFaultFailsafe = 4,
};

/// Assigned MovementOwner values; unknown receive-side reporting values are
/// preserved.
enum class MovementOwner : uint8_t {
  /// None numeric value.
  kNone = 0,
  /// Pc numeric value.
  kPc = 1,
  /// FlySky numeric value.
  kFlySky = 2,
  /// Autonomy numeric value.
  kAutonomy = 3,
};

/// Assigned ControlMode values; unknown receive-side reporting values are
/// preserved.
enum class ControlMode : uint8_t {
  /// None numeric value.
  kNone = 0,
  /// Effort numeric value.
  kEffort = 1,
};

/// Assigned BuoyancyTarget values; unknown receive-side reporting values are
/// preserved.
enum class BuoyancyTarget : uint8_t {
  /// Neutral numeric value.
  kNeutral = 0,
  /// Positive numeric value.
  kPositive = 1,
  /// Negative numeric value.
  kNegative = 2,
};

/// Assigned AuthorizedBuoyancyTarget values; unknown receive-side reporting
/// values are preserved.
enum class AuthorizedBuoyancyTarget : uint8_t {
  /// None numeric value.
  kNone = 0,
  /// Neutral numeric value.
  kNeutral = 1,
  /// Positive numeric value.
  kPositive = 2,
  /// Negative numeric value.
  kNegative = 3,
};

/// Assigned BuoyancyAction values; unknown receive-side reporting values are
/// preserved.
enum class BuoyancyAction : uint8_t {
  /// None numeric value.
  kNone = 0,
  /// SeekNeutral numeric value.
  kSeekNeutral = 1,
  /// SeekPositive numeric value.
  kSeekPositive = 2,
  /// SeekNegative numeric value.
  kSeekNegative = 3,
  /// EmergencyEvacuation numeric value.
  kEmergencyEvacuation = 4,
};

/// Assigned ActionStatus values; unknown receive-side reporting values are
/// preserved.
enum class ActionStatus : uint8_t {
  /// Inactive numeric value.
  kInactive = 0,
  /// Running numeric value.
  kRunning = 1,
  /// Completed numeric value.
  kCompleted = 2,
  /// Failed numeric value.
  kFailed = 3,
  /// Unavailable numeric value.
  kUnavailable = 4,
};

/// Assigned EstimatedBuoyancy values; unknown receive-side reporting values are
/// preserved.
enum class EstimatedBuoyancy : uint8_t {
  /// Unknown numeric value.
  kUnknown = 0,
  /// Neutral numeric value.
  kNeutral = 1,
  /// Positive numeric value.
  kPositive = 2,
  /// Negative numeric value.
  kNegative = 3,
};

/// Assigned FaultSeverity values; unknown receive-side reporting values are
/// preserved.
enum class FaultSeverity : uint8_t {
  /// Information numeric value.
  kInformation = 0,
  /// Warning numeric value.
  kWarning = 1,
  /// Blocking numeric value.
  kBlocking = 2,
  /// Critical numeric value.
  kCritical = 3,
};

/// Assigned FaultCode values; unknown receive-side reporting values are
/// preserved.
enum class FaultCode : uint16_t {
  /// DepthUnavailable numeric value.
  kDepthUnavailable = 1,
  /// BuoyancyControllerUnavailable numeric value.
  kBuoyancyControllerUnavailable = 2,
  /// BuoyancyControllerFailed numeric value.
  kBuoyancyControllerFailed = 3,
  /// EmergencyEvacuationFailed numeric value.
  kEmergencyEvacuationFailed = 4,
  /// IdentityStorageFailed numeric value.
  kIdentityStorageFailed = 5,
  /// ControlLoopTimingFailed numeric value.
  kControlLoopTimingFailed = 6,
  /// EmergencyLatchStorageFailed numeric value.
  kEmergencyLatchStorageFailed = 7,
};

/// Assigned SafetyCause values; unknown receive-side reporting values are
/// preserved.
enum class SafetyCause : uint8_t {
  /// Startup numeric value.
  kStartup = 0,
  /// ExplicitArm numeric value.
  kExplicitArm = 1,
  /// ExplicitDisarm numeric value.
  kExplicitDisarm = 2,
  /// OwnerSelectorChanged numeric value.
  kOwnerSelectorChanged = 3,
  /// MovementExpired numeric value.
  kMovementExpired = 4,
  /// ActivePcSessionRevoked numeric value.
  kActivePcSessionRevoked = 5,
  /// EmergencyStopAsserted numeric value.
  kEmergencyStopAsserted = 6,
  /// CommandLossFailsafeCleared numeric value.
  kCommandLossFailsafeCleared = 7,
  /// LocalEmergencyStopReset numeric value.
  kLocalEmergencyStopReset = 8,
  /// DepthUnavailable numeric value.
  kDepthUnavailable = 9,
  /// OtherBlockingFault numeric value.
  kOtherBlockingFault = 10,
  /// FaultFailsafeCleared numeric value.
  kFaultFailsafeCleared = 11,
};

/// Assigned CommandResultCode values; unknown receive-side reporting values are
/// preserved.
enum class CommandResultCode : uint8_t {
  /// Accepted numeric value.
  kAccepted = 0,
  /// UnsupportedMessage numeric value.
  kUnsupportedMessage = 1,
  /// InvalidPayload numeric value.
  kInvalidPayload = 2,
  /// NotMovementOwner numeric value.
  kNotMovementOwner = 3,
  /// UnknownOrExpiredTicket numeric value.
  kUnknownOrExpiredTicket = 4,
  /// WrongControlGeneration numeric value.
  kWrongControlGeneration = 5,
  /// InvalidSafetyState numeric value.
  kInvalidSafetyState = 6,
  /// NeutralHoldIncomplete numeric value.
  kNeutralHoldIncomplete = 7,
  /// BlockingFault numeric value.
  kBlockingFault = 8,
  /// LocalResetRequired numeric value.
  kLocalResetRequired = 9,
  /// RequestTooOld numeric value.
  kRequestTooOld = 10,
  /// RequestIdReused numeric value.
  kRequestIdReused = 11,
};

/// Assigned MeasurementId values; unknown receive-side reporting values are
/// preserved.
enum class MeasurementId : uint8_t {
  /// Depth numeric value.
  kDepth = 1,
  /// AbsolutePressure numeric value.
  kAbsolutePressure = 2,
  /// SupplyVoltage numeric value.
  kSupplyVoltage = 3,
  /// PropulsionRpm numeric value.
  kPropulsionRpm = 4,
  /// Heading numeric value.
  kHeading = 5,
  /// Pitch numeric value.
  kPitch = 6,
  /// Roll numeric value.
  kRoll = 7,
  /// WaterTemperature numeric value.
  kWaterTemperature = 8,
};

/// Syntax and caller-buffer errors; none imply control authorization.
enum class Error : uint8_t {
  /// None numeric value.
  kNone = 0,
  /// DestinationTooSmall numeric value.
  kDestinationTooSmall = 1,
  /// FrameTooLarge numeric value.
  kFrameTooLarge = 2,
  /// InvalidDelimiter numeric value.
  kInvalidDelimiter = 3,
  /// InvalidCobs numeric value.
  kInvalidCobs = 4,
  /// InvalidFrameLength numeric value.
  kInvalidFrameLength = 5,
  /// InvalidCrc numeric value.
  kInvalidCrc = 6,
  /// UnsupportedVersion numeric value.
  kUnsupportedVersion = 7,
  /// ReservedFlags numeric value.
  kReservedFlags = 8,
  /// UnsupportedMessage numeric value.
  kUnsupportedMessage = 9,
  /// InvalidDirection numeric value.
  kInvalidDirection = 10,
  /// InvalidPayloadLength numeric value.
  kInvalidPayloadLength = 11,
  /// InvalidPayload numeric value.
  kInvalidPayload = 12,
  /// DuplicateMeasurement numeric value.
  kDuplicateMeasurement = 13,
  /// InvalidArgument numeric value.
  kInvalidArgument = 14,
};

/// Result of consuming one byte without an internal message queue.
enum class StreamResult : uint8_t {
  /// NeedMoreData numeric value.
  kNeedMoreData = 0,
  /// PacketReady numeric value.
  kPacketReady = 1,
  /// Rejected numeric value.
  kRejected = 2,
};

/// Opaque bytes, not a UUID with mixed-endian fields.
struct OpaqueId128 {
  /// Sixteen identity bytes in their original transmission order.
  uint8_t bytes[16];
};

/// Unique identity within a provisioned vehicle: boot counter precedes session
/// counter.
struct SessionId {
  /// Persistent boot identity; provisioning/storage is outside the codec.
  uint32_t boot_counter;
  /// Session identity within the boot; never wraps while authorizing control.
  uint32_t session_counter;
};

/// Header values in host representation; never memcpy this struct onto the
/// wire.
struct Header {
  /// Assigned or raw message identifier.
  MessageType type;
  /// Vehicle-issued session identity; zero is reserved for HELLO.
  SessionId session;
  /// Packet sequence; stateful replay checks remain the caller's
  /// responsibility.
  uint32_t sequence;
};

/// Host representation of Hello; explicit serializers define its wire layout.
struct Hello {
  /// Opaque connection-attempt nonce in transmission order.
  OpaqueId128 nonce;
  /// Requested version.
  uint8_t requested_version;
};

/// Host representation of HelloReply; explicit serializers define its wire
/// layout.
struct HelloReply {
  /// Opaque connection-attempt nonce in transmission order.
  OpaqueId128 nonce;
  /// Selected version.
  uint8_t selected_version;
  /// Provisioned vehicle identity in transmission order.
  OpaqueId128 vehicle_id;
};

/// Host representation of SessionConfirm; explicit serializers define its wire
/// layout.
struct SessionConfirm {
  /// Opaque connection-attempt nonce in transmission order.
  OpaqueId128 nonce;
};

/// Host representation of SessionReady; explicit serializers define its wire
/// layout.
struct SessionReady {
  /// Current authorization generation; validated by a later stateful handler.
  uint32_t control_generation;
};

/// Host representation of ControlTicket; explicit serializers define its wire
/// layout.
struct ControlTicket {
  /// Ticket id.
  uint32_t ticket_id;
  /// Current authorization generation; validated by a later stateful handler.
  uint32_t control_generation;
  /// Validity ms in milliseconds.
  uint16_t validity_ms;
};

/// Host representation of MovementCommand; explicit serializers define its wire
/// layout.
struct MovementCommand {
  /// Ticket id.
  uint32_t ticket_id;
  /// Current authorization generation; validated by a later stateful handler.
  uint32_t control_generation;
  /// Signed effort from -1000 to 1000; positive means ahead.
  int16_t forward;
  /// Signed effort from -1000 to 1000; positive means starboard.
  int16_t yaw;
  /// Signed effort from -1000 to 1000; positive means downward.
  int16_t dive;
};

/// Host representation of Arm; explicit serializers define its wire layout.
struct Arm {
  /// Identifier used to correlate and deduplicate a discrete request.
  uint32_t request_id;
  /// Ticket id.
  uint32_t ticket_id;
  /// Current authorization generation; validated by a later stateful handler.
  uint32_t control_generation;
};

/// Host representation of Disarm; explicit serializers define its wire layout.
struct Disarm {
  /// Identifier used to correlate and deduplicate a discrete request.
  uint32_t request_id;
};

/// Host representation of ClearFailsafe; explicit serializers define its wire
/// layout.
struct ClearFailsafe {
  /// Identifier used to correlate and deduplicate a discrete request.
  uint32_t request_id;
  /// Ticket id.
  uint32_t ticket_id;
  /// Current authorization generation; validated by a later stateful handler.
  uint32_t control_generation;
};

/// Host representation of EmergencyStop; explicit serializers define its wire
/// layout.
struct EmergencyStop {
  /// Identifier used to correlate and deduplicate a discrete request.
  uint32_t request_id;
};

/// Host representation of CommandResult; explicit serializers define its wire
/// layout.
struct CommandResult {
  /// Identifier used to correlate and deduplicate a discrete request.
  uint32_t request_id;
  /// Original message type.
  MessageType original_message_type;
  /// Result.
  CommandResultCode result;
  /// Current authorization generation; validated by a later stateful handler.
  uint32_t control_generation;
};

/// Host representation of BuoyancyCommand; explicit serializers define its wire
/// layout.
struct BuoyancyCommand {
  /// Ticket id.
  uint32_t ticket_id;
  /// Current authorization generation; validated by a later stateful handler.
  uint32_t control_generation;
  /// Target.
  BuoyancyTarget target;
};

/// Host representation of Capabilities; explicit serializers define its wire
/// layout.
struct Capabilities {
  /// Capability revision.
  uint16_t capability_revision;
  /// Supported modes.
  uint16_t supported_modes;
  /// Supported axes.
  uint8_t supported_axes;
  /// Supported systems.
  uint16_t supported_systems;
  /// Supported telemetry.
  uint32_t supported_telemetry;
  /// Movement interval ms in milliseconds.
  uint16_t movement_interval_ms;
  /// Command timeout ms in milliseconds.
  uint16_t command_timeout_ms;
  /// Ticket interval ms in milliseconds.
  uint16_t ticket_interval_ms;
  /// Ticket lifetime ms in milliseconds.
  uint16_t ticket_lifetime_ms;
  /// Neutral hold ms in milliseconds.
  uint16_t neutral_hold_ms;
  /// Neutral tolerance.
  uint16_t neutral_tolerance;
};

/// Host representation of VehicleStatus; explicit serializers define its wire
/// layout.
struct VehicleStatus {
  /// Status revision.
  uint32_t status_revision;
  /// Vehicle uptime ms in milliseconds.
  uint32_t vehicle_uptime_ms;
  /// Safety state.
  SafetyState safety_state;
  /// Movement owner.
  MovementOwner movement_owner;
  /// Control mode.
  ControlMode control_mode;
  /// Current authorization generation; validated by a later stateful handler.
  uint32_t control_generation;
  /// Arming-blocker mask; unknown received bits remain available to the caller.
  uint32_t arming_blockers;
  /// Neutral hold progress ms in milliseconds.
  uint16_t neutral_hold_progress_ms;
  /// Movement age ms in milliseconds.
  uint16_t movement_age_ms;
  /// Movement time remaining ms in milliseconds.
  uint16_t movement_time_remaining_ms;
  /// Buoyancy action.
  BuoyancyAction buoyancy_action;
  /// Buoyancy action status.
  ActionStatus buoyancy_action_status;
  /// Safety cause.
  SafetyCause safety_cause;
  /// Estimated buoyancy.
  EstimatedBuoyancy estimated_buoyancy;
  /// Authorized normal target.
  AuthorizedBuoyancyTarget authorized_normal_target;
  /// Buoyancy command time remaining ms in milliseconds.
  uint16_t buoyancy_command_time_remaining_ms;
  /// Restriction mask; unknown received bits must not enable control.
  uint16_t active_restrictions;
};

/// Host representation of Fault; explicit serializers define its wire layout.
struct Fault {
  /// Fault code.
  FaultCode fault_code;
  /// Severity.
  FaultSeverity severity;
  /// Whether the fault is active rather than cleared.
  bool active;
  /// Occurrence counter.
  uint32_t occurrence_counter;
};

/// Single telemetry sample; validity/provenance and freshness need separate
/// policy checks.
struct TelemetryRecord {
  /// Assigned measurement identifier and its fixed unit.
  MeasurementId measurement_id;
  /// Validity/provenance bits; reserved bits are preserved but unusable for
  /// control.
  uint8_t quality_flags;
  /// Onboard age in milliseconds; 65535 means unknown.
  uint16_t sample_age_ms;
  /// Signed value in the measurement's fixed unit.
  int32_t value;
};

/// Bounded telemetry container; unknown received IDs are omitted after
/// duplicate checks.
struct Telemetry {
  /// Number of populated records, never greater than eleven.
  uint8_t count;
  /// Fixed storage; only entries below count are active.
  TelemetryRecord records[kMaxTelemetryRecords];
};

/// One body-relative observation.
struct ObjectObservation {
  /// Nonzero snapshot-local identifier, not a persistent track.
  uint16_t observation_id;
  /// Radial range from the sensor reference face in millimetres.
  uint32_t sensor_range_mm;
  /// Estimated range from the vehicle datum in millimetres.
  uint32_t vehicle_range_mm;
  /// Body-relative bearing in centidegrees; positive starboard.
  int16_t bearing_cdeg;
  /// Elevation positive up, or 32767 when unknown.
  int16_t elevation_cdeg;
  /// Vehicle-range uncertainty, or 65535 when unknown.
  uint16_t range_half_width_mm;
  /// Bearing uncertainty, or 65535 when unknown.
  uint16_t bearing_half_width_cdeg;
  /// Elevation uncertainty, or 65535 when unknown.
  uint16_t elevation_half_width_cdeg;
  /// Validity and provenance flags shared with telemetry.
  uint8_t quality_flags;
};

/// Complete per-source acquisition, fixed storage and no allocation.
struct ObjectReport {
  /// Observation layout revision, currently one.
  uint8_t format_revision;
  /// Stable configured source identifier from 1 through 254.
  uint8_t sensor_id;
  /// Nonwrapping acquisition counter within the vehicle boot.
  uint32_t scan_sequence;
  /// Age of oldest constituent acquisition; 65535 means unknown.
  uint16_t sample_age_ms;
  /// Zero unavailable, one ready, two fault, three uncalibrated.
  uint8_t sensor_state;
  /// Bit zero truncated, bit one simulated.
  uint8_t report_flags;
  /// Number of active observations, at most four.
  uint8_t count;
  /// Fixed storage for the complete snapshot.
  ObjectObservation observations[4];
};

/// Fixed storage for the payload selected by Header::type; contains no owning
/// pointers.
union Payload {
  /// Storage selected for Hello.
  Hello hello;
  /// Storage selected for HelloReply.
  HelloReply hello_reply;
  /// Storage selected for SessionConfirm.
  SessionConfirm session_confirm;
  /// Storage selected for SessionReady.
  SessionReady session_ready;
  /// Storage selected for ControlTicket.
  ControlTicket control_ticket;
  /// Storage selected for MovementCommand.
  MovementCommand movement;
  /// Storage selected for Arm.
  Arm arm;
  /// Storage selected for Disarm.
  Disarm disarm;
  /// Storage selected for ClearFailsafe.
  ClearFailsafe clear_failsafe;
  /// Storage selected for EmergencyStop.
  EmergencyStop emergency_stop;
  /// Storage selected for CommandResult.
  CommandResult command_result;
  /// Storage selected for BuoyancyCommand.
  BuoyancyCommand buoyancy;
  /// Storage selected for Capabilities.
  Capabilities capabilities;
  /// Storage selected for VehicleStatus.
  VehicleStatus status;
  /// Storage selected for Fault.
  Fault fault;
  /// Storage selected for Telemetry.
  Telemetry telemetry;
  /// Object snapshot storage.
  ObjectReport object_report;
};

/// Typed packet, independent of Arduino APIs, UI, transport and execution
/// authority.
struct Packet {
  /// Host-order header metadata.
  Header header;
  /// Payload selected by header.type; empty messages do not access this
  /// storage.
  Payload message;
};

/// Raw envelope with owned fixed payload storage; unknown message IDs are
/// permitted here.
struct Frame {
  /// Host-order header metadata.
  Header header;
  /// Number of populated payload bytes.
  uint8_t payload_length;
  /// Owned decoded bytes; only entries below payload_length are active.
  uint8_t payload[kMaxPayloadSize];
};

/// Computes CRC-16/CCITT-FALSE; data must address length readable bytes (null
/// permitted for zero length).
uint16_t ComputeCrc(const uint8_t* data, size_t length);

/// Encodes one raw frame atomically; failure leaves destination unchanged and
/// written zero.
bool EncodeFrame(const Frame& frame, uint8_t* destination, size_t capacity,
                 size_t& written, Error& error);

/// Decodes exactly one delimited envelope; failure leaves output unchanged.
bool DecodeFrame(const uint8_t* wire, size_t length, Frame& output,
                 Error& error);

/// Validates and encodes a typed packet; does not establish sessions or
/// authorize actions.
bool Encode(const Packet& packet, Direction direction, uint8_t* destination,
            size_t capacity, size_t& written, Error& error);

/// Decodes typed syntax, preserving unknown reporting enums; no freshness or
/// ownership checks are implied.
bool Decode(const uint8_t* wire, size_t length, Direction direction,
            Packet& output, Error& error);

/// Checks whether status contains unknown control enums or blocker/restriction
/// bits.
bool HasUnknownControlValues(const VehicleStatus& status);

/// Checks whether telemetry quality uses only assigned bits; validity/age must
/// still be evaluated separately.
bool HasKnownQualityFlags(const TelemetryRecord& record);

/// Fixed-buffer stream parser with no internal message queue; one instance per
/// receive stream.
class StreamDecoder {
 public:
  /// Creates a parser for one message direction.
  explicit StreamDecoder(Direction direction);
  /// Discards partial input on disconnect or stream Reset.
  void Reset();
  /// Consumes one byte; oversized input is discarded until its delimiter, then
  /// reports one rejection.
  StreamResult Push(uint8_t value, Packet& output, Error& error);

 private:
  /// Expected typed message direction.
  Direction direction_;
  /// Fixed storage for one complete wire frame.
  uint8_t buffer_[kMaxWireSize];
  /// Buffered bytes before the delimiter; bounded to 115.
  uint8_t count_;
  /// Whether oversized input is being discarded through its delimiter.
  bool discarding_;
};
}  // namespace bcp
}  // namespace bajols
#endif
