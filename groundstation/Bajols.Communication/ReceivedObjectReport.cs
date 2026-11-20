using Bajols.Protocol;
using Bajols.Core;

namespace Bajols.Communication;

/// <summary>Retains a per-sensor snapshot without renewing acquisition age.</summary>
/// <param name="Report">Complete source snapshot with onboard acquisition age.</param>
/// <param name="ReceivedAt">Monotonic local receipt timestamp.</param>
public sealed record ReceivedObjectReport(ObjectReport Report, long ReceivedAt)
{
    /// <summary>Checks source readiness and combined onboard/local acquisition age.</summary>
    public bool IsFresh(TimeProvider clock, TimeSpan maximumAge)
    {
        double elapsed = clock.GetElapsedTime(ReceivedAt).TotalMilliseconds;
        return Report.SensorState == 1 && Report.SampleAgeMs != ushort.MaxValue && elapsed >= 0 &&
            Report.SampleAgeMs + elapsed < maximumAge.TotalMilliseconds;
    }

    /// <summary>
    /// Projects the latest ready snapshot into forward markers, retaining expired returns as historical.
    /// A newer empty/fault snapshot removes previous returns through normal snapshot replacement.
    /// </summary>
    public IReadOnlyList<ForwardRangeSample> ForwardMarkers(TimeProvider clock, TimeSpan maximumAge)
    {
        double elapsed = clock.GetElapsedTime(ReceivedAt).TotalMilliseconds;
        if (Report.SensorState != 1 || elapsed < 0 || maximumAge <= TimeSpan.Zero)
        {
            return Array.Empty<ForwardRangeSample>();
        }

        bool stale = !IsFresh(clock, maximumAge);
        double fade = Report.SampleAgeMs == ushort.MaxValue ? 1 :
            Math.Clamp((Report.SampleAgeMs + elapsed - maximumAge.TotalMilliseconds) / 1000, 0, 1);
        return Report.Observations.Where(o =>
            (o.QualityFlags & (TelemetryQuality.Valid | TelemetryQuality.SensorFault)) == TelemetryQuality.Valid &&
            o.SensorRangeMm <= 6000 && Math.Abs(o.BearingCdeg) <= 9000)
            .Select(o => new ForwardRangeSample(ForwardRangeState.Detected, (int)o.SensorRangeMm, o.BearingCdeg / 100d,
                ReceivedAt, (o.QualityFlags & TelemetryQuality.Simulated) != 0, stale, fade, Report.SampleAgeMs)).ToArray();
    }
}
