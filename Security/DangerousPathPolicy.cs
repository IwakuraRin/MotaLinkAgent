namespace MotaBridge.Security;

public static class DangerousPathPolicy
{
    private static readonly HashSet<string> DangerousExactPaths = new(StringComparer.Ordinal)
    {
        Path.GetPathRoot(Environment.CurrentDirectory) ?? "/",
        "/",
        "/bin",
        "/dev",
        "/etc",
        "/Library",
        "/private",
        "/System",
        "/usr",
        "/var",
        "C:\\",
        "C:\\Windows",
        "C:\\Program Files",
        "C:\\Program Files (x86)"
    };

    public static bool IsDangerous(string path)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            return true;
        }

        try
        {
            var normalized = Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
            return DangerousExactPaths.Contains(normalized);
        }
        catch
        {
            return true;
        }
    }
}
