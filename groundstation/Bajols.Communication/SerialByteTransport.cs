using System.IO.Ports;
using Bajols.Core;
using Bajols.Protocol;

namespace Bajols.Communication;

/// <summary>Finite synchronous driver operations run off the UI thread. Cancellation closes the connection.</summary>
public sealed class SerialByteTransport : IByteTransport
{
    // Data bits in the BCP v1 8N1 serial profile.
    private const int DataBits = 8;
    // Wait after opening the port to allow the board to restart.
    private const int BoardStartupDelayMs = 1500;
    // Maximum bytes requested from the serial driver per read.
    private const int ReadChunkBytes = 256;

    /// <summary>
    /// Serial driver configured for the BCP v1 115200-baud, 8N1 profile.
    /// </summary>
    private readonly SerialPort port;

    /// <summary>
    /// Serializes bounded read operations over the serial driver.
    /// </summary>
    private readonly SemaphoreSlim readGate = new(1);

    /// <summary>
    /// Serializes whole-frame writes over the serial driver.
    /// </summary>
    private readonly SemaphoreSlim writeGate = new(1);

    /// <summary>
    /// Current serial-link lifecycle state, independent of protocol/session authorization.
    /// </summary>
    public ConnectionState State { get; private set; } = ConnectionState.Disconnected;

    /// <summary>
    /// Validates finite serial settings and configures the BCP v1 serial port without opening it.
    /// </summary>
    public SerialByteTransport(string name, int readTimeoutMs = 100, int writeTimeoutMs = 250, int bufferBytes = 4096)
    {
        if (string.IsNullOrWhiteSpace(name) || readTimeoutMs is < 10 or > 200 || writeTimeoutMs is < 10 or > 300 || bufferBytes is < 256 or > 65536)
        {
            throw new ArgumentException("Invalid bounded serial settings.");
        }

        port = new(name, BcpV1.SerialBaudRate, Parity.None, DataBits, StopBits.One)
        {
            Handshake = Handshake.None,
            DtrEnable = false,
            RtsEnable = false,
            ReadTimeout = readTimeoutMs,
            WriteTimeout = writeTimeoutMs,
            ReadBufferSize = bufferBytes,
            WriteBufferSize = bufferBytes
        };
    }

    /// <summary>
    /// Opens the serial driver off the UI thread and closes the connection if opening is canceled or fails.
    /// </summary>
    public async ValueTask OpenAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        State = ConnectionState.Connecting;
        using var registration = cancellationToken.Register(CancelConnection);
        try
        {
            await Task.Run(port.Open, cancellationToken);
            // Opening a USB serial port can reset an Arduino before modem
            // control settings take effect. Let its bootloader exit before sending BCP.
            await Task.Delay(TimeSpan.FromMilliseconds(BoardStartupDelayMs), cancellationToken);
            State = ConnectionState.Connected;
        }
        catch
        {
            port.Close();
            State = ConnectionState.Faulted;
            throw;
        }
    }

    /// <summary>
    /// Closes the serial port and marks the transport disconnected.
    /// </summary>
    public ValueTask CloseAsync(CancellationToken cancellationToken)
    {
        port.Close();
        State = ConnectionState.Disconnected;
        return ValueTask.CompletedTask;
    }

    /// <summary>
    /// Reads bounded chunks using finite driver timeouts and closes the port on cancellation.
    /// </summary>
    public async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken)
    {
        if (buffer.Length == 0)
        {
            throw new ArgumentException("Nonempty receive buffer required.", nameof(buffer));
        }

        await readGate.WaitAsync(cancellationToken);
        try
        {
            using var registration = cancellationToken.Register(CancelConnection);
            byte[] chunk = new byte[Math.Min(ReadChunkBytes, buffer.Length)];
            while (true)
            {
                cancellationToken.ThrowIfCancellationRequested();
                int count;
                try
                {
                    count = await Task.Run(() => port.Read(chunk, 0, chunk.Length), CancellationToken.None);
                }
                catch (TimeoutException)
                {
                    continue;
                }
                catch when (cancellationToken.IsCancellationRequested)
                {
                    throw new OperationCanceledException(cancellationToken);
                }

                chunk.AsMemory(0, count).CopyTo(buffer);
                return count;
            }
        }
        finally
        {
            readGate.Release();
        }
    }

    /// <summary>
    /// Writes one complete bounded frame off the UI thread and honors cancellation by closing the connection.
    /// </summary>
    public async ValueTask WriteAsync(ReadOnlyMemory<byte> buffer, CancellationToken cancellationToken)
    {
        if (buffer.Length is < 1 or > FrameCodec.MaxWireSize)
        {
            throw new ArgumentException("Writes must contain a single bounded BCP frame.");
        }

        await writeGate.WaitAsync(cancellationToken);
        try
        {
            using var registration = cancellationToken.Register(CancelConnection);
            var bytes = buffer.ToArray();
            try
            {
                await Task.Run(() => port.Write(bytes, 0, bytes.Length), CancellationToken.None);
            }
            catch when (cancellationToken.IsCancellationRequested)
            {
                throw new OperationCanceledException(cancellationToken);
            }

            cancellationToken.ThrowIfCancellationRequested();
        }
        finally
        {
            writeGate.Release();
        }
    }

    /// <summary>
    /// Closes the port during cancellation while tolerating concurrent close or driver failures.
    /// </summary>
    private void CancelConnection()
    {
        try
        {
            port.Close();
        }
        catch (IOException)
        {
        }
        catch (InvalidOperationException)
        {
        }
    }

    /// <summary>
    /// Closes and disposes the port and its read/write synchronization gates.
    /// </summary>
    public async ValueTask DisposeAsync()
    {
        await CloseAsync(CancellationToken.None);
        port.Dispose();
        readGate.Dispose();
        writeGate.Dispose();
    }
}
