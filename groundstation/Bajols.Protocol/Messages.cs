namespace Bajols.Protocol;

/// <summary>
/// Base representation of a typed BCP message with an immutable assigned wire type.
/// </summary>
public abstract record BcpMessage
{
    /// <summary>
    /// Initializes the immutable wire message type shared by typed BCP messages.
    /// </summary>
    protected BcpMessage(MessageType type) => Type = type;

    /// <summary>
    /// Immutable numeric wire message type associated with this typed message.
    /// </summary>
    public MessageType Type
    {
        get;
    }
}

/// <summary>
/// Requests a BCP session using a newly generated opaque connection nonce.
/// </summary>
/// <param name="Nonce">The opaque connection-attempt nonce, preserved in transmission order.</param>
/// <param name="RequestedVersion">The protocol version requested by the connecting peer.</param>
public sealed record Hello(OpaqueId128 Nonce, byte RequestedVersion) : BcpMessage(MessageType.Hello);

/// <summary>
/// Correlates a handshake reply with its connection nonce and identifies the selected vehicle.
/// </summary>
/// <param name="Nonce">The opaque connection-attempt nonce, preserved in transmission order.</param>
/// <param name="SelectedVersion">The protocol version selected by the vehicle.</param>
/// <param name="VehicleId">The provisioned opaque vehicle identity.</param>
public sealed record HelloReply(OpaqueId128 Nonce, byte SelectedVersion, OpaqueId128 VehicleId) : BcpMessage(MessageType.HelloReply);

/// <summary>
/// Confirms the pending vehicle-issued session using the echoed connection nonce.
/// </summary>
/// <param name="Nonce">The opaque connection-attempt nonce, preserved in transmission order.</param>
public sealed record SessionConfirm(OpaqueId128 Nonce) : BcpMessage(MessageType.SessionConfirm);

/// <summary>
/// Reports acceptance of a session and its current control authorization generation.
/// </summary>
/// <param name="ControlGeneration">The current generation of control authorization.</param>
public sealed record SessionReady(uint ControlGeneration) : BcpMessage(MessageType.SessionReady);

/// <summary>
/// Advertises a short-lived vehicle-issued ticket for freshly sampled control intent.
/// </summary>
/// <param name="TicketId">The identifier of the vehicle-issued freshness ticket.</param>
/// <param name="ControlGeneration">The current generation of control authorization.</param>
/// <param name="ValidityMs">The ticket lifetime in onboard milliseconds from issuance.</param>
public sealed record ControlTicket(uint TicketId, uint ControlGeneration, ushort ValidityMs) : BcpMessage(MessageType.ControlTicket);

/// <summary>
/// Reports peer liveness without renewing movement or buoyancy authorization.
/// </summary>
public sealed record Heartbeat() : BcpMessage(MessageType.Heartbeat);

/// <summary>
/// Carries normalized forward, yaw and dive effort bound to a freshness ticket and control generation.
/// </summary>
/// <param name="TicketId">The identifier of the vehicle-issued freshness ticket.</param>
/// <param name="ControlGeneration">The current generation of control authorization.</param>
/// <param name="Forward">Signed forward effort from -1000 to 1000; positive means ahead.</param>
/// <param name="Yaw">Signed turning effort from -1000 to 1000; positive means starboard.</param>
/// <param name="Dive">Signed diving effort from -1000 to 1000; positive means downward.</param>
public sealed record MovementCommand(uint TicketId, uint ControlGeneration, short Forward, short Yaw, short Dive) : BcpMessage(MessageType.MovementCommand);

/// <summary>
/// Requests explicit arming using a deduplicated request ID and current control authorization.
/// </summary>
/// <param name="RequestId">The identifier used to correlate and deduplicate a discrete request.</param>
/// <param name="TicketId">The identifier of the vehicle-issued freshness ticket.</param>
/// <param name="ControlGeneration">The current generation of control authorization.</param>
public sealed record Arm(uint RequestId, uint TicketId, uint ControlGeneration) : BcpMessage(MessageType.Arm);

/// <summary>
/// Requests routine disarming without requiring a movement-freshness ticket.
/// </summary>
/// <param name="RequestId">The identifier used to correlate and deduplicate a discrete request.</param>
public sealed record Disarm(uint RequestId) : BcpMessage(MessageType.Disarm);

