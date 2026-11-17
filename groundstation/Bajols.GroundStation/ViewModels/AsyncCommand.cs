using System.Windows.Input;

namespace Bajols.GroundStation.ViewModels;

public sealed class AsyncCommand(Func<Task> action, Action<Exception> onError, Func<bool>? enabled = null) : ICommand
{
    private bool busy;
    public event EventHandler? CanExecuteChanged;
    public bool CanExecute(object? parameter) => !busy && (enabled?.Invoke() ?? true);
    public async void Execute(object? parameter)
    {
        if (!CanExecute(parameter)) return;
        busy = true; Refresh();
        try { await action(); } catch (Exception e) { onError(e); }
        finally { busy = false; Refresh(); }
    }
    public void Refresh() => CanExecuteChanged?.Invoke(this, EventArgs.Empty);
}
