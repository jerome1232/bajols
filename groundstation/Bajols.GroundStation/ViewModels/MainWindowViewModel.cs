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
    /// <summary>
    /// Monotonic clock used to assess displayed measurement freshness.
    /// </summary>
    private readonly TimeProvider clock;

    /// <summary>
    /// UI dispatcher timer that refreshes presentation and held operator samples.
    /// </summary>
    private readonly DispatcherTimer timer;

    /// <summary>
    /// Validated station settings, or null when configuration prevents connection.
    /// </summary>
    private readonly StationConfiguration? configuration;

    /// <summary>
    /// Thread-safe fresh operator input shared with the background session client.
    /// </summary>
    private readonly OperatorInput input;

    /// <summary>
    /// Simulated vehicle retained across link reconnects to preserve its safety state.
    /// </summary>
    private readonly SimulatedVehicle simulatedVehicle = new();

    /// <summary>
    /// Monotonic origin shared by simulated connections during this application session.
    /// </summary>
    private readonly long simulationOrigin;

    /// <summary>
    /// Current explicit connection attempt or established client, or null while disconnected.
    /// </summary>
    private VehicleClient? client;

    /// <summary>
    /// Current simulator transport, or null when disconnected or using serial.
    /// </summary>
    private SimulatorTransport? simulator;

    /// <summary>
    /// Operator-facing configuration or command error that takes precedence in the status banner.
    /// </summary>
    private string? problem;

    /// <summary>
    /// Current on-screen forward-effort request normalized to -1 through 1.
    /// </summary>
    private double forward;

    /// <summary>
    /// Current on-screen starboard-positive turning request normalized to -1 through 1.
    /// </summary>
    private double yaw;

    /// <summary>
    /// Current on-screen downward-positive diving request normalized to -1 through 1.
    /// </summary>
    private double dive;

    /// <summary>
    /// Whether the operator is actively holding the input-enable control.
    /// </summary>
    private bool enableHeld;

    /// <summary>
    /// Current movement-loss fault-injection selection in the dashboard.
    /// </summary>
    private bool movementLoss;

    /// <summary>
    /// Current operator-selected renewable normal buoyancy target.
    /// </summary>
    private BuoyancyTarget buoyancy;

    /// <summary>
    /// Continuous display heading used to animate smoothly across magnetic north.
    /// </summary>
    private double unwrappedHeading;

    /// <summary>
    /// Whether continuous heading animation has a valid prior sample.
    /// </summary>
    private bool hasHeading;

    /// <summary>
    /// Notifies bound UI elements when projected state or operator input changes.
    /// </summary>
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <summary>
    /// Explicitly opens the configured link and establishes an identity-checked session.
    /// </summary>
    public AsyncCommand ConnectCommand
    {
        get;
    }

    /// <summary>
    /// Releases operator input and closes the current connection.
    /// </summary>
    public AsyncCommand DisconnectCommand
    {
        get;
    }

    /// <summary>
    /// Requests arming only after fresh neutral qualification and onboard eligibility checks.
    /// </summary>
    public AsyncCommand ArmCommand
    {
        get;
    }

    /// <summary>
    /// Requests routine disarm through any established configured session.
    /// </summary>
    public AsyncCommand DisarmCommand
    {
        get;
    }

    /// <summary>
    /// Asserts emergency stop without requiring fresh movement tickets or ownership.
    /// </summary>
    public AsyncCommand EmergencyStopCommand
    {
        get;
    }

    /// <summary>
    /// Requests explicit recovery from a non-emergency failsafe before requalification.
    /// </summary>
    public AsyncCommand ClearFailsafeCommand
    {
        get;
    }

    /// <summary>
    /// Represents a local reset at the simulated vehicle; never sends a remote reset message.
    /// </summary>
    public AsyncCommand LocalResetCommand
    {
        get;
    }

    /// <summary>
    /// Injects the five-metre depth condition into the simulated vehicle.
    /// </summary>
    public AsyncCommand DepthLimitCommand
    {
        get;
    }

    /// <summary>
    /// Injects simulated depth-sensor loss.
    /// </summary>
    public AsyncCommand DepthSensorFaultCommand
    {
        get;
    }

    /// <summary>
    /// Restores the simulated depth sensor at two metres below the surface.
    /// </summary>
    public AsyncCommand RestoreDepthCommand
    {
        get;
    }

    /// <summary>
    /// Initializes validated configuration, fresh operator input, explicit commands and presentation refresh scheduling.
    /// </summary>
    public MainWindowViewModel() : this(LoadConfiguration, TimeProvider.System)
    {
    }

    /// <summary>
    /// Initializes validated configuration, fresh operator input, explicit commands and presentation refresh scheduling.
    /// </summary>
    public MainWindowViewModel(Func<StationConfiguration> loadConfiguration, TimeProvider clock)
    {
        this.clock = clock;
        simulationOrigin = clock.GetTimestamp();
        try
        {
            configuration = loadConfiguration();
            configuration.Validate();
        }
        catch (Exception e)
        {
            problem = $"Configuration error: {e.Message}";
        }

        input = new(clock, configuration?.InputFreshnessMs ?? 200, configuration?.DeadZone ?? 0.05);
        // Reports an operator-action failure and refreshes dashboard command availability.
        void Error(Exception e)
        {
            problem = e.Message;
            Refresh();
        }

        ConnectCommand = new(ConnectAsync, Error, () => configuration != null && client == null);
        DisconnectCommand = new(DisconnectAsync, Error, () => client != null);
        ArmCommand = new(() => Request(MessageType.Arm), Error, () => CanArm);
        DisarmCommand = new(() => Request(MessageType.Disarm), Error, () => CanStop);
        EmergencyStopCommand = new(() => Request(MessageType.EmergencyStop), Error, () => CanStop);
        ClearFailsafeCommand = new(() => Request(MessageType.ClearFailsafe), Error, () => CanClear);
        LocalResetCommand = new(() =>
        {
            simulator?.LocalReset();
            return Task.CompletedTask;
        }, Error, () => simulator != null);
        DepthLimitCommand = new(() =>
        {
            simulator?.SetDepth(5000, true);
            return Task.CompletedTask;
        }, Error, () => simulator != null);
        DepthSensorFaultCommand = new(() =>
        {
            simulator?.SetDepth(2000, false);
            return Task.CompletedTask;
        }, Error, () => simulator != null);
        RestoreDepthCommand = new(() =>
        {
            simulator?.SetDepth(2000, true);
            return Task.CompletedTask;
        }, Error, () => simulator != null);
        timer = new DispatcherTimer(TimeSpan.FromMilliseconds(50), DispatcherPriority.Background, (_, _) => UpdatePresentation());
        timer.Start();
    }

    /// <summary>
    /// Selects the explicit command-line configuration path or the bundled YAML file.
    /// </summary>
    private static StationConfiguration LoadConfiguration()
    {
        string[] args = Environment.GetCommandLineArgs();
        int index = Array.IndexOf(args, "--config");
        if (index >= 0 && index + 1 >= args.Length)
        {
            throw new ConfigurationException("--config requires a file path.");
        }

        return StationConfiguration.Load(index >= 0 ? args[index + 1] : Path.Combine(AppContext.BaseDirectory, "station.yaml"));
    }

    /// <summary>
    /// Creates the selected transport and explicitly connects without restoring previous input or arming.
    /// </summary>
    public async Task ConnectAsync()
    {
        if (configuration == null || client != null)
        {
            return;
        }

        problem = null;
        ReleaseInput();
        IByteTransport transport;
        if (configuration.Transport == "simulator")
        {
            transport = simulator = new SimulatorTransport(clock, simulatedVehicle, simulationOrigin);
        }
        else
        {
            transport = new SerialByteTransport(configuration.Port, configuration.ReadTimeoutMs, configuration.WriteTimeoutMs, configuration.BufferBytes);
        }

        var connectingClient = new VehicleClient(transport, OpaqueId128.FromBytes(Convert.FromHexString(configuration.ExpectedVehicleId)), () => input.TrySample(out var value) ? value : null, () => buoyancy, clock);
        client = connectingClient;
        Refresh();
        try
        {
            await connectingClient.ConnectAsync();
        }
        catch
        {
            await connectingClient.DisposeAsync();
            if (ReferenceEquals(client, connectingClient))
            {
                client = null;
                simulator = null;
            }

            throw;
        }
        finally
        {
            Refresh();
        }
    }

    /// <summary>
    /// Releases operator input and disposes the current client without discarding simulated vehicle safety state.
    /// </summary>
    public async Task DisconnectAsync()
    {
        ReleaseInput();
        var old = client;
        client = null;
        simulator = null;
        if (old != null)
        {
            await old.DisposeAsync();
        }

        Refresh();
    }

    /// <summary>
    /// Forwards an explicit operator control action to the active client session.
    /// </summary>
    public Task Request(MessageType type) => client?.RequestAsync(type) ?? Task.FromException(new InvalidOperationException("Connect first."));

    /// <summary>
    /// Updates the operator hold-enable state and clears intent when the hold is released.
    /// </summary>
    public void HoldInput(bool held)
    {
        enableHeld = held;
        if (!held)
        {
            ReleaseInput();
        }
    }

    /// <summary>
    /// Clears held movement and normal buoyancy intent and updates the bound input controls.
    /// </summary>
    public void ReleaseInput()
    {
        enableHeld = false;
        input.Release();
        forward = yaw = dive = 0;
        buoyancy = BuoyancyTarget.Neutral;
        Changed(nameof(Forward));
        Changed(nameof(Yaw));
        Changed(nameof(Dive));
        NotifyBuoyancySelection();
    }

    /// <summary>
    /// Refreshes held operator samples and the dashboard projection of current peer data.
    /// </summary>
    public void UpdatePresentation()
    {
        if (enableHeld)
        {
            input.Update(new(forward, yaw, dive), true);
        }

        Refresh();
    }

    /// <summary>
    /// Finite normalized forward effort requested by sliders or keyboard input.
    /// </summary>
    public double Forward
    {
        get => forward;
        set
        {
            forward = double.IsFinite(value) ? Math.Clamp(value, -1, 1) : 0;
            Changed();
        }
    }

    /// <summary>
    /// Finite normalized starboard-positive turning effort requested by operator input.
    /// </summary>
    public double Yaw
    {
        get => yaw;
        set
        {
            yaw = double.IsFinite(value) ? Math.Clamp(value, -1, 1) : 0;
            Changed();
        }
    }

    /// <summary>
    /// Finite normalized downward-positive diving effort requested by operator input.
    /// </summary>
    public double Dive
    {
        get => dive;
        set
        {
            dive = double.IsFinite(value) ? Math.Clamp(value, -1, 1) : 0;
            Changed();
        }
    }

    /// <summary>
    /// Index of the selected normal buoyancy target in the UI, independent of emergency evacuation.
    /// </summary>
    public int BuoyancyIndex
    {
        get => (int)buoyancy;
        set
        {
            if (value is >= 0 and <= 2)
            {
                buoyancy = (BuoyancyTarget)value;
                NotifyBuoyancySelection();
            }
        }
    }

    /// <summary>
    /// Whether the neutral buoyancy mode button is selected; deselection cannot clear the active target.
    /// </summary>
    public bool IsNeutralBuoyancy
    {
        get => buoyancy == BuoyancyTarget.Neutral;
        set
        {
            if (value)
            {
                BuoyancyIndex = 0;
            }
        }
    }

    /// <summary>
    /// Whether the positive buoyancy mode button is selected; selecting it replaces the previous target.
    /// </summary>
    public bool IsPositiveBuoyancy
    {
        get => buoyancy == BuoyancyTarget.Positive;
        set
        {
            if (value)
            {
                BuoyancyIndex = 1;
            }
        }
    }

    /// <summary>
    /// Whether the negative buoyancy mode button is selected; selecting it replaces the previous target.
    /// </summary>
    public bool IsNegativeBuoyancy
    {
        get => buoyancy == BuoyancyTarget.Negative;
        set
        {
            if (value)
            {
                BuoyancyIndex = 2;
            }
        }
    }

    /// <summary>
    /// Updates the mutually exclusive mode buttons after selection or an input-release reset.
    /// </summary>
    private void NotifyBuoyancySelection()
    {
        Changed(nameof(BuoyancyIndex));
        Changed(nameof(IsNeutralBuoyancy));
        Changed(nameof(IsPositiveBuoyancy));
        Changed(nameof(IsNegativeBuoyancy));
    }

    /// <summary>
    /// Whether incoming movement is deliberately dropped by the simulated peer.
    /// </summary>
    public bool SimulateMovementLoss
    {
        get => movementLoss;
        set
        {
            movementLoss = value;
            simulator?.DropMovement(value);
            Changed();
        }
    }

    /// <summary>
    /// Latest immutable client snapshot, or null when no client exists.
    /// </summary>
    private ClientSnapshot? Snapshot => client?.Snapshot;

    /// <summary>
    /// Current connection lifecycle state, defaulting to disconnected without a client.
    /// </summary>
    public ConnectionState Connection => Snapshot?.Connection ?? ConnectionState.Disconnected;

    /// <summary>
    /// Operator-facing distinction between confirmed, stale and disconnected vehicle state.
    /// </summary>
    public string LinkFreshness => Snapshot is { SessionEstablished: true, StatusFresh: false } ? "STALE / UNCONFIRMED" : Snapshot is { StatusFresh: true } ? "CONFIRMED" : "DISCONNECTED / UNCONFIRMED";

    /// <summary>
    /// Confirmed safety-state label, or unknown/unsupported when it cannot be trusted.
    /// </summary>
    public string VehicleState => Snapshot is { Status: var s, StatusFresh: true } && s != null ? s.HasUnknownControlValues ? "UNSUPPORTED" : s.SafetyState.ToString() : "UNKNOWN";

    /// <summary>
    /// Confirmed movement-owner label, or unavailable when status is stale or missing.
    /// </summary>
    public string MovementOwner => Snapshot is { StatusFresh: true, Status: var s } && s != null ? s.MovementOwner.ToString() : "Unavailable";

    /// <summary>
    /// Confirmed effective buoyancy action, progress and estimate, or unavailable.
    /// </summary>
    public string BuoyancyStatus => Snapshot is { StatusFresh: true, Status: var s } && s != null ? $"{s.BuoyancyAction} / {s.BuoyancyActionStatus} / estimate {s.EstimatedBuoyancy}" : "Unavailable";

    /// <summary>
    /// Onboard neutral-hold progress and arming blockers from fresh status.
    /// </summary>
    public string Qualification => Snapshot is { StatusFresh: true, Status: var s } && s != null ? $"Neutral hold: {s.NeutralHoldProgressMs} / 1000 ms · {s.ArmingBlockers}" : "No confirmed arming qualification";

    /// <summary>
    /// Last reported onboard control restrictions, or unknown without status.
    /// </summary>
    public string Restrictions => Snapshot?.Status is { } s ? s.ActiveRestrictions.ToString() : "Unknown";

    /// <summary>
    /// Most recently reported fault label, activation state and explicit severity.
    /// </summary>
    public string LastFault => Snapshot?.LastFault is { } f ? $"{f.FaultCode}: {(f.Active ? "ACTIVE" : "cleared")} ({f.Severity})" : "No reported fault";

    /// <summary>
    /// Most recent correlated command result, not proof of current state or physical completion.
    /// </summary>
    public string LastAction => Snapshot?.LastResult is { } r ? $"{r.OriginalMessageType}: {r.Result}" : "No command result";

    /// <summary>
    /// Status banner combining configuration/connection errors and link provenance.
    /// </summary>
    public string StatusMessage => problem ?? Snapshot?.Error ?? (configuration?.Transport == "simulator" ? $"SIMULATION · {LinkFreshness}" : $"SERIAL · {LinkFreshness} · {configuration?.Port}");

    /// <summary>
    /// Whether validated configuration selects the simulated peer.
    /// </summary>
    public bool IsSimulation => configuration?.Transport == "simulator";

    /// <summary>
    /// Whether an established session can carry explicit disarm and emergency-stop requests.
    /// </summary>
    public bool CanStop => Snapshot?.SessionEstablished == true;

    /// <summary>
    /// Whether fresh status, ownership, held input and onboard blockers permit offering the Arm action.
    /// </summary>
    public bool CanArm => enableHeld && Snapshot is { StatusFresh: true, CompatibleCapabilities: true, Status: { HasUnknownControlValues: false, SafetyState: SafetyState.Disarmed, MovementOwner: Bajols.Protocol.MovementOwner.Pc, ArmingBlockers: ArmingBlockers.None } };

    /// <summary>
    /// Whether fresh owned status and held input permit requesting a non-emergency failsafe clear.
    /// </summary>
    public bool CanClear => enableHeld && Snapshot is { StatusFresh: true, CompatibleCapabilities: true, Status: { HasUnknownControlValues: false, MovementOwner: Bajols.Protocol.MovementOwner.Pc, SafetyState: SafetyState.CommandLossFailsafe or SafetyState.FaultFailsafe } };

    /// <summary>
    /// Whether a session exists for explicit control actions; individual commands retain their own checks.
    /// </summary>
    public bool CanSendCommands => CanStop;

    /// <summary>
    /// Reads a fresh, valid and physically bounded attitude measurement from confirmed peer telemetry.
    /// </summary>
    private double? Angle(MeasurementId id)
    {
        var snapshot = Snapshot;
        if (snapshot?.StatusFresh != true || !snapshot.Measurements.TryGetValue(id, out var sample) || sample.Record.SampleAgeMs == ushort.MaxValue || !sample.Record.HasKnownQualityFlags || (sample.Record.QualityFlags & (TelemetryQuality.Valid | TelemetryQuality.SensorFault)) != TelemetryQuality.Valid || clock.GetElapsedTime(sample.ReceivedAt).TotalMilliseconds + sample.Record.SampleAgeMs >= (configuration?.TelemetryFreshnessMs ?? 1000))
        {
            return null;
        }

        if (id == MeasurementId.Heading && sample.Record.Value is < 0 or >= 36000 || id == MeasurementId.Pitch && sample.Record.Value is < -18000 or > 18000)
        {
            return null;
        }

        return sample.Record.Value / 100d;
    }

    /// <summary>
    /// Whether fresh confirmed heading and pitch are available for both silhouettes.
    /// </summary>
    public bool HasAttitude => Angle(MeasurementId.Pitch) != null && Angle(MeasurementId.Heading) != null;

    /// <summary>
    /// Clockwise screen rotation derived from nose-up-positive pitch telemetry.
    /// </summary>
    public double PitchScreenAngle => -(Angle(MeasurementId.Pitch) ?? 0);

    /// <summary>
    /// Continuous heading rotation following the shortest path across north.
    /// </summary>
    public double HeadingAngle
    {
        get
        {
            if (Angle(MeasurementId.Heading) is not double heading)
            {
                hasHeading = false;
                return 0;
            }

            if (!hasHeading)
            {
                unwrappedHeading = heading;
                hasHeading = true;
            }
            else
            {
                unwrappedHeading += ((heading - unwrappedHeading) % 360 + 540) % 360 - 180;
            }

            return unwrappedHeading;
        }
    }

    /// <summary>
    /// Formatted measured pitch direction and magnitude, or an unavailable label.
    /// </summary>
    public string PitchLabel => Angle(MeasurementId.Pitch) is double pitch ? $"{Math.Abs(pitch):0.0}° nose {(pitch >= 0 ? "up" : "down")}" : "Pitch unavailable";

    /// <summary>
    /// Attitude provenance or unconfirmed-state label displayed beside the silhouettes.
    /// </summary>
    public string AttitudeSource => !HasAttitude ? "ATTITUDE UNAVAILABLE / UNCONFIRMED" : IsSimulation ? "SIMULATED ATTITUDE · ILLUSTRATIVE MOTION" : "VEHICLE ATTITUDE";

    /// <summary>
    /// Fresh depth formatted in metres with provenance, or an unavailable placeholder.
    /// </summary>
    public string Depth => Measurement(MeasurementId.Depth, 1000, "m");

    /// <summary>
    /// Fresh magnetic heading formatted in degrees with provenance, or an unavailable placeholder.
    /// </summary>
    public string Heading => Measurement(MeasurementId.Heading, 100, "°");

    /// <summary>
    /// Fresh supply voltage formatted in volts with provenance, or an unavailable placeholder.
    /// </summary>
    public string Voltage => Measurement(MeasurementId.SupplyVoltage, 1000, "V");

    /// <summary>
    /// Fresh propulsion speed formatted in RPM with provenance, or an unavailable placeholder.
    /// </summary>
    public string MotorRpm => Measurement(MeasurementId.PropulsionRpm, 1, "RPM");

    /// <summary>
    /// Formats a valid fresh measurement and labels simulated or estimated provenance.
    /// </summary>
    private string Measurement(MeasurementId id, double scale, string unit)
    {
        var snapshot = Snapshot;
        if (snapshot?.SessionEstablished != true || !snapshot.Measurements.TryGetValue(id, out var sample) || sample.Record.SampleAgeMs == ushort.MaxValue || !sample.Record.HasKnownQualityFlags || (sample.Record.QualityFlags & TelemetryQuality.Valid) == 0 || (sample.Record.QualityFlags & TelemetryQuality.SensorFault) != 0 || clock.GetElapsedTime(sample.ReceivedAt).TotalMilliseconds + sample.Record.SampleAgeMs >= (configuration?.TelemetryFreshnessMs ?? 1000))
        {
            return $"— {unit}";
        }

        string origin = (sample.Record.QualityFlags & TelemetryQuality.Simulated) != 0 ? " SIM" : (sample.Record.QualityFlags & TelemetryQuality.Estimated) != 0 ? " EST" : "";
        return (sample.Record.Value / scale).ToString("0.##", CultureInfo.InvariantCulture) + $" {unit}{origin}";
    }

    /// <summary>
    /// Notifies property bindings and explicit command controls that their projected state may have changed.
    /// </summary>
    private void Refresh()
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(null));
        foreach (var command in new[]
        {
            ConnectCommand,
            DisconnectCommand,
            ArmCommand,
            DisarmCommand,
            EmergencyStopCommand,
            ClearFailsafeCommand,
            LocalResetCommand,
            DepthLimitCommand,
            DepthSensorFaultCommand,
            RestoreDepthCommand
        }

        )
        {
            command?.Refresh();
        }
    }

    /// <summary>
    /// Notifies bindings that the named presentation property changed.
    /// </summary>
    private void Changed([CallerMemberName] string? name = null) => PropertyChanged?.Invoke(this, new(name));

    /// <summary>
    /// Stops UI refresh scheduling and asynchronously disconnects the current client.
    /// </summary>
    public async ValueTask DisposeAsync()
    {
        timer.Stop();
        await DisconnectAsync();
    }
}
