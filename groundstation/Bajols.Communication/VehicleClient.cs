using System.Collections.ObjectModel;
using System.Security.Cryptography;
using Bajols.Core;
using Bajols.Protocol;

namespace Bajols.Communication;

/// <summary>
/// Associates a received measurement with the local monotonic receipt timestamp.
/// </summary>
/// <param name="Record">Received measurement with its original validity, provenance and sample age.</param>
/// <param name="ReceivedAt">Local monotonic receipt timestamp used to age the measurement after arrival.</param>
public sealed record ReceivedMeasurement(TelemetryRecord Record, long ReceivedAt);

/// <summary>
/// Provides an immutable projection of current connection, peer status and telemetry observations.
/// </summary>
/// <param name="Connection">Current transport and session lifecycle state.</param>
/// <param name="SessionEstablished">Whether the explicit vehicle-identity-checked handshake has completed.</param>
/// <param name="StatusFresh">Whether retained status is current for both age and control generation.</param>
/// <param name="Status">Last accepted vehicle status, which may be absent or unconfirmed.</param>
/// <param name="Capabilities">Fixed capabilities advertised by the current session, when received.</param>
/// <param name="CompatibleCapabilities">Whether the advertised control profile is compatible with this client.</param>
/// <param name="Measurements">Latest known measurements and their local receipt timestamps.</param>
/// <param name="LastResult">Latest correlated discrete-request result; historical acceptance does not enable control.</param>
/// <param name="LastFault">Latest reported fault event, when available.</param>
/// <param name="Error">Latest connection or cleanup error retained for display.</param>
public sealed record ClientSnapshot(ConnectionState Connection, bool SessionEstablished, bool StatusFresh, VehicleStatus? Status, Capabilities? Capabilities, bool CompatibleCapabilities, IReadOnlyDictionary<MeasurementId, ReceivedMeasurement> Measurements, CommandResult? LastResult, Fault? LastFault, string? Error)
{
    /// <summary>Latest complete snapshots for at most sixteen sources; age must be checked before display.</summary>
    public IReadOnlyDictionary<byte, ReceivedObjectReport> ObjectReports
    {
        get;
        init;
    } = new ReadOnlyDictionary<byte, ReceivedObjectReport>(new Dictionary<byte, ReceivedObjectReport>());
}

/// <summary>One connection, explicit handshake, no auto-reconnect or arm. No transmit queue of old intent.</summary>
/// <param name="transport">Bounded byte stream owned by this single connection.</param>
/// <param name="expectedVehicleId">Provisioned vehicle identity that must match the handshake reply.</param>
/// <param name="sampleInput">Callback providing newly sampled movement intent, or null when input is unavailable.</param>
/// <param name="sampleBuoyancy">Callback providing the current normal buoyancy target.</param>
/// <param name="timeProvider">Optional monotonic clock for receipt ages and periodic scheduling.</param>
public sealed class VehicleClient(IByteTransport transport, OpaqueId128 expectedVehicleId, Func<MovementIntent?> sampleInput, Func<BuoyancyTarget> sampleBuoyancy, TimeProvider? timeProvider = null) : IAsyncDisposable
{
    /// <summary>
    /// Monotonic clock used for local receipt ages, scheduling and freshness checks.
    /// </summary>
    private readonly TimeProvider clock = timeProvider ?? TimeProvider.System;

    /// <summary>
    /// Synchronization guard for session state and immutable snapshot creation.
    /// </summary>
    private readonly object sync = new();

    /// <summary>
    /// Serializes complete outgoing frames and their packet-sequence assignment.
    /// </summary>
    private readonly SemaphoreSlim writer = new(1);

    /// <summary>
    /// Latest known measurements keyed by the bounded BCP measurement registry.
    /// </summary>
    private readonly Dictionary<MeasurementId, ReceivedMeasurement> measurements = [];

    /// <summary>Bounded source cache; scan counters persist until the session is cleared.</summary>
    private readonly ObjectReportCache objectReports = new();

    /// <summary>
    /// Cancellation source shared by connection work and background receive/transmit loops.
    /// </summary>
    private CancellationTokenSource? lifetime;

    /// <summary>
    /// Background task consuming and validating incoming wire data.
    /// </summary>
    private Task? readerTask;

    /// <summary>
    /// Background task scheduling handshakes, diagnostics and freshly sampled control intent.
    /// </summary>
    private Task? pumpTask;

    /// <summary>
    /// Completion signal for successful session establishment or connection failure.
    /// </summary>
    private TaskCompletionSource? ready;

