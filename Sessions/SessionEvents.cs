namespace MotaBridge.Sessions;

public sealed record SessionOutputEvent(string SessionId, string Stream, string Text);

public sealed record SessionExitEvent(string SessionId, int ExitCode);
