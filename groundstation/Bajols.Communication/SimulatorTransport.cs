using System.Threading.Channels;
using Bajols.Core;
using Bajols.Protocol;
using Bajols.Simulator;

namespace Bajols.Communication;

/// <summary>Bounded duplex wire link. Simulator gets no privileged bypass around the codec.</summary>
public sealed class SimulatorTransport : IByteTransport
{
    /// <summary>
    /// Bounded queue of whole encoded client frames awaiting peer processing.
    /// </summary>
    private readonly Channel<byte[]> incoming = Channel.CreateBounded<byte[]>(new BoundedChannelOptions(16) { FullMode = BoundedChannelFullMode.Wait });

    /// <summary>
    /// Bounded queue of whole encoded peer frames awaiting client reads.
    /// </summary>
    private readonly Channel<byte[]> outgoing = Channel.CreateBounded<byte[]>(new BoundedChannelOptions(16) { FullMode = BoundedChannelFullMode.Wait });

    /// <summary>
    /// Monotonic source for the simulated onboard clock and periodic peer scheduling.
    /// </summary>
    private readonly TimeProvider clock;

    /// <summary>
    /// Prevents concurrent reads from corrupting fragmented-frame cursor state.
    /// </summary>
    private readonly SemaphoreSlim readGate = new(1);

    /// <summary>
    /// Cancellation source owning the simulated peer loop.
    /// </summary>
    private readonly CancellationTokenSource lifetime = new();

    /// <summary>
    /// Background peer-loop task, or null before opening.
    /// </summary>
    private Task? peer;

    /// <summary>
    /// Current queued wire frame being copied into caller-owned receive buffers.
    /// </summary>
    private byte[]? current;

    /// <summary>
    /// Next unread byte within the current fragmented wire frame.
    /// </summary>
    private int offset;

    /// <summary>
    /// Monotonic start timestamp for the simulated onboard clock.
    /// </summary>
    private long origin;

    /// <summary>
    /// Deterministic simulated peer retained when reconnect behavior requires shared state.
    /// </summary>
    public SimulatedVehicle Vehicle
    {
        get;
    }

    /// <summary>
    /// Optional retained origin allowing simulated time to advance consistently across reconnects.
    /// </summary>
    private readonly long? timeOrigin;

    /// <summary>
    /// Current simulator-link lifecycle state, independent of vehicle arming.
    /// </summary>
    public ConnectionState State
    {
        get; private set;
    }

    /// <summary>
    /// Creates a bounded simulated wire link, optionally retaining an existing vehicle and simulation clock origin.
    /// </summary>
    public SimulatorTransport(TimeProvider? clock = null, SimulatedVehicle? vehicle = null, long? timeOrigin = null)
    {
        this.clock = clock ?? TimeProvider.System;
        Vehicle = vehicle ?? new();
        this.timeOrigin = timeOrigin;
    }

    /// <summary>
    /// Starts the background peer loop without establishing a session or arming the vehicle.
    /// </summary>
    public ValueTask OpenAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (peer != null)
        {
            throw new InvalidOperationException("Create a new simulator transport for each connection.");
        }

