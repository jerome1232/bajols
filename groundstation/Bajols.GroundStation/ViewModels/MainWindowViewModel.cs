using System.ComponentModel;
using System.Globalization;
using System.Runtime.CompilerServices;
using Avalonia.Threading;
using Avalonia.Media;
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
    /// <summary>Sixteen compass points in clockwise order, centred on north at zero degrees.</summary>
    private static readonly string[] CompassPoints =
    [
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
    ];

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
    /// Operator-selected local range-preview detection; never a real sensor result.
    /// </summary>
    private bool simulateForwardObstacle;

    /// <summary>
    /// Requested synthetic clearance in centimetres, independently of vehicle commands.
    /// </summary>
    private double simulatedObstacleCentimetres = 300;

    /// <summary>
    /// Synthetic relative azimuth; the preview hemisphere is not a calibrated sensor field of view.
    /// </summary>
    private double simulatedObstacleBearing;

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
    /// Places a stationary world object using the selected simulation range and bearing.
    /// </summary>
    public AsyncCommand PlaceObjectCommand
    {
        get;
    }

    /// <summary>Clears stationary simulation objects and the synthetic test return.</summary>
    public AsyncCommand ClearObjectsCommand
    {
        get;
    }

    /// <summary>Restores the simulated depth source.</summary>
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
        PlaceObjectCommand = new(() =>
        {
            simulator?.PlaceObject(simulatedObstacleCentimetres * 10, simulatedObstacleBearing);
            SimulateForwardObstacle = false;
            return Task.CompletedTask;
        }, Error, () => simulator != null);
        ClearObjectsCommand = new(() =>
        {
            simulator?.ClearObjects();
            SimulateForwardObstacle = false;
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
        Changed(nameof(DiveUpPercentage));
        Changed(nameof(DiveDownPercentage));
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
    /// Finite normalized forward effort requested by the movement pad or keyboard input.
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
            Changed(nameof(DiveUpPercentage));
            Changed(nameof(DiveDownPercentage));
        }
    }

    /// <summary>
    /// Unsigned upward effort displayed next to the top of the vertical slider.
    /// </summary>
    public string DiveUpPercentage => $"{Math.Max(-dive, 0):0%}";

    /// <summary>
    /// Unsigned downward effort displayed next to the bottom of the vertical slider.
    /// </summary>
    public string DiveDownPercentage => $"{Math.Max(dive, 0):0%}";

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
    /// Three-position buoyancy selector: positive up, neutral middle and negative down.
    /// </summary>
    public double BuoyancyPosition
    {
        get => buoyancy switch
        {
            BuoyancyTarget.Positive => 1,
            BuoyancyTarget.Negative => -1,
            _ => 0
        };
        set
        {
            if (!double.IsFinite(value))
            {
                return;
            }

            BuoyancyIndex = value >= 0.5 ? 1 : value <= -0.5 ? 2 : 0;
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
        Changed(nameof(BuoyancyPosition));
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
    /// Enables an explicitly synthetic forward detection in simulator mode only.
    /// </summary>
    public bool SimulateForwardObstacle
    {
        get => simulateForwardObstacle;
        set
        {
            simulateForwardObstacle = value;
            UpdateSimulatedReturn();
            UpdatePresentation();
        }
    }

    /// <summary>
    /// Synthetic sensor-to-return clearance in centimetres, limited to the provisional six-metre display range.
    /// </summary>
    public double SimulatedObstacleCentimetres
    {
        get => simulatedObstacleCentimetres;
        set
        {
            simulatedObstacleCentimetres = double.IsFinite(value) ? Math.Clamp(value, 0, 600) : 600;
            UpdateSimulatedReturn();
            UpdatePresentation();
        }
    }

    /// <summary>Selected simulated object range in metres, from zero to the provisional six-metre limit.</summary>
    public double SimulatedObstacleMetres
    {
        get => simulatedObstacleCentimetres / 100;
        set => SimulatedObstacleCentimetres = value * 100;
    }

    /// <summary>
    /// Synthetic bearing in the forward hemisphere; positive values are starboard.
    /// This is a display control, not the physical sensor's eventual field of view.
    /// </summary>
    public double SimulatedObstacleBearing
    {
        get => simulatedObstacleBearing;
        set
        {
            simulatedObstacleBearing = double.IsFinite(value) ? Math.Clamp(value, -90, 90) : 0;
            UpdateSimulatedReturn();
            UpdatePresentation();
        }
    }

    /// <summary>Updates the simulator test beam; display data still travels through BCP.</summary>
    private void UpdateSimulatedReturn()
    {
        simulator?.SetPreviewReturn(simulateForwardObstacle ? (uint)Math.Round(simulatedObstacleCentimetres * 10) : null,
            (short)Math.Round(simulatedObstacleBearing * 100));
    }

    /// <summary>Latest source snapshots from the confirmed session, including historical acquisitions.</summary>
    private IReadOnlyList<ReceivedObjectReport> ObjectReports => HasAttitude &&
        Snapshot is { SessionEstablished: true, StatusFresh: true } snapshot
            ? snapshot.ObjectReports.Values.ToArray()
            : Array.Empty<ReceivedObjectReport>();

    /// <summary>Whether at least one ready source has a fresh acquisition, including empty snapshots.</summary>
    public bool HasForwardRange => ObjectReports.Any(r => r.IsFresh(clock, TimeSpan.FromMilliseconds(configuration?.TelemetryFreshnessMs ?? 1000)));

    /// <summary>Current and historical forward observations within six metres; at most 64 markers.</summary>
    public IReadOnlyList<ForwardRangeSample> ForwardObstacles => ObjectReports
        .SelectMany(r => r.ForwardMarkers(clock, TimeSpan.FromMilliseconds(configuration?.TelemetryFreshnessMs ?? 1000))).ToArray();

    /// <summary>
    /// Continuous 1.5 Hz bright/dim warning pulse; range changes do not reset its phase.
    /// No return is ever represented as a flashing obstacle.
    /// </summary>
    public double ForwardObstaclePulse
    {
        get
        {
            if (!ForwardObstacles.Any(o => !o.IsStale))
            {
                return 0;
            }

            double seconds = clock.GetElapsedTime(simulationOrigin).TotalSeconds;
            return 0.5 + 0.5 * Math.Cos(seconds * 3 * Math.PI);
        }
    }

    /// <summary>
    /// Range state and provenance, never a claim that the path is clear.
    /// </summary>
    public string ForwardRangeSummary
    {
        get
        {
            var reports = ObjectReports;
            if (reports.Count == 0)
            {
                return "Object range unavailable / stale";
            }

            string origin = reports.All(r => (r.Report.ReportFlags & 2) != 0) ? "SIM · " : "";
            var observations = ForwardObstacles;
            int staleCount = observations.Count(o => o.IsStale);
            string stale = staleCount > 0 ? $" · {staleCount} stale" : "";
            string truncated = reports.Any(r => (r.Report.ReportFlags & 1) != 0) ? " · truncated" : "";
            return observations.Count == 0
                ? origin + (HasForwardRange ? "no reported return in view" : "range unavailable / stale") + truncated
                : origin + $"{observations.Count} return(s) · nearest {observations.Min(o => o.DistanceMillimetres) / 1000d:0.##} m" + stale + truncated;
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

    /// <summary>Fresh status with supported control values, never inferred from silence.</summary>
    private VehicleStatus? ConfirmedStatus => Snapshot is { StatusFresh: true, Status: { HasUnknownControlValues: false } status } ? status : null;

    /// <summary>Plain-language authoritative safety state for the status overlay.</summary>
    public string VehicleStatusLabel => ConfirmedStatus?.SafetyState switch
    {
        SafetyState.Disarmed => "Disarmed",
        SafetyState.Armed => "Armed",
        SafetyState.CommandLossFailsafe => "Stopped · input lost",
        SafetyState.FaultFailsafe => "Stopped · vehicle fault",
        SafetyState.EmergencyStopLatched => "Emergency stop latched",
        _ => "Status unconfirmed"
    };

    /// <summary>Names the selected movement source without exposing protocol enum spelling.</summary>
    public string ControlSourceLabel => "Control: " + (ConfirmedStatus?.MovementOwner switch
    {
        Bajols.Protocol.MovementOwner.Pc => "Ground station",
        Bajols.Protocol.MovementOwner.FlySky => "Radio controller",
        Bajols.Protocol.MovementOwner.Autonomy => "Onboard autonomy",
        Bajols.Protocol.MovementOwner.None => "No source selected",
        _ => "Unconfirmed"
    });

    /// <summary>Confirmed buoyancy activity and estimated tendency, distinct from the requested target.</summary>
    public string BuoyancyStatus
    {
        get
        {
            if (ConfirmedStatus is not { } status)
            {
                return "Buoyancy unconfirmed";
            }

            string action = status.BuoyancyAction switch
            {
                BuoyancyAction.None => "Buoyancy idle",
                BuoyancyAction.SeekNeutral => "Seeking neutral buoyancy",
                BuoyancyAction.SeekPositive => "Seeking positive buoyancy",
                BuoyancyAction.SeekNegative => "Seeking negative buoyancy",
                BuoyancyAction.EmergencyEvacuation => "Emergency ballast blow",
                _ => "Buoyancy action unavailable"
            };
            string estimate = status.EstimatedBuoyancy switch
            {
                EstimatedBuoyancy.Neutral => "estimated neutral",
                EstimatedBuoyancy.Positive => "estimated tendency to rise",
                EstimatedBuoyancy.Negative => "estimated tendency to sink",
                _ => "estimate unavailable"
            };
            string progress = status.BuoyancyActionStatus == ActionStatus.Inactive ? "" : $" · {FriendlyValue(status.BuoyancyActionStatus)}";
            return $"{action}{progress} · {estimate}";
        }
    }

    /// <summary>Onboard neutral qualification progress; absent or stale status resets the display.</summary>
    public double NeutralHoldProgress => ConfirmedStatus is { } status ? Math.Min(1000, (int)status.NeutralHoldProgressMs) : 0;

    /// <summary>Neutral qualification is an arming check and is not displayed during active movement.</summary>
    public bool ShowNeutralHold => ConfirmedStatus?.SafetyState != SafetyState.Armed;

    /// <summary>Explains that the progress bar checks movement input, not actual buoyancy.</summary>
    public string NeutralHoldLabel => ConfirmedStatus is null ? "Neutral input check unavailable" :
        NeutralHoldProgress >= 1000 ? "Neutral input check complete" : $"Neutral input check · {NeutralHoldProgress / 10:0}%";

    /// <summary>Plain-language reasons that prevent arming, without implying local authorization.</summary>
    public string Qualification
    {
        get
        {
            if (ConfirmedStatus is not { } status)
            {
                return "Waiting for confirmed vehicle status";
            }

            if (status.SafetyState == SafetyState.Armed)
            {
                return "Movement enabled";
            }

            if (status.SafetyState == SafetyState.CommandLossFailsafe)
            {
                return "Restore control input, then clear failsafe";
            }

            if (status.SafetyState == SafetyState.FaultFailsafe)
            {
                return "Resolve the fault, then clear failsafe";
            }

            if (status.SafetyState == SafetyState.EmergencyStopLatched)
            {
                return "Reset emergency stop at the vehicle";
            }

            var blockers = status.ArmingBlockers;
            if (blockers == ArmingBlockers.None)
            {
                return status.SafetyState == SafetyState.Armed ? "Movement enabled" : "Vehicle ready for an arm request";
            }

            var reasons = new List<string>();
            if (blockers.HasFlag(ArmingBlockers.NoOwner))
            {
                reasons.Add("Select a control source");
            }

            if (blockers.HasFlag(ArmingBlockers.StaleOwner))
            {
                reasons.Add("Waiting for fresh control input");
            }

            if (blockers.HasFlag(ArmingBlockers.NonNeutralInput))
            {
                reasons.Add("Center the movement controls");
            }

            if (blockers.HasFlag(ArmingBlockers.NeutralHoldIncomplete))
            {
                reasons.Add("Keep controls centered for one second");
            }

            if (blockers.HasFlag(ArmingBlockers.CommandLossLatch) || blockers.HasFlag(ArmingBlockers.FaultFailsafeLatch))
            {
                reasons.Add("Failsafe must be cleared");
            }

            if (blockers.HasFlag(ArmingBlockers.EmergencyLatch))
            {
                reasons.Add("Reset emergency stop at the vehicle");
            }

            if (blockers.HasFlag(ArmingBlockers.BlockingFault))
            {
                reasons.Add("Resolve the vehicle fault");
            }

            if (blockers.HasFlag(ArmingBlockers.RequiredSubsystemUnavailable))
            {
                reasons.Add("A required system is unavailable");
            }

            if (blockers.HasFlag(ArmingBlockers.ArmActionMustReset))
            {
                reasons.Add("Release the arm control before retrying");
            }

            return string.Join(" · ", reasons);
        }
    }

    /// <summary>Confirmed restrictions in operator-facing language; stale status cannot establish absence.</summary>
    public string Restrictions => ConfirmedStatus is not { } status ? "Restrictions unconfirmed" :
        status.ActiveRestrictions == ActiveRestrictions.None ? "No movement restrictions" :
        "Movement limits: " + string.Join(" · ", new[]
        {
            status.ActiveRestrictions.HasFlag(ActiveRestrictions.DepthLimitActive) ? "depth limit reached" : null,
            status.ActiveRestrictions.HasFlag(ActiveRestrictions.DepthUnavailable) ? "depth unavailable" : null,
            status.ActiveRestrictions.HasFlag(ActiveRestrictions.DownwardControlInhibited) ? "downward movement blocked" : null
        }.Where(value => value is not null));

    /// <summary>Historical fault report in readable wording, independent of current link freshness.</summary>
    public string LastFault => Snapshot?.LastFault is { } fault ? $"Last fault: {FriendlyValue(fault.FaultCode)} · {(fault.Active ? "active" : "cleared")} · {FriendlyValue(fault.Severity)}" : "No faults reported";

    /// <summary>Historical command outcome, not proof of current state or physical completion.</summary>
    public string LastAction => Snapshot?.LastResult is { } result ? $"Last action: {FriendlyValue(result.OriginalMessageType)} · {FriendlyValue(result.Result)}" : "No action results yet";

    /// <summary>Separates known protocol enum words for readable diagnostics without treating unknown values as success.</summary>
    private static string FriendlyValue<T>(T value) where T : struct, Enum
    {
        if (!Enum.IsDefined(value))
        {
            return "unrecognized value";
        }

        string name = value.ToString();
        return string.Concat(name.Select((letter, index) => index > 0 && char.IsUpper(letter) ? " " + char.ToLowerInvariant(letter) : char.ToLowerInvariant(letter).ToString()));
    }

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
    /// Whether fresh confirmed heading and pitch are available for the 3D pose.
    /// </summary>
    public bool HasAttitude => Angle(MeasurementId.Pitch) != null && Angle(MeasurementId.Heading) != null;

    /// <summary>
    /// Clockwise screen rotation derived from nose-up-positive pitch telemetry.
    /// </summary>
    public double PitchScreenAngle => -PitchAngle;

    /// <summary>
    /// Measured nose-up-positive pitch used to rotate the 3D vehicle model.
    /// </summary>
    public double PitchAngle => Angle(MeasurementId.Pitch) ?? 0;

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
    /// Attitude provenance or unconfirmed-state label displayed above the 3D view.
    /// </summary>
    public string AttitudeSource => !HasAttitude ? "ATTITUDE UNAVAILABLE / UNCONFIRMED" : IsSimulation ? "SIMULATED ATTITUDE · ILLUSTRATIVE MOTION" : "VEHICLE ATTITUDE";

    /// <summary>
    /// Fresh depth formatted in metres with provenance, or an unavailable placeholder.
    /// </summary>
    public string Depth => DepthMetres is not null ? Measurement(MeasurementId.Depth, 1000, "m") : "— m";

    /// <summary>Fresh nonnegative depth, or null when measurement validity cannot be established.</summary>
    public double? DepthMetres
    {
        get
        {
            var snapshot = Snapshot;
            if (snapshot is not { SessionEstablished: true, StatusFresh: true } ||
                !snapshot.Measurements.TryGetValue(MeasurementId.Depth, out var sample) ||
                sample.Record.Value < 0 || sample.Record.SampleAgeMs == ushort.MaxValue ||
                !sample.Record.HasKnownQualityFlags ||
                (sample.Record.QualityFlags & (TelemetryQuality.Valid | TelemetryQuality.SensorFault)) != TelemetryQuality.Valid)
            {
                return null;
            }

            double elapsed = clock.GetElapsedTime(sample.ReceivedAt).TotalMilliseconds;
            return elapsed >= 0 && elapsed + sample.Record.SampleAgeMs < (configuration?.TelemetryFreshnessMs ?? 1000)
                ? sample.Record.Value / 1000d : null;
        }
    }

    /// <summary>Whether a current depth marker can be shown rather than implying surface on data loss.</summary>
    public bool HasDepth => DepthMetres is not null;

    /// <summary>Marker top within the 48-pixel surface-to-4.5-metre scale, clamped beyond the limit.</summary>
    public double DepthIndicatorOffset => 6 + Math.Clamp((DepthMetres ?? 0) / 4.5, 0, 1) * 48;

    /// <summary>Depth readout and marker colour, warming from teal through amber to red at 4.5 metres.</summary>
    public IBrush DepthWarningBrush
    {
        get
        {
            if (DepthMetres is not double depth)
            {
                return new SolidColorBrush(Color.Parse("#A9BAC8"));
            }

            double fraction = Math.Clamp(depth / 4.5, 0, 1);
            Color start = Color.Parse(fraction <= 0.7 ? "#69CCC4" : "#F2C879");
            Color end = Color.Parse(fraction <= 0.7 ? "#F2C879" : "#F14959");
            double mix = fraction <= 0.7 ? fraction / 0.7 : (fraction - 0.7) / 0.3;
            return new SolidColorBrush(Color.FromRgb(
                (byte)Math.Round(start.R + (end.R - start.R) * mix),
                (byte)Math.Round(start.G + (end.G - start.G) * mix),
                (byte)Math.Round(start.B + (end.B - start.B) * mix)));
        }
    }

    /// <summary>
    /// Fresh magnetic heading with degrees, nearest sixteen-point compass direction and provenance.
    /// </summary>
    public string Heading => Angle(MeasurementId.Heading) is double heading
        ? Measurement(MeasurementId.Heading, 100, $"° {CompassPoints[(int)Math.Floor((heading + 11.25) / 22.5) % 16]}")
        : "— °";

    /// <summary>Prominent nearest sixteen-point compass direction, or an unavailable placeholder.</summary>
    public string HeadingDirection => Angle(MeasurementId.Heading) is double heading
        ? CompassPoints[(int)Math.Floor((heading + 11.25) / 22.5) % 16]
        : "—";

    /// <summary>Secondary heading in degrees with provenance, or an unavailable placeholder.</summary>
    public string HeadingDegreesLabel => Angle(MeasurementId.Heading) is not null
        ? Measurement(MeasurementId.Heading, 100, "°")
        : "— °";

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
            RestoreDepthCommand,
            PlaceObjectCommand,
            ClearObjectsCommand
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
