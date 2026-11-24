using System.Buffers.Binary;

namespace Bajols.Protocol;

/// <summary>Validates the bounded object payload before reading typed records.</summary>
internal static class ObjectReportValidation
{
    /// <summary>Checks exact length, supported revision, source, state, flags and record values.</summary>
    internal static bool Validate(ReadOnlySpan<byte> p, out CodecError error)
    {
        error = CodecError.InvalidPayloadLength;
        if (p.Length < 11 || p[10] > 4 || p.Length != 11 + 21 * p[10])
        {
            return false;
        }

        error = CodecError.InvalidPayload;
        if (p[0] != 1 || p[1] is 0 or 255 || p[8] > 3 || (p[9] & ~3) != 0 ||
            (p[8] != 1 && (p[10] != 0 || (p[9] & 1) != 0)))
        {
            return false;
        }

        for (int i = 0; i < p[10]; i++)
        {
            var record = p.Slice(11 + i * 21, 21);
            ushort id = BinaryPrimitives.ReadUInt16LittleEndian(record);
            short bearing = BinaryPrimitives.ReadInt16LittleEndian(record[10..]);
            short elevation = BinaryPrimitives.ReadInt16LittleEndian(record[12..]);
            ushort bearingWidth = BinaryPrimitives.ReadUInt16LittleEndian(record[16..]);
            ushort elevationWidth = BinaryPrimitives.ReadUInt16LittleEndian(record[18..]);
            if (id == 0 || BinaryPrimitives.ReadUInt32LittleEndian(record[2..]) == uint.MaxValue ||
                BinaryPrimitives.ReadUInt32LittleEndian(record[6..]) == uint.MaxValue ||
                bearing is < -18000 or > 17999 || (elevation != short.MaxValue && (elevation is < -9000 or > 9000)) ||
                (bearingWidth != ushort.MaxValue && bearingWidth > 18000) ||
                (elevationWidth != ushort.MaxValue && elevationWidth > 9000) ||
                (record[20] & ~15) != 0 || ((record[20] & 2) != (p[9] & 2)))
            {
                return false;
            }

            for (int j = 0; j < i; j++)
            {
                if (id == BinaryPrimitives.ReadUInt16LittleEndian(p[(11 + j * 21)..]))
                {
                    return false;
                }
            }
        }

        error = CodecError.None;
        return true;
    }
}
