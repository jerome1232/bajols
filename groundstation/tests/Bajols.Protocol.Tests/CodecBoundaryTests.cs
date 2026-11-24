using System.Buffers.Binary;
using Xunit;

namespace Bajols.Protocol.Tests;

/// <summary>
/// Boundary and malformed-input tests for the transport-independent wire codec.
/// </summary>
public sealed class CodecBoundaryTests
{
    /// <summary>
    /// Verifies crc reference check.
    /// </summary>
    [Fact]
    public void Crc_reference_check() => Assert.Equal((ushort)0x29B1, FrameCodec.ComputeCrc("123456789"u8));

    /// <summary>
    /// Verifies sender rejects invalid effort without writing.
    /// </summary>
    [Theory]
    [InlineData(-1001)]
    [InlineData(1001)]
    [InlineData(short.MinValue)]
    [InlineData(short.MaxValue)]
    public void Sender_rejects_invalid_effort_without_writing(short effort)
    {
        byte[] buffer = Enumerable.Repeat((byte)0xA5, 116).ToArray();
        Assert.False(BcpCodec.TryEncode(new(new(1, 1), 1, new MovementCommand(1, 1, effort, 0, 0)), WireDirection.PcToVehicle, buffer, out int size, out var error));
        Assert.Equal(0, size);
        Assert.Equal(CodecError.InvalidPayload, error);
        Assert.All(buffer, b => Assert.Equal(0xA5, b));
    }

    /// <summary>
    /// Verifies small output buffer is unchanged.
    /// </summary>
    [Fact]
    public void Small_output_buffer_is_unchanged()
    {
        byte[] buffer = [0xA5, 0xA5];
        Assert.False(BcpCodec.TryEncode(new(new(1, 1), 1, new Disarm(1)), WireDirection.PcToVehicle, buffer, out int size, out var error));
        Assert.Equal(0, size);
        Assert.Equal(CodecError.DestinationTooSmall, error);
        Assert.Equal(new byte[] { 0xA5, 0xA5 }, buffer);
    }

    /// <summary>
    /// Verifies opaque identity preserves byte order.
    /// </summary>
    [Fact]
    public void Opaque_identity_preserves_byte_order()
    {
        byte[] bytes = Enumerable.Range(0, 16).Select(i => (byte)i).ToArray();
        var id = OpaqueId128.FromBytes(bytes);
        byte[] output = new byte[16];
        id.WriteBytes(output);
        Assert.Equal(bytes, output);
        Assert.Throws<ArgumentException>(() => OpaqueId128.FromBytes(new byte[15]));
    }

    /// <summary>
    /// Verifies wrong direction rejects even otherwise valid stop.
    /// </summary>
    [Fact]
    public void Wrong_direction_rejects_even_otherwise_valid_stop()
    {
        Assert.False(BcpCodec.TryDecode(GoldenVectorTests.Wire("emergency_stop"), WireDirection.VehicleToPc, out var packet, out var error));
        Assert.Null(packet);
        Assert.Equal(CodecError.InvalidDirection, error);
        Assert.False(BcpCodec.TryEncode(new(new(1, 2), 1, new EmergencyStop(1)), WireDirection.VehicleToPc, new byte[116], out _, out error));
        Assert.Equal(CodecError.InvalidDirection, error);
    }

    /// <summary>
    /// Verifies raw payload bounds and zero blocks roundtrip.
    /// </summary>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    [InlineData(11)]
    [InlineData(96)]
    public void Raw_payload_bounds_and_zero_blocks_roundtrip(int length)
    {
        foreach (byte fill in new byte[]
        {
            0,
            1,
            255
        }

        )
        {
            byte[] payload = Enumerable.Repeat(fill, length).ToArray();
            byte[] wire = Raw(0x7f, payload);
            Assert.Equal(length + 20, wire.Length);
            Assert.True(FrameCodec.TryDecode(wire, out var frame, out var error), error.ToString());
            Assert.Equal(payload, frame!.Payload.ToArray());
        }
    }

