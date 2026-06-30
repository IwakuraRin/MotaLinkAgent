namespace MotaBridge.Config;

public sealed class CliOptions
{
    public string Id { get; set; } = string.Empty;

    public string Label { get; set; } = string.Empty;

    public string Command { get; set; } = string.Empty;

    public List<string> Args { get; set; } = [];

    public List<string> AllowedCwds { get; set; } = [];
}
