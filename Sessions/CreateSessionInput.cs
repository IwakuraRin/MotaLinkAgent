namespace MotaBridge.Sessions;

public sealed record CreateSessionInput(
    string Cli,
    string Cwd,
    int Cols,
    int Rows);
