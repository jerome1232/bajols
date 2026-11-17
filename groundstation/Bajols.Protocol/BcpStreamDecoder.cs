namespace Bajols.Protocol;

public enum StreamDecodeResult { NeedMoreData, Packet, Rejected }

/// <summary>
/// One bounded buffer, no internal message queue. Not thread safe; use one instance per
/// receive stream. Call Reset on disconnect. Push each byte; consume results immediately.
/// Oversized input is discarded until its delimiter, then one rejection is reported.
/// </summary>
public sealed class BcpStreamDecoder(WireDirection direction)
{
    private readonly byte[] buffer = new byte[FrameCodec.MaxWireSize];
    private int count;
    private bool discarding;

    public void Reset() { count = 0; discarding = false; }

    public StreamDecodeResult Push(byte value, out BcpPacket? packet, out CodecError error)
    {
        packet = null; error = CodecError.None;
        if (value == 0)
        {
            if (discarding) { Reset(); error = CodecError.FrameTooLarge; return StreamDecodeResult.Rejected; }
            if (count == 0) return StreamDecodeResult.NeedMoreData;
            buffer[count] = 0;
            int length = count + 1; count = 0;
            return BcpCodec.TryDecode(buffer.AsSpan(0, length), direction, out packet, out error)
                ? StreamDecodeResult.Packet : StreamDecodeResult.Rejected;
        }
        if (!discarding)
        {
            if (count == FrameCodec.MaxWireSize - 1) { discarding = true; count = 0; }
            else buffer[count++] = value;
        }
        return StreamDecodeResult.NeedMoreData;
    }
}
