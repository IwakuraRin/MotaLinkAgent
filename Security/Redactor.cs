using System.Text.RegularExpressions;

namespace MotaBridge.Security;

public static partial class Redactor
{
    public static string RedactSecretLikeFields(string text)
    {
        if (string.IsNullOrEmpty(text))
        {
            return text;
        }

        return SecretFieldRegex().Replace(text, "$1=<redacted>");
    }

    [GeneratedRegex(@"(?i)\b(token|secret|password|api[_-]?key|authorization)\s*[:=]\s*([^\s,;]+)")]
    private static partial Regex SecretFieldRegex();
}
