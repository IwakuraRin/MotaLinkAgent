namespace MotaBridge.Cli;

public interface ICwdPolicy
{
    bool IsAllowed(CliDefinition cli, string cwd);
}
