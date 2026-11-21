using Avalonia.Controls;
using Avalonia.Interactivity;

namespace Bajols.GroundStation.Views;

/// <summary>Displays the keyboard, on-screen and gamepad control reference.</summary>
public sealed partial class ControlsDialog : Window
{
    /// <summary>Loads the control reference.</summary>
    public ControlsDialog() => InitializeComponent();

    private void CloseDialog(object? sender, RoutedEventArgs e) => Close();
}
