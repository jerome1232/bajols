using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Bajols.GroundStation.ViewModels;

namespace Bajols.GroundStation.Views;

/// <summary>
/// Dashboard window that owns pointer/keyboard input routing and orderly connection cleanup.
/// </summary>
public partial class MainWindow : Window
{
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
        Deactivated += (_, _) => Model?.ReleaseInput();
        AddHandler(KeyDownEvent, OnKeyDown, RoutingStrategies.Tunnel, handledEventsToo: true);
        AddHandler(KeyUpEvent, OnKeyUp, RoutingStrategies.Tunnel, handledEventsToo: true);
        Closing += OnClosing;
    }

    /// <summary>
    /// Dashboard view model currently assigned as the window data context, or null.
    /// </summary>
    private MainWindowViewModel? Model => DataContext as MainWindowViewModel;

    /// <summary>
    /// Captures a left-button hold and enables fresh operator input.
    /// </summary>
    private void EnablePressed(object? sender, PointerPressedEventArgs e)
    {
        if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
        {
            return;
        }

        e.Pointer.Capture(InputEnable);
        Model?.HoldInput(true);
        e.Handled = true;
    }

    /// <summary>
    /// Releases captured pointer input and clears previous operator intent.
    /// </summary>
    private void EnableReleased(object? sender, PointerReleasedEventArgs e)
    {
        Model?.ReleaseInput();
        e.Pointer.Capture(null);
        e.Handled = true;
    }

    /// <summary>
    /// Clears operator input when pointer capture is lost.
    /// </summary>
    private void EnableLost(object? sender, PointerCaptureLostEventArgs e) => Model?.ReleaseInput();

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
