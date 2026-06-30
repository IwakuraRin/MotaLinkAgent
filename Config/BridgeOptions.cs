namespace MotaBridge.Config;

public sealed class BridgeOptions
{
    public string Host { get; set; } = "127.0.0.1";

    public int Port { get; set; } = 8765;

    public string AuthToken { get; set; } = string.Empty;

    public int SessionIdleTimeoutMs { get; set; } = 600_000;

    public bool TerminateOnDisconnect { get; set; } = true;

    public bool KeepAliveOnDisconnect { get; set; }

    public List<CliOptions> Clis { get; set; } = [];
}
