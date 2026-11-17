using System.IO.Ports;
using Bajols.Core;

namespace Bajols.Communication;

/// <summary>Finite synchronous driver operations run off the UI thread. Cancellation closes the connection.</summary>
public sealed class SerialByteTransport : IByteTransport
{
    private readonly SerialPort port;
    private readonly SemaphoreSlim readGate = new(1), writeGate = new(1);
    public ConnectionState State { get; private set; } = ConnectionState.Disconnected;
    public SerialByteTransport(string name, int readTimeoutMs = 100, int writeTimeoutMs = 250, int bufferBytes = 4096)
    {
        if (string.IsNullOrWhiteSpace(name) || readTimeoutMs is < 10 or > 200 || writeTimeoutMs is < 10 or > 300 || bufferBytes is < 256 or > 65536)
            throw new ArgumentException("Invalid bounded serial settings.");
        port = new(name, 115200, Parity.None, 8, StopBits.One) {
            Handshake = Handshake.None, DtrEnable = false, RtsEnable = false,
            ReadTimeout = readTimeoutMs, WriteTimeout = writeTimeoutMs, ReadBufferSize = bufferBytes, WriteBufferSize = bufferBytes };
    }
    public async ValueTask OpenAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested(); State = ConnectionState.Connecting;
        using var registration = cancellationToken.Register(CancelConnection);
        try { await Task.Run(port.Open, cancellationToken); cancellationToken.ThrowIfCancellationRequested(); State = ConnectionState.Connected; }
        catch { port.Close(); State = ConnectionState.Faulted; throw; }
    }
    public ValueTask CloseAsync(CancellationToken cancellationToken)
    {
        port.Close(); State = ConnectionState.Disconnected; return ValueTask.CompletedTask;
    }
    public async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken)
    {
        if (buffer.Length == 0) throw new ArgumentException("Nonempty receive buffer required.", nameof(buffer));
        await readGate.WaitAsync(cancellationToken);
        try
        {
            using var registration = cancellationToken.Register(CancelConnection);
            byte[] chunk = new byte[Math.Min(256, buffer.Length)];
            while (true)
            {
                cancellationToken.ThrowIfCancellationRequested();
                int count;
                try { count = await Task.Run(() => port.Read(chunk, 0, chunk.Length), CancellationToken.None); }
                catch (TimeoutException) { continue; }
                catch when (cancellationToken.IsCancellationRequested) { throw new OperationCanceledException(cancellationToken); }
                chunk.AsMemory(0, count).CopyTo(buffer); return count;
            }
        }
        finally { readGate.Release(); }
    }
    public async ValueTask WriteAsync(ReadOnlyMemory<byte> buffer, CancellationToken cancellationToken)
    {
        if (buffer.Length is < 1 or > 116) throw new ArgumentException("Writes must contain a single bounded BCP frame.");
        await writeGate.WaitAsync(cancellationToken);
        try
        {
            using var registration = cancellationToken.Register(CancelConnection);
            var bytes = buffer.ToArray();
            try { await Task.Run(() => port.Write(bytes, 0, bytes.Length), CancellationToken.None); }
            catch when (cancellationToken.IsCancellationRequested) { throw new OperationCanceledException(cancellationToken); }
            cancellationToken.ThrowIfCancellationRequested();
        }
        finally { writeGate.Release(); }
    }
    private void CancelConnection()
    {
        try { port.Close(); } catch (IOException) { } catch (InvalidOperationException) { }
    }
    public async ValueTask DisposeAsync() { await CloseAsync(CancellationToken.None); port.Dispose(); readGate.Dispose(); writeGate.Dispose(); }
}
