using System.Buffers.Binary;

namespace Bajols.Protocol;

/// <summary>
/// Direction in which a BCP message is allowed to travel.
/// </summary>
public enum WireDirection
{
    /// <summary>
    /// Ground-station request or intent sent to the onboard peer.
    /// </summary>
    PcToVehicle,

    /// <summary>
    /// Onboard status, telemetry or response sent to the ground station.
    /// </summary>
    VehicleToPc
}

/// <summary>
/// Assigned BCP v1 message identifiers; numeric values are part of the shared wire contract.
/// </summary>
public enum MessageType : byte
{
    /// <summary>
    /// Wire identifier for hello messages.
    /// </summary>
    Hello = 0x01,

    /// <summary>
    /// Wire identifier for hello reply messages.
    /// </summary>
    HelloReply = 0x02,

    /// <summary>
    /// Wire identifier for session confirm messages.
    /// </summary>
    SessionConfirm = 0x03,

    /// <summary>
    /// Wire identifier for session ready messages.
    /// </summary>
    SessionReady = 0x04,

    /// <summary>
    /// Wire identifier for control ticket messages.
    /// </summary>
    ControlTicket = 0x05,

    /// <summary>
    /// Wire identifier for heartbeat messages.
    /// </summary>
    Heartbeat = 0x06,

    /// <summary>
    /// Wire identifier for movement command messages.
    /// </summary>
    MovementCommand = 0x10,

    /// <summary>
    /// Wire identifier for arm messages.
    /// </summary>
    Arm = 0x11,

    /// <summary>
    /// Wire identifier for disarm messages.
    /// </summary>
    Disarm = 0x12,

    /// <summary>
    /// Wire identifier for clear failsafe messages.
    /// </summary>
    ClearFailsafe = 0x13,

    /// <summary>
    /// Wire identifier for emergency stop messages.
    /// </summary>
    EmergencyStop = 0x14,

    /// <summary>
    /// Wire identifier for command result messages.
    /// </summary>
    CommandResult = 0x15,

    /// <summary>
    /// Wire identifier for buoyancy command messages.
    /// </summary>
    BuoyancyCommand = 0x16,

    /// <summary>
    /// Wire identifier for capabilities messages.
    /// </summary>
    Capabilities = 0x20,

    /// <summary>
    /// Wire identifier for get capabilities messages.
    /// </summary>
    GetCapabilities = 0x21,

    /// <summary>
    /// Wire identifier for vehicle status messages.
    /// </summary>
    VehicleStatus = 0x30,

    /// <summary>
    /// Wire identifier for telemetry messages.
    /// </summary>
    Telemetry = 0x31,

    /// <summary>
    /// Wire identifier for fault messages.
    /// </summary>
    Fault = 0x32
}

/// <summary>
/// Onboard safety states, independent of desktop connectivity or input presentation.
/// </summary>
public enum SafetyState : byte
{
    /// <summary>
    /// Thrust is disabled and configured neutral actuator states apply.
    /// </summary>
    Disarmed,

    /// <summary>
    /// Fresh authorized movement may be applied; arming alone does not restore old intent.
    /// </summary>
    Armed,

    /// <summary>
    /// Command loss has stopped thrust and initiated neutral-buoyancy recovery.
    /// </summary>
    CommandLossFailsafe,

    /// <summary>
    /// Emergency stop has stopped thrust and initiated evacuation until local reset.
    /// </summary>
    EmergencyStopLatched,

    /// <summary>
    /// A blocking fault has stopped thrust and requires deliberate recovery.
    /// </summary>
    FaultFailsafe
}

/// <summary>
/// Source selected to own movement authority; other configured sources may still stop the vehicle.
/// </summary>
public enum MovementOwner : byte
{
    /// <summary>
    /// No movement source is selected.
    /// </summary>
    None,

    /// <summary>
    /// pc is selected as the sole movement owner.
    /// </summary>
    Pc,

    /// <summary>
    /// fly sky is selected as the sole movement owner.
    /// </summary>
    FlySky,

