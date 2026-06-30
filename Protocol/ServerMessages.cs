namespace MotaBridge.Protocol;

public sealed record ErrorMessage(string Type, string? RequestId, string Code, string Message)
{
    public static ErrorMessage Create(string? requestId, string code, string message) =>
        new(BridgeMessageTypes.Error, requestId, code, message);
}

public sealed record BridgeReadyMessage(string Type, IReadOnlyList<string> Clis)
{
    public static BridgeReadyMessage Create(IReadOnlyList<string> clis) =>
        new(BridgeMessageTypes.Ready, clis);
}

public sealed record SessionCreatedMessage(string Type, string? RequestId, string SessionId)
{
    public static SessionCreatedMessage Create(string? requestId, string sessionId) =>
        new(BridgeMessageTypes.SessionCreated, requestId, sessionId);
}

public sealed record SessionClosedMessage(string Type, string? RequestId, string SessionId)
{
    public static SessionClosedMessage Create(string? requestId, string sessionId) =>
        new(BridgeMessageTypes.SessionClosed, requestId, sessionId);
}

public sealed record SessionListMessage(string Type, string? RequestId, IReadOnlyList<SessionSummary> Sessions)
{
    public static SessionListMessage Create(string? requestId, IReadOnlyList<SessionSummary> sessions) =>
        new(BridgeMessageTypes.SessionListResult, requestId, sessions);
}

public sealed record SessionSummary(string SessionId, string Cli, string Cwd, DateTimeOffset CreatedAt, DateTimeOffset LastActivityAt);
