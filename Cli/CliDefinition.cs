namespace MotaBridge.Cli;

public sealed record CliDefinition(
    string Id,
    string Label,
    string Command,
    IReadOnlyList<string> Args,
    IReadOnlyList<string> AllowedCwds);
