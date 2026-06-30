namespace MotaBridge.Cli;

public interface ICliRegistry
{
    IReadOnlyList<CliDefinition> List();

    bool TryGet(string cliId, out CliDefinition definition);
}
