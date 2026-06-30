namespace MotaBridge.Auth;

public interface ITokenAuthenticator
{
    bool IsValid(string? token);
}
