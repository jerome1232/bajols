using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Bajols.GroundStation.ViewModels;
using Bajols.GroundStation.Views;

namespace Bajols.GroundStation;

/// <summary>
/// Avalonia application resources and desktop-window composition root.
/// </summary>
public partial class App : Application
{
    /// <summary>
    /// Loads application resources and the Avalonia theme.
    /// </summary>
    public override void Initialize()
    {
        AvaloniaXamlLoader.Load(this);
        StationThemes.Register(Resources);
    }

    /// <summary>
    /// Creates the desktop window and view model after Avalonia platform initialization.
    /// </summary>
    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            desktop.MainWindow = new MainWindow
            {
                DataContext = new MainWindowViewModel()
            };
        }

        base.OnFrameworkInitializationCompleted();
    }
}