    /// <summary>
    /// Verifies oversized payload is not encoded.
    /// </summary>
    [Fact]
    public void Oversized_payload_is_not_encoded()
    {
        Assert.False(FrameCodec.TryEncode(0x7f, new(1, 2), 1, new byte[97], new byte[116], out int n, out var e));
        Assert.Equal(0, n);
        Assert.Equal(CodecError.FrameTooLarge, e);
    }

    /// <summary>
    /// Verifies decoded payload owns its bytes.
    /// </summary>
    [Fact]
    public void Decoded_payload_owns_its_bytes()
    {
        byte[] wire = GoldenVectorTests.Wire("movement_mixed_boundaries");
        Assert.True(FrameCodec.TryDecode(wire, out var frame, out _));
        byte[] original = frame!.Payload.ToArray();
        Array.Fill(wire, (byte)0);
        Assert.Equal(original, frame.Payload.ToArray());
    }

    /// <summary>
    /// Verifies every bit corruption in decoded packet is detected.
    /// </summary>
    [Fact]
    public void Every_bit_corruption_in_decoded_packet_is_detected()
    {
        byte[] packet = Convert.FromHexString(GoldenVectorTests.Vector("movement_mixed_boundaries").GetProperty("decoded_packet_hex").GetString()!);
        for (int i = 0; i < packet.Length; i++)
        {
            for (int bit = 0; bit < 8; bit++)
            {
                var corrupted = (byte[])packet.Clone();
                corrupted[i] ^= (byte)(1 << bit);
                Assert.False(FrameCodec.TryDecode(EncodeDecodedForCorruption(corrupted), out var decoded, out _));
                Assert.Null(decoded);
            }
        }
    }

    /// <summary>
    /// Verifies truncation and extra delimiters are rejected.
    /// </summary>
    [Theory]
    [InlineData("hello")]
    [InlineData("movement_mixed_boundaries")]
    [InlineData("telemetry_depth_temperature")]
    public void Truncation_and_extra_delimiters_are_rejected(string name)
    {
        byte[] wire = GoldenVectorTests.Wire(name);
        for (int n = 0; n < wire.Length; n++)
        {
            Assert.False(FrameCodec.TryDecode(wire.AsSpan(0, n), out _, out _));
        }

        Assert.False(FrameCodec.TryDecode([.. wire, 0], out _, out _));
        Assert.False(FrameCodec.TryDecode([.. wire, .. wire], out _, out _));
    }

    /// <summary>
    /// Verifies unknown received status is preserved and flagged not ready.
    /// </summary>
    [Fact]
    public void Unknown_received_status_is_preserved_and_flagged_not_ready()
    {
        Assert.True(FrameCodec.TryDecode(GoldenVectorTests.Wire("vehicle_status_disarmed"), out var frame, out _));
        foreach (int offset in new[]
        {
            8,
            9,
            10,
            25,
            26,
            27,
            28,
            29
        }

        )
        {
            var payload = frame!.Payload.ToArray();
            payload[offset] = 0xfe;
            Assert.True(BcpCodec.TryDecode(Raw(0x30, payload), WireDirection.VehicleToPc, out var packet, out var error), error.ToString());
            var status = Assert.IsType<VehicleStatus>(packet!.Message);
            Assert.True(status.HasUnknownControlValues);
            Assert.False(BcpCodec.TryEncode(packet, WireDirection.VehicleToPc, new byte[116], out _, out error));
        }

        var blockers = frame!.Payload.ToArray();
        BinaryPrimitives.WriteUInt32LittleEndian(blockers.AsSpan(15), 1u << 31);
        Assert.True(BcpCodec.TryDecode(Raw(0x30, blockers), WireDirection.VehicleToPc, out var blocked, out _));
        Assert.True(Assert.IsType<VehicleStatus>(blocked!.Message).HasUnknownControlValues);
    }

