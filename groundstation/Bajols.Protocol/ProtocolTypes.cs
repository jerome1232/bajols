using System.Buffers.Binary;

namespace Bajols.Protocol;

public enum WireDirection { PcToVehicle, VehicleToPc }
public enum MessageType : byte
{
    Hello = 0x01, HelloReply = 0x02, SessionConfirm = 0x03, SessionReady = 0x04,
    ControlTicket = 0x05, Heartbeat = 0x06, MovementCommand = 0x10, Arm = 0x11,
    Disarm = 0x12, ClearFailsafe = 0x13, EmergencyStop = 0x14, CommandResult = 0x15,
    BuoyancyCommand = 0x16, Capabilities = 0x20, GetCapabilities = 0x21,
    VehicleStatus = 0x30, Telemetry = 0x31, Fault = 0x32
}
public enum SafetyState : byte { Disarmed, Armed, CommandLossFailsafe, EmergencyStopLatched, FaultFailsafe }
public enum MovementOwner : byte { None, Pc, FlySky, Autonomy }
public enum ControlMode : byte { None, Effort }
public enum BuoyancyTarget : byte { Neutral, Positive, Negative }
public enum AuthorizedBuoyancyTarget : byte { None, Neutral, Positive, Negative }
public enum BuoyancyAction : byte { None, SeekNeutral, SeekPositive, SeekNegative, EmergencyEvacuation }
public enum ActionStatus : byte { Inactive, Running, Completed, Failed, Unavailable }
public enum EstimatedBuoyancy : byte { Unknown, Neutral, Positive, Negative }
public enum FaultSeverity : byte { Information, Warning, Blocking, Critical }
public enum FaultCode : ushort
{
    DepthUnavailable = 1, BuoyancyControllerUnavailable, BuoyancyControllerFailed,
    EmergencyEvacuationFailed, IdentityStorageFailed, ControlLoopTimingFailed, EmergencyLatchStorageFailed
}
public enum SafetyCause : byte
{
    Startup, ExplicitArm, ExplicitDisarm, OwnerSelectorChanged, MovementExpired,
    ActivePcSessionRevoked, EmergencyStopAsserted, CommandLossFailsafeCleared,
    LocalEmergencyStopReset, DepthUnavailable, OtherBlockingFault, FaultFailsafeCleared
}
public enum CommandResultCode : byte
{
    Accepted, UnsupportedMessage, InvalidPayload, NotMovementOwner, UnknownOrExpiredTicket,
    WrongControlGeneration, InvalidSafetyState, NeutralHoldIncomplete, BlockingFault,
    LocalResetRequired, RequestTooOld, RequestIdReused
}
[Flags] public enum ArmingBlockers : uint
{
    None = 0, NoOwner = 1, StaleOwner = 2, NonNeutralInput = 4, NeutralHoldIncomplete = 8,
    CommandLossLatch = 16, EmergencyLatch = 32, BlockingFault = 64,
    RequiredSubsystemUnavailable = 128, ArmActionMustReset = 256, FaultFailsafeLatch = 512
}
[Flags] public enum ActiveRestrictions : ushort
{
    None = 0, DepthLimitActive = 1, DepthUnavailable = 2, DownwardControlInhibited = 4
}
[Flags] public enum TelemetryQuality : byte { None = 0, Valid = 1, Simulated = 2, Estimated = 4, SensorFault = 8 }
public enum MeasurementId : byte { Depth = 1, AbsolutePressure, SupplyVoltage, PropulsionRpm, Heading, Pitch, Roll, WaterTemperature }

/// <summary>Eight wire bytes: boot counter first, then session counter. Not an authorization check.</summary>
public readonly record struct SessionId(uint BootCounter, uint SessionCounter);

/// <summary>Opaque wire bytes; intentionally not System.Guid (no UUID endian conversion).</summary>
public readonly record struct OpaqueId128(ulong First, ulong Second)
{
    public static OpaqueId128 FromBytes(ReadOnlySpan<byte> bytes)
    {
        if (bytes.Length != 16) throw new ArgumentException("Exactly 16 bytes required.", nameof(bytes));
        return new(BinaryPrimitives.ReadUInt64LittleEndian(bytes), BinaryPrimitives.ReadUInt64LittleEndian(bytes[8..]));
    }
    public void WriteBytes(Span<byte> destination)
    {
        if (destination.Length < 16) throw new ArgumentException("At least 16 bytes required.", nameof(destination));
        BinaryPrimitives.WriteUInt64LittleEndian(destination, First);
        BinaryPrimitives.WriteUInt64LittleEndian(destination[8..], Second);
    }
}

public enum CodecError
{
    None, DestinationTooSmall, FrameTooLarge, InvalidDelimiter, InvalidCobs,
    InvalidFrameLength, InvalidCrc, UnsupportedVersion, ReservedFlags,
    UnsupportedMessage, InvalidDirection, InvalidPayloadLength, InvalidPayload, DuplicateMeasurement
}
