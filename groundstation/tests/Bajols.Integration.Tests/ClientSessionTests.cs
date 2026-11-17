using Bajols.Communication;
using Bajols.Core;
using Bajols.Protocol;
using Bajols.Simulator;
using Xunit;

namespace Bajols.Integration.Tests;

public sealed class ClientSessionTests
{
    internal static async Task Until(Func<bool> predicate, int timeoutMs = 5000)
    {
        using var limit = CancellationTokenSource.CreateLinkedTokenSource(TestContext.Current.CancellationToken);
        limit.CancelAfter(timeoutMs);
        while (!predicate()) await Task.Delay(20, limit.Token);
    }
    [Fact]
    public async Task Simulator_handshake_requires_identity_and_does_not_arm_automatically()
    {
        var link = new SimulatorTransport();
        await using var client = new VehicleClient(link, SimulatedVehicle.DefaultVehicleId, () => new MovementIntent(), () => BuoyancyTarget.Neutral);
        await client.ConnectAsync(TestContext.Current.CancellationToken);
        await Until(() => client.Snapshot is { StatusFresh: true, CompatibleCapabilities: true, Status: { NeutralHoldProgressMs: 1000 } });
        Assert.Equal(SafetyState.Disarmed, client.Snapshot.Status!.SafetyState);
        Assert.Contains(client.Snapshot.Measurements.Values, m => (m.Record.QualityFlags & TelemetryQuality.Simulated) != 0);
        await client.RequestAsync(MessageType.Arm, TestContext.Current.CancellationToken);
        await Until(() => client.Snapshot.Status is { SafetyState: SafetyState.Armed });
        await client.RequestAsync(MessageType.Disarm, TestContext.Current.CancellationToken);
        await Until(() => client.Snapshot.Status is { SafetyState: SafetyState.Disarmed });
    }
    [Fact]
    public async Task Wrong_vehicle_identity_cannot_establish_control()
    {
        await using var client = new VehicleClient(new SimulatorTransport(), new(9, 9), () => new MovementIntent(), () => BuoyancyTarget.Neutral);
        await Assert.ThrowsAsync<IOException>(() => client.ConnectAsync(TestContext.Current.CancellationToken));
        Assert.False(client.Snapshot.SessionEstablished); Assert.Equal(ConnectionState.Faulted, client.Snapshot.Connection);
    }
    [Fact]
    public async Task No_fresh_operator_sample_means_no_arming_qualification()
    {
        await using var client = new VehicleClient(new SimulatorTransport(), SimulatedVehicle.DefaultVehicleId, () => null, () => BuoyancyTarget.Neutral);
        await client.ConnectAsync(TestContext.Current.CancellationToken);
        await Until(() => client.Snapshot.StatusFresh);
        await Assert.ThrowsAsync<InvalidOperationException>(() => client.RequestAsync(MessageType.Arm, TestContext.Current.CancellationToken));
        Assert.Equal(SafetyState.Disarmed, client.Snapshot.Status!.SafetyState);
    }
    [Fact]
    public async Task Input_loss_fails_safe_and_restored_input_never_rearms()
    {
        bool held = true;
        var link = new SimulatorTransport();
        await using var client = new VehicleClient(link, SimulatedVehicle.DefaultVehicleId, () => Volatile.Read(ref held) ? new MovementIntent() : null, () => BuoyancyTarget.Neutral);
        await client.ConnectAsync(TestContext.Current.CancellationToken);
        await Until(() => client.Snapshot.Status is { ArmingBlockers: ArmingBlockers.None });
        await client.RequestAsync(MessageType.Arm, TestContext.Current.CancellationToken);
        await Until(() => client.Snapshot.Status is { SafetyState: SafetyState.Armed });
        Volatile.Write(ref held, false);
        await Until(() => client.Snapshot.Status is { SafetyState: SafetyState.CommandLossFailsafe });
        Volatile.Write(ref held, true);
        await Task.Delay(250, TestContext.Current.CancellationToken);
        Assert.Equal(SafetyState.CommandLossFailsafe, client.Snapshot.Status!.SafetyState);
        await client.RequestAsync(MessageType.ClearFailsafe, TestContext.Current.CancellationToken);
        await Until(() => client.Snapshot.Status is { SafetyState: SafetyState.Disarmed });
        Assert.NotEqual(ArmingBlockers.None, client.Snapshot.Status!.ArmingBlockers);
    }
    [Fact]
    public async Task Stale_status_is_unconfirmed_but_stop_stays_available()
    {
        var link = new SimulatorTransport();
        await using var client = new VehicleClient(link, SimulatedVehicle.DefaultVehicleId, () => new MovementIntent(), () => BuoyancyTarget.Neutral);
        await client.ConnectAsync(TestContext.Current.CancellationToken); await Until(() => client.Snapshot.StatusFresh);
        lock (link.Vehicle) link.Vehicle.SuppressStatus = true;
        await Until(() => !client.Snapshot.StatusFresh);
        Assert.True(client.Snapshot.SessionEstablished);
        await Assert.ThrowsAsync<InvalidOperationException>(() => client.RequestAsync(MessageType.Arm, TestContext.Current.CancellationToken));
        await client.RequestAsync(MessageType.EmergencyStop, TestContext.Current.CancellationToken);
        await Until(() => { lock (link.Vehicle) return link.Vehicle.State == SafetyState.EmergencyStopLatched; });
    }
    [Fact]
    public async Task Missing_inbound_tickets_cannot_be_replaced_by_heartbeats()
    {
        var link = new SimulatorTransport();
        await using var client = new VehicleClient(link, SimulatedVehicle.DefaultVehicleId, () => new MovementIntent(), () => BuoyancyTarget.Neutral);
        await client.ConnectAsync(TestContext.Current.CancellationToken); await Until(() => client.Snapshot.Status is { ArmingBlockers: ArmingBlockers.None });
        await client.RequestAsync(MessageType.Arm, TestContext.Current.CancellationToken); await Until(() => client.Snapshot.Status is { SafetyState: SafetyState.Armed });
        lock (link.Vehicle) link.Vehicle.SuppressTickets = true;
        await Until(() => client.Snapshot.Status is { SafetyState: SafetyState.CommandLossFailsafe });
    }
    [Fact]
    public async Task Disconnect_cancels_reads_and_clears_current_measurements()
    {
        var link = new SimulatorTransport();
        await using var client = new VehicleClient(link, SimulatedVehicle.DefaultVehicleId, () => null, () => BuoyancyTarget.Neutral);
        await client.ConnectAsync(TestContext.Current.CancellationToken); await Until(() => client.Snapshot.Measurements.Count > 0);
        await client.DisconnectAsync().WaitAsync(TimeSpan.FromSeconds(2), TestContext.Current.CancellationToken);
        Assert.Equal(ConnectionState.Disconnected, client.Snapshot.Connection);
        Assert.False(client.Snapshot.StatusFresh); Assert.Empty(client.Snapshot.Measurements);
    }
    [Fact]
    public async Task Simulator_read_honors_cancellation_and_wire_frame_bound()
    {
        await using var transport = new SimulatorTransport(); await transport.OpenAsync(CancellationToken.None);
        using var limit = new CancellationTokenSource(100);
        await Assert.ThrowsAnyAsync<OperationCanceledException>(async () => await transport.ReadAsync(new byte[256], limit.Token));
        await Assert.ThrowsAsync<ArgumentException>(async () => await transport.WriteAsync(new byte[117], CancellationToken.None));
    }

    [Fact]
    public async Task Disconnect_during_open_cancels_connection_before_disposal()
    {
        await using var client = new VehicleClient(new WaitingOpenTransport(), SimulatedVehicle.DefaultVehicleId,
            () => null, () => BuoyancyTarget.Neutral);
        var connecting = client.ConnectAsync(TestContext.Current.CancellationToken);
        await client.DisposeAsync().AsTask().WaitAsync(TimeSpan.FromSeconds(2), TestContext.Current.CancellationToken);
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => connecting);
        Assert.False(client.Snapshot.SessionEstablished);
    }

    private sealed class WaitingOpenTransport : IByteTransport
    {
        public ConnectionState State => ConnectionState.Connecting;
        public async ValueTask OpenAsync(CancellationToken token) => await Task.Delay(Timeout.Infinite, token);
        public ValueTask CloseAsync(CancellationToken token) => ValueTask.CompletedTask;
        public ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken token) => throw new InvalidOperationException();
        public ValueTask WriteAsync(ReadOnlyMemory<byte> buffer, CancellationToken token) => throw new InvalidOperationException();
        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
    }
}
