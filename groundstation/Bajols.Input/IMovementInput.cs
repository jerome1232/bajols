using Bajols.Core;

namespace Bajols.Input;

/// <summary>
/// Future keyboard/gamepad/virtual-joystick adapter. Samples must be newly read;
/// cached input must never masquerade as fresh input after device loss.
/// </summary>
public interface IMovementInput
{
    /// <summary>
    /// Attempts to obtain newly sampled normalized effort; returns false when operator input is unavailable or stale.
    /// </summary>
    bool TrySample(out MovementIntent intent);
}
