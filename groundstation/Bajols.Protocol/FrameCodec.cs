using System.Buffers.Binary;

namespace Bajols.Protocol;

/// <summary>Structurally validated frame. Its payload owns a bounded copy, independent of receive buffers.</summary>
public sealed record BcpFrame(byte MessageType, SessionId Session, uint Sequence, ReadOnlyMemory<byte> Payload);

/// <summary>Stateless envelope codec. Unknown message types are permitted here, not in BcpCodec.</summary>
public static class FrameCodec
{
    public const byte Version = 1;
    public const int HeaderSize = 16;
    public const int MaxPayloadSize = 96;
    public const int MaxDecodedSize = 114;
    public const int MaxWireSize = 116;

    public static ushort ComputeCrc(ReadOnlySpan<byte> data)
    {
        ushort crc = 0xFFFF;
        foreach (byte value in data)
        {
            crc ^= (ushort)(value << 8);
            for (int bit = 0; bit < 8; bit++)
                crc = (ushort)((crc & 0x8000) != 0 ? (crc << 1) ^ 0x1021 : crc << 1);
        }
        return crc;
    }

    /// <summary>Encodes one frame including its trailing zero. On failure written is zero.</summary>
    public static bool TryEncode(byte type, SessionId session, uint sequence, ReadOnlySpan<byte> payload,
        Span<byte> destination, out int written, out CodecError error)
    {
        written = 0;
        if (payload.Length > MaxPayloadSize) { error = CodecError.FrameTooLarge; return false; }
        Span<byte> decoded = stackalloc byte[MaxDecodedSize];
        decoded[0] = Version; decoded[1] = type; decoded[2] = 0;
        BinaryPrimitives.WriteUInt32LittleEndian(decoded[3..], session.BootCounter);
        BinaryPrimitives.WriteUInt32LittleEndian(decoded[7..], session.SessionCounter);
        BinaryPrimitives.WriteUInt32LittleEndian(decoded[11..], sequence);
        decoded[15] = (byte)payload.Length;
        payload.CopyTo(decoded[HeaderSize..]);
        int size = HeaderSize + payload.Length;
        BinaryPrimitives.WriteUInt16LittleEndian(decoded[size..], ComputeCrc(decoded[..size]));
        size += 2;

        // Packets are shorter than 254 bytes: exactly one COBS code per zero-separated block.
        Span<byte> encoded = stackalloc byte[MaxWireSize];
        int codeIndex = 0, index = 1; byte code = 1;
        foreach (byte value in decoded[..size])
        {
            if (value == 0) { encoded[codeIndex] = code; codeIndex = index++; code = 1; }
            else { encoded[index++] = value; code++; }
        }
        encoded[codeIndex] = code; encoded[index++] = 0;
        if (destination.Length < index) { error = CodecError.DestinationTooSmall; return false; }
        encoded[..index].CopyTo(destination); written = index; error = CodecError.None; return true;
    }

    /// <summary>Decodes exactly one delimited frame; performs no session or authorization checks.</summary>
    public static bool TryDecode(ReadOnlySpan<byte> wire, out BcpFrame? frame, out CodecError error)
    {
        frame = null;
        if (wire.Length > MaxWireSize) { error = CodecError.FrameTooLarge; return false; }
        if (wire.Length < 2 || wire[^1] != 0 || wire[..^1].Contains((byte)0))
        { error = CodecError.InvalidDelimiter; return false; }
        Span<byte> decoded = stackalloc byte[MaxDecodedSize];
        int input = 0, output = 0, end = wire.Length - 1;
        while (input < end)
        {
            byte code = wire[input++];
            int count = code - 1;
            if (input + count > end) { error = CodecError.InvalidCobs; return false; }
            if (output + count > decoded.Length) { error = CodecError.FrameTooLarge; return false; }
            wire.Slice(input, count).CopyTo(decoded[output..]); input += count; output += count;
            if (code != 255 && input < end)
            {
                if (output == decoded.Length) { error = CodecError.FrameTooLarge; return false; }
                decoded[output++] = 0;
            }
        }
        if (output < HeaderSize + 2 || decoded[15] > MaxPayloadSize || output != HeaderSize + decoded[15] + 2)
        { error = CodecError.InvalidFrameLength; return false; }
        if (ComputeCrc(decoded[..(output - 2)]) != BinaryPrimitives.ReadUInt16LittleEndian(decoded[(output - 2)..]))
        { error = CodecError.InvalidCrc; return false; }
        if (decoded[0] != Version) { error = CodecError.UnsupportedVersion; return false; }
        if (decoded[2] != 0) { error = CodecError.ReservedFlags; return false; }
        frame = new(decoded[1], new(BinaryPrimitives.ReadUInt32LittleEndian(decoded[3..]),
            BinaryPrimitives.ReadUInt32LittleEndian(decoded[7..])), BinaryPrimitives.ReadUInt32LittleEndian(decoded[11..]),
            decoded.Slice(HeaderSize, decoded[15]).ToArray());
        error = CodecError.None; return true;
    }
}
