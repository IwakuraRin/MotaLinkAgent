namespace MotaBridge.Cli;

public interface IPtyProcessFactory
{
    CliSessionProcess Start(CliDefinition cli, string cwd, int cols, int rows);
}
