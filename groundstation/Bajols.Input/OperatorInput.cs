using Bajols.Core;

namespace Bajols.Input;

/// <summary>Fresh operator snapshot, cleared on focus loss/disconnect. No automatic stale-value renewal.</summary>
public sealed class OperatorInput : IMovementInput
{
    private readonly TimeProvider time;
    private readonly int freshnessMs;
    private readonly double deadZone;
    public OperatorInput(TimeProvider? clock = null, int freshnessMs = 200, double deadZone = 0.05)
    {
        if (freshnessMs is < 50 or > 250 || !double.IsFinite(deadZone) || deadZone is < 0 or > 0.05)
            throw new ArgumentOutOfRangeException(nameof(freshnessMs), "Unsafe input freshness/dead-zone settings.");
        time = clock ?? TimeProvider.System; this.freshnessMs = freshnessMs; this.deadZone = deadZone;
    }
    private readonly object sync = new();
    private MovementIntent intent;
    private long sampled;
    private bool held;
    public void Update(MovementIntent value, bool enableHeld)
    {
        if (!double.IsFinite(value.Forward) || !double.IsFinite(value.Yaw) || !double.IsFinite(value.Dive) ||
            Math.Abs(value.Forward) > 1 || Math.Abs(value.Yaw) > 1 || Math.Abs(value.Dive) > 1)
            throw new ArgumentOutOfRangeException(nameof(value));
        lock (sync) { intent = value; held = enableHeld; sampled = time.GetTimestamp(); }
    }
    public void Release() { lock (sync) { held = false; intent = default; } }
    public bool TrySample(out MovementIntent value)
    {
        lock (sync)
        {
            value = default;
            if (!held || time.GetElapsedTime(sampled).TotalMilliseconds >= freshnessMs) return false;
            static double Normalize(double axis, double zone) => Math.Abs(axis) <= zone ? 0 : Math.CopySign((Math.Abs(axis) - zone) / (1 - zone), axis);
            value = new(Normalize(intent.Forward, deadZone), Normalize(intent.Yaw, deadZone), Normalize(intent.Dive, deadZone));
            return true;
        }
    }
}
