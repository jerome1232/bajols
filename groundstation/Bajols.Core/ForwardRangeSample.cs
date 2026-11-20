namespace Bajols.Core;

/// <summary>
/// Result of a forward-ranging acquisition; no-return is not proof of free space.
/// </summary>
public enum ForwardRangeState
{
    /// <summary>
    /// Sensor, adapter or acquisition is unavailable.
    /// </summary>
    Unavailable,

    /// <summary>
    /// Sensor reported a return with an explicit nonnegative clearance.
    /// </summary>
    Detected,

    /// <summary>
    /// Acquisition completed without a reported return; coverage is not established.
    /// </summary>
    NoReturn,

    /// <summary>
    /// Acquisition failed or the reading cannot be trusted.
    /// </summary>
    Fault
}

/// <summary>
/// Immutable display-facing acquisition. It adds no BCP measurement assignment
/// and grants no authority to steer, stop, or arm the vehicle.
/// </summary>
public sealed record ForwardRangeSample
{
    /// <summary>
    /// Explicit acquisition state, including unavailable/no-return/fault distinctions.
    /// </summary>
    public ForwardRangeState State { get; }

    /// <summary>
    /// Millimetres from the forward sensor's reference surface, or null without a return.
    /// </summary>
    public int? DistanceMillimetres { get; }

    /// <summary>
    /// Relative azimuth in degrees: zero ahead, positive starboard, negative port.
    /// Null means no return; no elevation or object extent is inferred.
    /// </summary>
    public double? BearingDegrees { get; }

    /// <summary>
    /// Monotonic receipt timestamp in the consuming TimeProvider's clock domain.
    /// </summary>
    public long ReceivedAt { get; }

    /// <summary>
    /// Whether the reading is synthetic and must be identified as such in the UI.
    /// </summary>
    public bool IsSimulated { get; }

    /// <summary>Whether the marker is historical rather than a current detection.</summary>
    public bool IsStale { get; }

    /// <summary>Fade progress from warning colour to grey/white, from zero through one.</summary>
    public double StaleFade { get; }

    /// <summary>Advertised onboard acquisition age at receipt; 65535 means unknown.</summary>
    public ushort SampleAgeMs { get; }

    /// <summary>
    /// Creates an acquisition while rejecting contradictory state/distance combinations.
    /// </summary>
    public ForwardRangeSample(ForwardRangeState state, int? distanceMillimetres, double? bearingDegrees, long receivedAt, bool isSimulated, bool isStale = false, double staleFade = 0, ushort sampleAgeMs = 0)
    {
        if (!Enum.IsDefined(state) || state == ForwardRangeState.Detected && distanceMillimetres is not >= 0 ||
            state != ForwardRangeState.Detected && distanceMillimetres is not null ||
            state == ForwardRangeState.Detected && (bearingDegrees is not double bearing || !double.IsFinite(bearing) || bearing < -180 || bearing >= 180) ||
            state != ForwardRangeState.Detected && bearingDegrees is not null)
        {
            throw new ArgumentException("A return requires nonnegative range and finite bearing in [-180,180); other states require neither.");
        }

        if (!double.IsFinite(staleFade) || staleFade < 0 || staleFade > 1)
        {
            throw new ArgumentOutOfRangeException(nameof(staleFade));
        }

        IsStale = isStale;
        StaleFade = isStale ? staleFade : 0;
        SampleAgeMs = sampleAgeMs;
        State = state;
        DistanceMillimetres = distanceMillimetres;
        BearingDegrees = bearingDegrees;
        ReceivedAt = receivedAt;
        IsSimulated = isSimulated;
    }

    /// <summary>
    /// Checks observation age using a monotonic clock; future and exact-expiry samples fail.
    /// </summary>
    public bool IsFresh(TimeProvider clock, TimeSpan maximumAge)
    {
        TimeSpan age = clock.GetElapsedTime(ReceivedAt);
        return !IsStale && SampleAgeMs != ushort.MaxValue && maximumAge > TimeSpan.Zero && age >= TimeSpan.Zero &&
            age.TotalMilliseconds + SampleAgeMs < maximumAge.TotalMilliseconds;
    }

    /// <summary>
    /// Returns display urgency in the selected forward range; null means no fresh in-range detection.
    /// </summary>
    public double? Proximity(TimeProvider clock, TimeSpan maximumAge, int displayRangeMillimetres = 6000)
    {
        if (displayRangeMillimetres <= 0 || !IsFresh(clock, maximumAge) || State != ForwardRangeState.Detected ||
            DistanceMillimetres is not int distance || distance > displayRangeMillimetres ||
            BearingDegrees is not double bearing || Math.Abs(bearing) > 90)
        {
            return null;
        }

        return 1 - distance / (double)displayRangeMillimetres;
    }
}
