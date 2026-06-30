namespace MotaBridge.Config;

public sealed record ConfigValidationResult(bool IsValid, IReadOnlyList<string> Errors)
{
    public static ConfigValidationResult Success { get; } = new(true, []);

    public static ConfigValidationResult Failure(List<string> errors) => new(false, errors);
}
