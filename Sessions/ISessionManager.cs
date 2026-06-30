using MotaBridge.Protocol;

namespace MotaBridge.Sessions;

public interface ISessionManager
{
    Task<string> CreateAsync(CreateSessionInput input, CancellationToken cancellationToken);

    Task WriteInputAsync(string sessionId, string text, CancellationToken cancellationToken);

    Task ResizeAsync(string sessionId, int cols, int rows, CancellationToken cancellationToken);

    Task SignalAsync(string sessionId, SessionSignal signal, CancellationToken cancellationToken);

    Task CloseAsync(string sessionId, CancellationToken cancellationToken);

    Task CloseIdleAsync(TimeSpan idleTimeout, CancellationToken cancellationToken);

    IReadOnlyList<SessionSummary> List();
}