    /// <summary>
    /// Verifies unknown capability bits remain available for forward compatibility.
    /// </summary>
    [Fact]
    public void Unknown_capability_bits_remain_available_for_forward_compatibility()
    {
        Assert.True(FrameCodec.TryDecode(GoldenVectorTests.Wire("capabilities"), out var frame, out _));
        var payload = frame!.Payload.ToArray();
        BinaryPrimitives.WriteUInt16LittleEndian(payload.AsSpan(2), 0x8001);
        Assert.True(BcpCodec.TryDecode(Raw(0x20, payload), WireDirection.VehicleToPc, out var packet, out _));
        Assert.Equal((ushort)0x8001, Assert.IsType<Capabilities>(packet!.Message).SupportedModes);
        Assert.False(BcpCodec.TryEncode(packet, WireDirection.VehicleToPc, new byte[116], out _, out _));
    }

    /// <summary>
    /// Verifies unknown fault and severity are preserved but not emitted.
    /// </summary>
    [Fact]
    public void Unknown_fault_and_severity_are_preserved_but_not_emitted()
    {
        byte[] payload = new byte[8];
        BinaryPrimitives.WriteUInt16LittleEndian(payload, 65000);
        payload[2] = 250;
        payload[3] = 1;
        Assert.True(BcpCodec.TryDecode(Raw(0x32, payload), WireDirection.VehicleToPc, out var packet, out _));
        var fault = Assert.IsType<Fault>(packet!.Message);
        Assert.Equal((FaultCode)65000, fault.FaultCode);
        Assert.Equal((FaultSeverity)250, fault.Severity);
        Assert.False(BcpCodec.TryEncode(packet, WireDirection.VehicleToPc, new byte[116], out _, out _));
        payload[3] = 2;
        Assert.False(BcpCodec.TryDecode(Raw(0x32, payload), WireDirection.VehicleToPc, out _, out var error));
        Assert.Equal(CodecError.InvalidPayload, error);
    }

    /// <summary>
    /// Verifies telemetry unknown ids skip but unknown quality is preserved unusable.
    /// </summary>
    [Fact]
    public void Telemetry_unknown_ids_skip_but_unknown_quality_is_preserved_unusable()
    {
        byte[] payload = new byte[17];
        payload[0] = 2;
        payload[1] = 0xfe;
        payload[9] = 1;
        payload[10] = 0x81;
        Assert.True(BcpCodec.TryDecode(Raw(0x31, payload), WireDirection.VehicleToPc, out var packet, out _));
        var record = Assert.Single(Assert.IsType<Telemetry>(packet!.Message).Records);
        Assert.Equal(MeasurementId.Depth, record.MeasurementId);
        Assert.False(record.HasKnownQualityFlags);
        Assert.False(BcpCodec.TryEncode(packet, WireDirection.VehicleToPc, new byte[116], out _, out _));
        payload[9] = 0xfe;
        Assert.False(BcpCodec.TryDecode(Raw(0x31, payload), WireDirection.VehicleToPc, out _, out var error));
        Assert.Equal(CodecError.DuplicateMeasurement, error); // Duplicates checked before skipping unknown IDs.
    }

    /// <summary>
    /// Verifies telemetry count and integer extremes are bounded.
    /// </summary>
    [Fact]
    public void Telemetry_count_and_integer_extremes_are_bounded()
    {
        var records = Enumerable.Range(1, 8).Select(i => new TelemetryRecord((MeasurementId)i, TelemetryQuality.None, 65535, i % 2 == 0 ? int.MinValue : int.MaxValue)).ToArray();
        byte[] wire = new byte[116];
        var packet = new BcpPacket(new(1, 2), uint.MaxValue, new Telemetry(records));
        Assert.True(BcpCodec.TryEncode(packet, WireDirection.VehicleToPc, wire, out int n, out _));
        Assert.True(BcpCodec.TryDecode(wire.AsSpan(0, n), WireDirection.VehicleToPc, out var parsed, out _));
        Assert.Equal(records, Assert.IsType<Telemetry>(parsed!.Message).Records);
        Assert.False(BcpCodec.TryEncode(packet with
        {
            Message = new Telemetry(Enumerable.Repeat(records[0], 12).ToArray())
        }, WireDirection.VehicleToPc, wire, out _, out _));
        Assert.False(BcpCodec.TryEncode(packet with
        {
            Message = new Telemetry([records[0], records[0]])
        }, WireDirection.VehicleToPc, wire, out _, out var error));
        Assert.Equal(CodecError.DuplicateMeasurement, error);
    }

