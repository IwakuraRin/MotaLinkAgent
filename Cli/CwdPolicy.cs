using MotaBridge.Security;

namespace MotaBridge.Cli;

public sealed class CwdPolicy : ICwdPolicy
{
    public bool IsAllowed(CliDefinition cli, string cwd)
    {
        if (string.IsNullOrWhiteSpace(cwd) || DangerousPathPolicy.IsDangerous(cwd))
        {
            return false;
        }

        var requested = Normalize(cwd);

        return cli.AllowedCwds
            .Select(Normalize)
            .Any(allowed => requested.Equals(allowed, StringComparison.Ordinal)
                || requested.StartsWith(allowed + Path.DirectorySeparatorChar, StringComparison.Ordinal));
    }

    private static string Normalize(string path) => Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
}
