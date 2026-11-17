using Bajols.Core;

namespace Bajols.Communication;

/// <summary>
/// Byte-stream boundary for future serial/network adapters. Implementations must
/// honor cancellation and bound internal buffering. Opening never arms a vehicle.
/// </summary>
public interface IByteTransport : IAsyncDisposable
{
    ConnectionState State { get; }
    ValueTask OpenAsync(CancellationToken cancellationToken);
    ValueTask CloseAsync(CancellationToken cancellationToken);

    /// <summary>Reads into caller-owned storage; zero indicates end of stream.</summary>
    ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken);

    /// <summary>Writes all supplied bytes or throws; does not retain the buffer.</summary>
    ValueTask WriteAsync(ReadOnlyMemory<byte> buffer, CancellationToken cancellationToken);
}
