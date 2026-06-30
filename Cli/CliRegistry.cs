using Microsoft.Extensions.Options;
using MotaBridge.Config;

namespace MotaBridge.Cli;

public sealed class CliRegistry : ICliRegistry
{
    private readonly Dictionary<string, CliDefinition> _clis;

    public CliRegistry(IOptions<BridgeOptions> options)
    {
        _clis = options.Value.Clis
            .Select(cli => new CliDefinition(cli.Id, cli.Label, cli.Command, cli.Args, cli.AllowedCwds))
            .ToDictionary(cli => cli.Id, StringComparer.OrdinalIgnoreCase);
    }

    public IReadOnlyList<CliDefinition> List() => _clis.Values.OrderBy(cli => cli.Id).ToList();

    public bool TryGet(string cliId, out CliDefinition definition) => _clis.TryGetValue(cliId, out definition!);
}
