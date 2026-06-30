namespace MotaBridge.Cli;

public interface IPtyProcessFactory
{
    Task<CliSessionProcess> StartAsync(CliDefinition cli, string cwd, int cols, int rows, CancellationToken cancellationToken);
}
