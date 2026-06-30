using System.Security.Cryptography;

namespace MotaBridge.Utils;

public static class IdGenerator
{
    public static string NewSessionId() => $"sess_{RandomHex(12)}";

    private static string RandomHex(int byteCount) => Convert.ToHexString(RandomNumberGenerator.GetBytes(byteCount)).ToLowerInvariant();
}