    /// <summary>
    /// Completion signal preventing disposal from racing an unfinished connection attempt.
    /// </summary>
    private readonly TaskCompletionSource connectionFinished = new(TaskCreationOptions.RunContinuationsAsynchronously);

    /// <summary>
    /// Whether this single-use client has begun a connection attempt.
    /// </summary>
    private bool connectStarted;

    /// <summary>
    /// Pending or established vehicle-issued session identity.
    /// </summary>
    private SessionId session;

    /// <summary>
    /// Random opaque nonce correlating replies with this connection attempt.
    /// </summary>
    private OpaqueId128 nonce;

    /// <summary>
    /// Last assigned PC-to-vehicle packet sequence.
    /// </summary>
    private uint outgoing;

    /// <summary>
    /// Highest accepted vehicle-to-PC packet sequence in the selected session.
    /// </summary>
    private uint incoming;

    /// <summary>
    /// Last assigned discrete-request identifier; identifiers never wrap within a session.
    /// </summary>
    private uint requestId;

    /// <summary>
    /// Newest accepted control authorization generation.
    /// </summary>
    private uint generation;

    /// <summary>
    /// Most recently received ticket for the current control generation, or null when unavailable.
    /// </summary>
    private ControlTicket? ticket;

    /// <summary>
    /// Local monotonic receipt timestamp of the retained freshness ticket.
    /// </summary>
    private long ticketAt;

    /// <summary>
    /// Local monotonic receipt timestamp of the retained vehicle status.
    /// </summary>
    private long statusAt;

    /// <summary>
    /// Local timestamp of the most recent handshake transmission.
    /// </summary>
    private long lastHello;

    /// <summary>
    /// Local timestamp of the most recent diagnostic heartbeat transmission.
    /// </summary>
    private long lastHeartbeat;

    /// <summary>
    /// Local timestamp of the most recent capability recovery request.
    /// </summary>
    private long lastCapabilities;

    /// <summary>
    /// Last accepted vehicle status; its age and generation must be checked before use.
    /// </summary>
    private VehicleStatus? status;

    /// <summary>
    /// Capability set fixed for the established session.
    /// </summary>
    private Capabilities? capabilities;

    /// <summary>
    /// Most recently received discrete-command result; historical acceptance never rearms the vehicle.
    /// </summary>
    private CommandResult? result;

    /// <summary>
    /// Most recently received fault event for operator-facing diagnostics.
    /// </summary>
    private Fault? lastFault;

    /// <summary>
    /// Current transport/session lifecycle state reported to the dashboard.
    /// </summary>
    private ConnectionState connection;

    /// <summary>
    /// Whether the identity-checked session handshake has completed.
    /// </summary>
    private bool established;

    /// <summary>
    /// Atomic guard ensuring connection resources are disposed at most once.
    /// </summary>
    private int disposed;

    /// <summary>
    /// Latest connection or cleanup failure retained for display.
    /// </summary>
    private string? error;

    /// <summary>
    /// Immutable view of connection state, freshness and received peer data; it grants no execution authority.
    /// </summary>
    public ClientSnapshot Snapshot
    {
        get
        {
            lock (sync)
            {
                return new(connection, established, FreshStatus(), status, capabilities, Compatible(), new ReadOnlyDictionary<MeasurementId, ReceivedMeasurement>(new Dictionary<MeasurementId, ReceivedMeasurement>(measurements)), result, lastFault, error)
                {
                    ObjectReports = objectReports.Snapshot
                };
            }
        }
    }

    /// <summary>
    /// Checks that status belongs to the current control generation and is less than one second old.
    /// </summary>
    private bool FreshStatus() => established && status != null && status.ControlGeneration == generation && clock.GetElapsedTime(statusAt).TotalMilliseconds < 1000;

    /// <summary>
    /// Checks that advertised capabilities implement the agreed BCP v1 effort-control profile.
    /// </summary>
    private bool Compatible() => capabilities is { SupportedModes: var modes, SupportedAxes: var axes, MovementIntervalMs: 50, CommandTimeoutMs: 500, TicketIntervalMs: 50, TicketLifetimeMs: 500, NeutralHoldMs: 1000, NeutralTolerance: 50 } && (modes & 1) != 0 && (axes & 7) == 7;

    /// <summary>
    /// Checks the locally observed ticket lifetime and current control generation.
    /// </summary>
    private bool FreshTicket() => ticket != null && ticket.ControlGeneration == generation && clock.GetElapsedTime(ticketAt).TotalMilliseconds < ticket.ValidityMs;

