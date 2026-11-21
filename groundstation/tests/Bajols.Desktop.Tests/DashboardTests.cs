using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.Input.Raw;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using Avalonia.VisualTree;
using Bajols.Configuration;
using Bajols.Core;
using Bajols.GroundStation;
using Bajols.GroundStation.ViewModels;
using Bajols.GroundStation.Views;
using Bajols.Protocol;
using Xunit;

namespace Bajols.Desktop.Tests;

/// <summary>
/// Headless tests of live dashboard state, operator input and reconnect behavior.
/// </summary>
public sealed class DashboardTests
{
    /// <summary>
    /// Verifies dashboard confirms live simulation releases input and never rearms on reconnect.
    /// </summary>
    [Fact]
    public void Dashboard_confirms_live_simulation_releases_input_and_never_rearms_on_reconnect()
    {
        AppBuilder.Configure<App>().UseSkia().UseHeadless(new AvaloniaHeadlessPlatformOptions
        {
            UseHeadlessDrawing = false
        }).SetupWithoutStarting();
        var vm = new MainWindowViewModel(() => new StationConfiguration(), TimeProvider.System);
        var window = new MainWindow
        {
            DataContext = vm
        };
        window.Show();
        try
        {
            Assert.Equal(ConnectionState.Disconnected, vm.Connection);
            Assert.Equal("UNKNOWN", vm.VehicleState);
            Assert.False(vm.ArmCommand.CanExecute(null));
            Assert.False(vm.DisarmCommand.CanExecute(null));
            AssertDashboardFits(window);
            AssertAttitudeProjection(window);
            vm.HoldInput(true);
            vm.Forward = 0.5;
            window.FindControl<Button>("ControlsButton")!.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            var controls = Assert.Single(window.OwnedWindows.OfType<ControlsDialog>());
            Assert.Equal(0, vm.Forward);
            var controlLabels = controls.GetVisualDescendants().OfType<TextBlock>().Select(text => text.Text);
            Assert.Contains("Space", controlLabels);
            Assert.Contains("Right trigger", controlLabels);
            controls.FindControl<Button>("CloseControlsButton")!.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Assert.Empty(window.OwnedWindows.OfType<ControlsDialog>());
            double defaultWidth = window.Width;
            double defaultHeight = window.Height;
            window.Width = window.MinWidth;
            window.Height = window.MinHeight;
            AssertDashboardFits(window);
            window.Width = defaultWidth;
            window.Height = defaultHeight;
            AssertMovementPad(window, vm);
            var buoyancyControl = window.FindControl<Slider>("BuoyancyControl")!;
            Assert.Equal(0, buoyancyControl.Value);
            Assert.True(buoyancyControl.IsSnapToTickEnabled);
            Assert.False(buoyancyControl.IsDirectionReversed);
            buoyancyControl.Value = 1;
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(1, vm.BuoyancyIndex);
            Assert.Equal(1, vm.BuoyancyPosition);
            buoyancyControl.Value = -1;
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(2, vm.BuoyancyIndex);
            Assert.Equal(-1, vm.BuoyancyPosition);
            vm.ReleaseInput();
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(0, buoyancyControl.Value);
            var throttleBounds = window.FindControl<Slider>("ThrottleControl")!.TranslatePoint(new Point(0, 0), window)!.Value;
            var buoyancyBounds = buoyancyControl.TranslatePoint(new Point(0, 0), window)!.Value;
            Assert.True(buoyancyBounds.X > throttleBounds.X);
            Assert.DoesNotContain(window.GetVisualDescendants().OfType<TextBlock>(), text => text.Text?.Contains("Hold Space") == true);
            Wait(vm.ConnectAsync(), vm);
            Until(() => vm.LinkFreshness == "CONFIRMED", vm);
            Assert.Equal("Disarmed", vm.VehicleState);
            AssertDashboardFits(window);
            Until(() => vm.Depth.EndsWith("SIM", StringComparison.Ordinal), vm);
            Assert.True(vm.HasAttitude);
            Assert.True(vm.HasDepth);
            Assert.Equal(2, vm.DepthMetres);
            Assert.InRange(vm.DepthIndicatorOffset, 27, 28);
            var attitudeScene = window.GetVisualDescendants().OfType<SubmarineScene>().Single();
            Assert.True(attitudeScene.HasAttitude);
            Assert.Equal(316, attitudeScene.Bounds.Height);
            Assert.True(attitudeScene.TopOverlayHeight > 0);
            Assert.Equal(vm.PitchAngle, attitudeScene.Pitch);
            Assert.Equal(90, attitudeScene.Heading);
            Assert.Equal("90 ° E SIM", vm.Heading);
            Assert.Equal("E", window.GetVisualDescendants().OfType<TextBlock>().Single(t => t.Name == "HeadingDirection")!.Text);
            Assert.Equal("90 ° SIM", window.GetVisualDescendants().OfType<TextBlock>().Single(t => t.Name == "HeadingDegrees")!.Text);
            var attitudeView = window.GetVisualDescendants().OfType<SubmarineAttitude>().Single();
            var simulateReturn = attitudeView.FindControl<CheckBox>("SimulateForwardObstacle")!;
            var returnDistance = attitudeView.FindControl<Slider>("SimulatedObstacleDistance")!;
            var returnBearing = attitudeView.FindControl<Slider>("SimulatedObstacleBearing")!;
            simulateReturn.IsChecked = true;
            returnDistance.Value = 6;
            returnBearing.Value = -30;
            Dispatcher.UIThread.RunJobs();
            Until(() => attitudeScene.ForwardObstacles.Count == 1 && attitudeScene.ForwardObstacles[0].BearingDegrees == -30, vm);
            Assert.True(attitudeScene.HasForwardRange);
            Assert.Single(attitudeScene.ForwardObstacles);
            Assert.Equal(6000, attitudeScene.ForwardObstacles[0].DistanceMillimetres);
            Assert.Equal(-30, attitudeScene.ForwardObstacles[0].BearingDegrees);
            Assert.True(attitudeScene.ForwardObstacles[0].IsSimulated);
            Assert.Contains("SIM", vm.ForwardRangeSummary);
            returnDistance.Value = 0.1;
            returnBearing.Value = 25;
            Dispatcher.UIThread.RunJobs();
            Until(() => attitudeScene.ForwardObstacles.Count == 1 && attitudeScene.ForwardObstacles[0].DistanceMillimetres == 100 && attitudeScene.ForwardObstacles[0].BearingDegrees == 25, vm);
            Assert.Equal(100, attitudeScene.ForwardObstacles[0].DistanceMillimetres);
            Assert.Equal(25, attitudeScene.ForwardObstacles[0].BearingDegrees);
            double initialPulse = attitudeScene.ObstaclePulse;
            Until(() => attitudeScene.ObstaclePulse != initialPulse, vm);
            AssertDashboardFits(window);
            simulateReturn.IsChecked = false;
            Dispatcher.UIThread.RunJobs();
            Until(() => attitudeScene.ForwardObstacles.Count == 0, vm);
            Assert.Empty(attitudeScene.ForwardObstacles);
            Assert.Contains("no reported return", vm.ForwardRangeSummary);
            var placeObject = attitudeView.FindControl<Button>("PlaceSimulatedObject")!;
            var clearObjects = attitudeView.FindControl<Button>("ClearSimulatedObjects")!;
            Assert.True(placeObject.Command!.CanExecute(null));
            placeObject.Command.Execute(null);
            Until(() => attitudeScene.ForwardObstacles.Count == 1, vm);
            Assert.Equal(100, attitudeScene.ForwardObstacles[0].DistanceMillimetres);
            clearObjects.Command!.Execute(null);
            Until(() => attitudeScene.ForwardObstacles.Count == 0, vm);
            Assert.Equal(90, vm.HeadingAngle);
            Assert.Contains(window.GetVisualDescendants().OfType<TextBlock>(), t => t.Text == "Disarmed");
            vm.HoldInput(true);
            Until(() => vm.CanArm, vm);
            Assert.Equal(1000, vm.NeutralHoldProgress);
            Assert.Equal("Neutral input check complete", vm.NeutralHoldLabel);
            vm.UpdatePresentation();
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(1000, window.GetVisualDescendants().OfType<ProgressBar>().Single(p => p.Name == "NeutralHoldProgress").Value);
            Wait(vm.Request(MessageType.Arm), vm);
            Until(() => vm.VehicleState == "Armed", vm);
            vm.Forward = 0.5;
            vm.UpdatePresentation();
            Until(() => vm.MotorRpm != "0 RPM SIM", vm);
            vm.Yaw = 0.5;
            vm.Dive = 0.5;
            Until(() => vm.PitchScreenAngle > 2 && vm.HeadingAngle > 91, vm);
            Until(() => attitudeScene.Pitch < -2 && attitudeScene.Heading > 91, vm);
            vm.ReleaseInput();
            Until(() => vm.VehicleState == "CommandLossFailsafe", vm);
            AssertDashboardFits(window);
            Assert.False(vm.CanArm);
            Wait(vm.Request(MessageType.EmergencyStop), vm);
            Until(() => vm.VehicleState == "EmergencyStopLatched", vm);
            vm.DepthLimitCommand.Execute(null);
            Until(() => vm.DepthMetres >= 4.5, vm);
            Assert.Equal(54, vm.DepthIndicatorOffset);
            Assert.Equal(Color.Parse("#F14959"), Assert.IsType<SolidColorBrush>(vm.DepthWarningBrush).Color);
            vm.DepthSensorFaultCommand.Execute(null);
            Until(() => !vm.HasDepth, vm);
            Assert.Equal("— m", vm.Depth);
            vm.RestoreDepthCommand.Execute(null);
            Until(() => vm.HasDepth, vm);
            Wait(vm.DisconnectAsync(), vm);
            Assert.Equal("UNKNOWN", vm.VehicleState);
            Wait(vm.ConnectAsync(), vm);
            Until(() => vm.LinkFreshness == "CONFIRMED", vm);
            Assert.Equal("EmergencyStopLatched", vm.VehicleState);
            Assert.False(vm.CanArm);
            Wait(vm.DisconnectAsync(), vm);
            Assert.Equal("— m", vm.Depth);
            Assert.False(vm.HasDepth);
            Assert.False(vm.HasAttitude);
            Dispatcher.UIThread.RunJobs();
            Assert.False(attitudeScene.HasAttitude);
            Assert.False(attitudeScene.HasForwardRange);
            Until(() => attitudeScene.ForwardObstacles.Count == 0, vm);
            Assert.Empty(attitudeScene.ForwardObstacles);
            Assert.Contains("unavailable", vm.ForwardRangeSummary);
            var bad = new MainWindowViewModel(() => throw new ConfigurationException("invalid fixture"), TimeProvider.System);
            Assert.False(bad.ConnectCommand.CanExecute(null));
            Assert.Contains("Configuration error", bad.StatusMessage);
            Wait(bad.DisposeAsync().AsTask(), bad);
        }
        finally
        {
            Wait(vm.DisposeAsync().AsTask(), vm);
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    /// <summary>
    /// Verifies fixed world compass orientation, nose-up pitch and bounded extreme poses.
    /// </summary>
    private static void AssertAttitudeProjection(MainWindow window)
    {
        var scene = window.GetVisualDescendants().OfType<SubmarineScene>().Single();
        double originalHeading = scene.Heading;
        double originalPitch = scene.Pitch;
        scene.SetCurrentValue(SubmarineScene.PitchProperty, 0);
        scene.SetCurrentValue(SubmarineScene.HeadingProperty, 0);
        Point northBow = scene.ProjectBodyPoint(0, 0, 2.52);
        Point northStern = scene.ProjectBodyPoint(0, 0, -2.93);
        Assert.True(northBow.Y < northStern.Y);
        Assert.True(scene.ProjectForwardObstacle(300, -45).X < scene.ProjectForwardObstacle(300, 45).X);
        scene.SetCurrentValue(SubmarineScene.HeadingProperty, 180);
        Assert.True(scene.ProjectBodyPoint(0, 0, 2.52).Y > scene.ProjectBodyPoint(0, 0, -2.93).Y);
        scene.SetCurrentValue(SubmarineScene.HeadingProperty, 90);
        Assert.True(scene.ProjectBodyPoint(0, 0, 2.52).X > scene.ProjectBodyPoint(0, 0, -2.93).X);
        Assert.Equal(scene.ProjectBodyPoint(0, 0, 5.92).X, scene.ProjectForwardObstacle(1000).X, 5);
        scene.SetCurrentValue(SubmarineScene.PitchProperty, 20);
        double noseUp = scene.ProjectBodyPoint(0, 0, 2.52).Y;
        scene.SetCurrentValue(SubmarineScene.PitchProperty, -20);
        Assert.True(noseUp < scene.ProjectBodyPoint(0, 0, 2.52).Y);
        scene.SetCurrentValue(SubmarineScene.HeadingProperty, 270);
        scene.SetCurrentValue(SubmarineScene.PitchProperty, 0);
        Assert.True(scene.ProjectBodyPoint(0, 0, 2.52).X < scene.ProjectBodyPoint(0, 0, -2.93).X);
        foreach (double heading in new[] { 0d, 90d, 180d, 270d, 360d })
        {
            foreach (double pitch in new[] { -90d, 0d, 90d })
            {
                scene.SetCurrentValue(SubmarineScene.HeadingProperty, heading);
                scene.SetCurrentValue(SubmarineScene.PitchProperty, pitch);
                foreach (double ahead in new[] { -2.93, 2.52 })
                {
                    Point end = scene.ProjectBodyPoint(0, 0, ahead);
                    Assert.InRange(end.X, 0, scene.Bounds.Width);
                    Assert.InRange(end.Y, 0, scene.Bounds.Height);
                    Point obstacle = scene.ProjectForwardObstacle(6000, 45);
                    Assert.InRange(obstacle.X, 0, scene.Bounds.Width);
                    Assert.InRange(obstacle.Y, 0, scene.Bounds.Height);
                }
            }
        }

        scene.SetCurrentValue(SubmarineScene.HeadingProperty, originalHeading);
        scene.SetCurrentValue(SubmarineScene.PitchProperty, originalPitch);
    }

    /// <summary>
    /// Checks pad axis signs, capture outside the disk, neutral release and keyboard reflection.
    /// </summary>
    private static void AssertMovementPad(MainWindow window, MainWindowViewModel vm)
    {
        window.UpdateLayout();
        var pad = window.FindControl<MovementPad>("MovementPad")!;
        var surface = pad.FindControl<Canvas>("InputSurface")!;
        var throttleControl = window.FindControl<Slider>("ThrottleControl")!;
        var neutralMark = window.FindControl<Border>("ThrottleNeutralMark")!;
        Assert.Equal(Orientation.Vertical, throttleControl.Orientation);
        Assert.False(throttleControl.IsDirectionReversed);
        Assert.Equal(pad.Bounds.Height, throttleControl.Bounds.Height);
        Assert.Equal(0.2, throttleControl.TickFrequency);
        Assert.Equal(TickPlacement.Outside, throttleControl.TickPlacement);
        Assert.Equal(neutralMark.Bounds.Center.Y, throttleControl.Bounds.Center.Y);
        throttleControl.Value = 0.5;
        Assert.Equal(0.5, vm.Forward);
        Assert.Equal("50%", window.FindControl<TextBlock>("ThrottleAheadPercentage")!.Text);
        Assert.Equal("0%", window.FindControl<TextBlock>("ThrottleReversePercentage")!.Text);
        Assert.True(neutralMark.Bounds.Width <= throttleControl.Bounds.Width);
        var diagonal = surface.TranslatePoint(new Point(179, 81), window)!.Value;
        vm.Forward = -0.25;
        window.MouseDown(diagonal, MouseButton.Left);
        Dispatcher.UIThread.RunJobs();
        Assert.Equal(-0.5, vm.Dive, 3);
        Assert.Equal(0.5, vm.Yaw, 3);
        Assert.Equal("50%", pad.FindControl<TextBlock>("PitchUpPercentage")!.Text);
        Assert.Equal("0%", pad.FindControl<TextBlock>("PitchDownPercentage")!.Text);
        Assert.Equal("50%", pad.FindControl<TextBlock>("StarboardPercentage")!.Text);
        Assert.Equal("0%", pad.FindControl<TextBlock>("PortPercentage")!.Text);
        var outside = surface.TranslatePoint(new Point(296, -36), window)!.Value;
        window.MouseMove(outside, RawInputModifiers.LeftMouseButton);
        Dispatcher.UIThread.RunJobs();
        Assert.InRange(vm.Dive, -0.71, -0.70);
        Assert.InRange(vm.Yaw, 0.70, 0.71);
        window.MouseUp(outside, MouseButton.Left);
        Dispatcher.UIThread.RunJobs();
        Assert.Equal(0, vm.Dive);
        Assert.Equal(0, vm.Yaw);
        Assert.Equal(-0.25, vm.Forward);
        Assert.Equal("0%", window.FindControl<TextBlock>("ThrottleAheadPercentage")!.Text);
        Assert.Equal("25%", window.FindControl<TextBlock>("ThrottleReversePercentage")!.Text);
        window.KeyPress(Key.R, RawInputModifiers.None, PhysicalKey.None, null);
        window.KeyPress(Key.A, RawInputModifiers.None, PhysicalKey.None, null);
        Dispatcher.UIThread.RunJobs();
        Assert.Equal(0.5, pad.Dive);
        Assert.Equal(-0.5, pad.Yaw);
        Assert.Equal("50%", pad.FindControl<TextBlock>("PitchDownPercentage")!.Text);
        Assert.Equal("50%", pad.FindControl<TextBlock>("PortPercentage")!.Text);
        window.KeyRelease(Key.R, RawInputModifiers.None, PhysicalKey.None, null);
        window.KeyRelease(Key.A, RawInputModifiers.None, PhysicalKey.None, null);
        vm.ReleaseInput();
        Dispatcher.UIThread.RunJobs();
        Assert.Equal(0, pad.Dive);
        Assert.Equal(0, throttleControl.Value);
        Assert.Equal(0, pad.Yaw);
        IPointer? pointer = null;
        EventHandler<PointerPressedEventArgs> observe = (_, e) => pointer = e.Pointer;
        window.AddHandler(InputElement.PointerPressedEvent, observe, RoutingStrategies.Tunnel);
        window.MouseDown(diagonal, MouseButton.Left);
        window.RemoveHandler(InputElement.PointerPressedEvent, observe);
        Assert.NotNull(pointer);
        var replacementCapture = window.FindControl<Slider>("ThrottleControl")!;
        pointer.Capture(replacementCapture);
        Dispatcher.UIThread.RunJobs();
        Assert.Same(replacementCapture, pointer.Captured);
        Assert.Equal(0, vm.Dive);
        Assert.Equal(0, vm.Yaw);
        pointer.Capture(null);
        window.MouseUp(diagonal, MouseButton.Left);
    }

    /// <summary>
    /// Checks visible panel and button bounds without relying on a scrolling viewport.
    /// </summary>
    private static void AssertDashboardFits(MainWindow window)
    {
        window.UpdateLayout();
        foreach (string name in new[] { "AttitudePanel", "ControlPanel" })
        {
            var panel = window.FindControl<Border>(name)!;
            var position = panel.TranslatePoint(new Point(0, 0), window)!.Value;
            Assert.True(position.Y + panel.Bounds.Height <= window.ClientSize.Height + 1,
                $"{name} extends below the visible window.");
            Assert.True(panel.Child!.DesiredSize.Height + panel.Padding.Top + panel.Padding.Bottom <= panel.Bounds.Height + 1,
                $"{name} content exceeds the available panel height.");
            Assert.True(panel.Child.DesiredSize.Width + panel.Padding.Left + panel.Padding.Right <= panel.Bounds.Width + 1,
                $"{name} content exceeds the available panel width.");
        }

        var controls = window.FindControl<Border>("ControlPanel")!;
        foreach (var button in controls.GetVisualDescendants().OfType<Button>())
        {
            var position = button.TranslatePoint(new Point(0, 0), controls)!.Value;
            Assert.True(position.Y + button.Bounds.Height <= controls.Bounds.Height - controls.Padding.Bottom + 1,
                $"{button.Content} bottom {position.Y + button.Bounds.Height} exceeds control panel content bottom {controls.Bounds.Height - controls.Padding.Bottom}; window height {window.ClientSize.Height}.");
        }

        Assert.Empty(window.GetVisualDescendants().OfType<ScrollViewer>());
    }

    /// <summary>
    /// Pumps the headless UI until an asynchronous dashboard operation finishes and then observes its result.
    /// </summary>
    private static void Wait(Task task, MainWindowViewModel vm)
    {
        Until(() => task.IsCompleted, vm);
        task.GetAwaiter().GetResult();
    }

    /// <summary>
    /// Pumps presentation updates until a dashboard condition holds or its bounded test deadline expires.
    /// </summary>
    private static void Until(Func<bool> predicate, MainWindowViewModel vm)
    {
        var deadline = DateTime.UtcNow.AddSeconds(8);
        while (!predicate())
        {
            TestContext.Current.CancellationToken.ThrowIfCancellationRequested();
            if (DateTime.UtcNow > deadline)
            {
                throw new TimeoutException($"Dashboard condition timed out: {vm.StatusMessage}; {vm.Qualification}");
            }

            Dispatcher.UIThread.RunJobs();
            vm.UpdatePresentation();
            Thread.Sleep(10);
        }

        Dispatcher.UIThread.RunJobs();
    }
}
