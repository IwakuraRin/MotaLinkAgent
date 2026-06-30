using System.Collections.Concurrent;
using MotaBridge.Cli;
using MotaBridge.Protocol;
using MotaBridge.Utils;

namespace MotaBridge.Sessions;

public sealed class SessionManager(
    ICliRegistry cliRegistry,
    ICwdPolicy cwdPolicy,
    IPtyProcessFactory ptyProcessFactory,
    ILogger<SessionManager> logger) : ISessionManager
{
    private const int MinCols = 20;
    private const int MaxCols = 300;
    private const int MinRows = 5;
    private const int MaxRows = 120;

    private readonly ConcurrentDictionary<string, CliSession> _sessions = new(StringComparer.Ordinal);

    public async Task<string> CreateAsync(CreateSessionInput input, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        ValidateTerminalSize(input.Cols, input.Rows);

        if (!cliRegistry.TryGet(input.Cli, out var cli))
        {
            throw new InvalidOperationException($"CLI is not allowed or not configured: {input.Cli}");
        }

        if (!cwdPolicy.IsAllowed(cli, input.Cwd))
        {
            throw new InvalidOperationException("cwd is not allowed for this CLI.");
        }

        var now = DateTimeOffset.UtcNow;
        var sessionId = IdGenerator.NewSessionId();
        var normalizedCwd = Path.GetFullPath(input.Cwd);
        var process = ptyProcessFactory.Start(cli, normalizedCwd, input.Cols, input.Rows);
        var session = new CliSession(sessionId, cli, process, normalizedCwd, input.Cols, input.Rows, now);

        if (!_sessions.TryAdd(sessionId, session))
        {
            await process.DisposeAsync();
            throw new InvalidOperationException("failed to register session.");
        }

        logger.LogInformation("Session {SessionId} created for CLI {CliId} in cwd {Cwd}", sessionId, cli.Id, session.Cwd);
        return sessionId;
    }

    public Task WriteInputAsync(string sessionId, string text, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var session = GetRequiredSession(sessionId);
        session.Process.Write(text);
        session.Touch(DateTimeOffset.UtcNow);
        return Task.CompletedTask;
    }

    public Task ResizeAsync(string sessionId, int cols, int rows, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        ValidateTerminalSize(cols, rows);
        var session = GetRequiredSession(sessionId);
        session.Process.Resize(cols, rows);
        session.Resize(cols, rows, DateTimeOffset.UtcNow);
        return Task.CompletedTask;
    }

    public Task SignalAsync(string sessionId, SessionSignal signal, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var session = GetRequiredSession(sessionId);

        switch (signal)
        {
            case SessionSignal.Interrupt:
                session.Process.Interrupt();
                break;
            case SessionSignal.Terminate:
                session.Process.Terminate();
                break;
            default:
                throw new InvalidOperationException($"Unsupported signal: {signal}.");
        }

        session.Touch(DateTimeOffset.UtcNow);
        return Task.CompletedTask;
    }

    public async Task CloseAsync(string sessionId, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        if (_sessions.TryRemove(sessionId, out var session))
        {
            await session.DisposeAsync();
            logger.LogInformation("Session {SessionId} closed for CLI {CliId}", session.SessionId, session.Cli.Id);
        }
    }

    public async Task CloseIdleAsync(TimeSpan idleTimeout, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var now = DateTimeOffset.UtcNow;

        foreach (var session in _sessions.Values)
        {
            if (now - session.LastActivityAt >= idleTimeout)
            {
                if (_sessions.TryRemove(session.SessionId, out var removedSession))
                {
                    await removedSession.DisposeAsync();
                    logger.LogInformation("Idle session {SessionId} closed", session.SessionId);
                }
            }
        }
    }

    public IReadOnlyList<SessionSummary> List() =>
        _sessions.Values
            .OrderBy(session => session.CreatedAt)
            .Select(session => new SessionSummary(session.SessionId, session.Cli.Id, session.Cwd, session.CreatedAt, session.LastActivityAt))
            .ToList();

    private CliSession GetRequiredSession(string sessionId)
    {
        if (_sessions.TryGetValue(sessionId, out var session))
        {
            return session;
        }

        throw new KeyNotFoundException($"session not found: {sessionId}");
    }

    private static void ValidateTerminalSize(int cols, int rows)
    {
        if (cols is < MinCols or > MaxCols)
        {
            throw new ArgumentOutOfRangeException(nameof(cols), $"cols must be between {MinCols} and {MaxCols}.");
        }

        if (rows is < MinRows or > MaxRows)
        {
            throw new ArgumentOutOfRangeException(nameof(rows), $"rows must be between {MinRows} and {MaxRows}.");
        }
    }
}
