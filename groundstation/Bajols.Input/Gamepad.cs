using System.Reflection;
using System.Runtime.InteropServices;
using Bajols.Core;

namespace Bajols.Input;

/// <summary>Controls held in the current controller sample.</summary>
[Flags]
public enum GamepadButtons
{
    /// <summary>No mapped buttons pressed.</summary>
    None = 0,
    /// <summary>Left shoulder input enable.</summary>
    Enable = 1,
    /// <summary>Right trigger.</summary>
    Arm = 2,
    /// <summary>Right shoulder.</summary>
    ClearFailsafe = 4,
    /// <summary>Left trigger.</summary>
    Disarm = 8,
    /// <summary>A (south face button).</summary>
    EmergencyStop = 16,
    /// <summary>D-pad up.</summary>
    PositiveBuoyancy = 32,
    /// <summary>D-pad down.</summary>
    NegativeBuoyancy = 64,
    /// <summary>D-pad left or right.</summary>
    NeutralBuoyancy = 128
}

/// <summary>A fresh snapshot from an SDL-mapped gamepad.</summary>
public readonly record struct GamepadSample(MovementIntent Movement, GamepadButtons Buttons);

/// <summary>Reads the first available SDL2 game controller on the UI thread.</summary>
public sealed class Gamepad : IDisposable
{
    private const uint GameControllerSubsystem = 0x2000;
    private const short TriggerPressThreshold = 19660;
    private const short TriggerReleaseThreshold = 13107;
    private bool? initialized;
    private nint controller;
    private bool armHeld;
    private bool disarmHeld;

    /// <summary>Returns a fresh sample or null when the controller is unavailable.</summary>
    public GamepadSample? Poll()
    {
        initialized ??= TryInitialize();
        if (initialized != true)
        {
            return null;
        }

        Native.SDL_GameControllerUpdate();
        if (controller != 0 && Native.SDL_GameControllerGetAttached(controller) == 0)
        {
            CloseController();
            return null;
        }

        if (controller == 0)
        {
            int count = Native.SDL_NumJoysticks();
            for (int index = 0; index < count && controller == 0; index++)
            {
                if (Native.SDL_IsGameController(index) != 0)
                {
                    controller = Native.SDL_GameControllerOpen(index);
                }
            }

            if (controller == 0)
            {
                return null;
            }

            Native.SDL_GameControllerUpdate();
        }

        double yaw = Normalize(Axis(ControllerAxis.LeftX));
        double pitch = Normalize(Axis(ControllerAxis.LeftY));
        double magnitude = Math.Max(1, Math.Sqrt(yaw * yaw + pitch * pitch));
        double throttle = -Normalize(Axis(ControllerAxis.RightY));
        armHeld = TriggerHeld(ControllerAxis.RightTrigger, armHeld);
        disarmHeld = TriggerHeld(ControllerAxis.LeftTrigger, disarmHeld);
        GamepadButtons buttons =
            Button(ControllerButton.LeftShoulder, GamepadButtons.Enable) |
            Button(ControllerButton.RightShoulder, GamepadButtons.ClearFailsafe) |
            Button(ControllerButton.A, GamepadButtons.EmergencyStop) |
            Button(ControllerButton.DpadUp, GamepadButtons.PositiveBuoyancy) |
            Button(ControllerButton.DpadDown, GamepadButtons.NegativeBuoyancy) |
            Button(ControllerButton.DpadLeft, GamepadButtons.NeutralBuoyancy) |
            Button(ControllerButton.DpadRight, GamepadButtons.NeutralBuoyancy) |
            (armHeld ? GamepadButtons.Arm : GamepadButtons.None) |
            (disarmHeld ? GamepadButtons.Disarm : GamepadButtons.None);
        return Native.SDL_GameControllerGetAttached(controller) != 0
            ? new(new(throttle, yaw / magnitude, pitch / magnitude), buttons)
            : null;
    }

    private static bool TryInitialize()
    {
        try
        {
            if (Native.SDL_InitSubSystem(GameControllerSubsystem) != 0)
            {
                return false;
            }

            Native.SDL_GameControllerEventState(0);
            Native.SDL_JoystickEventState(0);
            return true;
        }
        catch (DllNotFoundException)
        {
            return false;
        }
        catch (BadImageFormatException)
        {
            return false;
        }
    }

    private static double Normalize(short value) => value < 0 ? value / 32768d : value / 32767d;

    private short Axis(ControllerAxis axis) => Native.SDL_GameControllerGetAxis(controller, axis);

    private bool TriggerHeld(ControllerAxis axis, bool held) =>
        Axis(axis) >= (held ? TriggerReleaseThreshold : TriggerPressThreshold);

    private GamepadButtons Button(ControllerButton button, GamepadButtons value) =>
        Native.SDL_GameControllerGetButton(controller, button) != 0 ? value : GamepadButtons.None;

    private void CloseController()
    {
        if (controller != 0)
        {
            Native.SDL_GameControllerClose(controller);
            controller = 0;
        }

        armHeld = disarmHeld = false;
    }

    /// <summary>Releases the native controller and SDL subsystem.</summary>
    public void Dispose()
    {
        CloseController();
        if (initialized == true)
        {
            Native.SDL_QuitSubSystem(GameControllerSubsystem);
        }

        initialized = false;
    }

    private enum ControllerAxis { LeftX, LeftY, RightY = 3, LeftTrigger, RightTrigger }

    private enum ControllerButton { A, LeftShoulder = 9, RightShoulder, DpadUp, DpadDown, DpadLeft, DpadRight }

    private static class Native
    {
        private const string Sdl = "SDL2";

        static Native() => NativeLibrary.SetDllImportResolver(typeof(Native).Assembly, Resolve);

        private static nint Resolve(string name, Assembly assembly, DllImportSearchPath? searchPath)
        {
            if (name != Sdl)
            {
                return 0;
            }

            string library = OperatingSystem.IsLinux() ? "libSDL2-2.0.so.0" :
                OperatingSystem.IsMacOS() ? "libSDL2.dylib" : "SDL2.dll";
            return NativeLibrary.TryLoad(library, assembly, searchPath, out nint handle) ? handle : 0;
        }

        [DllImport(Sdl, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int SDL_InitSubSystem(uint flags);
        [DllImport(Sdl, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void SDL_QuitSubSystem(uint flags);
        [DllImport(Sdl, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int SDL_GameControllerEventState(int state);
        [DllImport(Sdl, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int SDL_JoystickEventState(int state);
        [DllImport(Sdl, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void SDL_GameControllerUpdate();
        [DllImport(Sdl, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int SDL_NumJoysticks();
        [DllImport(Sdl, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int SDL_IsGameController(int index);
        [DllImport(Sdl, CallingConvention = CallingConvention.Cdecl)]
        internal static extern nint SDL_GameControllerOpen(int index);
        [DllImport(Sdl, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void SDL_GameControllerClose(nint gamepad);
        [DllImport(Sdl, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int SDL_GameControllerGetAttached(nint gamepad);
        [DllImport(Sdl, CallingConvention = CallingConvention.Cdecl)]
        internal static extern short SDL_GameControllerGetAxis(nint gamepad, ControllerAxis axis);
        [DllImport(Sdl, CallingConvention = CallingConvention.Cdecl)]
        internal static extern byte SDL_GameControllerGetButton(nint gamepad, ControllerButton button);
    }
}
