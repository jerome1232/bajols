using Bajols.Configuration;
using Bajols.Core;
using Bajols.Input;
using Xunit;

namespace Bajols.Integration.Tests;

/// <summary>
/// Tests of input freshness and strict, bounded YAML configuration validation.
/// </summary>
public sealed class InputAndConfigurationTests
{
    /// <summary>
    /// Deterministic monotonic clock used to test operator-input expiry boundaries.
    /// </summary>
    private sealed class Clock : TimeProvider
    {
        /// <summary>
        /// Deterministic monotonic time in milliseconds controlled by the input test.
        /// </summary>
        public long Milliseconds;

        /// <summary>
        /// Number of deterministic timestamp units per second.
        /// </summary>
        public override long TimestampFrequency => 1000;

        /// <summary>
        /// Returns the deterministic test clock in milliseconds.
        /// </summary>
        public override long GetTimestamp() => Milliseconds;
    }

    /// <summary>
    /// Verifies input requires hold and new samples and release clears intent.
    /// </summary>
    [Fact]
    public void Input_requires_hold_and_new_samples_and_release_clears_intent()
    {
        var clock = new Clock();
        var input = new OperatorInput(clock);
        Assert.False(input.TrySample(out _));
        input.Update(new(0.05, -1, 1), true);
        Assert.True(input.TrySample(out var effort));
        Assert.Equal(new MovementIntent(0, -1, 1), effort);
        clock.Milliseconds = 199;
        Assert.True(input.TrySample(out _));
        clock.Milliseconds = 200;
        Assert.False(input.TrySample(out _));
        input.Update(new(1, 0, 0), true);
        input.Release();
        Assert.False(input.TrySample(out _));
        Assert.Throws<ArgumentOutOfRangeException>(() => input.Update(new(double.NaN, 0, 0), true));
        Assert.Throws<ArgumentOutOfRangeException>(() => new OperatorInput(clock, 501));
    }

    /// <summary>
    /// Verifies invalid yaml and unsafe values are rejected.
    /// </summary>
    [Theory]
    [InlineData("schema_version: 2")]
    [InlineData("unknown_key: 1")]
    [InlineData("transport: simulator\ntransport: serial")]
    [InlineData("transport: serial")]
    [InlineData("transport: serial\nport: COM3")]
    [InlineData("expected_vehicle_id: invalid")]
    [InlineData("expected_vehicle_id: null")]
    [InlineData("baud_rate: 9600")]
    [InlineData("buffer_bytes: 999999999")]
    [InlineData("dead_zone: 0.5")]
    [InlineData("input_freshness_ms: 500")]
    [InlineData("read_timeout_ms: -1")]
    public void Invalid_yaml_and_unsafe_values_are_rejected(string yaml)
    {
        string path = Path.GetTempFileName();
        try
        {
            File.WriteAllText(path, yaml);
            Assert.Throws<ConfigurationException>(() => StationConfiguration.Load(path));
        }
        finally
        {
            File.Delete(path);
        }
    }

    /// <summary>
    /// Verifies valid config defaults to simulation and missing or oversized files fail.
    /// </summary>
    [Fact]
    public void Valid_config_defaults_to_simulation_and_missing_or_oversized_files_fail()
    {
        var config = new StationConfiguration();
        config.Validate();
        Assert.Equal("simulator", config.Transport);
        Assert.Throws<ConfigurationException>(() => StationConfiguration.Load(Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"))));
        string path = Path.GetTempFileName();
        try
        {
            File.WriteAllText(path, "schema_version: 1\ntransport: simulator\n");
            Assert.Equal("simulator", StationConfiguration.Load(path).Transport);
            File.WriteAllText(path, new string('x', 16385));
            Assert.Throws<ConfigurationException>(() => StationConfiguration.Load(path));
        }
        finally
        {
            File.Delete(path);
        }
    }
}
