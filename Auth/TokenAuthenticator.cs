using System.Security.Cryptography;
using System.Text;
using Microsoft.Extensions.Options;
using MotaBridge.Config;

namespace MotaBridge.Auth;

public sealed class TokenAuthenticator(IOptions<BridgeOptions> options) : ITokenAuthenticator
{
    private readonly byte[] _expectedToken = Encoding.UTF8.GetBytes(options.Value.AuthToken);

    public bool IsValid(string? token)
    {
        if (string.IsNullOrEmpty(token))
        {
            return false;
        }

        var providedToken = Encoding.UTF8.GetBytes(token);
        return providedToken.Length == _expectedToken.Length
            && CryptographicOperations.FixedTimeEquals(providedToken, _expectedToken);
    }
}