        origin = timeOrigin ?? clock.GetTimestamp();
        State = ConnectionState.Connected;
        peer = Task.Run(() => RunAsync(lifetime.Token));
        return ValueTask.CompletedTask;
    }

    /// <summary>
    /// Current simulated onboard milliseconds computed from the retained monotonic origin.
    /// </summary>
    private uint Now => unchecked((uint)clock.GetElapsedTime(origin).TotalMilliseconds);

    /// <summary>
    /// Processes a bounded number of incoming frames per tick and publishes scheduled peer responses.
    /// </summary>
    private async Task RunAsync(CancellationToken token)
    {
        var decoder = new BcpStreamDecoder(WireDirection.PcToVehicle);
        using var timer = new PeriodicTimer(TimeSpan.FromMilliseconds(10), clock);
        try
        {
            while (await timer.WaitForNextTickAsync(token))
            {
                for (int n = 0; n < 8 && incoming.Reader.TryRead(out var frame); n++)
                {
                    foreach (byte b in frame)
                    {
                        if (decoder.Push(b, out var packet, out _) == StreamDecodeResult.Packet)
                        {
                            lock (Vehicle)
                            {
                                foreach (var response in Vehicle.Receive(packet!, Now))
                                {
                                    Publish(response);
                                }
                            }
                        }
                    }
                }

                lock (Vehicle)
                {
                    foreach (var response in Vehicle.Tick(Now))
                    {
                        Publish(response);
                    }
                }
            }
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested)
        {
        }
        catch (Exception e)
        {
            State = ConnectionState.Faulted;
            outgoing.Writer.TryComplete(e);
            incoming.Writer.TryComplete(e);
        }
        finally
        {
            outgoing.Writer.TryComplete();
        }
    }

    /// <summary>
    /// Encodes a peer response and drops the whole frame if the bounded outbound queue is full.
    /// </summary>
    private void Publish(BcpPacket packet)
    {
        byte[] wire = new byte[116];
        if (BcpCodec.TryEncode(packet, WireDirection.VehicleToPc, wire, out int length, out _))
        {
            outgoing.Writer.TryWrite(wire[..length]); // Congestion drops whole frames, never grows the queue.
        }
    }

    /// <summary>
    /// Injects simulated depth and sensor health under the vehicle synchronization lock.
    /// </summary>
    public void SetDepth(int depthMm, bool healthy)
    {
        lock (Vehicle)
        {
            Vehicle.SetDepth(depthMm, healthy, Now);
        }
    }

    /// <summary>
    /// Performs the simulated local emergency reset without sending a remote reset message.
    /// </summary>
    public void LocalReset()
    {
        lock (Vehicle)
        {
            Vehicle.LocalEmergencyReset(Now);
        }
    }

    /// <summary>
    /// Enables or disables simulated loss of incoming movement commands.
    /// </summary>
    public void DropMovement(bool drop)
    {
        lock (Vehicle)
        {
            Vehicle.DropIncomingMovement = drop;
        }
    }

    /// <summary>
    /// Copies queued peer bytes into caller-owned storage while honoring cancellation and frame fragmentation.
    /// </summary>
    public async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken)
    {
        if (buffer.Length == 0)
        {
            throw new ArgumentException("Nonempty receive buffer required.");
        }

        await readGate.WaitAsync(cancellationToken);
        try
        {
            if (current == null)
            {
                try
                {
                    current = await outgoing.Reader.ReadAsync(cancellationToken);
                    offset = 0;
                }
                catch (ChannelClosedException)
                {
                    return 0;
                }
            }

            int length = Math.Min(buffer.Length, current.Length - offset);
            current.AsMemory(offset, length).CopyTo(buffer);
            offset += length;
            if (offset == current.Length)
            {
                current = null;
            }

            return length;
        }
        finally
        {
            readGate.Release();
        }
    }

    /// <summary>
    /// Queues one bounded client frame with backpressure and cancellation support.
    /// </summary>
    public async ValueTask WriteAsync(ReadOnlyMemory<byte> buffer, CancellationToken cancellationToken)
    {
        if (State != ConnectionState.Connected)
        {
            throw new IOException("Simulator not connected.");
        }

        if (buffer.Length is < 1 or > 116)
        {
            throw new ArgumentException("Single bounded frame required.");
        }

        await incoming.Writer.WriteAsync(buffer.ToArray(), cancellationToken);
    }

    /// <summary>
    /// Cancels the peer loop and completes the simulated connection.
    /// </summary>
    public async ValueTask CloseAsync(CancellationToken cancellationToken)
    {
        lifetime.Cancel();
        incoming.Writer.TryComplete();
        if (peer != null)
        {
            await peer.WaitAsync(cancellationToken);
        }

        State = ConnectionState.Disconnected;
    }

    /// <summary>
    /// Closes the simulated link and releases its cancellation and synchronization resources.
    /// </summary>
    public async ValueTask DisposeAsync()
    {
        await CloseAsync(CancellationToken.None);
        lifetime.Dispose();
        readGate.Dispose();
    }
}