/// <summary>
/// Requests explicit recovery from a non-emergency failsafe using current control authorization.
/// </summary>
/// <param name="RequestId">The identifier used to correlate and deduplicate a discrete request.</param>
/// <param name="TicketId">The identifier of the vehicle-issued freshness ticket.</param>
/// <param name="ControlGeneration">The current generation of control authorization.</param>
public sealed record ClearFailsafe(uint RequestId, uint TicketId, uint ControlGeneration) : BcpMessage(MessageType.ClearFailsafe);

/// <summary>
/// Asserts emergency stop independently of movement ownership or ticket freshness.
/// </summary>
/// <param name="RequestId">The identifier used to correlate and deduplicate a discrete request.</param>
public sealed record EmergencyStop(uint RequestId) : BcpMessage(MessageType.EmergencyStop);

/// <summary>
/// Reports the acceptance or rejection of a correlated discrete control request.
/// </summary>
/// <param name="RequestId">The identifier used to correlate and deduplicate a discrete request.</param>
/// <param name="OriginalMessageType">The discrete message type whose result is being reported.</param>
/// <param name="Result">The explicit onboard acceptance or rejection code.</param>
/// <param name="ControlGeneration">The current generation of control authorization.</param>
public sealed record CommandResult(uint RequestId, MessageType OriginalMessageType, CommandResultCode Result, uint ControlGeneration) : BcpMessage(MessageType.CommandResult);

/// <summary>
/// Carries a renewable normal buoyancy target bound to a freshness ticket and control generation.
/// </summary>
/// <param name="TicketId">The identifier of the vehicle-issued freshness ticket.</param>
/// <param name="ControlGeneration">The current generation of control authorization.</param>
/// <param name="Target">The requested normal buoyancy target; emergency evacuation is separate.</param>
public sealed record BuoyancyCommand(uint TicketId, uint ControlGeneration, BuoyancyTarget Target) : BcpMessage(MessageType.BuoyancyCommand);

/// <summary>
/// Requests the established peer's fixed capability set.
/// </summary>
public sealed record GetCapabilities() : BcpMessage(MessageType.GetCapabilities);

/// <summary>
/// Describes implemented control functions, telemetry and BCP timing limits for the current session.
/// </summary>
/// <param name="CapabilityRevision">Revision identifying the fixed capability set in this session.</param>
/// <param name="SupportedModes">Supported-mode bitmask; bit zero advertises effort control.</param>
/// <param name="SupportedAxes">Supported-axis bitmask; bits zero through two represent forward, yaw and dive.</param>
/// <param name="SupportedSystems">Implemented buoyancy-system bitmask defined by the BCP registry.</param>
/// <param name="SupportedTelemetry">Supported-measurement bitmask using measurement identifier minus one as the bit index.</param>
/// <param name="MovementIntervalMs">Expected movement update interval in milliseconds; the v1 profile uses 50.</param>
/// <param name="CommandTimeoutMs">Maximum movement-command lifetime in milliseconds; ticket expiry can shorten it.</param>
/// <param name="TicketIntervalMs">Interval between vehicle-issued tickets in milliseconds.</param>
/// <param name="TicketLifetimeMs">Ticket lifetime in onboard milliseconds measured from issuance.</param>
/// <param name="NeutralHoldMs">Required uninterrupted fresh neutral-input duration in milliseconds.</param>
/// <param name="NeutralTolerance">Permitted neutral effort magnitude in fixed-point BCP units.</param>
public sealed record Capabilities(ushort CapabilityRevision, ushort SupportedModes, byte SupportedAxes, ushort SupportedSystems, uint SupportedTelemetry, ushort MovementIntervalMs, ushort CommandTimeoutMs, ushort TicketIntervalMs, ushort TicketLifetimeMs, ushort NeutralHoldMs, ushort NeutralTolerance) : BcpMessage(MessageType.Capabilities);

