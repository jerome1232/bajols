using Bajols.Core;

namespace Bajols.Input;

/// <summary>
/// Future keyboard/gamepad/virtual-joystick adapter. Samples must be newly read;
/// cached input must never masquerade as fresh input after device loss.
/// </summary>
public interface IMovementInput
{
    bool TrySample(out MovementIntent intent);
}
