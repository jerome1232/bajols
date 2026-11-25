using YamlDotNet.Serialization;
using YamlDotNet.Serialization.NamingConventions;

namespace Bajols.Configuration;

/// <summary>
/// Human-editable station settings validated before any connection is opened.
/// </summary>
public sealed record StationConfiguration
{
    // YAML revision understood by this loader.
    private const int SupportedSchemaVersion = 1;
    // Number of hexadecimal characters in a 128-bit vehicle ID.
    private const int VehicleIdHexLength = 32;
    // Reserved identity of the built-in simulator.
    private const string SimulatorVehicleId = "42414A4F4C532D53494D554C41544F52";
    // Baud rate required by the BCP v1 serial profile.
    private const int RequiredBaudRate = 115200;
    // Minimum finite driver timeout for either serial operation.
    private const int MinSerialTimeoutMs = 10;
    // Maximum wait per serial driver read.
    private const int MaxReadTimeoutMs = 200;
    // Maximum wait per serial driver write.
    private const int MaxWriteTimeoutMs = 300;
    // Minimum accepted serial driver buffer size.
    private const int MinBufferBytes = 256;
    // Maximum accepted serial driver buffer size.
    private const int MaxBufferBytes = 65536;
    // Maximum normalized dead zone as a fraction of full input.
    private const double MaxDeadZone = 0.05;
    // Minimum accepted lifetime for an input snapshot.
    private const int MinInputFreshnessMs = 50;
    // Maximum accepted lifetime for an input snapshot.
    private const int MaxInputFreshnessMs = 250;
    // Minimum accepted age limit for displayed telemetry.
    private const int MinTelemetryFreshnessMs = 100;
    // Maximum accepted age limit for displayed telemetry.
    private const int MaxTelemetryFreshnessMs = 5000;
    // Maximum YAML file size in bytes before parsing.
    private const int MaxConfigurationBytes = 16 * 1024;

    /// <summary>
    /// Supported YAML schema revision; currently only version one is accepted.
    /// </summary>
    public int SchemaVersion { get; init; } = SupportedSchemaVersion;

    /// <summary>
    /// Selected link implementation: simulator or serial.
    /// </summary>
    public string Transport { get; init; } = "simulator";

    /// <summary>
    /// Explicit serial device path or COM port name; unused for simulation.
    /// </summary>
    public string Port { get; init; } = "";

    /// <summary>
    /// Expected provisioned vehicle identity as 32 hexadecimal characters in wire-byte order.
    /// </summary>
    public string ExpectedVehicleId { get; init; } = SimulatorVehicleId;

    /// <summary>
    /// Serial baud rate; BCP v1 requires 115200.
    /// </summary>
    public int BaudRate { get; init; } = RequiredBaudRate;

    /// <summary>
    /// Finite serial read timeout in milliseconds used to keep cancellation responsive.
    /// </summary>
    public int ReadTimeoutMs { get; init; } = 100;

    /// <summary>
    /// Finite serial write timeout in milliseconds for bounded driver operations.
    /// </summary>
    public int WriteTimeoutMs { get; init; } = 250;

    /// <summary>
    /// Bounded serial-driver buffer capacity in bytes.
    /// </summary>
    public int BufferBytes { get; init; } = 4096;

    /// <summary>
    /// Finite normalized input dead zone, limited to zero through five percent.
    /// </summary>
    public double DeadZone { get; init; } = MaxDeadZone;

    /// <summary>
    /// Maximum local operator-snapshot age in milliseconds before movement renewal stops.
    /// </summary>
    public int InputFreshnessMs { get; init; } = 200;

    /// <summary>
    /// Maximum displayed measurement age in milliseconds, including onboard sample age.
    /// </summary>
    public int TelemetryFreshnessMs { get; init; } = 1000;

    /// <summary>
    /// Rejects unsupported schemas, identities and settings outside the configured safety and resource bounds.
    /// </summary>
    public void Validate()
    {
        if (SchemaVersion != SupportedSchemaVersion)
        {
            throw new ConfigurationException("Unsupported schema_version; expected 1.");
        }

        if (Transport is not ("simulator" or "serial"))
        {
            throw new ConfigurationException("transport must be simulator or serial.");
        }

        if (ExpectedVehicleId is null || ExpectedVehicleId.Length != VehicleIdHexLength || !ExpectedVehicleId.All(Uri.IsHexDigit) || ExpectedVehicleId.All(c => c == '0'))
        {
            throw new ConfigurationException("expected_vehicle_id must be exactly 32 hexadecimal characters.");
        }

        if (Transport == "serial" && string.IsNullOrWhiteSpace(Port))
        {
            throw new ConfigurationException("serial transport requires a port.");
        }

        if (Transport == "serial" && ExpectedVehicleId.Equals(SimulatorVehicleId, StringComparison.OrdinalIgnoreCase))
        {
            throw new ConfigurationException("Configure the physical vehicle ID before selecting serial.");
        }

        if (BaudRate != RequiredBaudRate)
        {
            throw new ConfigurationException("BCP v1 serial profile requires 115200 baud.");
        }

        if (ReadTimeoutMs is < MinSerialTimeoutMs or > MaxReadTimeoutMs ||
            WriteTimeoutMs is < MinSerialTimeoutMs or > MaxWriteTimeoutMs)
        {
            throw new ConfigurationException("Serial timeouts exceed bounded control-link limits.");
        }

        if (BufferBytes is < MinBufferBytes or > MaxBufferBytes)
        {
            throw new ConfigurationException("buffer_bytes must be 256..65536.");
        }

        if (!double.IsFinite(DeadZone) || DeadZone is < 0 or > MaxDeadZone)
        {
            throw new ConfigurationException("dead_zone must be finite and between 0 and 0.05.");
        }

        if (InputFreshnessMs is < MinInputFreshnessMs or > MaxInputFreshnessMs)
        {
            throw new ConfigurationException("input_freshness_ms must be 50..250.");
        }

        if (TelemetryFreshnessMs is < MinTelemetryFreshnessMs or > MaxTelemetryFreshnessMs)
        {
            throw new ConfigurationException("telemetry_freshness_ms must be 100..5000.");
        }
    }

    /// <summary>
    /// Loads bounded YAML, rejects duplicate or unknown keys and validates the resulting station configuration.
    /// </summary>
    public static StationConfiguration Load(string path)
    {
        if (!File.Exists(path))
        {
            throw new ConfigurationException($"Configuration not found: {path}");
        }

        if (new FileInfo(path).Length > MaxConfigurationBytes)
        {
            throw new ConfigurationException("Configuration exceeds 16 KiB.");
        }

        try
        {
            var config = new DeserializerBuilder().WithNamingConvention(UnderscoredNamingConvention.Instance).WithDuplicateKeyChecking().Build().Deserialize<StationConfiguration>(File.ReadAllText(path)) ?? throw new ConfigurationException("Configuration is empty.");
            config.Validate();
            return config;
        }
        catch (YamlDotNet.Core.YamlException e)
        {
            throw new ConfigurationException($"Invalid YAML: {e.Message}", e);
        }
    }
}

/// <summary>
/// Describes a station configuration failure suitable for operator-facing diagnostics.
/// </summary>
/// <param name="message">The configuration failure to show to the operator.</param>
/// <param name="inner">The underlying parsing or validation exception, when available.</param>
public sealed class ConfigurationException(string message, Exception? inner = null) : Exception(message, inner);
