using Microsoft.Extensions.Options;
using MotaBridge.Config;

namespace MotaBridge.Sessions;

public sealed class SessionIdleCleaner(
    ISessionManager sessions,
    IOptions<BridgeOptions> options,
    ILogger<SessionIdleCleaner> logger) : BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        var idleTimeout = TimeSpan.FromMilliseconds(options.Value.SessionIdleTimeoutMs);
        using var timer = new PeriodicTimer(TimeSpan.FromSeconds(30));

        logger.LogInformation("Session idle cleaner started with timeout {IdleTimeoutMs} ms", options.Value.SessionIdleTimeoutMs);

        while (await timer.WaitForNextTickAsync(stoppingToken))
        {
            await sessions.CloseIdleAsync(idleTimeout, stoppingToken);
        }
    }
}
