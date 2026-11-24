using Bajols.Communication;
using Bajols.Core;
using Bajols.Protocol;
using Bajols.Simulator;
using Xunit;

namespace Bajols.Integration.Tests;

/// <summary>Object acquisition ordering, age, bounded storage and encoded simulated-peer tests.</summary>
public sealed class ObjectReportingTests
{
    /// <summary>Controllable millisecond clock for exact acquisition expiry.</summary>
    private sealed class Clock : TimeProvider
    {
        /// <summary>Current monotonic time in milliseconds.</summary>
        public long Milliseconds
        {
            get;
            set;
        }

        /// <summary>Timestamp units per second.</summary>
        public override long TimestampFrequency => 1000;

        /// <summary>Reads deterministic monotonic time.</summary>
        public override long GetTimestamp() => Milliseconds;
    }

    /// <summary>Creates a simulated, valid body-relative observation.</summary>
    private static ObjectObservation Observation(ushort id, uint range) => new(id, range, range, 0, 0, 0, 0, 0, TelemetryQuality.Valid | TelemetryQuality.Simulated);

    /// <summary>Creates a complete synthetic source report.</summary>
    private static ObjectReport Report(byte source, uint scan, params ObjectObservation[] observations) => new(1, source, scan, 100, 1, 2, observations);

    /// <summary>Repeated scans cannot renew age; replacement, state loss and bounds remain per source.</summary>
    [Fact]
    public void Cache_replaces_complete_snapshots_without_refreshing_replays()
    {
        var cache = new ObjectReportCache();
        var clock = new Clock();
        var report = Report(1, 10, Observation(1, 200), Observation(2, 300));
        Assert.True(cache.Accept(report, 0));
        Assert.True(cache.Accept(Report(2, 1, Observation(1, 500)), 0));
        clock.Milliseconds = 899;
        Assert.True(cache.Snapshot[1].IsFresh(clock, TimeSpan.FromSeconds(1)));
        Assert.False(cache.Accept(report with { SampleAgeMs = 0 }, 899));
        Assert.False(cache.Accept(report with { ScanSequence = 9 }, 899));
        clock.Milliseconds = 900;
        Assert.False(cache.Snapshot[1].IsFresh(clock, TimeSpan.FromSeconds(1)));
        Assert.Equal(0, cache.Snapshot[1].ReceivedAt);
        Assert.True(cache.Accept(Report(1, 11), 900));
        Assert.Empty(cache.Snapshot[1].Report.Observations);
        Assert.Single(cache.Snapshot[2].Report.Observations);
        Assert.True(cache.Accept(Report(1, 12) with { SensorState = 2 }, 900));
        Assert.False(cache.Snapshot[1].IsFresh(clock, TimeSpan.FromSeconds(1)));
        Assert.True(cache.Accept(Report(1, 13) with { SampleAgeMs = ushort.MaxValue }, 900));
        Assert.False(cache.Snapshot[1].IsFresh(clock, TimeSpan.FromSeconds(1)));
        for (byte source = 3; source <= 16; source++)
        {
            Assert.True(cache.Accept(Report(source, 1), 900));
        }

        Assert.False(cache.Accept(Report(17, 1), 900));
        Assert.Equal(16, cache.Snapshot.Count);
        cache.Clear();
        Assert.Empty(cache.Snapshot);
        Assert.True(cache.Accept(Report(1, 1), 900));
    }

    /// <summary>Six-metre returns fade after exact expiry without being refreshed by display projection.</summary>
    [Fact]
    public void Expired_markers_remain_historical_and_fade_to_grey()
    {
        var clock = new Clock();
        var report = Report(1, 1, Observation(1, 6000), Observation(2, 6001));
        var received = new ReceivedObjectReport(report, 0);
        clock.Milliseconds = 899;
        var fresh = Assert.Single(received.ForwardMarkers(clock, TimeSpan.FromSeconds(1)));
        Assert.Equal(6000, fresh.DistanceMillimetres);
        Assert.False(fresh.IsStale);
        Assert.True(fresh.IsFresh(clock, TimeSpan.FromSeconds(1)));
        clock.Milliseconds = 900;
        var expired = Assert.Single(received.ForwardMarkers(clock, TimeSpan.FromSeconds(1)));
        Assert.True(expired.IsStale);
        Assert.Equal(0, expired.StaleFade);
        Assert.False(expired.IsFresh(clock, TimeSpan.FromSeconds(1)));
        Assert.Null(expired.Proximity(clock, TimeSpan.FromSeconds(1)));
        clock.Milliseconds = 1400;
        Assert.Equal(0.5, Assert.Single(received.ForwardMarkers(clock, TimeSpan.FromSeconds(1))).StaleFade);
        clock.Milliseconds = 1900;
        Assert.Equal(1, Assert.Single(received.ForwardMarkers(clock, TimeSpan.FromSeconds(1))).StaleFade);
        Assert.Equal(0, received.ReceivedAt);
        Assert.Empty(new ReceivedObjectReport(report with { Observations = Array.Empty<ObjectObservation>() }, 1900).ForwardMarkers(clock, TimeSpan.FromSeconds(1)));
        Assert.Empty(new ReceivedObjectReport(report with { SensorState = 2, Observations = Array.Empty<ObjectObservation>() }, 1900).ForwardMarkers(clock, TimeSpan.FromSeconds(1)));
        Assert.True(Assert.Single(new ReceivedObjectReport(report with { SampleAgeMs = ushort.MaxValue }, 1900).ForwardMarkers(clock, TimeSpan.FromSeconds(1))).IsStale);
    }

