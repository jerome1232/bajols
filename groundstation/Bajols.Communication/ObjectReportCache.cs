using System.Collections.ObjectModel;
using Bajols.Protocol;

namespace Bajols.Communication;

/// <summary>Bounded per-session cache for already decoded, session-checked object snapshots.</summary>
public sealed class ObjectReportCache
{
    /// <summary>Retained source snapshots, including counters for unavailable or expired sources.</summary>
    private readonly Dictionary<byte, ReceivedObjectReport> reports = [];

    /// <summary>Returns a detached immutable projection; callers must still evaluate acquisition age.</summary>
    public IReadOnlyDictionary<byte, ReceivedObjectReport> Snapshot =>
        new ReadOnlyDictionary<byte, ReceivedObjectReport>(new Dictionary<byte, ReceivedObjectReport>(reports));

    /// <summary>Replaces one source only for a strictly newer acquisition; never grants command authorization.</summary>
    public bool Accept(ObjectReport report, long receivedAt)
    {
        if (reports.TryGetValue(report.SensorId, out var previous))
        {
            if (report.ScanSequence <= previous.Report.ScanSequence)
            {
                return false;
            }
        }
        else if (reports.Count >= 16)
        {
            return false;
        }

        reports[report.SensorId] = new(report with { Observations = Array.AsReadOnly(report.Observations.ToArray()) }, receivedAt);
        return true;
    }

    /// <summary>Clears observations and source counters when the owning connection/session ends.</summary>
    public void Clear()
    {
        reports.Clear();
    }
}
