namespace MotaBridge.Protocol;

public sealed record SessionCreateRequest(
    string Type,
    string? RequestId,
    string Cli,
    string Cwd,
    int Cols,
    int Rows);

public sealed record SessionInputRequest(
    string Type,
    string? RequestId,
    string SessionId,
    string Text);

public sealed record SessionSignalRequest(
    string Type,
    string? RequestId,
    string SessionId,
    string Signal);

public sealed record SessionResizeRequest(
    string Type,
    string? RequestId,
    string SessionId,
    int Cols,
    int Rows);

public sealed record SessionCloseRequest(
    string Type,
    string? RequestId,
    string SessionId);
