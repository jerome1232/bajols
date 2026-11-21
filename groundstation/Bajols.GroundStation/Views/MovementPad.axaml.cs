using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Data;
using Avalonia.Input;

namespace Bajols.GroundStation.Views;

/// <summary>
/// Spring-centered circular movement input. It changes intent only, never input
/// enable, arming, buoyancy, or the independent throttle axis.
/// </summary>
public partial class MovementPad : UserControl
{
    /// <summary>
    /// Two-way normalized dive effort; negative values request nose up.
    /// </summary>
    public static readonly StyledProperty<double> DiveProperty =
        AvaloniaProperty.Register<MovementPad, double>(nameof(Dive), defaultBindingMode: BindingMode.TwoWay);

    /// <summary>
    /// Two-way normalized yaw effort; positive values request starboard.
    /// </summary>
    public static readonly StyledProperty<double> YawProperty =
        AvaloniaProperty.Register<MovementPad, double>(nameof(Yaw), defaultBindingMode: BindingMode.TwoWay);

    /// <summary>
    /// Horizontal center in drawing coordinates, leaving room for side percentages.
    /// </summary>
    private const double CenterX = 140;

    /// <summary>
    /// Vertical center in drawing coordinates, independent of display scale.
    /// </summary>
    private const double CenterY = 120;

    /// <summary>
    /// Maximum pointer displacement represented as normalized unit effort.
    /// </summary>
    private const double Radius = 78;

    /// <summary>
    /// Fixed-coordinate canvas used to translate pointer positions through the viewbox.
    /// </summary>
    private readonly Canvas surface;

    /// <summary>
    /// Indicator of the current dive/yaw input, including keyboard changes.
    /// </summary>
    private readonly Ellipse puck;

    /// <summary>
    /// Line connecting neutral to the current input indicator.
    /// </summary>
    private readonly Line vector;

    /// <summary>
    /// Nose-up percentage between the top direction label and disk.
    /// </summary>
    private readonly TextBlock pitchUpPercentage;

    /// <summary>
    /// Nose-down percentage between the bottom direction label and disk.
    /// </summary>
    private readonly TextBlock pitchDownPercentage;

    /// <summary>
    /// Port-only yaw percentage immediately left of the disk.
    /// </summary>
    private readonly TextBlock portPercentage;

    /// <summary>
    /// Starboard-only yaw percentage immediately right of the disk.
    /// </summary>
    private readonly TextBlock starboardPercentage;

    /// <summary>
    /// Captured dragging pointer; other simultaneous pointers cannot replace it.
    /// </summary>
    private IPointer? draggingPointer;

    /// <summary>
    /// Requested downward-positive effort, bound to the existing input view model.
    /// </summary>
    public double Dive
    {
        get => GetValue(DiveProperty);
        set => SetValue(DiveProperty, value);
    }

    /// <summary>
    /// Requested starboard-positive yaw, bound independently from dive effort.
    /// </summary>
    public double Yaw
    {
        get => GetValue(YawProperty);
        set => SetValue(YawProperty, value);
    }

    /// <summary>
    /// Loads the circular control and registers bounded pointer/lifecycle handlers.
    /// </summary>
    public MovementPad()
    {
        InitializeComponent();
        surface = this.FindControl<Canvas>("InputSurface")!;
        puck = this.FindControl<Ellipse>("Puck")!;
        vector = this.FindControl<Line>("IntentVector")!;
        pitchUpPercentage = this.FindControl<TextBlock>("PitchUpPercentage")!;
        pitchDownPercentage = this.FindControl<TextBlock>("PitchDownPercentage")!;
        portPercentage = this.FindControl<TextBlock>("PortPercentage")!;
        starboardPercentage = this.FindControl<TextBlock>("StarboardPercentage")!;
        surface.PointerPressed += OnPressed;
        surface.PointerMoved += OnMoved;
        surface.PointerReleased += OnReleased;
        surface.PointerCaptureLost += OnCaptureLost;
        DetachedFromVisualTree += OnDetached;
        UpdateVisual();
    }