    /// <summary>Placed objects reach the client over the codec, truncate deterministically and expire on source silence.</summary>
    [Fact]
    public async Task Placed_objects_cross_wire_and_expire_without_heartbeat_renewal()
    {
        var link = new SimulatorTransport();
        for (int i = 0; i < 5; i++)
        {
            link.PlaceObject(3000 + i * 500, -40 + i * 20);
        }

        await using var client = new VehicleClient(link, SimulatedVehicle.DefaultVehicleId, () => null, () => BuoyancyTarget.Neutral);
        await client.ConnectAsync(TestContext.Current.CancellationToken);
        await ClientSessionTests.Until(() => client.Snapshot.ObjectReports.Count == 1);
        var received = client.Snapshot.ObjectReports[1];
        Assert.Equal(4, received.Report.Observations.Count);
        Assert.Equal(3, received.Report.ReportFlags);
        Assert.Equal(new uint[] { 3000, 3500, 4000, 4500 }, received.Report.Observations.Select(o => o.SensorRangeMm));
        Assert.Equal(-4000, received.Report.Observations[0].BearingCdeg);
        Assert.All(received.Report.Observations, o => Assert.True((o.QualityFlags & TelemetryQuality.Simulated) != 0));
        lock (link.Vehicle)
        {
            link.Vehicle.SuppressObjects = true;
        }

        await ClientSessionTests.Until(() => !client.Snapshot.ObjectReports[1].IsFresh(TimeProvider.System, TimeSpan.FromMilliseconds(400)));
        Assert.True(client.Snapshot.StatusFresh);
        Assert.Equal(SafetyState.Disarmed, client.Snapshot.Status!.SafetyState);
        link.ClearObjects();
        lock (link.Vehicle)
        {
            link.Vehicle.SuppressObjects = false;
        }

        await ClientSessionTests.Until(() => client.Snapshot.ObjectReports[1].Report.Observations.Count == 0);
        Assert.True(client.Snapshot.ObjectReports[1].IsFresh(TimeProvider.System, TimeSpan.FromSeconds(1)));
        await client.DisposeAsync();
        Assert.Empty(client.Snapshot.ObjectReports);
    }

    /// <summary>Stationary world objects draw closer under authorized thrust rather than following the vehicle.</summary>
    [Fact]
    public async Task Thrust_changes_range_to_stationary_world_objects()
    {
        var link = new SimulatorTransport();
        link.PlaceObject(550, 0);
        MovementIntent intent = new();
        await using var client = new VehicleClient(link, SimulatedVehicle.DefaultVehicleId, () => intent, () => BuoyancyTarget.Neutral);
        await client.ConnectAsync(TestContext.Current.CancellationToken);
        await ClientSessionTests.Until(() => client.Snapshot is { StatusFresh: true, Status: { ArmingBlockers: ArmingBlockers.None } } && client.Snapshot.ObjectReports.Count == 1);
        Assert.InRange(client.Snapshot.ObjectReports[1].Report.Observations[0].SensorRangeMm, 549u, 550u);
        await client.RequestAsync(MessageType.Arm, TestContext.Current.CancellationToken);
        await ClientSessionTests.Until(() => client.Snapshot.Status?.SafetyState == SafetyState.Armed);
        intent = new(0.5, 0, 0);
        await ClientSessionTests.Until(() => client.Snapshot.ObjectReports[1].Report.Observations.Any(o => o.SensorRangeMm < 500));
        Assert.Equal(SafetyState.Armed, client.Snapshot.Status!.SafetyState);
        await client.RequestAsync(MessageType.Disarm, TestContext.Current.CancellationToken);
    }

    /// <summary>Placement rejects nonfinite inputs and bounds the world point collection.</summary>
    [Fact]
    public void World_points_remain_fixed_and_placement_is_bounded()
    {
        var vehicle = new SimulatedVehicle();
        Assert.Throws<ArgumentOutOfRangeException>(() => vehicle.PlaceObject(double.NaN, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => vehicle.PlaceObject(100, 180));
        for (int i = 0; i < 32; i++)
        {
            vehicle.PlaceObject(300, 0);
        }

        Assert.Equal(32, vehicle.PlacedObjectCount);
        Assert.Throws<InvalidOperationException>(() => vehicle.PlaceObject(300, 0));
        vehicle.ClearObjects();
        Assert.Equal(0, vehicle.PlacedObjectCount);
    }
}
