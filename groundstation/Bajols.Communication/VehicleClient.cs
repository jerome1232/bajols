using System.Collections.ObjectModel;
using System.Security.Cryptography;
using Bajols.Core;
using Bajols.Protocol;

namespace Bajols.Communication;

public sealed record ReceivedMeasurement(TelemetryRecord Record, long ReceivedAt);
public sealed record ClientSnapshot(ConnectionState Connection, bool SessionEstablished, bool StatusFresh,
    VehicleStatus? Status, Capabilities? Capabilities, bool CompatibleCapabilities,
    IReadOnlyDictionary<MeasurementId, ReceivedMeasurement> Measurements, CommandResult? LastResult, Fault? LastFault, string? Error);

/// <summary>One connection, explicit handshake, no auto-reconnect or arm. No transmit queue of old intent.</summary>
public sealed class VehicleClient(IByteTransport transport, OpaqueId128 expectedVehicleId,
    Func<MovementIntent?> sampleInput, Func<BuoyancyTarget> sampleBuoyancy, TimeProvider? timeProvider = null) : IAsyncDisposable
{
    private readonly TimeProvider clock = timeProvider ?? TimeProvider.System;
    private readonly object sync = new();
    private readonly SemaphoreSlim writer = new(1);
    private readonly Dictionary<MeasurementId, ReceivedMeasurement> measurements = [];
    private CancellationTokenSource? lifetime;
    private Task? readerTask, pumpTask;
    private TaskCompletionSource? ready;
    private readonly TaskCompletionSource connectionFinished = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private bool connectStarted;
    private SessionId session;
    private OpaqueId128 nonce;
    private uint outgoing, incoming, requestId, generation;
    private ControlTicket? ticket;
    private long ticketAt, statusAt, lastHello, lastHeartbeat, lastCapabilities;
    private VehicleStatus? status;
    private Capabilities? capabilities;
    private CommandResult? result;
    private Fault? lastFault;
    private ConnectionState connection;
    private bool established;
    private int disposed;
    private string? error;
    public ClientSnapshot Snapshot
    {
        get { lock (sync) return new(connection, established, FreshStatus(), status, capabilities, Compatible(),
            new ReadOnlyDictionary<MeasurementId, ReceivedMeasurement>(new Dictionary<MeasurementId, ReceivedMeasurement>(measurements)), result, lastFault, error); }
    }
    private bool FreshStatus() => established && status != null && status.ControlGeneration == generation && clock.GetElapsedTime(statusAt).TotalMilliseconds < 1000;
    private bool Compatible() => capabilities is { SupportedModes: var modes, SupportedAxes: var axes,
        MovementIntervalMs: 50, CommandTimeoutMs: 500, TicketIntervalMs: 50, TicketLifetimeMs: 500,
        NeutralHoldMs: 1000, NeutralTolerance: 50 } && (modes & 1) != 0 && (axes & 7) == 7;
    private bool FreshTicket() => ticket != null && ticket.ControlGeneration == generation && clock.GetElapsedTime(ticketAt).TotalMilliseconds < ticket.ValidityMs;
    public async Task ConnectAsync(CancellationToken cancellationToken = default)
    {
        lock (sync)
        {
            if (lifetime != null) throw new InvalidOperationException("This client can only connect once; create a new client after disconnect.");
            connectStarted = true; lifetime = new(); ready = new(TaskCreationOptions.RunContinuationsAsynchronously);
            nonce = OpaqueId128.FromBytes(RandomNumberGenerator.GetBytes(16)); connection = ConnectionState.Connecting;
        }
        try
        {
            if (expectedVehicleId == default) throw new IOException("A provisioned vehicle identity is required.");
            using var connecting = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, lifetime.Token);
            await transport.OpenAsync(connecting.Token);
            readerTask = Task.Run(() => ReadLoop(lifetime.Token)); pumpTask = Task.Run(() => PumpLoop(lifetime.Token));
            await ready.Task.WaitAsync(TimeSpan.FromSeconds(3), connecting.Token);
        }
        catch (Exception e)
        {
            Fail(e); await StopLoops(); throw;
        }
        finally { connectionFinished.TrySetResult(); }
    }
    private void Fail(Exception e)
    {
        lock (sync) { error = e.Message; connection = ConnectionState.Faulted; established = false; ticket = null; measurements.Clear(); }
        ready?.TrySetException(e); lifetime?.Cancel();
    }
    private async Task ReadLoop(CancellationToken token)
    {
        var decoder = new BcpStreamDecoder(WireDirection.VehicleToPc);
        byte[] buffer = new byte[256];
        try
        {
            while (!token.IsCancellationRequested)
            {
                int count = await transport.ReadAsync(buffer, token);
                if (count == 0) throw new IOException("Control link closed.");
                for (int i = 0; i < count; i++)
                    if (decoder.Push(buffer[i], out var packet, out _) == StreamDecodeResult.Packet)
                        await Receive(packet!, token);
            }
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested) { }
        catch (Exception e) { Fail(e); }
        finally { await CloseTransport(); }
    }
    private async Task Receive(BcpPacket packet, CancellationToken token)
    {
        bool confirm = false;
        lock (sync)
        {
            if (!established && packet.Message is HelloReply reply)
            {
                if (reply.Nonce != nonce || reply.SelectedVersion != 1 || packet.Session == default || packet.Sequence == 0) return;
                if (reply.VehicleId != expectedVehicleId) throw new IOException("Vehicle identity does not match the selected profile.");
                if (session != default && packet.Session != session) return;
                session = packet.Session; incoming = Math.Max(incoming, packet.Sequence); confirm = true;
            }
            else
            {
                if (packet.Session != session || session == default || packet.Sequence <= incoming) return;
                incoming = packet.Sequence;
                if (!established && packet.Message is SessionReady accepted)
                { established = true; generation = accepted.ControlGeneration; connection = ConnectionState.Connected; ready!.TrySetResult(); }
                if (!established) return;
                switch (packet.Message)
                {
                    case ControlTicket t when t.ValidityMs == 500 && t.ControlGeneration >= generation:
                        if (t.ControlGeneration != generation) { ticket = null; generation = t.ControlGeneration; }
                        ticket = t; ticketAt = clock.GetTimestamp(); break;
                    case VehicleStatus s when s.ControlGeneration >= generation:
                        if (s.ControlGeneration != generation) { ticket = null; generation = s.ControlGeneration; }
                        status = s; statusAt = clock.GetTimestamp(); break;
                    case Capabilities c:
                        if (capabilities != null && capabilities != c) throw new IOException("Capabilities changed within a session.");
                        capabilities = c; break;
                    case Telemetry telemetry:
                        foreach (var sample in telemetry.Records) measurements[sample.MeasurementId] = new(sample, clock.GetTimestamp()); break;
                    case Fault fault: lastFault = fault; break;
                    case CommandResult r:
                        result = r;
                        if (r.ControlGeneration > generation) { generation = r.ControlGeneration; ticket = null; }
                        break;
                }
            }
        }
        if (confirm) await Send(new SessionConfirm(nonce), token);
    }
    private async Task PumpLoop(CancellationToken token)
    {
        using var timer = new PeriodicTimer(TimeSpan.FromMilliseconds(50), clock);
        try
        {
            while (await timer.WaitForNextTickAsync(token))
            {
                BcpMessage? movement = null, buoyancy = null, handshake = null;
                bool heartbeat = false;
                lock (sync)
                {
                    if (!established)
                    {
                        if (clock.GetElapsedTime(lastHello).TotalMilliseconds >= 200 || lastHello == 0)
                        { handshake = session == default ? new Hello(nonce, 1) : new SessionConfirm(nonce); lastHello = clock.GetTimestamp(); }
                    }
                    else
                    {
                        if (capabilities == null && clock.GetElapsedTime(lastCapabilities).TotalMilliseconds >= 500)
                        { handshake = new GetCapabilities(); lastCapabilities = clock.GetTimestamp(); }
                        heartbeat = clock.GetElapsedTime(lastHeartbeat).TotalMilliseconds >= 1000;
                        if (heartbeat) lastHeartbeat = clock.GetTimestamp();
                        MovementIntent? intent = sampleInput();
                        if (intent is MovementIntent value && FreshStatus() && Compatible() && !status!.HasUnknownControlValues &&
                            status.MovementOwner == MovementOwner.Pc && status.SafetyState != SafetyState.EmergencyStopLatched && FreshTicket())
                        {
                            static short Axis(double v) => checked((short)Math.Round(v * 1000));
                            if (!double.IsFinite(value.Forward) || !double.IsFinite(value.Yaw) || !double.IsFinite(value.Dive) ||
                                Math.Abs(value.Forward) > 1 || Math.Abs(value.Yaw) > 1 || Math.Abs(value.Dive) > 1) throw new IOException("Invalid input sample.");
                            movement = new MovementCommand(ticket!.TicketId, generation, Axis(value.Forward), Axis(value.Yaw), Axis(value.Dive));
                            if (status.SafetyState == SafetyState.Armed)
                            {
                                var target = sampleBuoyancy();
                                if ((capabilities!.SupportedSystems & (target switch { BuoyancyTarget.Neutral => 2, BuoyancyTarget.Positive => 4, BuoyancyTarget.Negative => 8, _ => 0 })) != 0)
                                    buoyancy = new BuoyancyCommand(ticket.TicketId, generation, target);
                            }
                        }
                    }
                }
                if (handshake is Hello hello) await SendHello(hello, token);
                else if (handshake != null) await Send(handshake, token);
                if (heartbeat) await Send(new Heartbeat(), token);
                if (movement != null) await Send(movement, token);
                if (buoyancy != null) await Send(buoyancy, token);
            }
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested) { }
        catch (Exception e) { Fail(e); }
        finally { await CloseTransport(); }
    }
    public async Task RequestAsync(MessageType type, CancellationToken cancellationToken = default)
    {
        BcpMessage command;
        lock (sync)
        {
            if (!established) throw new InvalidOperationException("No established session.");
            if (requestId == uint.MaxValue)
            { var failure = new IOException("Request counter exhausted; connection stopped."); Fail(failure); throw failure; }
            if (type is MessageType.Arm or MessageType.ClearFailsafe)
            {
                if (!FreshStatus() || !Compatible() || status!.HasUnknownControlValues || status.MovementOwner != MovementOwner.Pc || !FreshTicket())
                    throw new InvalidOperationException("Fresh authorized status and ticket required.");
                if (sampleInput() is null) throw new InvalidOperationException("Hold the input-enable control before arming or clearing failsafe.");
                if (type == MessageType.Arm && (status.SafetyState != SafetyState.Disarmed || status.ArmingBlockers != ArmingBlockers.None))
                    throw new InvalidOperationException("Onboard arming qualification is incomplete.");
                if (type == MessageType.ClearFailsafe && status.SafetyState is not (SafetyState.CommandLossFailsafe or SafetyState.FaultFailsafe))
                    throw new InvalidOperationException("No clearable failsafe.");
            }
            uint id = ++requestId;
            command = type switch {
                MessageType.Arm => new Arm(id, ticket!.TicketId, generation),
                MessageType.ClearFailsafe => new ClearFailsafe(id, ticket!.TicketId, generation),
                MessageType.Disarm => new Disarm(id), MessageType.EmergencyStop => new EmergencyStop(id),
                _ => throw new ArgumentException("Not an explicit control action.", nameof(type)) };
        }
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, lifetime!.Token);
        try { await Send(command, linked.Token); }
        catch (Exception e) { Fail(e); throw; }
    }
    private async Task SendHello(Hello message, CancellationToken token)
    {
        await writer.WaitAsync(token);
        try { await Write(new(default, 0, message), token); } finally { writer.Release(); }
    }
    private async Task Send(BcpMessage message, CancellationToken token)
    {
        await writer.WaitAsync(token);
        try
        {
            BcpPacket packet;
            lock (sync)
            {
                // A generation transition while waiting never upgrades an already sampled command.
                if (message is MovementCommand m && m.ControlGeneration != generation || message is BuoyancyCommand b && b.ControlGeneration != generation) return;
                if (outgoing == uint.MaxValue) throw new IOException("Packet sequence exhausted; reconnect while stopped.");
                packet = new(session, ++outgoing, message);
            }
            await Write(packet, token);
        }
        finally { writer.Release(); }
    }
    private async ValueTask Write(BcpPacket packet, CancellationToken token)
    {
        byte[] bytes = new byte[116];
        if (!BcpCodec.TryEncode(packet, WireDirection.PcToVehicle, bytes, out int n, out var failure)) throw new IOException($"Cannot encode command: {failure}");
        await transport.WriteAsync(bytes.AsMemory(0, n), token);
    }
    public async Task DisconnectAsync()
    {
        lifetime?.Cancel(); await StopLoops();
        lock (sync) { connection = ConnectionState.Disconnected; established = false; ticket = null; measurements.Clear(); }
    }
    private async Task CloseTransport() { try { await transport.CloseAsync(CancellationToken.None); } catch (Exception e) { lock (sync) error ??= e.Message; } }
    private async Task StopLoops()
    {
        await CloseTransport();
        if (readerTask != null) await readerTask;
        if (pumpTask != null) await pumpTask;
    }
    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref disposed, 1) != 0) return;
        lifetime?.Cancel(); await CloseTransport();
        if (connectStarted) await connectionFinished.Task;
        await DisconnectAsync(); await transport.DisposeAsync(); lifetime?.Dispose(); writer.Dispose();
    }
}
