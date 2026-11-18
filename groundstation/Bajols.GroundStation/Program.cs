using Avalonia;

namespace Bajols.GroundStation;

/// <summary>
/// Desktop process entry point and platform-specific Avalonia bootstrap configuration.
/// </summary>
internal static class Program
{
    /// <summary>
    /// Starts the Avalonia desktop application using the supplied command-line arguments.
    /// </summary>
    [STAThread]
    public static void Main(string[] args) => BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);

    /// <summary>
    /// Configures platform detection, bundled fonts and diagnostic logging for the desktop application.
    /// </summary>
    public static AppBuilder BuildAvaloniaApp() => AppBuilder.Configure<App>().UsePlatformDetect().WithInterFont().LogToTrace();
}
