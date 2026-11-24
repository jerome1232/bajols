using System.Runtime.InteropServices;
using System.Text;
using Bajols.Communication;
using Microsoft.Win32.SafeHandles;
using Xunit;

namespace Bajols.Integration.Tests;

/// <summary>
/// Serial-driver bounds and cancellation tests, including Linux pseudo-terminal byte transfer.
/// </summary>
public sealed class SerialTransportTests
{
    /// <summary>
    /// Creates a Linux pseudo-terminal pair for serial-driver integration tests.
    /// </summary>
    [DllImport("libc", SetLastError = true)]
    private static extern int openpty(out int master, out int slave, StringBuilder name, nint termios, nint winsize);

    /// <summary>
    /// Native descriptor layout used to poll a Linux pseudo-terminal with a bounded timeout.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    private struct PollDescriptor
    {
        /// <summary>
        /// Native pseudo-terminal file descriptor to monitor.
        /// </summary>
        public int Fd;

        /// <summary>
        /// Requested native poll event mask.
        /// </summary>
        public short Events;

        /// <summary>
        /// Native poll event mask reported by the operating system.
        /// </summary>
        public short ReturnedEvents;
    }

    /// <summary>
    /// Waits for pseudo-terminal readability using a finite native timeout.
    /// </summary>
    [DllImport("libc", SetLastError = true)]
    private static extern int poll(ref PollDescriptor descriptor, uint count, int timeout);

    /// <summary>
    /// Verifies linux pseudo terminal transfers bytes and cancel interrupts idle read.
    /// </summary>
    [Fact]
    public async Task Linux_pseudo_terminal_transfers_bytes_and_cancel_interrupts_idle_read()
    {
        if (!OperatingSystem.IsLinux())
        {
            Assert.Skip("Pseudo-terminal driver test requires Linux; Windows requires a loopback serial device.");
        }

        var name = new StringBuilder(128);
        Assert.Equal(0, openpty(out int masterFd, out int slaveFd, name, 0, 0));
        using var slave = new SafeFileHandle((nint)slaveFd, true);
        using var masterHandle = new SafeFileHandle((nint)masterFd, true);
        using var master = new FileStream(masterHandle, FileAccess.ReadWrite, 256, false);
        await using var transport = new SerialByteTransport(name.ToString());
        await transport.OpenAsync(CancellationToken.None);
        byte[] message = [1, 2, 3, 4, 0];
        await master.WriteAsync(message, TestContext.Current.CancellationToken);
        await master.FlushAsync(TestContext.Current.CancellationToken);
        byte[] receive = new byte[256];
        using var limit = new CancellationTokenSource(3000);
        int n = await transport.ReadAsync(receive, limit.Token);
        Assert.Equal(message, receive[..n]);
        await transport.WriteAsync(message, limit.Token);
        var descriptor = new PollDescriptor
        {
            Fd = masterFd,
            Events = 1
        };
        Assert.Equal(1, poll(ref descriptor, 1, 2000));
        int count = master.Read(receive, 0, receive.Length);
        Assert.Equal(message, receive[..count]);
        using var cancelIdleRead = new CancellationTokenSource(150);
        await Assert.ThrowsAnyAsync<OperationCanceledException>(async () => await transport.ReadAsync(receive, cancelIdleRead.Token));
        await transport.CloseAsync(CancellationToken.None);
    }

    /// <summary>
    /// Verifies invalid settings and canceled open never open a device.
    /// </summary>
    [Fact]
    public async Task Invalid_settings_and_canceled_open_never_open_a_device()
    {
        Assert.Throws<ArgumentException>(() => new SerialByteTransport("COM3", -1));
        await using var transport = new SerialByteTransport("no-such-device");
        using var token = new CancellationTokenSource();
        token.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(async () => await transport.OpenAsync(token.Token));
        await Assert.ThrowsAsync<ArgumentException>(async () => await transport.WriteAsync(new byte[117], CancellationToken.None));
    }
}
