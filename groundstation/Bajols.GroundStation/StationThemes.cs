using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Styling;

namespace Bajols.GroundStation;

internal static class StationThemes
{
    public static readonly ThemeVariant Green = new("Green CRT", ThemeVariant.Dark);
    public static readonly ThemeVariant Amber = new("Amber CRT", ThemeVariant.Dark);

    public static readonly FontFamily Monospace = new("Cascadia Mono, Consolas, Menlo, DejaVu Sans Mono, Courier New, monospace");

    private static readonly (string Key, string Modern, string Green, string Amber)[] Colors =
    [
        ("StationBackground", "#101820", "#09130E", "#171006"),
        ("StationPanel", "#1B2833", "#102219", "#291C0C"),
        ("StationMuted", "#A9BAC8", "#A3C8A9", "#D5B78D"),
        ("StationNotice", "#F2C879", "#F2D883", "#FFD99C"),
        ("StationNoticeBackground", "#342D20", "#2F2A15", "#3B2A12"),
        ("StationOutline", "#4B6C7D", "#527C5A", "#896336"),
        ("StationAccent", "#69CCC4", "#83F49C", "#FFC36A"),
        ("StationDisplay", "#101C27", "#08150D", "#1A1106"),
        ("StationOverlay", "#CC101C27", "#E608150D", "#E61A1106"),
        ("StationHighlight", "#D4E7EC", "#D0FAD6", "#FFE2B9"),
        ("StationInnerGrid", "#243B4B", "#25442D", "#513819"),
        ("StationCrosshair", "#365162", "#395D41", "#684B24"),
        ("StationMarker", "#F2F6FA", "#E4FFE7", "#FFF0D4")
    ];

    public static void Register(IResourceDictionary resources)
    {
        var modernResources = new ResourceDictionary();
        foreach (var (key, modern, _, _) in Colors)
        {
            modernResources[key] = Brush.Parse(modern);
        }

        modernResources["StationFont"] = FontFamily.Default;
        modernResources["StationPanelRadius"] = new CornerRadius(8);
        modernResources["StationInsetRadius"] = new CornerRadius(6);
        modernResources["StationPhosphor"] = Brushes.Transparent;
        resources.ThemeDictionaries.Add(ThemeVariant.Dark, modernResources);
        resources.ThemeDictionaries.Add(Green, CreateRetro(false));
        resources.ThemeDictionaries.Add(Amber, CreateRetro(true));
    }

    private static ResourceDictionary CreateRetro(bool amber)
    {
        var resources = new ResourceDictionary();
        foreach (var (key, _, green, amberColor) in Colors)
        {
            resources[key] = Brush.Parse(amber ? amberColor : green);
        }

        resources["StationFont"] = Monospace;
        resources["StationPanelRadius"] = new CornerRadius(0);
        resources["StationInsetRadius"] = new CornerRadius(0);
        resources["StationPhosphor"] = resources["StationAccent"];
        resources["ControlCornerRadius"] = new CornerRadius(0);
        resources["OverlayCornerRadius"] = new CornerRadius(0);
        resources["SystemControlForegroundBaseHighBrush"] = resources["StationHighlight"];
        resources["SystemControlBackgroundBaseLowBrush"] = resources["StationOutline"];
        resources["SystemAccentColor"] = Color.Parse(amber ? "#FFC36A" : "#83F49C");
        return resources;
    }
}