    /// <summary>
    /// Opens the transport and explicitly establishes a vehicle-identity-checked session without arming.
    /// </summary>
    public async Task ConnectAsync(CancellationToken cancellationToken = default)
    {
        lock (sync)
        {
            if (lifetime != null)
            {
                throw new InvalidOperationException("This client can only connect once; create a new client after disconnect.");
            }

            connectStarted = true;
            lifetime = new();
            ready = new(TaskCreationOptions.RunContinuationsAsynchronously);
            nonce = OpaqueId128.FromBytes(RandomNumberGenerator.GetBytes(16));
            connection = ConnectionState.Connecting;
        }

        try
        {
            if (expectedVehicleId == default)
            {
                throw new IOException("A provisioned vehicle identity is required.");
            }

            using var connecting = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, lifetime.Token);
            await transport.OpenAsync(connecting.Token);
            readerTask = Task.Run(() => ReadLoop(lifetime.Token));
            pumpTask = Task.Run(() => PumpLoop(lifetime.Token));
            await ready.Task.WaitAsync(TimeSpan.FromSeconds(3), connecting.Token);
        }
        catch (Exception e)
        {
            Fail(e);
            await StopLoops();
            throw;
        }
        finally
        {
            connectionFinished.TrySetResult();
        }
    }

    /// <summary>
    /// Records a connection fault, revokes local session permission and cancels background work.
    /// </summary>
    private void Fail(Exception e)
    {
        lock (sync)
        {
            error = e.Message;
            connection = ConnectionState.Faulted;
            established = false;
            ticket = null;
            measurements.Clear();
            objectReports.Clear();
        }

        ready?.TrySetException(e);
        lifetime?.Cancel();
    }

    /// <summary>
    /// Decodes bounded incoming wire data and dispatches complete peer packets until cancellation or link loss.
    /// </summary>
    private async Task ReadLoop(CancellationToken token)
    {
        var decoder = new BcpStreamDecoder(WireDirection.VehicleToPc);
        byte[] buffer = new byte[256];
        try
        {
            while (!token.IsCancellationRequested)
            {
                int count = await transport.ReadAsync(buffer, token);
                if (count == 0)
                {
                    throw new IOException("Control link closed.");
                }

                for (int i = 0; i < count; i++)
                {
                    if (decoder.Push(buffer[i], out var packet, out _) == StreamDecodeResult.Packet)
                    {
                        await Receive(packet!, token);
                    }
                }
            }
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested)
        {
        }
        catch (Exception e)
        {
            Fail(e);
        }
        finally
        {
            await CloseTransport();
        }
    }

    /// <summary>
    /// Checks session and packet ordering before applying handshake, ticket, status and telemetry updates.
    /// </summary>
    private async Task Receive(BcpPacket packet, CancellationToken token)
    {
        bool confirm = false;
        lock (sync)
        {
            if (!established && packet.Message is HelloReply reply)
            {
                if (reply.Nonce != nonce || reply.SelectedVersion != 1 || packet.Session == default || packet.Sequence == 0)
                {
                    return;
                }

                if (reply.VehicleId != expectedVehicleId)
                {
                    throw new IOException("Vehicle identity does not match the selected profile.");
                }

                if (session != default && packet.Session != session)
                {
                    return;
                }

                session = packet.Session;
                incoming = Math.Max(incoming, packet.Sequence);
                confirm = true;
            }
            else
            {
                if (packet.Session != session || session == default || packet.Sequence <= incoming)
                {
                    return;
                }

                incoming = packet.Sequence;
                if (!established && packet.Message is SessionReady accepted)
                {
                    established = true;
                    generation = accepted.ControlGeneration;
                    connection = ConnectionState.Connected;
                    ready!.TrySetResult();
                }

                if (!established)
                {
                    return;
                }

                switch (packet.Message)
                {
                    case ControlTicket t when t.ValidityMs == 500 && t.ControlGeneration >= generation:
                        if (t.ControlGeneration != generation)
                        {
                            ticket = null;
                            generation = t.ControlGeneration;
                        }

                        ticket = t;
                        ticketAt = clock.GetTimestamp();
                        break;
                    case VehicleStatus s when s.ControlGeneration >= generation:
                        if (s.ControlGeneration != generation)
                        {
                            ticket = null;
                            generation = s.ControlGeneration;
                        }

                        status = s;
                        statusAt = clock.GetTimestamp();
                        break;
                    case Capabilities c:
                        if (capabilities != null && capabilities != c)
                        {
                            throw new IOException("Capabilities changed within a session.");
                        }

                        capabilities = c;
                        break;
                    case Telemetry telemetry:
                        foreach (var sample in telemetry.Records)
                        {
                            measurements[sample.MeasurementId] = new(sample, clock.GetTimestamp());
                        }

                        break;
                    case ObjectReport report when capabilities is { } supported && (supported.SupportedSystems & 16) != 0:
                        objectReports.Accept(report, clock.GetTimestamp());
                        break;
                    case Fault fault:
                        lastFault = fault;
                        break;
                    case CommandResult r:
                        result = r;
                        if (r.ControlGeneration > generation)
                        {
                            generation = r.ControlGeneration;
                            ticket = null;
                        }

                        break;
                }
            }
        }

        if (confirm)
        {
            await Send(new SessionConfirm(nonce), token);
        }
    }

    /// <summary>
    /// Schedules handshake retries, diagnostics and newly sampled control intent without renewing stale operator input.
    /// </summary>
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
                        {
                            handshake = session == default ? new Hello(nonce, 1) : new SessionConfirm(nonce);
                            lastHello = clock.GetTimestamp();
                        }
                    }
                    else
                    {
                        if (capabilities == null && clock.GetElapsedTime(lastCapabilities).TotalMilliseconds >= 500)
                        {
                            handshake = new GetCapabilities();
                            lastCapabilities = clock.GetTimestamp();
                        }

                        heartbeat = clock.GetElapsedTime(lastHeartbeat).TotalMilliseconds >= 1000;
                        if (heartbeat)
                        {
                            lastHeartbeat = clock.GetTimestamp();
                        }

                        MovementIntent? intent = sampleInput();
                        if (intent is MovementIntent value && FreshStatus() && Compatible() && !status!.HasUnknownControlValues && status.MovementOwner == MovementOwner.Pc && status.SafetyState != SafetyState.EmergencyStopLatched && FreshTicket())
                        {
                            // Converts newly sampled normalized effort to signed fixed-point wire units.
                            static short Axis(double v) => checked((short)Math.Round(v * 1000));
                            if (!double.IsFinite(value.Forward) || !double.IsFinite(value.Yaw) || !double.IsFinite(value.Dive) || Math.Abs(value.Forward) > 1 || Math.Abs(value.Yaw) > 1 || Math.Abs(value.Dive) > 1)
                            {
                                throw new IOException("Invalid input sample.");
                            }

                            movement = new MovementCommand(ticket!.TicketId, generation, Axis(value.Forward), Axis(value.Yaw), Axis(value.Dive));
                            if (status.SafetyState == SafetyState.Armed)
                            {
                                var target = sampleBuoyancy();
                                if ((capabilities!.SupportedSystems & (target switch
                                {
                                    BuoyancyTarget.Neutral => 2,
                                    BuoyancyTarget.Positive => 4,
                                    BuoyancyTarget.Negative => 8,
                                    _ => 0
                                })) != 0)
                                {
                                    buoyancy = new BuoyancyCommand(ticket.TicketId, generation, target);
                                }
                            }
                        }
                    }
                }

                if (handshake is Hello hello)
                {
                    await SendHello(hello, token);
                }
                else
                {
                    if (handshake != null)
                    {
                        await Send(handshake, token);
                    }
                }

                if (heartbeat)
                {
                    await Send(new Heartbeat(), token);
                }

                if (movement != null)
                {
                    await Send(movement, token);
                }

                if (buoyancy != null)
                {
                    await Send(buoyancy, token);
                }
            }
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested)
        {
        }
        catch (Exception e)
        {
            Fail(e);
        }
        finally
        {
            await CloseTransport();
        }
    }

    /// <summary>
    /// Validates local eligibility and sends one explicit arm, disarm, failsafe-clear or emergency-stop request.
    /// </summary>
    public async Task RequestAsync(MessageType type, CancellationToken cancellationToken = default)
    {
        BcpMessage command;
        lock (sync)
        {
            if (!established)
            {
                throw new InvalidOperationException("No established session.");
            }

            if (requestId == uint.MaxValue)
            {
                var failure = new IOException("Request counter exhausted; connection stopped.");
                Fail(failure);
                throw failure;
            }

            if (type is MessageType.Arm or MessageType.ClearFailsafe)
            {
                if (!FreshStatus() || !Compatible() || status!.HasUnknownControlValues || status.MovementOwner != MovementOwner.Pc || !FreshTicket())
                {
                    throw new InvalidOperationException("Fresh authorized status and ticket required.");
                }

                if (sampleInput() is null)
                {
                    throw new InvalidOperationException("Hold the input-enable control before arming or clearing failsafe.");
                }

                if (type == MessageType.Arm && (status.SafetyState != SafetyState.Disarmed || status.ArmingBlockers != ArmingBlockers.None))
                {
                    throw new InvalidOperationException("Onboard arming qualification is incomplete.");
                }

                if (type == MessageType.ClearFailsafe && status.SafetyState is not (SafetyState.CommandLossFailsafe or SafetyState.FaultFailsafe))
                {
                    throw new InvalidOperationException("No clearable failsafe.");
                }
            }

            uint id = ++requestId;
            command = type switch
            {
                MessageType.Arm => new Arm(id, ticket!.TicketId, generation),
                MessageType.ClearFailsafe => new ClearFailsafe(id, ticket!.TicketId, generation),
                MessageType.Disarm => new Disarm(id),
                MessageType.EmergencyStop => new EmergencyStop(id),
                _ => throw new ArgumentException("Not an explicit control action.", nameof(type))
            };
        }

        using var linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, lifetime!.Token);
        try
        {
            await Send(command, linked.Token);
        }
        catch (Exception e)
        {
            Fail(e);
            throw;
        }
    }

    /// <summary>
    /// Serializes a pre-session HELLO with the reserved zero identity and sequence.
    /// </summary>
    private async Task SendHello(Hello message, CancellationToken token)
    {
        await writer.WaitAsync(token);
        try
        {
            await Write(new(default, 0, message), token);
        }
        finally
        {
            writer.Release();
        }
    }

    /// <summary>
    /// Assigns the next packet sequence and serializes a message without upgrading an older sampled command&apos;s generation.
    /// </summary>
    private async Task Send(BcpMessage message, CancellationToken token)
    {
        await writer.WaitAsync(token);
        try
        {
            BcpPacket packet;
            lock (sync)
            {
                // A generation transition while waiting never upgrades an already sampled command.
                if (message is MovementCommand m && m.ControlGeneration != generation || message is BuoyancyCommand b && b.ControlGeneration != generation)
                {
                    return;
                }

                if (outgoing == uint.MaxValue)
                {
                    throw new IOException("Packet sequence exhausted; reconnect while stopped.");
                }

                packet = new(session, ++outgoing, message);
            }

            await Write(packet, token);
        }
        finally
        {
            writer.Release();
        }
    }

    /// <summary>
    /// Encodes a bounded PC-to-vehicle frame and writes its complete wire bytes to the transport.
    /// </summary>
    private async ValueTask Write(BcpPacket packet, CancellationToken token)
    {
        byte[] bytes = new byte[116];
        if (!BcpCodec.TryEncode(packet, WireDirection.PcToVehicle, bytes, out int n, out var failure))
        {
            throw new IOException($"Cannot encode command: {failure}");
        }

        await transport.WriteAsync(bytes.AsMemory(0, n), token);
    }

    /// <summary>
    /// Cancels connection work, waits for cleanup and clears current telemetry without automatically reconnecting.
    /// </summary>
    public async Task DisconnectAsync()
    {
        lifetime?.Cancel();
        await StopLoops();
        lock (sync)
        {
            connection = ConnectionState.Disconnected;
            established = false;
            ticket = null;
            measurements.Clear();
            objectReports.Clear();
        }
    }

    /// <summary>
    /// Closes the underlying transport and retains any cleanup failure for display.
    /// </summary>
    private async Task CloseTransport()
    {
        try
        {
            await transport.CloseAsync(CancellationToken.None);
        }
        catch (Exception e)
        {
            lock (sync)
            {
                error ??= e.Message;
            }
        }
    }

    /// <summary>
    /// Closes the transport and waits for both background connection loops to finish.
    /// </summary>
    private async Task StopLoops()
    {
        await CloseTransport();
        if (readerTask != null)
        {
            await readerTask;
        }

        if (pumpTask != null)
        {
            await pumpTask;
        }
    }

    /// <summary>
    /// Cancels an in-progress connection and releases transport resources exactly once.
    /// </summary>
    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref disposed, 1) != 0)
        {
            return;
        }

        lifetime?.Cancel();
        await CloseTransport();
        if (connectStarted)
        {
            await connectionFinished.Task;
        }

        await DisconnectAsync();
        await transport.DisposeAsync();
        lifetime?.Dispose();
        writer.Dispose();
    }
}
