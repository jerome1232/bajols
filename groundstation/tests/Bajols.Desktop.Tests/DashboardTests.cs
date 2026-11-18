using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
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
        AppBuilder.Configure<App>().UseHeadless(new AvaloniaHeadlessPlatformOptions()).SetupWithoutStarting();
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
            var neutralMode = window.FindControl<RadioButton>("NeutralBuoyancyMode")!;
            var positiveMode = window.FindControl<RadioButton>("PositiveBuoyancyMode")!;
            var negativeMode = window.FindControl<RadioButton>("NegativeBuoyancyMode")!;
            Assert.True(neutralMode.IsChecked);
            positiveMode.IsChecked = true;
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(1, vm.BuoyancyIndex);
            Assert.True(positiveMode.IsChecked);
            Assert.False(neutralMode.IsChecked);
            Assert.False(negativeMode.IsChecked);
            negativeMode.IsChecked = true;
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(2, vm.BuoyancyIndex);
            Assert.True(negativeMode.IsChecked);
            Assert.False(positiveMode.IsChecked);
            vm.ReleaseInput();
            Dispatcher.UIThread.RunJobs();
            Assert.True(neutralMode.IsChecked);
            Assert.False(negativeMode.IsChecked);
            Wait(vm.ConnectAsync(), vm);
            Until(() => vm.LinkFreshness == "CONFIRMED", vm);
            Assert.Equal("Disarmed", vm.VehicleState);
            AssertDashboardFits(window);
            Until(() => vm.Depth.EndsWith("SIM", StringComparison.Ordinal), vm);
            Assert.True(vm.HasAttitude);
            Assert.Equal(90, vm.HeadingAngle);
            Assert.Contains(window.GetVisualDescendants().OfType<TextBlock>(), t => t.Text == "Disarmed");
            vm.HoldInput(true);
            Until(() => vm.CanArm, vm);
            Wait(vm.Request(MessageType.Arm), vm);
            Until(() => vm.VehicleState == "Armed", vm);
            vm.Forward = 0.5;
            vm.UpdatePresentation();
            Until(() => vm.MotorRpm != "0 RPM SIM", vm);
            vm.Yaw = 0.5;
            vm.Dive = 0.5;
            Until(() => vm.PitchScreenAngle > 2 && vm.HeadingAngle > 91, vm);
            vm.ReleaseInput();
            Until(() => vm.VehicleState == "CommandLossFailsafe", vm);
            AssertDashboardFits(window);
            Assert.False(vm.CanArm);
            Wait(vm.Request(MessageType.EmergencyStop), vm);
            Until(() => vm.VehicleState == "EmergencyStopLatched", vm);
            Wait(vm.DisconnectAsync(), vm);
            Assert.Equal("UNKNOWN", vm.VehicleState);
            Wait(vm.ConnectAsync(), vm);
            Until(() => vm.LinkFreshness == "CONFIRMED", vm);
            Assert.Equal("EmergencyStopLatched", vm.VehicleState);
            Assert.False(vm.CanArm);
            Wait(vm.DisconnectAsync(), vm);
            Assert.Equal("— m", vm.Depth);
            Assert.False(vm.HasAttitude);
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
    /// Checks that the default dashboard fits its panels without relying on a scrolling viewport.
    /// </summary>
    private static void AssertDashboardFits(MainWindow window)
    {
        window.UpdateLayout();
        foreach (string name in new[] { "AttitudePanel", "StatusPanel", "ControlPanel" })
        {
            var panel = window.FindControl<Border>(name)!;
            Assert.True(panel.Child!.DesiredSize.Height + panel.Padding.Top + panel.Padding.Bottom <= panel.Bounds.Height + 1,
                $"{name} content exceeds the available panel height.");
            Assert.True(panel.Child.DesiredSize.Width + panel.Padding.Left + panel.Padding.Right <= panel.Bounds.Width + 1,
                $"{name} content exceeds the available panel width.");
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
