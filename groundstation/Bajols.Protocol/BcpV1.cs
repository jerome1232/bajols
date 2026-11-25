namespace Bajols.Protocol;

/// <summary>Shared constants for the BCP v1 control and serial profiles.</summary>
public static class BcpV1
{
    /// <summary>Interval between movement command transmissions in milliseconds.</summary>
    public const ushort MovementIntervalMs = 50;

    /// <summary>Maximum age of accepted command input in milliseconds.</summary>
    public const ushort CommandTimeoutMs = 500;

    /// <summary>Interval between control ticket publications in milliseconds.</summary>
    public const ushort TicketIntervalMs = 50;

    /// <summary>Maximum age of a control ticket in milliseconds.</summary>
    public const ushort TicketLifetimeMs = 500;

    /// <summary>Duration of fresh neutral input required for arming in milliseconds.</summary>
    public const ushort NeutralHoldMs = 1000;

    /// <summary>Maximum absolute effort treated as neutral.</summary>
    public const ushort NeutralTolerance = 50;

    /// <summary>Absolute endpoint of the signed movement effort scale.</summary>
    public const short FullEffort = 1000;

    /// <summary>Lifetime of an unconfirmed session handshake in milliseconds.</summary>
    public const ushort HandshakeTimeoutMs = 2000;

    /// <summary>Periodic status interval in milliseconds when the status is unchanged.</summary>
    public const ushort StatusIntervalMs = 200;

    /// <summary>Periodic heartbeat interval in milliseconds.</summary>
    public const ushort HeartbeatIntervalMs = 1000;

    /// <summary>Repeat interval for active faults in milliseconds.</summary>
    public const ushort FaultRepeatIntervalMs = 1000;

    /// <summary>Duration of qualifying input before releasing depth intervention.</summary>
    public const ushort DepthReleaseHoldMs = 1000;

    /// <summary>Baud rate of the BCP v1 serial profile.</summary>
    public const int SerialBaudRate = 115200;

    /// <summary>Wire sentinel for an unknown sample or command age.</summary>
    public const ushort UnknownAgeMs = ushort.MaxValue;

    /// <summary>Largest age representable without using the unknown sentinel.</summary>
    public const ushort MaxKnownAgeMs = UnknownAgeMs - 1;
}
