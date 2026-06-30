using System.Collections.Concurrent;
using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using MotaBridge.Config;
using MotaBridge.Protocol;
using MotaBridge.Sessions;

namespace MotaBridge.WebSockets;

public sealed class WebSocketConnection(
    WebSocket socket,
    ISessionManager sessions,
    BridgeOptions options,
    ILogger logger) : IAsyncDisposable
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    private readonly ConcurrentDictionary<string, byte> _ownedSessions = new(StringComparer.Ordinal);
    private readonly SemaphoreSlim _sendLock = new(1, 1);

    public async Task ReceiveLoopAsync(CancellationToken cancellationToken)
    {
        sessions.OutputReceived += HandleSessionOutput;
        sessions.SessionExited += HandleSessionExit;

        try
        {
            while (socket.State == WebSocketState.Open && !cancellationToken.IsCancellationRequested)
            {
                var message = await ReceiveTextMessageAsync(cancellationToken);
                if (message is null)
                {
                    break;
                }

                await HandleMessageAsync(message, cancellationToken);
            }
        }
        finally
        {
            sessions.OutputReceived -= HandleSessionOutput;
            sessions.SessionExited -= HandleSessionExit;
            await CloseOwnedSessionsOnDisconnectAsync();
        }
    }

    public async Task SendAsync<T>(T message, CancellationToken cancellationToken)
    {
        var json = JsonSerializer.Serialize(message, JsonOptions);
        var bytes = Encoding.UTF8.GetBytes(json);

        await _sendLock.WaitAsync(cancellationToken);
        try
        {
            if (socket.State == WebSocketState.Open)
            {
                await socket.SendAsync(bytes, WebSocketMessageType.Text, true, cancellationToken);
            }
        }
        finally
        {
            _sendLock.Release();
        }
    }

    public ValueTask DisposeAsync()
    {
        _sendLock.Dispose();
        return ValueTask.CompletedTask;
    }

    private async Task<string?> ReceiveTextMessageAsync(CancellationToken cancellationToken)
    {
        var buffer = new byte[8192];
        using var output = new MemoryStream();

        while (true)
        {
            var result = await socket.ReceiveAsync(buffer, cancellationToken);
            if (result.MessageType == WebSocketMessageType.Close)
            {
                await socket.CloseAsync(WebSocketCloseStatus.NormalClosure, "closing", cancellationToken);
                return null;
            }

            if (result.MessageType != WebSocketMessageType.Text)
            {
                await SendAsync(ErrorMessage.Create(null, "invalid_message_type", "Only text messages are supported."), cancellationToken);
                return null;
            }

            output.Write(buffer, 0, result.Count);
            if (result.EndOfMessage)
            {
                return Encoding.UTF8.GetString(output.ToArray());
            }
        }
    }

    private async Task HandleMessageAsync(string message, CancellationToken cancellationToken)
    {
        string? requestId = null;

        try
        {
            using var document = JsonDocument.Parse(message);
            requestId = TryReadString(document.RootElement, "requestId");
            var type = TryReadString(document.RootElement, "type");

            switch (type)
            {
                case BridgeMessageTypes.SessionCreate:
                    await HandleCreateAsync(message, cancellationToken);
                    break;
                case BridgeMessageTypes.SessionInput:
                    await HandleInputAsync(message, cancellationToken);
                    break;
                case BridgeMessageTypes.SessionResize:
                    await HandleResizeAsync(message, cancellationToken);
                    break;
                case BridgeMessageTypes.SessionSignal:
                    await HandleSignalAsync(message, cancellationToken);
                    break;
                case BridgeMessageTypes.SessionClose:
                    await HandleCloseAsync(message, cancellationToken);
                    break;
                case BridgeMessageTypes.SessionList:
                    await SendAsync(SessionListMessage.Create(requestId, sessions.List()), cancellationToken);
                    break;
                default:
                    await SendAsync(ErrorMessage.Create(requestId, "unknown_message_type", "Unknown message type."), cancellationToken);
                    break;
            }
        }
        catch (JsonException)
        {
            await SendAsync(ErrorMessage.Create(requestId, "invalid_json", "Message must be valid JSON."), cancellationToken);
        }
        catch (Exception ex) when (ex is InvalidOperationException or ArgumentException or ArgumentOutOfRangeException or KeyNotFoundException or IOException)
        {
            logger.LogWarning(ex, "Rejected WebSocket message {RequestId}", requestId);
            await SendAsync(ErrorMessage.Create(requestId, "request_failed", ex.Message), cancellationToken);
        }
    }

    private async Task HandleCreateAsync(string message, CancellationToken cancellationToken)
    {
        var request = JsonSerializer.Deserialize<SessionCreateRequest>(message, JsonOptions)
            ?? throw new InvalidOperationException("Invalid session.create payload.");
        var sessionId = await sessions.CreateAsync(new CreateSessionInput(request.Cli, request.Cwd, request.Cols, request.Rows), cancellationToken);
        _ownedSessions.TryAdd(sessionId, 0);
        await SendAsync(SessionCreatedMessage.Create(request.RequestId, sessionId), cancellationToken);
    }

    private async Task HandleInputAsync(string message, CancellationToken cancellationToken)
    {
        var request = JsonSerializer.Deserialize<SessionInputRequest>(message, JsonOptions)
            ?? throw new InvalidOperationException("Invalid session.input payload.");
        await sessions.WriteInputAsync(request.SessionId, request.Text, cancellationToken);
    }

    private async Task HandleResizeAsync(string message, CancellationToken cancellationToken)
    {
        var request = JsonSerializer.Deserialize<SessionResizeRequest>(message, JsonOptions)
            ?? throw new InvalidOperationException("Invalid session.resize payload.");
        await sessions.ResizeAsync(request.SessionId, request.Cols, request.Rows, cancellationToken);
    }

    private async Task HandleSignalAsync(string message, CancellationToken cancellationToken)
    {
        var request = JsonSerializer.Deserialize<SessionSignalRequest>(message, JsonOptions)
            ?? throw new InvalidOperationException("Invalid session.signal payload.");

        var signal = request.Signal switch
        {
            "interrupt" => SessionSignal.Interrupt,
            "terminate" => SessionSignal.Terminate,
            _ => throw new InvalidOperationException("Unsupported session signal.")
        };

        await sessions.SignalAsync(request.SessionId, signal, cancellationToken);
    }

    private async Task HandleCloseAsync(string message, CancellationToken cancellationToken)
    {
        var request = JsonSerializer.Deserialize<SessionCloseRequest>(message, JsonOptions)
            ?? throw new InvalidOperationException("Invalid session.close payload.");
        await sessions.CloseAsync(request.SessionId, cancellationToken);
        _ownedSessions.TryRemove(request.SessionId, out _);
        await SendAsync(SessionClosedMessage.Create(request.RequestId, request.SessionId), cancellationToken);
    }

    private static string? TryReadString(JsonElement element, string propertyName) =>
        element.TryGetProperty(propertyName, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString()
            : null;

    private void HandleSessionOutput(object? sender, SessionOutputEvent output)
    {
        if (!_ownedSessions.ContainsKey(output.SessionId))
        {
            return;
        }

        _ = SendSessionEventAsync(SessionOutputMessage.Create(output.SessionId, output.Stream, output.Text));
    }

    private void HandleSessionExit(object? sender, SessionExitEvent exit)
    {
        if (!_ownedSessions.TryRemove(exit.SessionId, out _))
        {
            return;
        }

        _ = SendSessionEventAsync(SessionExitMessage.Create(exit.SessionId, exit.ExitCode));
    }

    private async Task SendSessionEventAsync<T>(T message)
    {
        try
        {
            await SendAsync(message, CancellationToken.None);
        }
        catch (Exception ex) when (ex is ObjectDisposedException or InvalidOperationException or WebSocketException)
        {
            logger.LogDebug(ex, "Failed to send session event to closed WebSocket connection.");
        }
    }

    private async Task CloseOwnedSessionsOnDisconnectAsync()
    {
        if (!options.TerminateOnDisconnect || options.KeepAliveOnDisconnect)
        {
            return;
        }

        foreach (var sessionId in _ownedSessions.Keys)
        {
            if (_ownedSessions.TryRemove(sessionId, out _))
            {
                await sessions.CloseAsync(sessionId, CancellationToken.None);
            }
        }
    }
}
