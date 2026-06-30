namespace MotaBridge.Cli;

public sealed class PtyProcessFactory : IPtyProcessFactory
{
    public CliSessionProcess Start(CliDefinition cli, string cwd, int cols, int rows)
    {
        throw new NotImplementedException("PTY process startup will be implemented in the CLI runtime phase.");
    }
}
