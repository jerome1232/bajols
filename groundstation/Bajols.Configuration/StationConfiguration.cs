using YamlDotNet.Serialization;
using YamlDotNet.Serialization.NamingConventions;

namespace Bajols.Configuration;

/// <summary>
/// Human-editable station settings validated before any connection is opened.
/// </summary>
public sealed record StationConfiguration
{
    /// <summary>
    /// Supported YAML schema revision; currently only version one is accepted.
    /// </summary>
    public int SchemaVersion { get; init; } = 1;

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
    public string ExpectedVehicleId { get; init; } = "42414A4F4C532D53494D554C41544F52";

    /// <summary>
    /// Serial baud rate; BCP v1 requires 115200.
    /// </summary>
    public int BaudRate { get; init; } = 115200;

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
    public double DeadZone { get; init; } = 0.05;

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
        if (SchemaVersion != 1)
        {
            throw new ConfigurationException("Unsupported schema_version; expected 1.");
        }

        if (Transport is not ("simulator" or "serial"))
        {
            throw new ConfigurationException("transport must be simulator or serial.");
        }

        if (ExpectedVehicleId is null || ExpectedVehicleId.Length != 32 || !ExpectedVehicleId.All(Uri.IsHexDigit) || ExpectedVehicleId.All(c => c == '0'))
        {
            throw new ConfigurationException("expected_vehicle_id must be exactly 32 hexadecimal characters.");
        }

        if (Transport == "serial" && string.IsNullOrWhiteSpace(Port))
        {
            throw new ConfigurationException("serial transport requires a port.");
        }

        if (Transport == "serial" && ExpectedVehicleId.Equals("42414A4F4C532D53494D554C41544F52", StringComparison.OrdinalIgnoreCase))
        {
            throw new ConfigurationException("Configure the physical vehicle ID before selecting serial.");
        }

        if (BaudRate != 115200)
        {
            throw new ConfigurationException("BCP v1 serial profile requires 115200 baud.");
        }

        if (ReadTimeoutMs is < 10 or > 200 || WriteTimeoutMs is < 10 or > 300)
        {
            throw new ConfigurationException("Serial timeouts exceed bounded control-link limits.");
        }

        if (BufferBytes is < 256 or > 65536)
        {
            throw new ConfigurationException("buffer_bytes must be 256..65536.");
        }

        if (!double.IsFinite(DeadZone) || DeadZone is < 0 or > 0.05)
        {
            throw new ConfigurationException("dead_zone must be finite and between 0 and 0.05.");
        }

        if (InputFreshnessMs is < 50 or > 250)
        {
            throw new ConfigurationException("input_freshness_ms must be 50..250.");
        }

        if (TelemetryFreshnessMs is < 100 or > 5000)
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

        if (new FileInfo(path).Length > 16384)
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
