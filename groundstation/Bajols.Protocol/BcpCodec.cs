namespace Bajols.Protocol;

/// <summary>
/// Typed BCP v1 wire codec. Successful decoding never grants control permission.
/// Session identity, sequence replay, tickets, ownership and safety require a stateful handler.
/// </summary>
public static class BcpCodec
{
    public static bool TryEncode(BcpPacket packet, WireDirection direction, Span<byte> destination,
        out int written, out CodecError error)
    {
        ArgumentNullException.ThrowIfNull(packet);
        ArgumentNullException.ThrowIfNull(packet.Message);
        written = 0;
        if (!ValidateDirection(packet.Message.Type, direction, out error)) return false;
        if (!ValidSenderValues(packet.Message)) { error = CodecError.InvalidPayload; return false; }
        Span<byte> payload = stackalloc byte[FrameCodec.MaxPayloadSize];
        var w = new PayloadWriter(payload);
        switch (packet.Message)
        {
            case Hello m: w.Id(m.Nonce); w.U8(m.RequestedVersion); break;
            case HelloReply m: w.Id(m.Nonce); w.U8(m.SelectedVersion); w.Id(m.VehicleId); break;
            case SessionConfirm m: w.Id(m.Nonce); break;
            case SessionReady m: w.U32(m.ControlGeneration); break;
            case ControlTicket m: w.U32(m.TicketId); w.U32(m.ControlGeneration); w.U16(m.ValidityMs); break;
            case Heartbeat or GetCapabilities: break;
            case MovementCommand m:
                w.U32(m.TicketId); w.U32(m.ControlGeneration); w.I16(m.Forward); w.I16(m.Yaw); w.I16(m.Dive); break;
            case Arm m: w.U32(m.RequestId); w.U32(m.TicketId); w.U32(m.ControlGeneration); break;
            case ClearFailsafe m: w.U32(m.RequestId); w.U32(m.TicketId); w.U32(m.ControlGeneration); break;
            case Disarm m: w.U32(m.RequestId); break;
            case EmergencyStop m: w.U32(m.RequestId); break;
            case CommandResult m: w.U32(m.RequestId); w.U8((byte)m.OriginalMessageType); w.U8((byte)m.Result); w.U32(m.ControlGeneration); break;
            case BuoyancyCommand m: w.U32(m.TicketId); w.U32(m.ControlGeneration); w.U8((byte)m.Target); break;
            case Capabilities m:
                w.U16(m.CapabilityRevision); w.U16(m.SupportedModes); w.U8(m.SupportedAxes);
                w.U16(m.SupportedSystems); w.U32(m.SupportedTelemetry); w.U16(m.MovementIntervalMs);
                w.U16(m.CommandTimeoutMs); w.U16(m.TicketIntervalMs); w.U16(m.TicketLifetimeMs);
                w.U16(m.NeutralHoldMs); w.U16(m.NeutralTolerance); break;
            case VehicleStatus m:
                w.U32(m.StatusRevision); w.U32(m.VehicleUptimeMs); w.U8((byte)m.SafetyState);
                w.U8((byte)m.MovementOwner); w.U8((byte)m.ControlMode); w.U32(m.ControlGeneration);
                w.U32((uint)m.ArmingBlockers); w.U16(m.NeutralHoldProgressMs); w.U16(m.MovementAgeMs);
                w.U16(m.MovementTimeRemainingMs); w.U8((byte)m.BuoyancyAction); w.U8((byte)m.BuoyancyActionStatus);
                w.U8((byte)m.SafetyCause); w.U8((byte)m.EstimatedBuoyancy); w.U8((byte)m.AuthorizedNormalTarget);
                w.U16(m.BuoyancyCommandTimeRemainingMs); w.U16((ushort)m.ActiveRestrictions); break;
            case Telemetry m:
                if (m.Records.Count > 11) { error = CodecError.InvalidPayloadLength; return false; }
                w.U8((byte)m.Records.Count);
                foreach (var record in m.Records)
                {
                    w.U8((byte)record.MeasurementId); w.U8((byte)record.QualityFlags);
                    w.U16(record.SampleAgeMs); w.I32(record.Value);
                }
                break;
            case Fault m: w.U16((ushort)m.FaultCode); w.U8((byte)m.Severity); w.U8(m.Active ? (byte)1 : (byte)0); w.U32(m.OccurrenceCounter); break;
            default: error = CodecError.UnsupportedMessage; return false;
        }
        if (!ValidatePayload(packet.Message.Type, payload[..w.Written], out error)) return false;
        return FrameCodec.TryEncode((byte)packet.Message.Type, packet.Session, packet.Sequence,
            payload[..w.Written], destination, out written, out error);
    }

