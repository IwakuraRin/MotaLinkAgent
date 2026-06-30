using System.Net;
using MotaBridge.Security;

namespace MotaBridge.Config;

public static class ConfigValidator
{
    public static ConfigValidationResult Validate(BridgeOptions options)
    {
        var errors = new List<string>();

        if (string.IsNullOrWhiteSpace(options.Host) || !IPAddress.TryParse(options.Host, out _))
        {
            errors.Add("host must be a valid IP address, for example 127.0.0.1, 0.0.0.0, or 192.168.x.x.");
        }

        if (options.Port is < 1 or > 65_535)
        {
            errors.Add("port must be between 1 and 65535.");
        }

        if (string.IsNullOrWhiteSpace(options.AuthToken))
        {
            errors.Add("authToken must not be empty.");
        }

        if (options.SessionIdleTimeoutMs < 30_000)
        {
            errors.Add("sessionIdleTimeoutMs must be at least 30000.");
        }

        if (options.Clis.Count == 0)
        {
            errors.Add("at least one CLI must be configured.");
        }

        foreach (var group in options.Clis.GroupBy(cli => cli.Id, StringComparer.OrdinalIgnoreCase).Where(group => group.Count() > 1))
        {
            errors.Add($"duplicate CLI id: {group.Key}.");
        }

        foreach (var cli in options.Clis)
        {
            ValidateCli(cli, errors);
        }

        return errors.Count == 0 ? ConfigValidationResult.Success : ConfigValidationResult.Failure(errors);
    }

    private static void ValidateCli(CliOptions cli, List<string> errors)
    {
        if (string.IsNullOrWhiteSpace(cli.Id))
        {
            errors.Add("CLI id must not be empty.");
        }

        if (string.IsNullOrWhiteSpace(cli.Label))
        {
            errors.Add($"CLI '{cli.Id}' label must not be empty.");
        }

        if (string.IsNullOrWhiteSpace(cli.Command))
        {
            errors.Add($"CLI '{cli.Id}' command must not be empty.");
        }

        if (cli.AllowedCwds.Count == 0)
        {
            errors.Add($"CLI '{cli.Id}' must define at least one allowed cwd.");
        }

        foreach (var cwd in cli.AllowedCwds)
        {
            if (string.IsNullOrWhiteSpace(cwd))
            {
                errors.Add($"CLI '{cli.Id}' allowed cwd must not be empty.");
                continue;
            }

            if (DangerousPathPolicy.IsDangerous(cwd))
            {
                errors.Add($"CLI '{cli.Id}' allowed cwd is dangerous: {cwd}.");
            }
        }
    }
}
