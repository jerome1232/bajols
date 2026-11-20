namespace Bajols.Protocol;

/// <summary>Bounded body-relative object observation.</summary>
/// <param name="ObservationId">Nonzero snapshot-local identifier, not a persistent track.</param>
/// <param name="SensorRangeMm">Radial range from the sensor reference face in millimetres.</param>
/// <param name="VehicleRangeMm">Estimated range from the vehicle datum in millimetres.</param>
/// <param name="BearingCdeg">Body-relative bearing in centidegrees; positive starboard.</param>
/// <param name="ElevationCdeg">Elevation positive up, or 32767 when unknown.</param>
/// <param name="RangeHalfWidthMm">Vehicle-range uncertainty, or 65535 when unknown.</param>
/// <param name="BearingHalfWidthCdeg">Bearing uncertainty, or 65535 when unknown.</param>
/// <param name="ElevationHalfWidthCdeg">Elevation uncertainty, or 65535 when unknown.</param>
/// <param name="QualityFlags">Validity and provenance flags shared with telemetry.</param>
public sealed record ObjectObservation(ushort ObservationId, uint SensorRangeMm, uint VehicleRangeMm, short BearingCdeg, short ElevationCdeg, ushort RangeHalfWidthMm, ushort BearingHalfWidthCdeg, ushort ElevationHalfWidthCdeg, TelemetryQuality QualityFlags);

/// <summary>Bounded per-sensor acquisition snapshot; grants no control authority.</summary>
/// <param name="FormatRevision">Observation layout revision, currently one.</param>
/// <param name="SensorId">Stable configured source identifier from 1 through 254.</param>
/// <param name="ScanSequence">Nonwrapping acquisition counter within the vehicle boot.</param>
/// <param name="SampleAgeMs">Age of oldest constituent acquisition; 65535 means unknown.</param>
/// <param name="SensorState">Zero unavailable, one ready, two fault, three uncalibrated.</param>
/// <param name="ReportFlags">Bit zero truncated, bit one simulated.</param>
/// <param name="Observations">Complete replacement snapshot, containing at most four returns.</param>
public sealed record ObjectReport(byte FormatRevision, byte SensorId, uint ScanSequence, ushort SampleAgeMs, byte SensorState, byte ReportFlags, IReadOnlyList<ObjectObservation> Observations) : BcpMessage(MessageType.ObjectReport);
