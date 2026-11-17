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

public sealed class DashboardTests
{
    [Fact]
    public void Dashboard_confirms_live_simulation_releases_input_and_never_rearms_on_reconnect()
    {
        AppBuilder.Configure<App>().UseHeadless(new AvaloniaHeadlessPlatformOptions()).SetupWithoutStarting();
        var vm = new MainWindowViewModel(() => new StationConfiguration(), TimeProvider.System);
        var window = new MainWindow { DataContext = vm }; window.Show();
        try
        {
            Assert.Equal(ConnectionState.Disconnected, vm.Connection); Assert.Equal("UNKNOWN", vm.VehicleState);
            Assert.False(vm.ArmCommand.CanExecute(null)); Assert.False(vm.DisarmCommand.CanExecute(null));
            Wait(vm.ConnectAsync(), vm);
            Until(() => vm.LinkFreshness == "CONFIRMED", vm);
            Assert.Equal("Disarmed", vm.VehicleState);
            Until(() => vm.Depth.EndsWith("SIM", StringComparison.Ordinal), vm);
            Assert.True(vm.HasAttitude);
            Assert.Equal(90, vm.HeadingAngle);
            Assert.Contains(window.GetVisualDescendants().OfType<TextBlock>(), t => t.Text == "Disarmed");
            vm.HoldInput(true);
            Until(() => vm.CanArm, vm);
            Wait(vm.Request(MessageType.Arm), vm); Until(() => vm.VehicleState == "Armed", vm);
            vm.Forward = 0.5; vm.UpdatePresentation();
            Until(() => vm.MotorRpm != "0 RPM SIM", vm);
            vm.Yaw = 0.5; vm.Dive = 0.5;
            Until(() => vm.PitchScreenAngle > 2 && vm.HeadingAngle > 91, vm);
            vm.ReleaseInput();
            Until(() => vm.VehicleState == "CommandLossFailsafe", vm);
            Assert.False(vm.CanArm);
            Wait(vm.Request(MessageType.EmergencyStop), vm); Until(() => vm.VehicleState == "EmergencyStopLatched", vm);
            Wait(vm.DisconnectAsync(), vm); Assert.Equal("UNKNOWN", vm.VehicleState);
            Wait(vm.ConnectAsync(), vm); Until(() => vm.LinkFreshness == "CONFIRMED", vm);
            Assert.Equal("EmergencyStopLatched", vm.VehicleState); Assert.False(vm.CanArm);
            Wait(vm.DisconnectAsync(), vm);
            Assert.Equal("— m", vm.Depth);
            Assert.False(vm.HasAttitude);
            var bad = new MainWindowViewModel(() => throw new ConfigurationException("invalid fixture"), TimeProvider.System);
            Assert.False(bad.ConnectCommand.CanExecute(null)); Assert.Contains("Configuration error", bad.StatusMessage);
            Wait(bad.DisposeAsync().AsTask(), bad);
        }
        finally { Wait(vm.DisposeAsync().AsTask(), vm); window.Close(); Dispatcher.UIThread.RunJobs(); }
    }
    private static void Wait(Task task, MainWindowViewModel vm)
    {
        Until(() => task.IsCompleted, vm); task.GetAwaiter().GetResult();
    }
    private static void Until(Func<bool> predicate, MainWindowViewModel vm)
    {
        var deadline = DateTime.UtcNow.AddSeconds(8);
        while (!predicate())
        {
            TestContext.Current.CancellationToken.ThrowIfCancellationRequested();
            if (DateTime.UtcNow > deadline) throw new TimeoutException($"Dashboard condition timed out: {vm.StatusMessage}; {vm.Qualification}");
            Dispatcher.UIThread.RunJobs(); vm.UpdatePresentation(); Thread.Sleep(10);
        }
        Dispatcher.UIThread.RunJobs();
    }
}
