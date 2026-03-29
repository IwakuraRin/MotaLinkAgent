using System.IO.Ports;                // 使用串口相关功能
using System.Diagnostics;             // 使用进程和调试功能
using System.Runtime.InteropServices; // 检测运行时平台
using Microsoft.AspNetCore.Http;      // 使用 ASP.NET Core 的 HttpContext 和相关类型
using System.Net;                     // 网络 IPAddress
using System.Net.WebSockets;          // WebSocket 类型

// DTOs and helpers
record SerialOpenDto(string PortName, int BaudRate); // 串口打开请求的数据传输对象，包含端口名和波特率
record SerialSendDto(string Data);                   // 串口发送请求的数据传输对象，包含要发送的字符串

class SerialManager // 管理串口连接和数据接收的类
{ // SerialManager 类开始
    SerialPort? port; // 当前打开的串口实例，可能为空
    readonly object sync = new(); // 用于同步访问串口的锁对象
    public event Action<string>? DataReceived; // 当收到串口数据时触发的事件

    public string[] GetPorts() => SerialPort.GetPortNames(); // 获取系统可用串口名称数组

    public bool Open(string portName, int baudRate) // 打开指定串口和波特率
    { // Open 方法开始
        lock (sync) // 使用锁保证线程安全
        { // lock 块开始
            try
            { // 尝试打开串口
                if (port is not null && port.IsOpen) port.Close(); // 如果已有打开的串口，先关闭它
                port = new SerialPort(portName, baudRate) { NewLine = "\n", ReadTimeout = 500 }; // 创建新的 SerialPort 实例并设置换行符和读取超时
                port.DataReceived += Port_DataReceived; // 订阅数据接收事件
                port.Open(); // 打开串口
                return true; // 打开成功返回 true
            }
            catch
            { // 捕获任何异常并处理
                port = null; // 重置 port 为 null
                return false; // 返回 false 表示打开失败
            }
        } // lock 块结束
    } // Open 方法结束

    private void Port_DataReceived(object? sender, SerialDataReceivedEventArgs e) // 串口数据接收回调
    { // Port_DataReceived 方法开始
        try
        { // 尝试读取接收的数据
            var p = sender as SerialPort; // 将 sender 转换为 SerialPort
            if (p is null) return; // 如果转换失败则退出
            var s = p.ReadExisting(); // 读取所有可用的数据为字符串
            if (!string.IsNullOrEmpty(s)) DataReceived?.Invoke(s); // 如果有数据则触发 DataReceived 事件
        }
        catch { } // 忽略读取过程中的异常
    } // Port_DataReceived 方法结束

    public void Close() // 关闭串口并清理
    { // Close 方法开始
        lock (sync) // 使用锁保证线程安全
        { // lock 块开始
            if (port is not null)
            { // 如果有串口实例则尝试关闭
                try { port.DataReceived -= Port_DataReceived; port.Close(); } catch { } // 取消订阅事件并关闭串口，忽略异常
                port = null; // 重置 port 为 null
            }
        } // lock 块结束
    } // Close 方法结束

    public bool Send(string data) // 向串口发送一行数据
    { // Send 方法开始
        lock (sync) // 使用锁保证线程安全
        { // lock 块开始
            try
            { // 尝试发送数据
                if (port is null || !port.IsOpen) return false; // 如果串口未打开则返回 false
                port.WriteLine(data); // 发送一行数据（包含换行符）
                return true; // 发送成功返回 true
            }
            catch { return false; } // 发送异常则返回 false
        } // lock 块结束
    }     // Send 方法结束
}         // SerialManager 类结束

