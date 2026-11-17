using System.Threading.Channels;
using Bajols.Core;
using Bajols.Protocol;
using Bajols.Simulator;

namespace Bajols.Communication;

/// <summary>Bounded duplex wire link. Simulator gets no privileged bypass around the codec.</summary>
public sealed class SimulatorTransport : IByteTransport
{
    private readonly Channel<byte[]> incoming = Channel.CreateBounded<byte[]>(new BoundedChannelOptions(16) { FullMode = BoundedChannelFullMode.Wait });
    private readonly Channel<byte[]> outgoing = Channel.CreateBounded<byte[]>(new BoundedChannelOptions(16) { FullMode = BoundedChannelFullMode.Wait });
    private readonly TimeProvider clock;
    private readonly SemaphoreSlim readGate = new(1);
    private readonly CancellationTokenSource lifetime = new();
    private Task? peer;
    private byte[]? current;
    private int offset;
    private long origin;
    public SimulatedVehicle Vehicle { get; }
    private readonly long? timeOrigin;
    public ConnectionState State { get; private set; }
    public SimulatorTransport(TimeProvider? clock = null, SimulatedVehicle? vehicle = null, long? timeOrigin = null)
    { this.clock = clock ?? TimeProvider.System; Vehicle = vehicle ?? new(); this.timeOrigin = timeOrigin; }
    public ValueTask OpenAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (peer != null) throw new InvalidOperationException("Create a new simulator transport for each connection.");
        origin = timeOrigin ?? clock.GetTimestamp(); State = ConnectionState.Connected; peer = Task.Run(() => RunAsync(lifetime.Token)); return ValueTask.CompletedTask;
    }
    private uint Now => unchecked((uint)clock.GetElapsedTime(origin).TotalMilliseconds);
    private async Task RunAsync(CancellationToken token)
    {
        var decoder = new BcpStreamDecoder(WireDirection.PcToVehicle);
        using var timer = new PeriodicTimer(TimeSpan.FromMilliseconds(10), clock);
        try
        {
            while (await timer.WaitForNextTickAsync(token))
            {
                for (int n = 0; n < 8 && incoming.Reader.TryRead(out var frame); n++)
                    foreach (byte b in frame)
                        if (decoder.Push(b, out var packet, out _) == StreamDecodeResult.Packet)
                            lock (Vehicle) { foreach (var response in Vehicle.Receive(packet!, Now)) Publish(response); }
                lock (Vehicle) { foreach (var response in Vehicle.Tick(Now)) Publish(response); }
            }
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested) { }
        catch (Exception e) { State = ConnectionState.Faulted; outgoing.Writer.TryComplete(e); incoming.Writer.TryComplete(e); }
        finally { outgoing.Writer.TryComplete(); }
    }
    private void Publish(BcpPacket packet)
    {
        byte[] wire = new byte[116];
        if (BcpCodec.TryEncode(packet, WireDirection.VehicleToPc, wire, out int length, out _))
            outgoing.Writer.TryWrite(wire[..length]); // Congestion drops whole frames, never grows the queue.
    }
    public void SetDepth(int depthMm, bool healthy) { lock (Vehicle) Vehicle.SetDepth(depthMm, healthy, Now); }
    public void LocalReset() { lock (Vehicle) Vehicle.LocalEmergencyReset(Now); }
    public void DropMovement(bool drop) { lock (Vehicle) Vehicle.DropIncomingMovement = drop; }
    public async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken)
    {
        if (buffer.Length == 0) throw new ArgumentException("Nonempty receive buffer required.");
        await readGate.WaitAsync(cancellationToken);
        try
        {
            if (current == null)
            {
                try { current = await outgoing.Reader.ReadAsync(cancellationToken); offset = 0; }
                catch (ChannelClosedException) { return 0; }
            }
            int length = Math.Min(buffer.Length, current.Length - offset);
            current.AsMemory(offset, length).CopyTo(buffer); offset += length;
            if (offset == current.Length) current = null;
            return length;
        }
        finally { readGate.Release(); }
    }
    public async ValueTask WriteAsync(ReadOnlyMemory<byte> buffer, CancellationToken cancellationToken)
    {
        if (State != ConnectionState.Connected) throw new IOException("Simulator not connected.");
        if (buffer.Length is < 1 or > 116) throw new ArgumentException("Single bounded frame required.");
        await incoming.Writer.WriteAsync(buffer.ToArray(), cancellationToken);
    }
    public async ValueTask CloseAsync(CancellationToken cancellationToken)
    {
        lifetime.Cancel(); incoming.Writer.TryComplete();
        if (peer != null) await peer.WaitAsync(cancellationToken);
        State = ConnectionState.Disconnected;
    }
    public async ValueTask DisposeAsync() { await CloseAsync(CancellationToken.None); lifetime.Dispose(); readGate.Dispose(); }
}
