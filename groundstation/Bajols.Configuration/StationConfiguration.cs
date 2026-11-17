using YamlDotNet.Serialization;
using YamlDotNet.Serialization.NamingConventions;

namespace Bajols.Configuration;

public sealed record StationConfiguration
{
    public int SchemaVersion { get; init; } = 1;
    public string Transport { get; init; } = "simulator";
    public string Port { get; init; } = "";
    public string ExpectedVehicleId { get; init; } = "42414A4F4C532D53494D554C41544F52";
    public int BaudRate { get; init; } = 115200;
    public int ReadTimeoutMs { get; init; } = 100;
    public int WriteTimeoutMs { get; init; } = 250;
    public int BufferBytes { get; init; } = 4096;
    public double DeadZone { get; init; } = 0.05;
    public int InputFreshnessMs { get; init; } = 200;
    public int TelemetryFreshnessMs { get; init; } = 1000;
    public void Validate()
    {
        if (SchemaVersion != 1) throw new ConfigurationException("Unsupported schema_version; expected 1.");
        if (Transport is not ("simulator" or "serial")) throw new ConfigurationException("transport must be simulator or serial.");
        if (ExpectedVehicleId is null || ExpectedVehicleId.Length != 32 || !ExpectedVehicleId.All(Uri.IsHexDigit) || ExpectedVehicleId.All(c => c == '0')) throw new ConfigurationException("expected_vehicle_id must be exactly 32 hexadecimal characters.");
        if (Transport == "serial" && string.IsNullOrWhiteSpace(Port)) throw new ConfigurationException("serial transport requires a port.");
        if (Transport == "serial" && ExpectedVehicleId.Equals("42414A4F4C532D53494D554C41544F52", StringComparison.OrdinalIgnoreCase))
            throw new ConfigurationException("Configure the physical vehicle ID before selecting serial.");
        if (BaudRate != 115200) throw new ConfigurationException("BCP v1 serial profile requires 115200 baud.");
        if (ReadTimeoutMs is < 10 or > 200 || WriteTimeoutMs is < 10 or > 300) throw new ConfigurationException("Serial timeouts exceed bounded control-link limits.");
        if (BufferBytes is < 256 or > 65536) throw new ConfigurationException("buffer_bytes must be 256..65536.");
        if (!double.IsFinite(DeadZone) || DeadZone is < 0 or > 0.05) throw new ConfigurationException("dead_zone must be finite and between 0 and 0.05.");
        if (InputFreshnessMs is < 50 or > 250) throw new ConfigurationException("input_freshness_ms must be 50..250.");
        if (TelemetryFreshnessMs is < 100 or > 5000) throw new ConfigurationException("telemetry_freshness_ms must be 100..5000.");
    }
    public static StationConfiguration Load(string path)
    {
        if (!File.Exists(path)) throw new ConfigurationException($"Configuration not found: {path}");
        if (new FileInfo(path).Length > 16384) throw new ConfigurationException("Configuration exceeds 16 KiB.");
        try
        {
            var config = new DeserializerBuilder().WithNamingConvention(UnderscoredNamingConvention.Instance)
                .WithDuplicateKeyChecking().Build().Deserialize<StationConfiguration>(File.ReadAllText(path))
                ?? throw new ConfigurationException("Configuration is empty.");
            config.Validate(); return config;
        }
        catch (YamlDotNet.Core.YamlException e) { throw new ConfigurationException($"Invalid YAML: {e.Message}", e); }
    }
}
public sealed class ConfigurationException(string message, Exception? inner = null) : Exception(message, inner);
