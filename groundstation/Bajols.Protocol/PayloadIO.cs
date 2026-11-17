using System.Buffers.Binary;

namespace Bajols.Protocol;

// Used only after exact payload length/count validation; never reads untrusted lengths unchecked.
internal ref struct PayloadReader(ReadOnlySpan<byte> data)
{
    private ReadOnlySpan<byte> remaining = data;
    public byte U8() { byte v = remaining[0]; remaining = remaining[1..]; return v; }
    public ushort U16() { ushort v = BinaryPrimitives.ReadUInt16LittleEndian(remaining); remaining = remaining[2..]; return v; }
    public uint U32() { uint v = BinaryPrimitives.ReadUInt32LittleEndian(remaining); remaining = remaining[4..]; return v; }
    public short I16() => unchecked((short)U16());
    public int I32() => unchecked((int)U32());
    public OpaqueId128 Id() { var v = OpaqueId128.FromBytes(remaining[..16]); remaining = remaining[16..]; return v; }
}
internal ref struct PayloadWriter(Span<byte> data)
{
    private Span<byte> remaining = data;
    public int Written { get; private set; }
    public void U8(byte v) { remaining[0] = v; remaining = remaining[1..]; Written++; }
    public void U16(ushort v) { BinaryPrimitives.WriteUInt16LittleEndian(remaining, v); remaining = remaining[2..]; Written += 2; }
    public void U32(uint v) { BinaryPrimitives.WriteUInt32LittleEndian(remaining, v); remaining = remaining[4..]; Written += 4; }
    public void I16(short v) => U16(unchecked((ushort)v));
    public void I32(int v) => U32(unchecked((uint)v));
    public void Id(OpaqueId128 v) { v.WriteBytes(remaining); remaining = remaining[16..]; Written += 16; }
}