    /// <summary>
    /// autonomy is selected as the sole movement owner.
    /// </summary>
    Autonomy
}

/// <summary>
/// Operating mode defining the semantics of accepted high-level movement intent.
/// </summary>
public enum ControlMode : byte
{
    /// <summary>
    /// No operating mode is selected.
    /// </summary>
    None,

    /// <summary>
    /// Commands express normalized movement effort rather than rate or position setpoints.
    /// </summary>
    Effort
}

/// <summary>
/// Renewable normal buoyancy targets carried by commands; emergency evacuation is separate.
/// </summary>
public enum BuoyancyTarget : byte
{
    /// <summary>
    /// Neutral buoyancy without a tendency to rise or sink.
    /// </summary>
    Neutral,

    /// <summary>
    /// Positive buoyancy with a tendency to rise.
    /// </summary>
    Positive,

    /// <summary>
    /// Negative buoyancy with a tendency to sink.
    /// </summary>
    Negative
}

/// <summary>
/// Currently authorized normal buoyancy target, including the absence of authorization.
/// </summary>
public enum AuthorizedBuoyancyTarget : byte
{
    /// <summary>
    /// No normal buoyancy target is currently authorized.
    /// </summary>
    None,

    /// <summary>
    /// Neutral buoyancy without a tendency to rise or sink.
    /// </summary>
    Neutral,

    /// <summary>
    /// Positive buoyancy with a tendency to rise.
    /// </summary>
    Positive,

    /// <summary>
    /// Negative buoyancy with a tendency to sink.
    /// </summary>
    Negative
}

/// <summary>
/// Effective onboard buoyancy action after safety overrides and target expiry.
/// </summary>
public enum BuoyancyAction : byte
{
    /// <summary>
    /// No buoyancy action is active.
    /// </summary>
    None,

    /// <summary>
    /// The effective onboard action is seek neutral.
    /// </summary>
    SeekNeutral,

    /// <summary>
    /// The effective onboard action is seek positive.
    /// </summary>
    SeekPositive,

    /// <summary>
    /// The effective onboard action is seek negative.
    /// </summary>
    SeekNegative,

    /// <summary>
    /// The effective onboard action is emergency evacuation.
    /// </summary>
    EmergencyEvacuation
}

/// <summary>
/// Reported progress or availability of the effective onboard buoyancy action.
/// </summary>
public enum ActionStatus : byte
{
    /// <summary>
    /// The effective buoyancy action is inactive.
    /// </summary>
    Inactive,

    /// <summary>
    /// The effective buoyancy action is running.
    /// </summary>
    Running,

    /// <summary>
    /// The effective buoyancy action is completed.
    /// </summary>
    Completed,

    /// <summary>
    /// The effective buoyancy action is failed.
    /// </summary>
    Failed,

    /// <summary>
    /// The effective buoyancy action is unavailable.
    /// </summary>
    Unavailable
}

/// <summary>
/// Onboard estimate of actual buoyancy, distinct from requested targets and vertical speed.
/// </summary>
public enum EstimatedBuoyancy : byte
{
    /// <summary>
    /// Available feedback does not establish actual buoyancy.
    /// </summary>
    Unknown,

    /// <summary>
    /// Neutral buoyancy without a tendency to rise or sink.
    /// </summary>
    Neutral,

    /// <summary>
    /// Positive buoyancy with a tendency to rise.
    /// </summary>
    Positive,

    /// <summary>
    /// Negative buoyancy with a tendency to sink.
    /// </summary>
    Negative
}

/// <summary>
/// Explicit fault-reporting severity; control responses are assigned separately by fault policy.
/// </summary>
public enum FaultSeverity : byte
{
    /// <summary>
    /// Explicit information reporting severity; it does not alone select a recovery action.
    /// </summary>
    Information,

    /// <summary>
    /// Explicit warning reporting severity; it does not alone select a recovery action.
    /// </summary>
    Warning,

    /// <summary>
    /// Explicit blocking reporting severity; it does not alone select a recovery action.
    /// </summary>
    Blocking,

