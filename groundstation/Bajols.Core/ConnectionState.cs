namespace Bajols.Core;

/// <summary>
/// Lifecycle of a desktop transport connection, independent of vehicle safety and arming.
/// </summary>
public enum ConnectionState
{
    /// <summary>
    /// The desktop connection is disconnected; vehicle arming remains an independent state.
    /// </summary>
    Disconnected,

    /// <summary>
    /// The desktop connection is connecting; vehicle arming remains an independent state.
    /// </summary>
    Connecting,

    /// <summary>
    /// The desktop connection is connected; vehicle arming remains an independent state.
    /// </summary>
    Connected,

    /// <summary>
    /// The desktop connection is faulted; vehicle arming remains an independent state.
    /// </summary>
    Faulted
}
