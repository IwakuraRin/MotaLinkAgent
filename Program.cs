using MotaBridge.Auth;
using MotaBridge.Cli;
using MotaBridge.Config;
using MotaBridge.Sessions;
using MotaBridge.WebSockets;

var builder = WebApplication.CreateBuilder(args);

builder.Configuration.AddJsonFile("mota-bridge.config.json", optional: false, reloadOnChange: true);

var bridgeOptions = builder.Configuration.Get<BridgeOptions>() ?? new BridgeOptions();
var validation = ConfigValidator.Validate(bridgeOptions);
if (!validation.IsValid)
{
    throw new InvalidOperationException($"Invalid bridge configuration: {string.Join("; ", validation.Errors)}");
}

builder.WebHost.UseUrls($"http://{bridgeOptions.Host}:{bridgeOptions.Port}");

builder.Services.Configure<BridgeOptions>(builder.Configuration);
builder.Services.AddSingleton<ITokenAuthenticator, TokenAuthenticator>();
builder.Services.AddSingleton<ICwdPolicy, CwdPolicy>();
builder.Services.AddSingleton<ICliRegistry, CliRegistry>();
builder.Services.AddSingleton<IPtyProcessFactory, PtyProcessFactory>();
builder.Services.AddSingleton<ISessionManager, SessionManager>();
builder.Services.AddSingleton<BridgeWebSocketEndpoint>();
builder.Services.AddHostedService<SessionIdleCleaner>();

var app = builder.Build();

app.UseWebSockets(new WebSocketOptions
{
    KeepAliveInterval = TimeSpan.FromSeconds(30)
});

app.MapGet("/", () => Results.Ok(new
{
    name = "Mota Bridge",
    status = "running"
}));

app.Map("/ws", async context =>
{
    var endpoint = context.RequestServices.GetRequiredService<BridgeWebSocketEndpoint>();
    await endpoint.HandleAsync(context, context.RequestAborted);
});

var logger = app.Services.GetRequiredService<ILoggerFactory>().CreateLogger("MotaBridge.Startup");
logger.LogInformation(
    "Mota Bridge listening on {Host}:{Port}. WebSocket path: /ws. Configured CLIs: {CliIds}",
    bridgeOptions.Host,
    bridgeOptions.Port,
    string.Join(", ", bridgeOptions.Clis.Select(cli => cli.Id)));

await app.RunAsync();
