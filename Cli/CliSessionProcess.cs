namespace MotaBridge.Cli;

public sealed class CliSessionProcess : IAsyncDisposable
{
    public event Action<string>? OutputReceived;
    public event Action<int>? Exited;

    public void Write(string text)
    {
        throw new NotImplementedException("PTY process writing will be implemented in the CLI runtime phase.");
    }

    public void Resize(int cols, int rows)
    {
        throw new NotImplementedException("PTY resize will be implemented in the CLI runtime phase.");
    }

    public void Interrupt()
    {
        Write("\u0003");
    }

    public void Terminate()
    {
        throw new NotImplementedException("PTY termination will be implemented in the CLI runtime phase.");
    }

    public ValueTask DisposeAsync()
    {
        return ValueTask.CompletedTask;
    }

    private void OnOutputReceived(string text) => OutputReceived?.Invoke(text);

    private void OnExited(int exitCode) => Exited?.Invoke(exitCode);
}
