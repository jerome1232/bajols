using System.Buffers.Binary;

namespace Bajols.Protocol;

/// <summary>
/// Reads explicit little-endian values from an already length-validated payload.
/// </summary>
/// <param name="data">Bounded payload bytes read or written in explicit transmission order.</param>
internal ref struct PayloadReader(ReadOnlySpan<byte> data)
{
    /// <summary>
    /// Unread portion of the already length-validated payload.
    /// </summary>
    private ReadOnlySpan<byte> remaining = data;

    /// <summary>
    /// Reads an unsigned byte and advances the validated payload cursor.
    /// </summary>
    public byte U8()
    {
        byte v = remaining[0];
        remaining = remaining[1..];
        return v;
    }

    /// <summary>
    /// Reads a little-endian unsigned 16-bit value and advances the payload cursor.
    /// </summary>
    public ushort U16()
    {
        ushort v = BinaryPrimitives.ReadUInt16LittleEndian(remaining);
        remaining = remaining[2..];
        return v;
    }

    /// <summary>
    /// Reads a little-endian unsigned 32-bit value and advances the payload cursor.
    /// </summary>
    public uint U32()
    {
        uint v = BinaryPrimitives.ReadUInt32LittleEndian(remaining);
        remaining = remaining[4..];
        return v;
    }

    /// <summary>
    /// Reads a little-endian signed 16-bit value without changing its bit representation.
    /// </summary>
    public short I16() => unchecked((short)U16());

    /// <summary>
    /// Reads a little-endian signed 32-bit value without changing its bit representation.
    /// </summary>
    public int I32() => unchecked((int)U32());

    /// <summary>
    /// Reads an opaque 16-byte identity without UUID byte reordering.
    /// </summary>
    public OpaqueId128 Id()
    {
        var v = OpaqueId128.FromBytes(remaining[..16]);
        remaining = remaining[16..];
        return v;
    }
}

/// <summary>
/// Writes explicit little-endian values into bounded caller-owned payload storage.
/// </summary>
/// <param name="data">Bounded payload bytes read or written in explicit transmission order.</param>
internal ref struct PayloadWriter(Span<byte> data)
{
    /// <summary>
    /// Unused portion of the bounded caller-owned payload buffer.
    /// </summary>
    private Span<byte> remaining = data;

    /// <summary>
    /// Number of payload bytes written so far.
    /// </summary>
    public int Written
    {
        get; private set;
    }

    /// <summary>
    /// Writes an unsigned byte and advances the bounded payload cursor.
    /// </summary>
    public void U8(byte v)
    {
        remaining[0] = v;
        remaining = remaining[1..];
        Written++;
    }

    /// <summary>
    /// Writes an unsigned 16-bit value in little-endian order.
    /// </summary>
    public void U16(ushort v)
    {
        BinaryPrimitives.WriteUInt16LittleEndian(remaining, v);
        remaining = remaining[2..];
        Written += 2;
    }

    /// <summary>
    /// Writes an unsigned 32-bit value in little-endian order.
    /// </summary>
    public void U32(uint v)
    {
        BinaryPrimitives.WriteUInt32LittleEndian(remaining, v);
        remaining = remaining[4..];
        Written += 4;
    }

    /// <summary>
    /// Writes a signed 16-bit value using its two&apos;s-complement representation.
    /// </summary>
    public void I16(short v) => U16(unchecked((ushort)v));

    /// <summary>
    /// Writes a signed 32-bit value using its two&apos;s-complement representation.
    /// </summary>
    public void I32(int v) => U32(unchecked((uint)v));

    /// <summary>
    /// Writes all 16 opaque identity bytes without UUID byte reordering.
    /// </summary>
    public void Id(OpaqueId128 v)
    {
        v.WriteBytes(remaining);
        remaining = remaining[16..];
        Written += 16;
    }
}
