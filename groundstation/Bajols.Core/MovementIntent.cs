namespace Bajols.Core;

/// <summary>
/// High-level effort, not actuator outputs: ahead, starboard and down are positive.
/// Input adapters must normalize each axis to [-1, 1]. Validation and authorization
/// must precede transmission; this data type alone grants no control permission.
/// </summary>
/// <param name="Forward">The forward value supplied to the constructor.</param>
/// <param name="Yaw">The yaw value supplied to the constructor.</param>
/// <param name="Dive">The dive value supplied to the constructor.</param>
public readonly record struct MovementIntent(double Forward, double Yaw, double Dive);
