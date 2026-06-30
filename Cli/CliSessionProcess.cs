using System.Text;
using Porta.Pty;

namespace MotaBridge.Cli;

public sealed class CliSessionProcess : IAsyncDisposable
{
    private readonly IPtyConnection _connection;
    private readonly CancellationTokenSource _disposeCts = new();
    private readonly Task _outputTask;
    private int? _exitCode;
    private int _disposed;

    public CliSessionProcess(IPtyConnection connection)
    {
        _connection = connection;
        _connection.ProcessExited += HandleExited;
        _outputTask = ReadOutputAsync(_connection.ReaderStream, _disposeCts.Token);
    }

    public event Action<CliProcessOutput>? OutputReceived;

    public event Action<int>? Exited;

    public bool TryGetExitCode(out int exitCode)
    {
        if (_exitCode.HasValue)
        {
            exitCode = _exitCode.Value;
            return true;
        }

        exitCode = 0;
        return false;
    }

    public void Write(string text)
    {
        ObjectDisposedException.ThrowIf(_disposed != 0, this);

        if (_exitCode.HasValue)
        {
            throw new InvalidOperationException("CLI process has already exited.");
        }

        var bytes = Encoding.UTF8.GetBytes(text);
        _connection.WriterStream.Write(bytes, 0, bytes.Length);
        _connection.WriterStream.Flush();
    }

    public void Resize(int cols, int rows)
    {
        ObjectDisposedException.ThrowIf(_disposed != 0, this);
        _connection.Resize(cols, rows);
    }

    public void Interrupt()
    {
        ObjectDisposedException.ThrowIf(_disposed != 0, this);

        if (_exitCode.HasValue)
        {
            throw new InvalidOperationException("CLI process has already exited.");
        }

        Write("\u0003");
    }

    public void Terminate()
    {
        ObjectDisposedException.ThrowIf(_disposed != 0, this);

        if (!_exitCode.HasValue)
        {
            _connection.Kill();
        }
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
        {
            return;
        }

        try
        {
            if (!_exitCode.HasValue)
            {
                _connection.ProcessExited -= HandleExited;
                _connection.Kill();
                await _disposeCts.CancelAsync();
            }

            await _outputTask.WaitAsync(TimeSpan.FromSeconds(2));
        }
        catch (Exception ex) when (ex is InvalidOperationException or TaskCanceledException or OperationCanceledException or TimeoutException)
        {
            // Disposal is best-effort; process termination is already requested above.
        }
        finally
        {
            _connection.ProcessExited -= HandleExited;
            await _disposeCts.CancelAsync();
            _disposeCts.Dispose();
            _connection.Dispose();
        }
    }

    private async Task ReadOutputAsync(Stream stream, CancellationToken cancellationToken)
    {
        var buffer = new byte[4096];
        var decoder = Encoding.UTF8.GetDecoder();
        var chars = new char[Encoding.UTF8.GetMaxCharCount(buffer.Length)];

        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                var bytesRead = await stream.ReadAsync(buffer, cancellationToken);
                if (bytesRead == 0)
                {
                    break;
                }

                var charCount = decoder.GetChars(buffer, 0, bytesRead, chars, 0);
                if (charCount > 0)
                {
                    OnOutputReceived(new CliProcessOutput("stdout", new string(chars, 0, charCount)));
                }
            }
        }
        catch (Exception ex) when (ex is ObjectDisposedException or IOException or OperationCanceledException)
        {
            // The stream is expected to close when the process exits or the session is disposed.
        }
    }

    private void HandleExited(object? sender, EventArgs args)
    {
        _exitCode = _connection.ExitCode;
        OnExited(_exitCode.Value);
    }

    private void OnOutputReceived(CliProcessOutput output) => OutputReceived?.Invoke(output);

    private void OnExited(int exitCode) => Exited?.Invoke(exitCode);
}

public sealed record CliProcessOutput(string Stream, string Text);
