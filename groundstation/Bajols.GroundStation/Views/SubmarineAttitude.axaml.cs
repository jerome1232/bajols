using Avalonia.Controls;

namespace Bajols.GroundStation.Views;

/// <summary>
/// Telemetry-driven submarine silhouettes for pitch and magnetic heading.
/// </summary>
public partial class SubmarineAttitude : UserControl
{
    /// <summary>
    /// Loads the telemetry-driven side and top submarine silhouette views.
    /// </summary>
    public SubmarineAttitude() => InitializeComponent();
}
