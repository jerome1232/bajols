using Avalonia.Controls;

namespace Bajols.GroundStation.Views;

/// <summary>
/// Unified telemetry-driven 3D submarine pose for pitch and magnetic heading.
/// </summary>
public partial class SubmarineAttitude : UserControl
{
    /// <summary>
    /// Loads the unified perspective view and its telemetry provenance labels.
    /// </summary>
    public SubmarineAttitude() => InitializeComponent();
}