    /// <summary>
    /// Reflects bound keyboard, reset and pointer updates in the displayed vector.
    /// </summary>
    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == DiveProperty || change.Property == YawProperty)
        {
            UpdateVisual();
        }
    }

    /// <summary>
    /// Clamps the visual indicator to the circle without changing externally supplied intent.
    /// </summary>
    private void UpdateVisual()
    {
        if (puck is null || vector is null || pitchUpPercentage is null || pitchDownPercentage is null ||
            portPercentage is null || starboardPercentage is null)
        {
            return;
        }

        double dive = double.IsFinite(Dive) ? Math.Clamp(Dive, -1, 1) : 0;
        double yaw = double.IsFinite(Yaw) ? Math.Clamp(Yaw, -1, 1) : 0;
        double magnitude = Math.Max(1, Math.Sqrt(dive * dive + yaw * yaw));
        double x = CenterX + Radius * yaw / magnitude;
        double y = CenterY + Radius * dive / magnitude;
        Canvas.SetLeft(puck, x - 9);
        Canvas.SetTop(puck, y - 9);
        vector.EndPoint = new Point(x, y);
        pitchUpPercentage.Text = $"{Math.Max(-dive, 0):0%}";
        pitchDownPercentage.Text = $"{Math.Max(dive, 0):0%}";
        portPercentage.Text = $"{Math.Max(-yaw, 0):0%}";
        starboardPercentage.Text = $"{Math.Max(yaw, 0):0%}";
    }

    /// <summary>
    /// Maps scaled pointer coordinates to a unit disk while preserving axis signs.
    /// </summary>
    private void ApplyPointer(Point position)
    {
        double yaw = (position.X - CenterX) / Radius;
        double dive = (position.Y - CenterY) / Radius;
        double magnitude = Math.Max(1, Math.Sqrt(dive * dive + yaw * yaw));
        SetCurrentValue(DiveProperty, dive / magnitude);
        SetCurrentValue(YawProperty, yaw / magnitude);
    }

    /// <summary>
    /// Captures a new left-button drag without enabling input or authorizing commands.
    /// </summary>
    private void OnPressed(object? sender, PointerPressedEventArgs e)
    {
        if (draggingPointer is not null || !e.GetCurrentPoint(surface).Properties.IsLeftButtonPressed)
        {
            return;
        }

        Focus();
        draggingPointer = e.Pointer;
        e.Pointer.Capture(surface);
        ApplyPointer(e.GetPosition(surface));
        e.Handled = true;
    }

    /// <summary>
    /// Updates the current drag even outside the disk; normalized effort remains bounded.
    /// </summary>
    private void OnMoved(object? sender, PointerEventArgs e)
    {
        if (draggingPointer != e.Pointer)
        {
            return;
        }

        if (!e.GetCurrentPoint(surface).Properties.IsLeftButtonPressed)
        {
            ReleasePointer();
            return;
        }

        ApplyPointer(e.GetPosition(surface));
        e.Handled = true;
    }

    /// <summary>
    /// Returns both pad axes to neutral when the operator releases the captured pointer.
    /// </summary>
    private void OnReleased(object? sender, PointerReleasedEventArgs e)
    {
        if (draggingPointer != e.Pointer)
        {
            return;
        }

        ReleasePointer();
        e.Handled = true;
    }

    /// <summary>
    /// Cancels intent when the platform or another control takes pointer capture.
    /// </summary>
    private void OnCaptureLost(object? sender, PointerCaptureLostEventArgs e)
    {
        if (draggingPointer == e.Pointer)
        {
            ReleasePointer();
        }
    }

    /// <summary>
    /// Cancels any live pointer intent when this control leaves the window.
    /// </summary>
    private void OnDetached(object? sender, VisualTreeAttachmentEventArgs e)
    {
        if (draggingPointer is not null)
        {
            ReleasePointer();
        }
    }

    /// <summary>
    /// Releases capture and neutralizes the pad while retaining its two-way bindings.
    /// </summary>
    private void ReleasePointer()
    {
        IPointer? pointer = draggingPointer;
        draggingPointer = null;
        SetCurrentValue(DiveProperty, 0);
        SetCurrentValue(YawProperty, 0);
        if (pointer?.Captured == surface)
        {
            pointer.Capture(null);
        }
    }
}