    /// <summary>
    /// Explicit critical reporting severity; it does not alone select a recovery action.
    /// </summary>
    Critical
}

/// <summary>
/// Assigned BCP v1 fault identifiers describing the affected subsystem or safety mechanism.
/// </summary>
public enum FaultCode : ushort
{
    /// <summary>
    /// Reports depth unavailable; its control response is defined separately by fault policy.
    /// </summary>
    DepthUnavailable = 1,

    /// <summary>
    /// Reports buoyancy controller unavailable; its control response is defined separately by fault policy.
    /// </summary>
    BuoyancyControllerUnavailable,

    /// <summary>
    /// Reports buoyancy controller failed; its control response is defined separately by fault policy.
    /// </summary>
    BuoyancyControllerFailed,

    /// <summary>
    /// Reports emergency evacuation failed; its control response is defined separately by fault policy.
    /// </summary>
    EmergencyEvacuationFailed,

    /// <summary>
    /// Reports identity storage failed; its control response is defined separately by fault policy.
    /// </summary>
    IdentityStorageFailed,

    /// <summary>
    /// Reports control loop timing failed; its control response is defined separately by fault policy.
    /// </summary>
    ControlLoopTimingFailed,

    /// <summary>
    /// Reports emergency latch storage failed; its control response is defined separately by fault policy.
    /// </summary>
    EmergencyLatchStorageFailed
}

/// <summary>
/// Event responsible for entering the reported onboard safety state.
/// </summary>
public enum SafetyCause : byte
{
    /// <summary>
    /// The reported safety-state entry was caused by startup.
    /// </summary>
    Startup,

    /// <summary>
    /// The reported safety-state entry was caused by explicit arm.
    /// </summary>
    ExplicitArm,

    /// <summary>
    /// The reported safety-state entry was caused by explicit disarm.
    /// </summary>
    ExplicitDisarm,

    /// <summary>
    /// The reported safety-state entry was caused by owner selector changed.
    /// </summary>
    OwnerSelectorChanged,

    /// <summary>
    /// The reported safety-state entry was caused by movement expired.
    /// </summary>
    MovementExpired,

    /// <summary>
    /// The reported safety-state entry was caused by active pc session revoked.
    /// </summary>
    ActivePcSessionRevoked,

    /// <summary>
    /// The reported safety-state entry was caused by emergency stop asserted.
    /// </summary>
    EmergencyStopAsserted,

    /// <summary>
    /// The reported safety-state entry was caused by command loss failsafe cleared.
    /// </summary>
    CommandLossFailsafeCleared,

    /// <summary>
    /// The reported safety-state entry was caused by local emergency stop reset.
    /// </summary>
    LocalEmergencyStopReset,

    /// <summary>
    /// The reported safety-state entry was caused by depth unavailable.
    /// </summary>
    DepthUnavailable,

    /// <summary>
    /// The reported safety-state entry was caused by other blocking fault.
    /// </summary>
    OtherBlockingFault,

    /// <summary>
    /// The reported safety-state entry was caused by fault failsafe cleared.
    /// </summary>
    FaultFailsafeCleared
}

/// <summary>
/// Assigned acceptance and rejection results for correlated discrete requests.
/// </summary>
public enum CommandResultCode : byte
{
    /// <summary>
    /// The request was accepted, not necessarily physically completed.
    /// </summary>
    Accepted,

    /// <summary>
    /// The discrete request was rejected because of unsupported message.
    /// </summary>
    UnsupportedMessage,

    /// <summary>
    /// The discrete request was rejected because of invalid payload.
    /// </summary>
    InvalidPayload,

    /// <summary>
    /// The discrete request was rejected because of not movement owner.
    /// </summary>
    NotMovementOwner,

    /// <summary>
    /// The discrete request was rejected because of unknown or expired ticket.
    /// </summary>
    UnknownOrExpiredTicket,

    /// <summary>
    /// The discrete request was rejected because of wrong control generation.
    /// </summary>
    WrongControlGeneration,

    /// <summary>
    /// The discrete request was rejected because of invalid safety state.
    /// </summary>
    InvalidSafetyState,

