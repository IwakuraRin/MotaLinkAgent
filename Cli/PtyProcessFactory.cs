using Porta.Pty;

namespace MotaBridge.Cli;

/// Starts the configured CLI inside a pseudo terminal so interactive tools behave like real terminal apps.
public sealed class PtyProcessFactory : IPtyProcessFactory
{
    public async Task<CliSessionProcess> StartAsync(
        CliDefinition cli,
        string cwd,
        int cols,
        int rows,
        CancellationToken cancellationToken)
    {
        var options = new PtyOptions
        {
            Name = cli.Id,
            App = cli.Command,
            CommandLine = cli.Args.ToArray(),
            Cwd = cwd,
            Cols = cols,
            Rows = rows,
            Environment = new Dictionary<string, string>
            {
                ["TERM"] = "xterm-256color"
            }
        };

        try
        {
            var connection = await PtyProvider.SpawnAsync(options, cancellationToken);
            return new CliSessionProcess(connection);
        }
        catch (Exception ex) when (ex is InvalidOperationException or IOException)
        {
            throw new InvalidOperationException($"CLI command could not be started: {cli.Id}.", ex);
        }
    }
}
