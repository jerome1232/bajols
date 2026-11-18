using System.Windows.Input;

namespace Bajols.GroundStation.ViewModels;

/// <summary>
/// Runs an asynchronous operator action with availability gating and explicit error reporting.
/// </summary>
/// <param name="action">The asynchronous operator action to execute.</param>
/// <param name="onError">The callback that reports execution failures.</param>
/// <param name="enabled">The optional predicate controlling action availability.</param>
public sealed class AsyncCommand(Func<Task> action, Action<Exception> onError, Func<bool>? enabled = null) : ICommand
{
    /// <summary>
    /// Whether the asynchronous action is currently running and must not be started again.
    /// </summary>
    private bool busy;

    /// <summary>
    /// Notifies bound controls that execution eligibility may have changed.
    /// </summary>
    public event EventHandler? CanExecuteChanged;

    /// <summary>
    /// Checks that the action is idle and its current availability predicate permits execution.
    /// </summary>
    public bool CanExecute(object? parameter) => !busy && (enabled?.Invoke() ?? true);

    /// <summary>
    /// Runs one asynchronous UI action, reports failures and refreshes availability when it finishes.
    /// </summary>
    public async void Execute(object? parameter)
    {
        if (!CanExecute(parameter))
        {
            return;
        }

        busy = true;
        Refresh();
        try
        {
            await action();
        }
        catch (Exception e)
        {
            onError(e);
        }
        finally
        {
            busy = false;
            Refresh();
        }
    }

    /// <summary>
    /// Notifies bound controls that command availability may have changed.
    /// </summary>
    public void Refresh() => CanExecuteChanged?.Invoke(this, EventArgs.Empty);
}