    /// <summary>
    /// The discrete request was rejected because of neutral hold incomplete.
    /// </summary>
    NeutralHoldIncomplete,

    /// <summary>
    /// The discrete request was rejected because of blocking fault.
    /// </summary>
    BlockingFault,

    /// <summary>
    /// The discrete request was rejected because of local reset required.
    /// </summary>
    LocalResetRequired,

    /// <summary>
    /// The discrete request was rejected because of request too old.
    /// </summary>
    RequestTooOld,

    /// <summary>
    /// The discrete request was rejected because of request id reused.
    /// </summary>
    RequestIdReused
}

/// <summary>
/// Independent onboard conditions preventing arming; multiple bits may be active.
/// </summary>
[Flags]
public enum ArmingBlockers : uint
{
    /// <summary>
    /// No onboard arming blockers are reported.
    /// </summary>
    None = 0,

    /// <summary>
    /// Arming is blocked by no owner.
    /// </summary>
    NoOwner = 1,

    /// <summary>
    /// Arming is blocked by stale owner.
    /// </summary>
    StaleOwner = 2,

    /// <summary>
    /// Arming is blocked by non neutral input.
    /// </summary>
    NonNeutralInput = 4,

    /// <summary>
    /// Arming is blocked by neutral hold incomplete.
    /// </summary>
    NeutralHoldIncomplete = 8,

    /// <summary>
    /// Arming is blocked by command loss latch.
    /// </summary>
    CommandLossLatch = 16,

    /// <summary>
    /// Arming is blocked by emergency latch.
    /// </summary>
    EmergencyLatch = 32,

    /// <summary>
    /// Arming is blocked by blocking fault.
    /// </summary>
    BlockingFault = 64,

    /// <summary>
    /// Arming is blocked by required subsystem unavailable.
    /// </summary>
    RequiredSubsystemUnavailable = 128,

    /// <summary>
    /// Arming is blocked by arm action must reset.
    /// </summary>
    ArmActionMustReset = 256,

    /// <summary>
    /// Arming is blocked by fault failsafe latch.
    /// </summary>
    FaultFailsafeLatch = 512
}

/// <summary>
/// Current onboard control restrictions reported independently of requested intent.
/// </summary>
[Flags]
public enum ActiveRestrictions : ushort
{
    /// <summary>
    /// No control restrictions are reported.
    /// </summary>
    None = 0,

    /// <summary>
    /// The onboard controller reports depth limit active.
    /// </summary>
    DepthLimitActive = 1,

    /// <summary>
    /// The onboard controller reports depth unavailable.
    /// </summary>
    DepthUnavailable = 2,

    /// <summary>
    /// The onboard controller reports downward control inhibited.
    /// </summary>
    DownwardControlInhibited = 4
}

/// <summary>
/// Independent validity and provenance bits attached to a sampled measurement.
/// </summary>
[Flags]
public enum TelemetryQuality : byte
{
    /// <summary>
    /// No validity or provenance bits are asserted.
    /// </summary>
    None = 0,

    /// <summary>
    /// The source marks the measurement valid; freshness checks remain separate.
    /// </summary>
    Valid = 1,

    /// <summary>
    /// The value comes from simulation rather than a physical sensor.
    /// </summary>
    Simulated = 2,

    /// <summary>
    /// The value is an estimate rather than a direct measurement.
    /// </summary>
    Estimated = 4,

    /// <summary>
    /// The sensor reports a fault; the value is not usable as valid control feedback.
    /// </summary>
    SensorFault = 8
}

/// <summary>
/// Assigned measurement identifiers whose units are fixed by the BCP v1 registry.
/// </summary>
public enum MeasurementId : byte
{
    /// <summary>
    /// Depth below the water surface in millimetres.
    /// </summary>
    Depth = 1,

    /// <summary>
    /// Absolute pressure in pascals.
    /// </summary>
    AbsolutePressure,

    /// <summary>
    /// Supply voltage in millivolts.
    /// </summary>
    SupplyVoltage,

