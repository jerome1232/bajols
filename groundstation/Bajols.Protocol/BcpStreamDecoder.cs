namespace Bajols.Protocol;

/// <summary>
/// Outcome of consuming one byte without conflating incomplete input with rejected frames.
/// </summary>
public enum StreamDecodeResult
{
    /// <summary>
    /// No complete frame is available yet.
    /// </summary>
    NeedMoreData,

    /// <summary>
    /// A complete typed packet is available for separate stateful authorization.
    /// </summary>
    Packet,

    /// <summary>
    /// A completed frame was rejected without producing an executable command.
    /// </summary>
    Rejected
}

/// <summary>
/// One bounded buffer, no internal message queue. Not thread safe; use one instance per
/// receive stream. Call Reset on disconnect. Push each byte; consume results immediately.
/// Oversized input is discarded until its delimiter, then one rejection is reported.
/// </summary>
/// <param name="direction">Direction accepted by this receive stream.</param>
public sealed class BcpStreamDecoder(WireDirection direction)
{
    /// <summary>
    /// Fixed-size storage for one encoded frame and its delimiter.
    /// </summary>
    private readonly byte[] buffer = new byte[FrameCodec.MaxWireSize];

    /// <summary>
    /// Number of encoded non-delimiter bytes currently buffered.
    /// </summary>
    private int count;

    /// <summary>
    /// Whether an oversized frame is being discarded through its next delimiter.
    /// </summary>
    private bool discarding;

    /// <summary>
    /// Discards partial frame state when a stream disconnects or resynchronizes.
    /// </summary>
    public void Reset()
    {
        count = 0;
        discarding = false;
    }

    /// <summary>
    /// Consumes one wire byte and reports a complete packet or a bounded-frame rejection at its delimiter.
    /// </summary>
    public StreamDecodeResult Push(byte value, out BcpPacket? packet, out CodecError error)
    {
        packet = null;
        error = CodecError.None;
        if (value == 0)
        {
            if (discarding)
            {
                Reset();
                error = CodecError.FrameTooLarge;
                return StreamDecodeResult.Rejected;
            }

            if (count == 0)
            {
                return StreamDecodeResult.NeedMoreData;
            }

            buffer[count] = 0;
            int length = count + 1;
            count = 0;
            return BcpCodec.TryDecode(buffer.AsSpan(0, length), direction, out packet, out error) ? StreamDecodeResult.Packet : StreamDecodeResult.Rejected;
        }

        if (!discarding)
        {
            if (count == FrameCodec.MaxWireSize - 1)
            {
                discarding = true;
                count = 0;
            }
            else
            {
                buffer[count++] = value;
            }
        }

        return StreamDecodeResult.NeedMoreData;
    }
}
