using System.Text.Json;
using Xunit;

namespace Bajols.Protocol.Tests;

/// <summary>
/// Cross-language conformance tests against independent shared BCP wire fixtures.
/// </summary>
public sealed class GoldenVectorTests
{
    /// <summary>
    /// Loads independent shared wire fixtures from the linked JSON file.
    /// </summary>
    private static JsonElement[] Load()
    {
        using var document = JsonDocument.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "wire-v1.json")));
        return document.RootElement.GetProperty("vectors").EnumerateArray().Select(v => v.Clone()).ToArray();
    }

    /// <summary>
    /// Names of shared wire fixtures supplied as independent parameterized test cases.
    /// </summary>
    public static IEnumerable<object[]> VectorNames => Load().Select(v => new object[] { v.GetProperty("name").GetString()! });

    /// <summary>
    /// Reads a named fixture&apos;s expected transmitted bytes.
    /// </summary>
    internal static byte[] Wire(string name) => Convert.FromHexString(Vector(name).GetProperty("wire_hex").GetString()!);

    /// <summary>
    /// Finds the named shared protocol fixture without duplicating its expected values.
    /// </summary>
    internal static JsonElement Vector(string name) => Load().Single(v => v.GetProperty("name").GetString() == name);

    /// <summary>
    /// Resolves a fixture&apos;s transmission direction, defaulting malformed-frame samples to PC-to-vehicle.
    /// </summary>
    internal static WireDirection Direction(JsonElement v) => v.TryGetProperty("direction", out var d) && d.GetString() == "vehicle_to_pc" ? WireDirection.VehicleToPc : WireDirection.PcToVehicle;

    /// <summary>
    /// Verifies shared vector matches frame and typed contract.
    /// </summary>
    [Theory]
    [MemberData(nameof(VectorNames))]
    public void Shared_vector_matches_frame_and_typed_contract(string name)
    {
        var v = Vector(name);
        var wire = Wire(name);
        var expected = v.GetProperty("expected").GetString();
        var direction = Direction(v);
        bool success = BcpCodec.TryDecode(wire, direction, out var packet, out var error);
        if (expected == "wire_valid")
        {
            Assert.True(success, $"{name}: {error}");
            var h = v.GetProperty("header");
            Assert.Equal(new SessionId(U32(h, "boot_counter"), U32(h, "session_counter")), packet!.Session);
            Assert.Equal(U32(h, "sequence"), packet.Sequence);
            var message = ExpectedMessage((MessageType)h.GetProperty("message_type").GetByte(), v.GetProperty("fields"));
            Assert.Equivalent(message, packet.Message, strict: true);
            // Encoding starts from independently described field values, not decoded bytes.
            var independentlyConstructed = new BcpPacket(packet.Session, packet.Sequence, message);
            byte[] encoded = new byte[FrameCodec.MaxWireSize];
            Assert.True(BcpCodec.TryEncode(independentlyConstructed, direction, encoded, out int size, out error), error.ToString());
            Assert.Equal(wire, encoded[..size]);
            VerifyRaw(v, wire);
        }
        else
        {
            if (expected == "framing_valid_unknown_message")
            {
                Assert.False(success);
                Assert.Null(packet);
                Assert.Equal(CodecError.UnsupportedMessage, error);
                VerifyRaw(v, wire);
            }
            else
            {
                if (expected == "skip_unknown_measurement")
                {
                    Assert.True(success, error.ToString());
                    Assert.Empty(Assert.IsType<Telemetry>(packet!.Message).Records);
                    VerifyRaw(v, wire);
                }
                else
                {
                    Assert.False(success);
                    Assert.Null(packet);
                    var expectedError = expected switch
                    {
                        "reject_direction" => CodecError.InvalidDirection,
                        "reject_payload" => CodecError.InvalidPayload,
                        "reject_payload_length" => CodecError.InvalidPayloadLength,
                        "reject_version" => CodecError.UnsupportedVersion,
                        "reject_reserved_flags" => CodecError.ReservedFlags,
                        "reject_telemetry_duplicate_ids" => CodecError.DuplicateMeasurement,
                        "reject_crc" => CodecError.InvalidCrc,
                        "reject_frame_length" => CodecError.InvalidFrameLength,
                        "reject_cobs" => CodecError.InvalidCobs,
                        "discard_through_delimiter" => CodecError.FrameTooLarge,
                        _ => throw new InvalidOperationException($"Unhandled vector expectation: {expected}")
                    };
                    Assert.Equal(expectedError, error);
                }
            }
        }
    }

    /// <summary>
    /// Checks raw payload bytes, CRC coverage and exact frame re-encoding against the shared fixture.
    /// </summary>
    private static void VerifyRaw(JsonElement v, byte[] wire)
    {
        Assert.True(FrameCodec.TryDecode(wire, out var frame, out var error), error.ToString());
        Assert.Equal(Convert.FromHexString(v.GetProperty("payload_hex").GetString()!), frame!.Payload.ToArray());
        var input = Convert.FromHexString(v.GetProperty("crc_input_hex").GetString()!);
        Assert.Equal(v.GetProperty("crc_value").GetUInt16(), FrameCodec.ComputeCrc(input));
        var decoded = Convert.FromHexString(v.GetProperty("decoded_packet_hex").GetString()!);
        Assert.Equal(input, decoded[..^2]);
        Assert.Equal(v.GetProperty("crc_value").GetUInt16(), System.Buffers.Binary.BinaryPrimitives.ReadUInt16LittleEndian(decoded.AsSpan(decoded.Length - 2)));
        byte[] encoded = new byte[FrameCodec.MaxWireSize];
        Assert.True(FrameCodec.TryEncode(frame.MessageType, frame.Session, frame.Sequence, frame.Payload.Span, encoded, out int size, out error), error.ToString());
        Assert.Equal(wire, encoded[..size]);
    }

    /// <summary>
    /// Reads an expected unsigned 32-bit field from a JSON fixture.
    /// </summary>
    private static uint U32(JsonElement f, string n) => f.GetProperty(n).GetUInt32();

    /// <summary>
    /// Reads an expected unsigned 16-bit field from a JSON fixture.
    /// </summary>
    private static ushort U16(JsonElement f, string n) => f.GetProperty(n).GetUInt16();

    /// <summary>
    /// Reads an expected unsigned byte field from a JSON fixture.
    /// </summary>
    private static byte U8(JsonElement f, string n) => f.GetProperty(n).GetByte();

    /// <summary>
    /// Reads an expected signed 16-bit field from a JSON fixture.
    /// </summary>
    private static short I16(JsonElement f, string n) => f.GetProperty(n).GetInt16();

    /// <summary>
    /// Reads expected opaque identity bytes from a JSON fixture without UUID conversion.
    /// </summary>
    private static OpaqueId128 Id(JsonElement f, string n) => OpaqueId128.FromBytes(Convert.FromHexString(f.GetProperty(n).GetString()!));

    /// <summary>
    /// Constructs a typed expected message independently from the fixture&apos;s human-readable field values.
    /// </summary>
    private static BcpMessage ExpectedMessage(MessageType type, JsonElement f) => type switch
    {
        MessageType.Hello => new Hello(Id(f, "nonce_hex"), U8(f, "requested_version")),
        MessageType.HelloReply => new HelloReply(Id(f, "nonce_hex"), U8(f, "selected_version"), Id(f, "vehicle_id_hex")),
        MessageType.SessionConfirm => new SessionConfirm(Id(f, "nonce_hex")),
        MessageType.SessionReady => new SessionReady(U32(f, "control_generation")),
        MessageType.ControlTicket => new ControlTicket(U32(f, "ticket_id"), U32(f, "control_generation"), U16(f, "validity_ms")),
        MessageType.Heartbeat => new Heartbeat(),
        MessageType.MovementCommand => new MovementCommand(U32(f, "ticket_id"), U32(f, "control_generation"), I16(f, "forward"), I16(f, "yaw"), I16(f, "dive")),
        MessageType.Arm => new Arm(U32(f, "request_id"), U32(f, "ticket_id"), U32(f, "control_generation")),
        MessageType.Disarm => new Disarm(U32(f, "request_id")),
        MessageType.ClearFailsafe => new ClearFailsafe(U32(f, "request_id"), U32(f, "ticket_id"), U32(f, "control_generation")),
        MessageType.EmergencyStop => new EmergencyStop(U32(f, "request_id")),
        MessageType.CommandResult => new CommandResult(U32(f, "request_id"), (MessageType)U8(f, "original_message_type"), (CommandResultCode)U8(f, "result"), U32(f, "control_generation")),
        MessageType.BuoyancyCommand => new BuoyancyCommand(U32(f, "ticket_id"), U32(f, "control_generation"), (BuoyancyTarget)U8(f, "target")),
        MessageType.GetCapabilities => new GetCapabilities(),
        MessageType.Capabilities => new Capabilities(U16(f, "capability_revision"), U16(f, "supported_modes"), U8(f, "supported_axes"), U16(f, "supported_systems"), U32(f, "supported_telemetry"), U16(f, "movement_interval_ms"), U16(f, "command_timeout_ms"), U16(f, "ticket_interval_ms"), U16(f, "ticket_lifetime_ms"), U16(f, "neutral_hold_ms"), U16(f, "neutral_tolerance")),
        MessageType.VehicleStatus => new VehicleStatus(U32(f, "status_revision"), U32(f, "vehicle_uptime_ms"), (SafetyState)U8(f, "safety_state"), (MovementOwner)U8(f, "movement_owner"), (ControlMode)U8(f, "control_mode"), U32(f, "control_generation"), (ArmingBlockers)U32(f, "arming_blockers"), U16(f, "neutral_hold_progress_ms"), U16(f, "movement_age_ms"), U16(f, "movement_time_remaining_ms"), (BuoyancyAction)U8(f, "buoyancy_action"), (ActionStatus)U8(f, "buoyancy_action_status"), (SafetyCause)U8(f, "safety_cause"), (EstimatedBuoyancy)U8(f, "estimated_buoyancy"), (AuthorizedBuoyancyTarget)U8(f, "authorized_normal_target"), U16(f, "buoyancy_command_time_remaining_ms"), (ActiveRestrictions)U16(f, "active_restrictions")),
        MessageType.Telemetry => new Telemetry(f.GetProperty("records").EnumerateArray().Select(r => new TelemetryRecord((MeasurementId)U8(r, "measurement_id"), (TelemetryQuality)U8(r, "quality_flags"), U16(r, "sample_age_ms"), r.GetProperty("value").GetInt32())).ToArray()),
        MessageType.ObjectReport => new ObjectReport(U8(f, "format_revision"), U8(f, "sensor_id"), U32(f, "scan_sequence"), U16(f, "sample_age_ms"), U8(f, "sensor_state"), U8(f, "report_flags"), f.GetProperty("observations").EnumerateArray().Select(r => new ObjectObservation(U16(r, "observation_id"), U32(r, "sensor_range_mm"), U32(r, "vehicle_range_mm"), I16(r, "bearing_cdeg"), I16(r, "elevation_cdeg"), U16(r, "range_half_width_mm"), U16(r, "bearing_half_width_cdeg"), U16(r, "elevation_half_width_cdeg"), (TelemetryQuality)U8(r, "quality_flags"))).ToArray()),
        MessageType.Fault => new Fault((FaultCode)U16(f, "fault_code"), (FaultSeverity)U8(f, "severity"), U8(f, "active") != 0, U32(f, "occurrence_counter")),
        _ => throw new InvalidOperationException($"Unhandled type {type}")
    };
}