    /// <summary>
    /// Propulsion speed magnitude in revolutions per minute.
    /// </summary>
    PropulsionRpm,

    /// <summary>
    /// Clockwise magnetic heading in centidegrees from north.
    /// </summary>
    Heading,

    /// <summary>
    /// Pitch in centidegrees; positive means nose-up.
    /// </summary>
    Pitch,

    /// <summary>
    /// Roll in centidegrees; positive means starboard-down.
    /// </summary>
    Roll,

    /// <summary>
    /// Water temperature in millidegrees Celsius.
    /// </summary>
    WaterTemperature
}

/// <summary>Eight wire bytes: boot counter first, then session counter. Not an authorization check.</summary>
/// <param name="BootCounter">Boot identity counter serialized first in the eight-byte session identifier.</param>
/// <param name="SessionCounter">Session identity counter serialized after the boot counter.</param>
public readonly record struct SessionId(uint BootCounter, uint SessionCounter);

/// <summary>Opaque wire bytes; intentionally not System.Guid (no UUID endian conversion).</summary>
/// <param name="First">The first eight opaque identity bytes interpreted without changing their wire order.</param>
/// <param name="Second">The last eight opaque identity bytes interpreted without changing their wire order.</param>
public readonly record struct OpaqueId128(ulong First, ulong Second)
{
    /// <summary>
    /// Creates an opaque identity from exactly 16 bytes in transmission order.
    /// </summary>
    public static OpaqueId128 FromBytes(ReadOnlySpan<byte> bytes)
    {
        if (bytes.Length != 16)
        {
            throw new ArgumentException("Exactly 16 bytes required.", nameof(bytes));
        }

        return new(BinaryPrimitives.ReadUInt64LittleEndian(bytes), BinaryPrimitives.ReadUInt64LittleEndian(bytes[8..]));
    }

    /// <summary>
    /// Copies the identity into caller-owned storage in its original transmission order.
    /// </summary>
    public void WriteBytes(Span<byte> destination)
    {
        if (destination.Length < 16)
        {
            throw new ArgumentException("At least 16 bytes required.", nameof(destination));
        }

        BinaryPrimitives.WriteUInt64LittleEndian(destination, First);
        BinaryPrimitives.WriteUInt64LittleEndian(destination[8..], Second);
    }
}

/// <summary>
/// Local serialization or syntax-validation failures, not onboard command authorization results.
/// </summary>
public enum CodecError
{
    /// <summary>
    /// No syntax or serialization failure occurred.
    /// </summary>
    None,

    /// <summary>
    /// The codec rejected input because of destination too small.
    /// </summary>
    DestinationTooSmall,

    /// <summary>
    /// The codec rejected input because of frame too large.
    /// </summary>
    FrameTooLarge,

    /// <summary>
    /// The codec rejected input because of invalid delimiter.
    /// </summary>
    InvalidDelimiter,

    /// <summary>
    /// The codec rejected input because of invalid cobs.
    /// </summary>
    InvalidCobs,

    /// <summary>
    /// The codec rejected input because of invalid frame length.
    /// </summary>
    InvalidFrameLength,

    /// <summary>
    /// The codec rejected input because of invalid crc.
    /// </summary>
    InvalidCrc,

    /// <summary>
    /// The codec rejected input because of unsupported version.
    /// </summary>
    UnsupportedVersion,

    /// <summary>
    /// The codec rejected input because of reserved flags.
    /// </summary>
    ReservedFlags,

    /// <summary>
    /// The codec rejected input because of unsupported message.
    /// </summary>
    UnsupportedMessage,

    /// <summary>
    /// The codec rejected input because of invalid direction.
    /// </summary>
    InvalidDirection,

    /// <summary>
    /// The codec rejected input because of invalid payload length.
    /// </summary>
    InvalidPayloadLength,

    /// <summary>
    /// The codec rejected input because of invalid payload.
    /// </summary>
    InvalidPayload,

    /// <summary>
    /// The codec rejected input because of duplicate measurement.
    /// </summary>
    DuplicateMeasurement
}