/// <summary>
/// Reports current vehicle safety, ownership, restrictions and command-freshness state.
/// </summary>
/// <param name="StatusRevision">Wrapping change indicator for discrete vehicle status fields.</param>
/// <param name="VehicleUptimeMs">Onboard snapshot time in wrapping milliseconds.</param>
/// <param name="SafetyState">Authoritative onboard safety state, independent of desktop connectivity.</param>
/// <param name="MovementOwner">Source selected as the sole movement owner.</param>
/// <param name="ControlMode">Mode defining the semantics of movement intent.</param>
/// <param name="ControlGeneration">The current generation of control authorization.</param>
/// <param name="ArmingBlockers">All currently reported onboard reasons preventing arming.</param>
/// <param name="NeutralHoldProgressMs">Fresh neutral-input qualification progress in milliseconds, capped at 1000.</param>
/// <param name="MovementAgeMs">Age of accepted movement in milliseconds; 65535 means unavailable.</param>
/// <param name="MovementTimeRemainingMs">Remaining movement authorization in milliseconds, or zero when none remains.</param>
/// <param name="BuoyancyAction">Effective buoyancy action after onboard safety overrides.</param>
/// <param name="BuoyancyActionStatus">Reported progress or availability of the effective buoyancy action.</param>
/// <param name="SafetyCause">Event that entered the reported safety state.</param>
/// <param name="EstimatedBuoyancy">Estimated actual buoyancy; unknown is distinct from neutral.</param>
/// <param name="AuthorizedNormalTarget">Currently authorized renewable buoyancy target, or none.</param>
/// <param name="BuoyancyCommandTimeRemainingMs">Remaining normal buoyancy authorization in milliseconds, or zero.</param>
/// <param name="ActiveRestrictions">Reported depth and downward-control restrictions.</param>
public sealed record VehicleStatus(uint StatusRevision, uint VehicleUptimeMs, SafetyState SafetyState, MovementOwner MovementOwner, ControlMode ControlMode, uint ControlGeneration, ArmingBlockers ArmingBlockers, ushort NeutralHoldProgressMs, ushort MovementAgeMs, ushort MovementTimeRemainingMs, BuoyancyAction BuoyancyAction, ActionStatus BuoyancyActionStatus, SafetyCause SafetyCause, EstimatedBuoyancy EstimatedBuoyancy, AuthorizedBuoyancyTarget AuthorizedNormalTarget, ushort BuoyancyCommandTimeRemainingMs, ActiveRestrictions ActiveRestrictions) : BcpMessage(MessageType.VehicleStatus)
{
    /// <summary>Unknown received values must not be presented as ready to arm.</summary>
    public bool HasUnknownControlValues => !Enum.IsDefined(SafetyState) || !Enum.IsDefined(MovementOwner) || !Enum.IsDefined(ControlMode) || !Enum.IsDefined(BuoyancyAction) || !Enum.IsDefined(BuoyancyActionStatus) || !Enum.IsDefined(SafetyCause) || !Enum.IsDefined(EstimatedBuoyancy) || !Enum.IsDefined(AuthorizedNormalTarget) || ((uint)ArmingBlockers & ~0x3FFu) != 0 || ((ushort)ActiveRestrictions & ~7) != 0;
}

/// <summary>
/// Carries a measurement value with its validity, provenance and onboard sample age.
/// </summary>
/// <param name="MeasurementId">Measurement identifier that determines the value units.</param>
/// <param name="QualityFlags">Independent validity, simulation, estimation and sensor-fault bits.</param>
/// <param name="SampleAgeMs">Onboard sample age in milliseconds; 65535 means unknown.</param>
/// <param name="Value">Signed measurement value in the fixed units assigned to its measurement identifier.</param>
public readonly record struct TelemetryRecord(MeasurementId MeasurementId, TelemetryQuality QualityFlags, ushort SampleAgeMs, int Value)
{
    /// <summary>
    /// Whether the record contains only allocated quality bits; reserved bits prevent use in control.
    /// </summary>
    public bool HasKnownQualityFlags => ((byte)QualityFlags & ~15) == 0;
}

/// <summary>At most 11 records. Unknown measurement IDs are omitted on receipt.</summary>
/// <param name="Records">At most eleven sampled records; unknown identifiers are omitted on receipt.</param>
public sealed record Telemetry(IReadOnlyList<TelemetryRecord> Records) : BcpMessage(MessageType.Telemetry);

/// <summary>
/// Reports an explicit fault condition, severity and occurrence count without inferring a recovery action.
/// </summary>
/// <param name="FaultCode">Numeric fault identifier; unknown received codes remain available for diagnostics.</param>
/// <param name="Severity">Explicit reporting severity, not an inferred recovery action.</param>
/// <param name="Active">Whether the fault is active rather than cleared.</param>
/// <param name="OccurrenceCounter">Saturating inactive-to-active occurrence count within the current boot.</param>
public sealed record Fault(FaultCode FaultCode, FaultSeverity Severity, bool Active, uint OccurrenceCounter) : BcpMessage(MessageType.Fault);

/// <summary>
/// Combines a session identity and transmission sequence with a typed BCP message.
/// </summary>
/// <param name="Session">The vehicle-issued boot and session counters.</param>
/// <param name="Sequence">The transmission sequence within this session.</param>
/// <param name="Message">The typed message carried by the packet.</param>
public sealed record BcpPacket(SessionId Session, uint Sequence, BcpMessage Message);