public partial class Program // 精简后的 Program：不再嵌入前端，只提供 API 和 WebSocket
{
    private static string GetFfmpegArguments()
    {
        var env = Environment.GetEnvironmentVariable("FFMPEG_ARGS");
        if (!string.IsNullOrEmpty(env)) return env;
        var videoDevice = Environment.GetEnvironmentVariable("VIDEO_DEVICE");
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
        {
            return "-f dshow -i video=Integrated Camera -r 25 -s 640x480 -f mjpeg pipe:1";
        }
        else
        {
            var dev = string.IsNullOrEmpty(videoDevice) ? "/dev/video0" : videoDevice;
            return $"-f v4l2 -framerate 25 -video_size 640x480 -i {dev} -f mjpeg pipe:1";
        }
    }

    public static async System.Threading.Tasks.Task Main(string[] args)
    {
        var builder = WebApplication.CreateBuilder(args);

        // Configure Kestrel to optionally listen on two endpoints (internal + external)
        builder.WebHost.ConfigureKestrel(options =>
        {
            try
            {
                var internalIp = IPAddress.Parse(Environment.GetEnvironmentVariable("INTERNAL_IP") ?? "192.168.10.124");
                var internalPort = int.Parse(Environment.GetEnvironmentVariable("INTERNAL_PORT") ?? "5000");
                var externalIp = IPAddress.Parse(Environment.GetEnvironmentVariable("EXTERNAL_IP") ?? "0.0.0.0");
                var externalPort = int.Parse(Environment.GetEnvironmentVariable("EXTERNAL_PORT") ?? "5001");
                options.Listen(internalIp, internalPort);
                options.Listen(externalIp, externalPort);
            }
            catch
            {
                // If parsing fails, fall back to default Kestrel settings
            }
        });

        var app = builder.Build();

        // Enable WebSockets
        var wsOptions = new WebSocketOptions { KeepAliveInterval = TimeSpan.FromSeconds(30) };
        app.UseWebSockets(wsOptions);

        var serialManager = new SerialManager();

        // Minimal root endpoint to indicate service is running
        app.MapGet("/", () => Results.Text("MULD host backend - serial & camera API"));

        // REST endpoints (serial control)
        app.MapGet("/api/serial/ports", () => serialManager.GetPorts());
        app.MapPost("/api/serial/open", async (HttpContext ctx) =>
        {
            var dto = await ctx.Request.ReadFromJsonAsync<SerialOpenDto>();
            if (dto == null || string.IsNullOrEmpty(dto.PortName)) return Results.BadRequest();
            var ok = serialManager.Open(dto.PortName, dto.BaudRate == 0 ? 115200 : dto.BaudRate);
            return ok ? Results.Ok() : Results.StatusCode(500);
        });
        app.MapPost("/api/serial/close", () => { serialManager.Close(); return Results.Ok(); });
        app.MapPost("/api/serial/send", async (HttpContext ctx) =>
        {
            var dto = await ctx.Request.ReadFromJsonAsync<SerialSendDto>();
            if (dto == null) return Results.BadRequest();
            var ok = serialManager.Send(dto.Data);
            return ok ? Results.Ok() : Results.StatusCode(500);
        });

        // Camera MJPEG endpoint (unchanged behavior)
        app.MapGet("/camera", async (HttpContext ctx) =>
        {
            ctx.Response.Headers["Cache-Control"] = "no-cache";
            ctx.Response.ContentType = "multipart/x-mixed-replace; boundary=frame";
            var ct = ctx.RequestAborted;
            var ffmpegArgs = GetFfmpegArguments();
            var psi = new ProcessStartInfo
            {
                FileName = "ffmpeg",
                Arguments = ffmpegArgs,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true
            };
            using var ff = Process.Start(psi);
            if (ff == null)
            {
                ctx.Response.StatusCode = 500;
                await ctx.Response.WriteAsync("Failed to start ffmpeg. Make sure ffmpeg is installed and in PATH.");
                return;
            }
            var stdout = ff.StandardOutput.BaseStream;
            var buffer = new byte[4096];
            var ms = new MemoryStream();
            try
            {
                while (!ct.IsCancellationRequested)
                {
                    int read = await stdout.ReadAsync(buffer, 0, buffer.Length, ct);
                    if (read == 0) break;
                    ms.Write(buffer, 0, read);
                    while (true)
                    {
                        var data = ms.ToArray();
                        int start = -1, end = -1;
                        for (int i = 0; i + 1 < data.Length; i++)
                        {
                            if (data[i] == 0xFF && data[i + 1] == 0xD8) { start = i; break; }
                        }
                        if (start == -1) break;
                        for (int i = start + 2; i + 1 < data.Length; i++)
                        {
                            if (data[i] == 0xFF && data[i + 1] == 0xD9) { end = i + 1; break; }
                        }
                        if (end == -1) break;
                        int len = end - start + 1;
                        var frame = new byte[len];
                        Array.Copy(data, start, frame, 0, len);
                        var remaining = new byte[data.Length - (end + 1)];
                        Array.Copy(data, end + 1, remaining, 0, remaining.Length);
                        ms.SetLength(0);
                        ms.Write(remaining, 0, remaining.Length);
                        var header = $"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: {frame.Length}\r\n\r\n";
                        var headerBytes = System.Text.Encoding.ASCII.GetBytes(header);
                        await ctx.Response.Body.WriteAsync(headerBytes, 0, headerBytes.Length, ct);
                        await ctx.Response.Body.WriteAsync(frame, 0, frame.Length, ct);
                        var trailer = System.Text.Encoding.ASCII.GetBytes("\r\n");
                        await ctx.Response.Body.WriteAsync(trailer, 0, trailer.Length, ct);
                        await ctx.Response.Body.FlushAsync(ct);
                    }
                }
            }
            catch (OperationCanceledException) { }
            finally { try { ff.Kill(true); } catch { } }
        });

        // WebSocket endpoint for bidirectional serial control
        var webSockets = new System.Collections.Concurrent.ConcurrentDictionary<WebSocket, byte>();

        // Forward serial data to connected WebSocket clients
        void BroadcastSerial(string line)
        {
            var msg = System.Text.Encoding.UTF8.GetBytes(line);
            foreach (var kv in webSockets.Keys)
            {
                var ws = kv;
                if (ws.State != WebSocketState.Open) continue;
                _ = ws.SendAsync(new ArraySegment<byte>(msg), WebSocketMessageType.Text, true, CancellationToken.None)
                    .ContinueWith(t => { if (t.IsFaulted) { webSockets.TryRemove(ws, out _); } });
            }
        }

        serialManager.DataReceived += BroadcastSerial;

        app.MapGet("/ws/serial", async (HttpContext ctx) =>
        {
            if (!ctx.WebSockets.IsWebSocketRequest) { ctx.Response.StatusCode = 400; return; }
            using var ws = await ctx.WebSockets.AcceptWebSocketAsync();
            webSockets.TryAdd(ws, 0);
            // 连接后立即推送当前串口状态
            try
            {
                string statusMsg = serialManager.GetPorts().Length > 0 ? "串口服务已就绪" : "无可用串口";
                var statusJson = System.Text.Encoding.UTF8.GetBytes($"{{\"type\":\"status\",\"status\":\"{statusMsg}\"}}");
                await ws.SendAsync(new ArraySegment<byte>(statusJson), WebSocketMessageType.Text, true, CancellationToken.None);
            }
            catch { }
            var buffer = new byte[1024 * 4];
            try
            {
                while (ws.State == WebSocketState.Open)
                {
                    var result = await ws.ReceiveAsync(new ArraySegment<byte>(buffer), CancellationToken.None);
                    if (result.MessageType == WebSocketMessageType.Close) break;
                    if (result.MessageType == WebSocketMessageType.Text)
                    {
                        var text = System.Text.Encoding.UTF8.GetString(buffer, 0, result.Count);
                        // treat incoming text as serial data to send
                        serialManager.Send(text);
                    }
                }
            }
            catch { }
            finally { webSockets.TryRemove(ws, out _); try { await ws.CloseAsync(WebSocketCloseStatus.NormalClosure, "", CancellationToken.None); } catch { } }
        });

        await app.RunAsync();
    }
}

