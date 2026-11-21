using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Threading;
using Bajols.GroundStation.ViewModels;
using Bajols.Input;

namespace Bajols.GroundStation.Views;

/// <summary>
/// Dashboard window that owns pointer/keyboard input routing and orderly connection cleanup.
/// </summary>
public partial class MainWindow : Window
{
    private readonly Gamepad gamepad = new();
    private readonly DispatcherTimer gamepadTimer;
    private GamepadButtons? previousGamepadButtons;

    /// <summary>
    /// Whether asynchronous cleanup has completed sufficiently to allow final window closure.
    /// </summary>
    private bool closing;

    /// <summary>
    /// Loads the dashboard and registers focus, keyboard and shutdown handlers.
    /// </summary>
    public MainWindow()
    {
        InitializeComponent();
        gamepadTimer = new(TimeSpan.FromMilliseconds(25), DispatcherPriority.Input, (_, _) => PollGamepad());
        gamepadTimer.Stop();
        Activated += (_, _) => gamepadTimer.Start();
        Deactivated += (_, _) =>
        {
            gamepadTimer.Stop();
            previousGamepadButtons = null;
            Model?.ReleaseInput();
        };
        Closed += (_, _) =>
        {
            gamepadTimer.Stop();
            gamepad.Dispose();
        };
        AddHandler(KeyDownEvent, OnKeyDown, RoutingStrategies.Tunnel, handledEventsToo: true);
        AddHandler(KeyUpEvent, OnKeyUp, RoutingStrategies.Tunnel, handledEventsToo: true);
        Closing += OnClosing;
    }

    /// <summary>
    /// Dashboard view model currently assigned as the window data context, or null.
    /// </summary>
    private MainWindowViewModel? Model => DataContext as MainWindowViewModel;

    private async void ShowControls(object? sender, RoutedEventArgs e)
    {
        Model?.ReleaseInput();
        await new ControlsDialog().ShowDialog(this);
    }

    private void PollGamepad()
    {
        if (Model is not { } model)
        {
            previousGamepadButtons = null;
            return;
        }

        if (gamepad.Poll() is not { } sample)
        {
            previousGamepadButtons = null;
            if (model.GamepadInputHeld)
            {
                model.ReleaseInput();
            }
            return;
        }

        GamepadButtons buttons = sample.Buttons;
        // Establish a baseline on attach and focus gain so held buttons
        // cannot start a hold or issue commands without a fresh press.
        GamepadButtons pressed = buttons & ~(previousGamepadButtons ?? buttons);
        previousGamepadButtons = buttons;
        if ((buttons & GamepadButtons.Enable) != 0)
        {
            model.UpdateGamepadInput(sample.Movement, (pressed & GamepadButtons.Enable) != 0);
            if (model.GamepadInputHeld)
            {
                const GamepadButtons buoyancyButtons = GamepadButtons.PositiveBuoyancy |
                    GamepadButtons.NegativeBuoyancy | GamepadButtons.NeutralBuoyancy;
                if ((pressed & buoyancyButtons) != 0)
                {
                    model.BuoyancyPosition = (buttons & buoyancyButtons) switch
                    {
                        GamepadButtons.PositiveBuoyancy => 1,
                        GamepadButtons.NegativeBuoyancy => -1,
                        _ => 0
                    };
                }
            }
        }
        else if (model.GamepadInputHeld)
        {
            model.ReleaseInput();
        }

        // Stop inputs take precedence over arm and clear requests.
        if ((buttons & GamepadButtons.EmergencyStop) != 0)
        {
            if ((pressed & GamepadButtons.EmergencyStop) != 0)
            {
                model.EmergencyStopCommand.Execute(null);
            }
        }
        else if ((buttons & GamepadButtons.Disarm) != 0)
        {
            if ((pressed & GamepadButtons.Disarm) != 0)
            {
                model.DisarmCommand.Execute(null);
            }
        }
        else if ((pressed & GamepadButtons.ClearFailsafe) != 0)
        {
            model.ClearFailsafeCommand.Execute(null);
        }
        else if ((pressed & GamepadButtons.Arm) != 0)
        {
            model.ArmCommand.Execute(null);
        }
    }

    /// <summary>
    /// Applies a pressed keyboard control through the shared key-mapping handler.
    /// </summary>
    private void OnKeyDown(object? sender, KeyEventArgs e) => Key(e, true);

    /// <summary>
    /// Releases a keyboard control through the shared key-mapping handler.
    /// </summary>
    private void OnKeyUp(object? sender, KeyEventArgs e) => Key(e, false);

    /// <summary>
    /// Maps Space and movement keys to normalized operator input while consuming handled events.
    /// </summary>
    private void Key(KeyEventArgs e, bool down)
    {
        if (Model is not { } model)
        {
            return;
        }

        if (model.GamepadInputHeld && e.Key is Avalonia.Input.Key.Space or Avalonia.Input.Key.W or
            Avalonia.Input.Key.S or Avalonia.Input.Key.A or Avalonia.Input.Key.D or Avalonia.Input.Key.R or Avalonia.Input.Key.F)
        {
            e.Handled = true;
            return;
        }

        switch (e.Key)
        {
            case Avalonia.Input.Key.Space:
                model.HoldInput(down);
                break;
            case Avalonia.Input.Key.W:
                model.Forward = down ? 0.5 : 0;
                break;
            case Avalonia.Input.Key.S:
                model.Forward = down ? -0.5 : 0;
                break;
            case Avalonia.Input.Key.A:
                model.Yaw = down ? -0.5 : 0;
                break;
            case Avalonia.Input.Key.D:
                model.Yaw = down ? 0.5 : 0;
                break;
            case Avalonia.Input.Key.R:
                model.Dive = down ? 0.5 : 0;
                break;
            case Avalonia.Input.Key.F:
                model.Dive = down ? -0.5 : 0;
                break;
            default:
                return;
        }

        e.Handled = true;
    }

    /// <summary>
    /// Releases input and completes asynchronous connection cleanup before allowing the window to close.
    /// </summary>
    private async void OnClosing(object? sender, WindowClosingEventArgs e)
    {
        if (closing)
        {
            return;
        }

        e.Cancel = true;
        gamepadTimer.Stop();
        Model?.ReleaseInput();
        try
        {
            if (Model != null)
            {
                await Model.DisposeAsync();
            }
        }
        catch (Exception failure)
        {
            System.Diagnostics.Debug.WriteLine(failure);
        }
        finally
        {
            closing = true;
            Close();
        }
    }
}
