using Bajols.Core;

namespace Bajols.Input;

/// <summary>Fresh operator snapshot, cleared on focus loss/disconnect. No automatic stale-value renewal.</summary>
public sealed class OperatorInput : IMovementInput
{
    /// <summary>
    /// Monotonic clock used to expire operator snapshots independently of communication.
    /// </summary>
    private readonly TimeProvider time;

    /// <summary>
    /// Maximum permitted age of a held operator sample in milliseconds.
    /// </summary>
    private readonly int freshnessMs;

    /// <summary>
    /// Normalized axis dead zone applied before producing movement intent.
    /// </summary>
    private readonly double deadZone;

    /// <summary>
    /// Validates input freshness and dead-zone bounds and selects the monotonic clock.
    /// </summary>
    public OperatorInput(TimeProvider? clock = null, int freshnessMs = 200, double deadZone = 0.05)
    {
        if (freshnessMs is < 50 or > 250 || !double.IsFinite(deadZone) || deadZone is < 0 or > 0.05)
        {
            throw new ArgumentOutOfRangeException(nameof(freshnessMs), "Unsafe input freshness/dead-zone settings.");
        }

        time = clock ?? TimeProvider.System;
        this.freshnessMs = freshnessMs;
        this.deadZone = deadZone;
    }

    /// <summary>
    /// Synchronization guard protecting the current operator snapshot and hold state.
    /// </summary>
    private readonly object sync = new();

    /// <summary>
    /// Most recently supplied finite normalized movement effort.
    /// </summary>
    private MovementIntent intent;

    /// <summary>
    /// Monotonic timestamp of the latest supplied operator sample.
    /// </summary>
    private long sampled;

    /// <summary>
    /// Whether the current snapshot was supplied with input enable held.
    /// </summary>
    private bool held;

    /// <summary>
    /// Stores finite normalized operator effort and its fresh hold-enable state.
    /// </summary>
    public void Update(MovementIntent value, bool enableHeld)
    {
        if (!double.IsFinite(value.Forward) || !double.IsFinite(value.Yaw) || !double.IsFinite(value.Dive) || Math.Abs(value.Forward) > 1 || Math.Abs(value.Yaw) > 1 || Math.Abs(value.Dive) > 1)
        {
            throw new ArgumentOutOfRangeException(nameof(value));
        }

        lock (sync)
        {
            intent = value;
            held = enableHeld;
            sampled = time.GetTimestamp();
        }
    }

    /// <summary>
    /// Clears held input and previous movement intent after release, focus loss or disconnect.
    /// </summary>
    public void Release()
    {
        lock (sync)
        {
            held = false;
            intent = default;
        }
    }

    /// <summary>
    /// Returns normalized effort only while hold-enable input remains fresh.
    /// </summary>
    public bool TrySample(out MovementIntent value)
    {
        lock (sync)
        {
            value = default;
            if (!held || time.GetElapsedTime(sampled).TotalMilliseconds >= freshnessMs)
            {
                return false;
            }

            // Applies the input dead zone while retaining the full normalized effort range.
            static double Normalize(double axis, double zone) => Math.Abs(axis) <= zone ? 0 : Math.CopySign((Math.Abs(axis) - zone) / (1 - zone), axis);
            value = new(Normalize(intent.Forward, deadZone), Normalize(intent.Yaw, deadZone), Normalize(intent.Dive, deadZone));
            return true;
        }
    }
}
