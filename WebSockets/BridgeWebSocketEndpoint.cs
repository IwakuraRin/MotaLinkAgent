using System.Net.WebSockets;
using Microsoft.Extensions.Options;
using MotaBridge.Auth;
using MotaBridge.Cli;
using MotaBridge.Config;
using MotaBridge.Protocol;
using MotaBridge.Sessions;

namespace MotaBridge.WebSockets;

public sealed class BridgeWebSocketEndpoint(
    ITokenAuthenticator authenticator,
    ICliRegistry cliRegistry,
    ISessionManager sessions,
    IOptions<BridgeOptions> options,
    ILogger<BridgeWebSocketEndpoint> logger)
{
    public async Task HandleAsync(HttpContext context, CancellationToken cancellationToken)
    {
        if (!context.WebSockets.IsWebSocketRequest)
        {
            context.Response.StatusCode = StatusCodes.Status400BadRequest;
            await context.Response.WriteAsync("WebSocket request required.", cancellationToken);
            return;
        }

        var token = ReadToken(context);
        if (!authenticator.IsValid(token))
        {
            context.Response.StatusCode = StatusCodes.Status401Unauthorized;
            logger.LogWarning("Rejected WebSocket connection from {RemoteIp}", context.Connection.RemoteIpAddress);
            return;
        }

        using var socket = await context.WebSockets.AcceptWebSocketAsync();
        logger.LogInformation("Accepted WebSocket connection from {RemoteIp}", context.Connection.RemoteIpAddress);

        await using var connection = new WebSocketConnection(socket, sessions, options.Value, logger);
        await connection.SendAsync(BridgeReadyMessage.Create(cliRegistry.List().Select(cli => cli.Id).ToList()), cancellationToken);
        await connection.ReceiveLoopAsync(cancellationToken);
    }

    private static string? ReadToken(HttpContext context)
    {
        if (context.Request.Query.TryGetValue("token", out var queryToken))
        {
            return queryToken.ToString();
        }

        var authorization = context.Request.Headers.Authorization.ToString();
        const string bearerPrefix = "Bearer ";
        return authorization.StartsWith(bearerPrefix, StringComparison.OrdinalIgnoreCase)
            ? authorization[bearerPrefix.Length..]
            : null;
    }
}