    /// <summary>
    /// Verifies decoder never throws on bounded arbitrary wire input.
    /// </summary>
    [Fact]
    public void Decoder_never_throws_on_bounded_arbitrary_wire_input()
    {
        var random = new Random(124567);
        for (int i = 0; i < 10000; i++)
        {
            var bytes = new byte[random.Next(0, 140)];
            random.NextBytes(bytes);
            if (bytes.Length > 0)
            {
                bytes[^1] = 0;
            }

            BcpCodec.TryDecode(bytes, WireDirection.VehicleToPc, out _, out _);
        }
    }

    /// <summary>
    /// Verifies well framed arbitrary payloads do not throw.
    /// </summary>
    [Fact]
    public void Well_framed_arbitrary_payloads_do_not_throw()
    {
        var random = new Random(8123);
        foreach (var type in Enum.GetValues<MessageType>())
        {
            for (int length = 0; length <= 96; length++)
            {
                byte[] payload = new byte[length];
                random.NextBytes(payload);
                byte[] wire = Raw((byte)type, payload);
                BcpCodec.TryDecode(wire, WireDirection.PcToVehicle, out _, out _);
                BcpCodec.TryDecode(wire, WireDirection.VehicleToPc, out _, out _);
            }
        }
    }

    /// <summary>
    /// Verifies known messages reject short and long payloads before reading fields.
    /// </summary>
    [Fact]
    public void Known_messages_reject_short_and_long_payloads_before_reading_fields()
    {
        foreach (object[] row in GoldenVectorTests.VectorNames)
        {
            var vector = GoldenVectorTests.Vector((string)row[0]);
            if (vector.GetProperty("expected").GetString() != "wire_valid")
            {
                continue;
            }

            byte type = vector.GetProperty("header").GetProperty("message_type").GetByte();
            byte[] payload = Convert.FromHexString(vector.GetProperty("payload_hex").GetString()!);
            var direction = GoldenVectorTests.Direction(vector);
            Assert.False(BcpCodec.TryDecode(Raw(type, [.. payload, 0]), direction, out _, out var error));
            Assert.Equal(CodecError.InvalidPayloadLength, error);
            if (payload.Length == 0)
            {
                continue;
            }

            Assert.False(BcpCodec.TryDecode(Raw(type, payload[..^1]), direction, out _, out error));
            Assert.Equal(CodecError.InvalidPayloadLength, error);
        }
    }

    /// <summary>
    /// Builds a raw frame for payload-validation tests without applying typed sender validation.
    /// </summary>
    internal static byte[] Raw(byte type, byte[] payload)
    {
        byte[] wire = new byte[116];
        Assert.True(FrameCodec.TryEncode(type, new(1, 2), 7, payload, wire, out int n, out _));
        return wire[..n];
    }

    /// <summary>
    /// Frames deliberately corrupted decoded bytes without repairing their CRC.
    /// </summary>
    private static byte[] EncodeDecodedForCorruption(byte[] data)
    {
        var result = new List<byte>();
        int begin = 0;
        for (int i = 0; i <= data.Length; i++)
        {
            if (i < data.Length && data[i] != 0)
            {
                continue;
            }

            result.Add((byte)(i - begin + 1));
            result.AddRange(data[begin..i]);
            begin = i + 1;
        }

        result.Add(0);
        return result.ToArray();
    }
}
