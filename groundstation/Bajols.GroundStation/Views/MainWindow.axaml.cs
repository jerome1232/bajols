using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Bajols.GroundStation.ViewModels;

namespace Bajols.GroundStation.Views;

public partial class MainWindow : Window
{
    private bool closing;
    public MainWindow()
    {
        InitializeComponent();
        Deactivated += (_, _) => Model?.ReleaseInput();
        AddHandler(KeyDownEvent, OnKeyDown, RoutingStrategies.Tunnel, handledEventsToo: true);
        AddHandler(KeyUpEvent, OnKeyUp, RoutingStrategies.Tunnel, handledEventsToo: true);
        Closing += OnClosing;
    }
    private MainWindowViewModel? Model => DataContext as MainWindowViewModel;
    private void EnablePressed(object? sender, PointerPressedEventArgs e)
    {
        if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
        e.Pointer.Capture(InputEnable); Model?.HoldInput(true); e.Handled = true;
    }
    private void EnableReleased(object? sender, PointerReleasedEventArgs e) { Model?.ReleaseInput(); e.Pointer.Capture(null); e.Handled = true; }
    private void EnableLost(object? sender, PointerCaptureLostEventArgs e) => Model?.ReleaseInput();
    private void OnKeyDown(object? sender, KeyEventArgs e) => Key(e, true);
    private void OnKeyUp(object? sender, KeyEventArgs e) => Key(e, false);
    private void Key(KeyEventArgs e, bool down)
    {
        if (Model is not { } model) return;
        switch (e.Key)
        {
            case Avalonia.Input.Key.Space: model.HoldInput(down); break;
            case Avalonia.Input.Key.W: model.Forward = down ? 0.5 : 0; break;
            case Avalonia.Input.Key.S: model.Forward = down ? -0.5 : 0; break;
            case Avalonia.Input.Key.A: model.Yaw = down ? -0.5 : 0; break;
            case Avalonia.Input.Key.D: model.Yaw = down ? 0.5 : 0; break;
            case Avalonia.Input.Key.R: model.Dive = down ? 0.5 : 0; break;
            case Avalonia.Input.Key.F: model.Dive = down ? -0.5 : 0; break;
            default: return;
        }
        e.Handled = true;
    }
    private async void OnClosing(object? sender, WindowClosingEventArgs e)
    {
        if (closing) return;
        e.Cancel = true; Model?.ReleaseInput();
        try { if (Model != null) await Model.DisposeAsync(); }
        catch (Exception failure) { System.Diagnostics.Debug.WriteLine(failure); }
        finally { closing = true; Close(); }
    }
}