    public static bool TryDecode(ReadOnlySpan<byte> wire, WireDirection direction,
        out BcpPacket? packet, out CodecError error)
    {
        packet = null;
        if (!FrameCodec.TryDecode(wire, out var frame, out error)) return false;
        return TryDecode(frame!, direction, out packet, out error);
    }

    public static bool TryDecode(BcpFrame frame, WireDirection direction,
        out BcpPacket? packet, out CodecError error)
    {
        ArgumentNullException.ThrowIfNull(frame);
        packet = null;
        var type = (MessageType)frame.MessageType;
        if (!ValidateDirection(type, direction, out error) || !ValidatePayload(type, frame.Payload.Span, out error)) return false;
        var r = new PayloadReader(frame.Payload.Span);
        BcpMessage message;
        switch (type)
        {
            case MessageType.Hello: message = new Hello(r.Id(), r.U8()); break;
            case MessageType.HelloReply: message = new HelloReply(r.Id(), r.U8(), r.Id()); break;
            case MessageType.SessionConfirm: message = new SessionConfirm(r.Id()); break;
            case MessageType.SessionReady: message = new SessionReady(r.U32()); break;
            case MessageType.ControlTicket: message = new ControlTicket(r.U32(), r.U32(), r.U16()); break;
            case MessageType.Heartbeat: message = new Heartbeat(); break;
            case MessageType.GetCapabilities: message = new GetCapabilities(); break;
            case MessageType.MovementCommand: message = new MovementCommand(r.U32(), r.U32(), r.I16(), r.I16(), r.I16()); break;
            case MessageType.Arm: message = new Arm(r.U32(), r.U32(), r.U32()); break;
            case MessageType.Disarm: message = new Disarm(r.U32()); break;
            case MessageType.ClearFailsafe: message = new ClearFailsafe(r.U32(), r.U32(), r.U32()); break;
            case MessageType.EmergencyStop: message = new EmergencyStop(r.U32()); break;
            case MessageType.CommandResult: message = new CommandResult(r.U32(), (MessageType)r.U8(), (CommandResultCode)r.U8(), r.U32()); break;
            case MessageType.BuoyancyCommand: message = new BuoyancyCommand(r.U32(), r.U32(), (BuoyancyTarget)r.U8()); break;
            case MessageType.Capabilities:
                message = new Capabilities(r.U16(), r.U16(), r.U8(), r.U16(), r.U32(), r.U16(), r.U16(), r.U16(), r.U16(), r.U16(), r.U16()); break;
            case MessageType.VehicleStatus:
                message = new VehicleStatus(r.U32(), r.U32(), (SafetyState)r.U8(), (MovementOwner)r.U8(),
                    (ControlMode)r.U8(), r.U32(), (ArmingBlockers)r.U32(), r.U16(), r.U16(), r.U16(),
                    (BuoyancyAction)r.U8(), (ActionStatus)r.U8(), (SafetyCause)r.U8(), (EstimatedBuoyancy)r.U8(),
                    (AuthorizedBuoyancyTarget)r.U8(), r.U16(), (ActiveRestrictions)r.U16()); break;
            case MessageType.Telemetry:
                int count = r.U8();
                var records = new List<TelemetryRecord>(count);
                for (int i = 0; i < count; i++)
                {
                    var record = new TelemetryRecord((MeasurementId)r.U8(), (TelemetryQuality)r.U8(), r.U16(), r.I32());
                    if (Enum.IsDefined(record.MeasurementId)) records.Add(record);
                }
                message = new Telemetry(records.AsReadOnly()); break;
            case MessageType.Fault: message = new Fault((FaultCode)r.U16(), (FaultSeverity)r.U8(), r.U8() != 0, r.U32()); break;
            default: error = CodecError.UnsupportedMessage; return false;
        }
        packet = new(frame.Session, frame.Sequence, message); error = CodecError.None; return true;
    }

