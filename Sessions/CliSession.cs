using MotaBridge.Cli;

namespace MotaBridge.Sessions;

public sealed class CliSession : IAsyncDisposable
{
    public CliSession(
        string sessionId,
        CliDefinition cli,
        CliSessionProcess process,
        string cwd,
        int cols,
        int rows,
        DateTimeOffset createdAt)
    {
        SessionId = sessionId;
        Cli = cli;
        Process = process;
        Cwd = cwd;
        Cols = cols;
        Rows = rows;
        CreatedAt = createdAt;
        LastActivityAt = createdAt;
    }

    public string SessionId { get; }

    public CliDefinition Cli { get; }

    public CliSessionProcess Process { get; }

    public string Cwd { get; }

    public int Cols { get; private set; }

    public int Rows { get; private set; }

    public DateTimeOffset CreatedAt { get; }

    public DateTimeOffset LastActivityAt { get; private set; }

    public void Touch(DateTimeOffset now) => LastActivityAt = now;

    public void Resize(int cols, int rows, DateTimeOffset now)
    {
        Cols = cols;
        Rows = rows;
        Touch(now);
    }

    public ValueTask DisposeAsync() => Process.DisposeAsync();
}
