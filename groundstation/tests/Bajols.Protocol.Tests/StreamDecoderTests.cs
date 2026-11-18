using Xunit;

namespace Bajols.Protocol.Tests;

/// <summary>
/// Tests for fragmentation, concatenation, bounded buffering and parser recovery.
/// </summary>
public sealed class StreamDecoderTests
{
    /// <summary>
    /// Verifies every fragment boundary preserves one complete packet.
    /// </summary>
    [Theory]
    [InlineData("hello", WireDirection.PcToVehicle)]
    [InlineData("movement_mixed_boundaries", WireDirection.PcToVehicle)]
    [InlineData("vehicle_status_disarmed", WireDirection.VehicleToPc)]
    public void Every_fragment_boundary_preserves_one_complete_packet(string name, WireDirection direction)
    {
        byte[] wire = GoldenVectorTests.Wire(name);
        for (int split = 0; split <= wire.Length; split++)
        {
            var decoder = new BcpStreamDecoder(direction);
            var packets = new List<BcpPacket>();
            Feed(decoder, wire[..split], packets);
            Feed(decoder, wire[split..], packets);
            Assert.Single(packets);
        }
    }

    /// <summary>
    /// Verifies concatenated packets and empty delimiters.
    /// </summary>
    [Fact]
    public void Concatenated_packets_and_empty_delimiters()
    {
        var decoder = new BcpStreamDecoder(WireDirection.PcToVehicle);
        var packets = new List<BcpPacket>();
        Feed(decoder, [0, .. GoldenVectorTests.Wire("hello"), 0, .. GoldenVectorTests.Wire("emergency_stop"), 0], packets);
        Assert.Equal(2, packets.Count);
        Assert.IsType<Hello>(packets[0].Message);
        Assert.IsType<EmergencyStop>(packets[1].Message);
    }

    /// <summary>
    /// Verifies oversized input discards until delimiter and recovers.
    /// </summary>
    [Fact]
    public void Oversized_input_discards_until_delimiter_and_recovers()
    {
        var decoder = new BcpStreamDecoder(WireDirection.PcToVehicle);
        for (int i = 0; i < 100000; i++)
        {
            Assert.Equal(StreamDecodeResult.NeedMoreData, decoder.Push(1, out _, out _));
        }

        // A valid frame without an intervening delimiter is still part of the oversized frame.
        byte[] wire = GoldenVectorTests.Wire("disarm");
        foreach (byte b in wire[..^1])
        {
            Assert.Equal(StreamDecodeResult.NeedMoreData, decoder.Push(b, out _, out _));
        }

        Assert.Equal(StreamDecodeResult.Rejected, decoder.Push(0, out var packet, out var error));
        Assert.Null(packet);
        Assert.Equal(CodecError.FrameTooLarge, error);
        var packets = new List<BcpPacket>();
        Feed(decoder, wire, packets);
        Assert.Single(packets);
    }

    /// <summary>
    /// Verifies malformed frame recovery and reset.
    /// </summary>
    [Fact]
    public void Malformed_frame_recovery_and_reset()
    {
        var decoder = new BcpStreamDecoder(WireDirection.PcToVehicle);
        byte[] bad = GoldenVectorTests.Wire("reject_corrupt_crc");
        foreach (byte b in bad[..^1])
        {
            decoder.Push(b, out _, out _);
        }

        Assert.Equal(StreamDecodeResult.Rejected, decoder.Push(0, out _, out var error));
        Assert.Equal(CodecError.InvalidCrc, error);
        var packets = new List<BcpPacket>();
        Feed(decoder, GoldenVectorTests.Wire("disarm"), packets);
        Assert.Single(packets);
        decoder.Push(0xff, out _, out _);
        decoder.Reset();
        packets.Clear();
        Feed(decoder, GoldenVectorTests.Wire("disarm"), packets);
        Assert.Single(packets);
    }

    /// <summary>
    /// Verifies maximum size frame is not reported as buffer overflow.
    /// </summary>
    [Fact]
    public void Maximum_size_frame_is_not_reported_as_buffer_overflow()
    {
        var decoder = new BcpStreamDecoder(WireDirection.PcToVehicle);
        byte[] wire = GoldenVectorTests.Wire("framing_maximum_transport_only");
        Assert.Equal(116, wire.Length);
        foreach (byte b in wire[..^1])
        {
            Assert.Equal(StreamDecodeResult.NeedMoreData, decoder.Push(b, out _, out _));
        }

        Assert.Equal(StreamDecodeResult.Rejected, decoder.Push(0, out _, out var error));
        Assert.Equal(CodecError.UnsupportedMessage, error);
    }

    /// <summary>
    /// Feeds a valid fragment to the stream decoder and collects complete packets while rejecting unexpected parser failures.
    /// </summary>
    private static void Feed(BcpStreamDecoder decoder, byte[] bytes, List<BcpPacket> packets)
    {
        foreach (byte b in bytes)
        {
            var result = decoder.Push(b, out var packet, out var error);
            Assert.NotEqual(StreamDecodeResult.Rejected, result);
            Assert.Equal(CodecError.None, error);
            if (result == StreamDecodeResult.Packet)
            {
                packets.Add(Assert.IsType<BcpPacket>(packet));
            }
        }
    }
}
