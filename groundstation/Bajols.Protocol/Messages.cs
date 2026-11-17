namespace Bajols.Protocol;

public abstract record BcpMessage
{
    protected BcpMessage(MessageType type) => Type = type;
    public MessageType Type { get; }
}
public sealed record Hello(OpaqueId128 Nonce, byte RequestedVersion) : BcpMessage(MessageType.Hello);
public sealed record HelloReply(OpaqueId128 Nonce, byte SelectedVersion, OpaqueId128 VehicleId) : BcpMessage(MessageType.HelloReply);
public sealed record SessionConfirm(OpaqueId128 Nonce) : BcpMessage(MessageType.SessionConfirm);
public sealed record SessionReady(uint ControlGeneration) : BcpMessage(MessageType.SessionReady);
public sealed record ControlTicket(uint TicketId, uint ControlGeneration, ushort ValidityMs) : BcpMessage(MessageType.ControlTicket);
public sealed record Heartbeat() : BcpMessage(MessageType.Heartbeat);
public sealed record MovementCommand(uint TicketId, uint ControlGeneration, short Forward, short Yaw, short Dive) : BcpMessage(MessageType.MovementCommand);
public sealed record Arm(uint RequestId, uint TicketId, uint ControlGeneration) : BcpMessage(MessageType.Arm);
public sealed record Disarm(uint RequestId) : BcpMessage(MessageType.Disarm);
public sealed record ClearFailsafe(uint RequestId, uint TicketId, uint ControlGeneration) : BcpMessage(MessageType.ClearFailsafe);
public sealed record EmergencyStop(uint RequestId) : BcpMessage(MessageType.EmergencyStop);
public sealed record CommandResult(uint RequestId, MessageType OriginalMessageType, CommandResultCode Result, uint ControlGeneration) : BcpMessage(MessageType.CommandResult);
public sealed record BuoyancyCommand(uint TicketId, uint ControlGeneration, BuoyancyTarget Target) : BcpMessage(MessageType.BuoyancyCommand);
public sealed record GetCapabilities() : BcpMessage(MessageType.GetCapabilities);
public sealed record Capabilities(
    ushort CapabilityRevision, ushort SupportedModes, byte SupportedAxes, ushort SupportedSystems,
    uint SupportedTelemetry, ushort MovementIntervalMs, ushort CommandTimeoutMs,
    ushort TicketIntervalMs, ushort TicketLifetimeMs, ushort NeutralHoldMs, ushort NeutralTolerance
) : BcpMessage(MessageType.Capabilities);
public sealed record VehicleStatus(
    uint StatusRevision, uint VehicleUptimeMs, SafetyState SafetyState, MovementOwner MovementOwner,
    ControlMode ControlMode, uint ControlGeneration, ArmingBlockers ArmingBlockers,
    ushort NeutralHoldProgressMs, ushort MovementAgeMs, ushort MovementTimeRemainingMs,
    BuoyancyAction BuoyancyAction, ActionStatus BuoyancyActionStatus, SafetyCause SafetyCause,
    EstimatedBuoyancy EstimatedBuoyancy, AuthorizedBuoyancyTarget AuthorizedNormalTarget,
    ushort BuoyancyCommandTimeRemainingMs, ActiveRestrictions ActiveRestrictions
) : BcpMessage(MessageType.VehicleStatus)
{
    /// <summary>Unknown received values must not be presented as ready to arm.</summary>
    public bool HasUnknownControlValues =>
        !Enum.IsDefined(SafetyState) || !Enum.IsDefined(MovementOwner) || !Enum.IsDefined(ControlMode) ||
        !Enum.IsDefined(BuoyancyAction) || !Enum.IsDefined(BuoyancyActionStatus) || !Enum.IsDefined(SafetyCause) ||
        !Enum.IsDefined(EstimatedBuoyancy) || !Enum.IsDefined(AuthorizedNormalTarget) ||
        ((uint)ArmingBlockers & ~0x3FFu) != 0 || ((ushort)ActiveRestrictions & ~7) != 0;
}
public readonly record struct TelemetryRecord(MeasurementId MeasurementId, TelemetryQuality QualityFlags, ushort SampleAgeMs, int Value)
{
    public bool HasKnownQualityFlags => ((byte)QualityFlags & ~15) == 0;
}
/// <summary>At most 11 records. Unknown measurement IDs are omitted on receipt.</summary>
public sealed record Telemetry(IReadOnlyList<TelemetryRecord> Records) : BcpMessage(MessageType.Telemetry);
public sealed record Fault(FaultCode FaultCode, FaultSeverity Severity, bool Active, uint OccurrenceCounter) : BcpMessage(MessageType.Fault);
public sealed record BcpPacket(SessionId Session, uint Sequence, BcpMessage Message);
