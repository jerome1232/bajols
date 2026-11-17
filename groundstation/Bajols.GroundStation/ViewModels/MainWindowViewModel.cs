using System.ComponentModel;
using System.Globalization;
using System.Runtime.CompilerServices;
using Avalonia.Threading;
using Bajols.Communication;
using Bajols.Configuration;
using Bajols.Core;
using Bajols.Input;
using Bajols.Protocol;
using Bajols.Simulator;

namespace Bajols.GroundStation.ViewModels;

/// <summary>UI projection of confirmed peer data. Never infers disarmed from missing/stale status.</summary>
public sealed class MainWindowViewModel : INotifyPropertyChanged, IAsyncDisposable
{
    private readonly TimeProvider clock;
    private readonly DispatcherTimer timer;
    private readonly StationConfiguration? configuration;
    private readonly OperatorInput input;
    private readonly SimulatedVehicle simulatedVehicle = new();
    private readonly long simulationOrigin;
    private VehicleClient? client;
    private SimulatorTransport? simulator;
    private string? problem;
    private double forward, yaw, dive;
    private bool enableHeld, movementLoss;
    private BuoyancyTarget buoyancy;
    private double unwrappedHeading;
    private bool hasHeading;
    public event PropertyChangedEventHandler? PropertyChanged;
    public AsyncCommand ConnectCommand { get; }
    public AsyncCommand DisconnectCommand { get; }
    public AsyncCommand ArmCommand { get; }
    public AsyncCommand DisarmCommand { get; }
    public AsyncCommand EmergencyStopCommand { get; }
    public AsyncCommand ClearFailsafeCommand { get; }
    public AsyncCommand LocalResetCommand { get; }
    public AsyncCommand DepthLimitCommand { get; }
    public AsyncCommand DepthSensorFaultCommand { get; }
    public AsyncCommand RestoreDepthCommand { get; }
    public MainWindowViewModel() : this(LoadConfiguration, TimeProvider.System) { }
    public MainWindowViewModel(Func<StationConfiguration> loadConfiguration, TimeProvider clock)
    {
        this.clock = clock; simulationOrigin = clock.GetTimestamp();
        try { configuration = loadConfiguration(); configuration.Validate(); }
        catch (Exception e) { problem = $"Configuration error: {e.Message}"; }
        input = new(clock, configuration?.InputFreshnessMs ?? 200, configuration?.DeadZone ?? 0.05);
        void Error(Exception e) { problem = e.Message; Refresh(); }
        ConnectCommand = new(ConnectAsync, Error, () => configuration != null && client == null);
        DisconnectCommand = new(DisconnectAsync, Error, () => client != null);
        ArmCommand = new(() => Request(MessageType.Arm), Error, () => CanArm);
        DisarmCommand = new(() => Request(MessageType.Disarm), Error, () => CanStop);
        EmergencyStopCommand = new(() => Request(MessageType.EmergencyStop), Error, () => CanStop);
        ClearFailsafeCommand = new(() => Request(MessageType.ClearFailsafe), Error, () => CanClear);
        LocalResetCommand = new(() => { simulator?.LocalReset(); return Task.CompletedTask; }, Error, () => simulator != null);
        DepthLimitCommand = new(() => { simulator?.SetDepth(5000, true); return Task.CompletedTask; }, Error, () => simulator != null);
        DepthSensorFaultCommand = new(() => { simulator?.SetDepth(2000, false); return Task.CompletedTask; }, Error, () => simulator != null);
        RestoreDepthCommand = new(() => { simulator?.SetDepth(2000, true); return Task.CompletedTask; }, Error, () => simulator != null);
        timer = new DispatcherTimer(TimeSpan.FromMilliseconds(50), DispatcherPriority.Background, (_, _) => UpdatePresentation());
        timer.Start();
    }
    private static StationConfiguration LoadConfiguration()
    {
        string[] args = Environment.GetCommandLineArgs();
        int index = Array.IndexOf(args, "--config");
        if (index >= 0 && index + 1 >= args.Length) throw new ConfigurationException("--config requires a file path.");
        return StationConfiguration.Load(index >= 0 ? args[index + 1] : Path.Combine(AppContext.BaseDirectory, "station.yaml"));
    }
    public async Task ConnectAsync()
    {
        if (configuration == null || client != null) return;
        problem = null; ReleaseInput();
        IByteTransport transport;
        if (configuration.Transport == "simulator") transport = simulator = new SimulatorTransport(clock, simulatedVehicle, simulationOrigin);
        else transport = new SerialByteTransport(configuration.Port, configuration.ReadTimeoutMs, configuration.WriteTimeoutMs, configuration.BufferBytes);
        var connectingClient = new VehicleClient(transport, OpaqueId128.FromBytes(Convert.FromHexString(configuration.ExpectedVehicleId)),
            () => input.TrySample(out var value) ? value : null, () => buoyancy, clock);
        client = connectingClient;
        Refresh();
        try { await connectingClient.ConnectAsync(); }
        catch { await connectingClient.DisposeAsync(); if (ReferenceEquals(client, connectingClient)) { client = null; simulator = null; } throw; }
        finally { Refresh(); }
    }
    public async Task DisconnectAsync()
    {
        ReleaseInput();
        var old = client; client = null; simulator = null;
        if (old != null) await old.DisposeAsync();
        Refresh();
    }
    public Task Request(MessageType type) => client?.RequestAsync(type) ?? Task.FromException(new InvalidOperationException("Connect first."));
    public void HoldInput(bool held) { enableHeld = held; if (!held) ReleaseInput(); }
    public void ReleaseInput()
    {
        enableHeld = false; input.Release(); forward = yaw = dive = 0; buoyancy = BuoyancyTarget.Neutral;
        Changed(nameof(Forward)); Changed(nameof(Yaw)); Changed(nameof(Dive)); Changed(nameof(BuoyancyIndex));
    }
    public void UpdatePresentation()
    {
        if (enableHeld) input.Update(new(forward, yaw, dive), true);
        Refresh();
    }
    public double Forward { get => forward; set { forward = double.IsFinite(value) ? Math.Clamp(value, -1, 1) : 0; Changed(); } }
    public double Yaw { get => yaw; set { yaw = double.IsFinite(value) ? Math.Clamp(value, -1, 1) : 0; Changed(); } }
    public double Dive { get => dive; set { dive = double.IsFinite(value) ? Math.Clamp(value, -1, 1) : 0; Changed(); } }
    public int BuoyancyIndex { get => (int)buoyancy; set { if (value is >= 0 and <= 2) { buoyancy = (BuoyancyTarget)value; Changed(); } } }
    public bool SimulateMovementLoss
    {
        get => movementLoss;
        set { movementLoss = value; simulator?.DropMovement(value); Changed(); }
    }
    private ClientSnapshot? Snapshot => client?.Snapshot;
    public ConnectionState Connection => Snapshot?.Connection ?? ConnectionState.Disconnected;
    public string LinkFreshness => Snapshot is { SessionEstablished: true, StatusFresh: false } ? "STALE / UNCONFIRMED" : Snapshot is { StatusFresh: true } ? "CONFIRMED" : "DISCONNECTED / UNCONFIRMED";
    public string VehicleState => Snapshot is { Status: var s, StatusFresh: true } && s != null ? s.HasUnknownControlValues ? "UNSUPPORTED" : s.SafetyState.ToString() : "UNKNOWN";
    public string MovementOwner => Snapshot is { StatusFresh: true, Status: var s } && s != null ? s.MovementOwner.ToString() : "Unavailable";
    public string BuoyancyStatus => Snapshot is { StatusFresh: true, Status: var s } && s != null ? $"{s.BuoyancyAction} / {s.BuoyancyActionStatus} / estimate {s.EstimatedBuoyancy}" : "Unavailable";
    public string Qualification => Snapshot is { StatusFresh: true, Status: var s } && s != null ? $"Neutral hold: {s.NeutralHoldProgressMs} / 1000 ms · {s.ArmingBlockers}" : "No confirmed arming qualification";
    public string Restrictions => Snapshot?.Status is { } s ? s.ActiveRestrictions.ToString() : "Unknown";
    public string LastFault => Snapshot?.LastFault is { } f ? $"{f.FaultCode}: {(f.Active ? "ACTIVE" : "cleared")} ({f.Severity})" : "No reported fault";
    public string LastAction => Snapshot?.LastResult is { } r ? $"{r.OriginalMessageType}: {r.Result}" : "No command result";
    public string StatusMessage => problem ?? Snapshot?.Error ?? (configuration?.Transport == "simulator" ? $"SIMULATION · {LinkFreshness}" : $"SERIAL · {LinkFreshness} · {configuration?.Port}");
    public bool IsSimulation => configuration?.Transport == "simulator";
    public bool CanStop => Snapshot?.SessionEstablished == true;
    public bool CanArm => enableHeld && Snapshot is { StatusFresh: true, CompatibleCapabilities: true, Status: { HasUnknownControlValues: false, SafetyState: SafetyState.Disarmed, MovementOwner: Bajols.Protocol.MovementOwner.Pc, ArmingBlockers: ArmingBlockers.None } };
    public bool CanClear => enableHeld && Snapshot is { StatusFresh: true, CompatibleCapabilities: true, Status: { HasUnknownControlValues: false, MovementOwner: Bajols.Protocol.MovementOwner.Pc, SafetyState: SafetyState.CommandLossFailsafe or SafetyState.FaultFailsafe } };
    public bool CanSendCommands => CanStop;
    private double? Angle(MeasurementId id)
    {
        var snapshot = Snapshot;
        if (snapshot?.StatusFresh != true || !snapshot.Measurements.TryGetValue(id, out var sample) ||
            sample.Record.SampleAgeMs == ushort.MaxValue || !sample.Record.HasKnownQualityFlags ||
            (sample.Record.QualityFlags & (TelemetryQuality.Valid | TelemetryQuality.SensorFault)) != TelemetryQuality.Valid ||
            clock.GetElapsedTime(sample.ReceivedAt).TotalMilliseconds + sample.Record.SampleAgeMs >= (configuration?.TelemetryFreshnessMs ?? 1000)) return null;
        if (id == MeasurementId.Heading && sample.Record.Value is < 0 or >= 36000 ||
            id == MeasurementId.Pitch && sample.Record.Value is < -18000 or > 18000) return null;
        return sample.Record.Value / 100d;
    }
    public bool HasAttitude => Angle(MeasurementId.Pitch) != null && Angle(MeasurementId.Heading) != null;
    public double PitchScreenAngle => -(Angle(MeasurementId.Pitch) ?? 0);
    public double HeadingAngle
    {
        get
        {
            if (Angle(MeasurementId.Heading) is not double heading) { hasHeading = false; return 0; }
            if (!hasHeading) { unwrappedHeading = heading; hasHeading = true; }
            else unwrappedHeading += ((heading - unwrappedHeading) % 360 + 540) % 360 - 180;
            return unwrappedHeading;
        }
    }
    public string PitchLabel => Angle(MeasurementId.Pitch) is double pitch ? $"{Math.Abs(pitch):0.0}° nose {(pitch >= 0 ? "up" : "down")}" : "Pitch unavailable";
    public string AttitudeSource => !HasAttitude ? "ATTITUDE UNAVAILABLE / UNCONFIRMED" : IsSimulation ? "SIMULATED ATTITUDE · ILLUSTRATIVE MOTION" : "VEHICLE ATTITUDE";
    public string Depth => Measurement(MeasurementId.Depth, 1000, "m");
    public string Heading => Measurement(MeasurementId.Heading, 100, "°");
    public string Voltage => Measurement(MeasurementId.SupplyVoltage, 1000, "V");
    public string MotorRpm => Measurement(MeasurementId.PropulsionRpm, 1, "RPM");
    private string Measurement(MeasurementId id, double scale, string unit)
    {
        var snapshot = Snapshot;
        if (snapshot?.SessionEstablished != true || !snapshot.Measurements.TryGetValue(id, out var sample) ||
            sample.Record.SampleAgeMs == ushort.MaxValue || !sample.Record.HasKnownQualityFlags ||
            (sample.Record.QualityFlags & TelemetryQuality.Valid) == 0 || (sample.Record.QualityFlags & TelemetryQuality.SensorFault) != 0 ||
            clock.GetElapsedTime(sample.ReceivedAt).TotalMilliseconds + sample.Record.SampleAgeMs >= (configuration?.TelemetryFreshnessMs ?? 1000)) return $"— {unit}";
        string origin = (sample.Record.QualityFlags & TelemetryQuality.Simulated) != 0 ? " SIM" : (sample.Record.QualityFlags & TelemetryQuality.Estimated) != 0 ? " EST" : "";
        return (sample.Record.Value / scale).ToString("0.##", CultureInfo.InvariantCulture) + $" {unit}{origin}";
    }
    private void Refresh()
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(null));
        foreach (var command in new[] { ConnectCommand, DisconnectCommand, ArmCommand, DisarmCommand, EmergencyStopCommand, ClearFailsafeCommand, LocalResetCommand, DepthLimitCommand, DepthSensorFaultCommand, RestoreDepthCommand }) command?.Refresh();
    }
    private void Changed([CallerMemberName] string? name = null) => PropertyChanged?.Invoke(this, new(name));
    public async ValueTask DisposeAsync() { timer.Stop(); await DisconnectAsync(); }
}