    private static bool ValidateDirection(MessageType type, WireDirection direction, out CodecError error)
    {
        if (!Enum.IsDefined(type)) { error = CodecError.UnsupportedMessage; return false; }
        bool pc = type is MessageType.Hello or MessageType.SessionConfirm or MessageType.Heartbeat or
            MessageType.MovementCommand or MessageType.Arm or MessageType.Disarm or MessageType.ClearFailsafe or
            MessageType.EmergencyStop or MessageType.BuoyancyCommand or MessageType.GetCapabilities;
        bool vehicle = type is MessageType.HelloReply or MessageType.SessionReady or MessageType.ControlTicket or
            MessageType.Heartbeat or MessageType.CommandResult or MessageType.Capabilities or MessageType.VehicleStatus or
            MessageType.Telemetry or MessageType.Fault;
        bool valid = direction switch { WireDirection.PcToVehicle => pc, WireDirection.VehicleToPc => vehicle, _ => false };
        error = valid ? CodecError.None : CodecError.InvalidDirection; return valid;
    }

    private static bool ValidSenderValues(BcpMessage message) => message switch
    {
        BuoyancyCommand m => Enum.IsDefined(m.Target),
        VehicleStatus m => !m.HasUnknownControlValues,
        CommandResult m => Enum.IsDefined(m.Result) && m.OriginalMessageType is MessageType.Arm or MessageType.Disarm or MessageType.ClearFailsafe or MessageType.EmergencyStop,
        Capabilities m => (m.SupportedModes & ~1) == 0 && (m.SupportedAxes & ~7) == 0 &&
            (m.SupportedSystems & ~15) == 0 && (m.SupportedTelemetry & ~255u) == 0,
        Telemetry m => m.Records is not null && m.Records.Count <= 11 &&
            m.Records.All(r => Enum.IsDefined(r.MeasurementId) && r.HasKnownQualityFlags),
        Fault m => Enum.IsDefined(m.FaultCode) && Enum.IsDefined(m.Severity),
        _ => true
    };

    private static bool ValidatePayload(MessageType type, ReadOnlySpan<byte> p, out CodecError error)
    {
        int length = type switch
        {
            MessageType.Hello => 17, MessageType.HelloReply => 33, MessageType.SessionConfirm => 16,
            MessageType.SessionReady => 4, MessageType.ControlTicket => 10, MessageType.Heartbeat => 0,
            MessageType.MovementCommand => 14, MessageType.Arm or MessageType.ClearFailsafe => 12,
            MessageType.Disarm or MessageType.EmergencyStop => 4, MessageType.CommandResult => 10,
            MessageType.BuoyancyCommand => 9, MessageType.Capabilities => 23, MessageType.GetCapabilities => 0,
            MessageType.VehicleStatus => 34, MessageType.Fault => 8, MessageType.Telemetry => -1, _ => -2
        };
        if (length == -2) { error = CodecError.UnsupportedMessage; return false; }
        if (length >= 0 && p.Length != length) { error = CodecError.InvalidPayloadLength; return false; }
        if (type == MessageType.Telemetry)
        {
            if (p.Length == 0 || p[0] > 11 || p.Length != 1 + p[0] * 8)
            { error = CodecError.InvalidPayloadLength; return false; }
            Span<bool> seen = stackalloc bool[256]; seen.Clear();
            for (int i = 0; i < p[0]; i++)
            {
                byte id = p[1 + i * 8];
                if (seen[id]) { error = CodecError.DuplicateMeasurement; return false; }
                seen[id] = true;
            }
        }
        if (type == MessageType.MovementCommand)
        {
            var r = new PayloadReader(p[8..]);
            if (!Axis(r.I16()) || !Axis(r.I16()) || !Axis(r.I16()))
            { error = CodecError.InvalidPayload; return false; }
        }
        if ((type == MessageType.BuoyancyCommand && p[8] > 2) || (type == MessageType.Fault && p[3] > 1))
        { error = CodecError.InvalidPayload; return false; }
        error = CodecError.None; return true;
    }
    private static bool Axis(short value) => value is >= -1000 and <= 1000;
}
