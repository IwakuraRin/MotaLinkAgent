using System.IO.Ports; // 使用串口相关功能
using System.Diagnostics; // 使用进程和调试功能
using System.Runtime.InteropServices; // 检测运行时平台
using Microsoft.AspNetCore.Http; // 使用 ASP.NET Core 的 HttpContext 和相关类型

// DTOs and helpers
record SerialOpenDto(string PortName, int BaudRate); // 串口打开请求的数据传输对象，包含端口名和波特率
record SerialSendDto(string Data); // 串口发送请求的数据传输对象，包含要发送的字符串

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
    } // Send 方法结束
} // SerialManager 类结束

public partial class Program // 将 Program 声明为 partial，以便与代码库中其他部分合并
{ // Program 类开始
    // Html page moved into Program as a static member
    private const string HtmlPage = @"<!doctype html>
<html>
<head>
  <meta charset='utf-8'/>
  <title>Host PC - Camera & Serial</title>
  <style>body{font-family:Arial;margin:20px} img{border:1px solid #ccc}</style>
</head>
<body>
  <h1>Camera Preview</h1>
  <p><img id='cam' src='/camera' alt='camera' width='640' height='480'/></p>

  <h2>Serial Port</h2>
  <p>
    <label>Port: <select id='ports'></select></label>
    <label>Baud: <input id='baud' value='115200' style='width:80px'/></label>
    <button id='open'>Open</button>
    <button id='close'>Close</button>
  </p>
  <p>
    <input id='cmd' style='width:400px' placeholder='command to send'/>
    <button id='send'>Send</button>
  </p>

  <h3>Serial Log</h3>
  <pre id='log' style='height:200px;overflow:auto;border:1px solid #ccc;padding:8px'></pre>

  <script>
    async function loadPorts(){
      const res = await fetch('/api/serial/ports');
      const ports = await res.json();
      const sel = document.getElementById('ports'); sel.innerHTML = '';
      ports.forEach(p=>{ const o=document.createElement('option'); o.value=p; o.text=p; sel.appendChild(o); });
    }
    document.getElementById('open').addEventListener('click', async ()=>{
      const port = document.getElementById('ports').value;
      const baud = parseInt(document.getElementById('baud').value||'115200');
      await fetch('/api/serial/open',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({portName:port,baudRate:baud})});
    });
    document.getElementById('close').addEventListener('click', async ()=>{ await fetch('/api/serial/close',{method:'POST'}); });
    document.getElementById('send').addEventListener('click', async ()=>{
      const d = document.getElementById('cmd').value; await fetch('/api/serial/send',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({data:d})});
    });

    // SSE for incoming serial data
    const evt = new EventSource('/api/serial/stream');
    evt.onmessage = e=>{ const pre = document.getElementById('log'); pre.textContent += JSON.parse(e.data); pre.scrollTop = pre.scrollHeight; };

    loadPorts();
  </script>
</body>
</html>"; // HtmlPage 常量，包含页面的完整 HTML（内部内容不添加注释以避免破坏字符串）

    private static string GetFfmpegArguments() // 获取用于启动 ffmpeg 的默认参数或从环境变量覆盖
    { // GetFfmpegArguments 方法开始
        // Allow override via environment variables
        var env = Environment.GetEnvironmentVariable("FFMPEG_ARGS"); // 从环境变量读取 ffmpeg 参数覆盖
        if (!string.IsNullOrEmpty(env)) return env; // 如果设置了环境变量则直接返回它

        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
        { // 如果是 Windows 平台
            // Default DirectShow device. Users must replace the device name if needed via FFMPEG_ARGS.
            // Example override for Windows:
            // setx FFMPEG_ARGS "-f dshow -i video=Integrated Camera -r 25 -s 640x480 -f mjpeg pipe:1"
            return "-f dshow -i video=Integrated Camera -r 25 -s 640x480 -f mjpeg pipe:1"; // Windows 默认参数
        }
        else
        { // 非 Windows 平台（假设为 Linux）
            // Linux default v4l2
            return "-f v4l2 -framerate 25 -video_size 640x480 -i /dev/video0 -f mjpeg pipe:1"; // Linux 默认参数
        }
    } // GetFfmpegArguments 方法结束

    public static async System.Threading.Tasks.Task Main(string[] args) // 应用程序入口点 Main
    { // Main 方法开始
        var builder = WebApplication.CreateBuilder(args); // 创建 WebApplicationBuilder
        var app = builder.Build(); // 构建 WebApplication

        // In-memory serial port manager
        var serialManager = new SerialManager(); // 实例化串口管理器

        app.MapGet("/", async ctx =>
        { // 映射根路径，返回 HTML 页面
            ctx.Response.ContentType = "text/html; charset=utf-8"; // 设置响应的内容类型为 HTML
            await ctx.Response.WriteAsync(HtmlPage); // 将 HtmlPage 写入响应
        });

        // Camera MJPEG endpoint. Spawns ffmpeg per client and pipes MJPEG frames.
        app.MapGet("/camera", async (HttpContext ctx) =>
        { // 映射摄像头流路径
            ctx.Response.Headers["Cache-Control"] = "no-cache"; // 禁用缓存
            ctx.Response.ContentType = "multipart/x-mixed-replace; boundary=frame"; // 设置为 MJPEG 多部分响应

            var ct = ctx.RequestAborted; // 获取请求取消令牌

            var ffmpegArgs = GetFfmpegArguments(); // 获取 ffmpeg 参数

            var psi = new ProcessStartInfo
            { // 设置启动 ffmpeg 的进程参数
                FileName = "ffmpeg", // 可执行文件名
                Arguments = ffmpegArgs, // 参数
                RedirectStandardOutput = true, // 重定向标准输出以读取 MJPEG 数据
                RedirectStandardError = true, // 重定向标准错误（可用于日志）
                UseShellExecute = false, // 不使用 shell 执行
                CreateNoWindow = true // 不创建窗口
            };

            using var ff = Process.Start(psi); // 启动 ffmpeg 进程
            if (ff == null)
            { // 如果启动失败则返回 500
                ctx.Response.StatusCode = 500; // 设置状态码
                await ctx.Response.WriteAsync("Failed to start ffmpeg. Make sure ffmpeg is installed and in PATH."); // 写入错误信息
                return; // 结束请求处理
            }

            var stdout = ff.StandardOutput.BaseStream; // 获取 ffmpeg 的标准输出流

            var buffer = new byte[4096]; // 读取缓冲区
            var ms = new MemoryStream(); // 用于累积来自 ffmpeg 的字节数据

            try
            { // 尝试读取并提取 JPEG 帧
                while (!ct.IsCancellationRequested)
                { // 循环直到请求取消
                    int read = await stdout.ReadAsync(buffer, 0, buffer.Length, ct); // 从 ffmpeg 输出读取字节
                    if (read == 0) break; // 读取到 0 表示流结束，跳出循环
                    ms.Write(buffer, 0, read); // 将读取到的字节写入内存流

                    // Try to extract complete JPEG frames (0xFFD8 ... 0xFFD9)
                    while (true)
                    { // 尝试从累积的数据中解析完整 JPEG 帧
                        var data = ms.ToArray(); // 获取当前缓冲区所有字节
                        int start = -1, end = -1; // 初始化起始和结束索引
                        for (int i = 0; i + 1 < data.Length; i++)
                        { // 查找 JPEG 起始标识 0xFFD8
                            if (data[i] == 0xFF && data[i + 1] == 0xD8)
                            {
                                start = i; // 找到起始索引
                                break; // 退出 for 循环
                            }
                        }
                        if (start == -1) break; // 没有找到起始标识，则等待更多数据
                        for (int i = start + 2; i + 1 < data.Length; i++)
                        { // 从起始位置之后查找 JPEG 结束标识 0xFFD9
                            if (data[i] == 0xFF && data[i + 1] == 0xD9)
                            {
                                end = i + 1; // 找到结束索引（包含 0xD9）
                                break; // 退出 for 循环
                            }
                        }
                        if (end == -1) break; // 未找到结束标识，等待更多数据

                        int len = end - start + 1; // 计算帧长度
                        var frame = new byte[len]; // 创建帧数组
                        Array.Copy(data, start, frame, 0, len); // 从缓冲区复制完整帧

                        // remove consumed bytes
                        var remaining = new byte[data.Length - (end + 1)]; // 计算剩余字节长度
                        Array.Copy(data, end + 1, remaining, 0, remaining.Length); // 复制剩余字节
                        ms.SetLength(0); // 清空内存流
                        ms.Write(remaining, 0, remaining.Length); // 将剩余字节写回内存流

                        // write multipart frame
                        var header = $"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: {frame.Length}\r\n\r\n"; // 构造多部分响应头
                        var headerBytes = System.Text.Encoding.ASCII.GetBytes(header); // 将头转换为字节
                        await ctx.Response.Body.WriteAsync(headerBytes, 0, headerBytes.Length, ct); // 写入头部
                        await ctx.Response.Body.WriteAsync(frame, 0, frame.Length, ct); // 写入帧数据
                        var trailer = System.Text.Encoding.ASCII.GetBytes("\r\n"); // 分隔符
                        await ctx.Response.Body.WriteAsync(trailer, 0, trailer.Length, ct); // 写入分隔符
                        await ctx.Response.Body.FlushAsync(ct); // 刷新响应流
                    }
                }
            }
            catch (OperationCanceledException) { } // 捕获取消异常并忽略
            finally
            {
                try { ff.Kill(true); } catch { } // 尝试终止 ffmpeg 进程并忽略异常
            }
        });

        // Serial control APIs
        app.MapGet("/api/serial/ports", () => serialManager.GetPorts()); // 返回可用串口列表的 API

        app.MapPost("/api/serial/open", async (HttpContext ctx) =>
        { // 打开串口的 API
            var dto = await ctx.Request.ReadFromJsonAsync<SerialOpenDto>(); // 从请求中解析 DTO
            if (dto == null || string.IsNullOrEmpty(dto.PortName)) return Results.BadRequest(); // 验证请求参数
            var ok = serialManager.Open(dto.PortName, dto.BaudRate == 0 ? 115200 : dto.BaudRate); // 打开串口
            return ok ? Results.Ok() : Results.StatusCode(500); // 返回结果状态
        });

        app.MapPost("/api/serial/close", () => { serialManager.Close(); return Results.Ok(); }); // 关闭串口的 API

        app.MapPost("/api/serial/send", async (HttpContext ctx) =>
        { // 发送串口数据的 API
            var dto = await ctx.Request.ReadFromJsonAsync<SerialSendDto>(); // 从请求中解析 DTO
            if (dto == null) return Results.BadRequest(); // 验证请求
            var ok = serialManager.Send(dto.Data); // 发送数据到串口
            return ok ? Results.Ok() : Results.StatusCode(500); // 返回发送结果
        });

        // Server-Sent Events for incoming serial data
        app.MapGet("/api/serial/stream", async (HttpContext ctx) =>
        { // 串口数据的 SSE 流 API
            ctx.Response.Headers["Cache-Control"] = "no-cache"; // 禁用缓存
            ctx.Response.ContentType = "text/event-stream"; // SSE 内容类型
            var token = ctx.RequestAborted; // 获取取消令牌

            void OnData(string line)
            { // 当收到串口数据时，写入 SSE 事件
                try
                {
                    var msg = $"data: {System.Text.Json.JsonSerializer.Serialize(line)}\n\n"; // 格式化 SSE 消息
                    var bytes = System.Text.Encoding.UTF8.GetBytes(msg); // 将消息转换为字节
                    ctx.Response.Body.WriteAsync(bytes, 0, bytes.Length, token).Wait(); // 同步写入响应体（阻塞）
                    ctx.Response.Body.FlushAsync(token).Wait(); // 刷新响应体
                }
                catch { } // 写入时忽略异常
            }

            serialManager.DataReceived += OnData; // 订阅串口数据事件
            try
            {
                // Keep request open
                while (!token.IsCancellationRequested)
                {
                    await System.Threading.Tasks.Task.Delay(1000, token); // 每秒检查一次取消令牌
                }
            }
            catch (OperationCanceledException) { } // 捕获取消异常并忽略
            finally
            {
                serialManager.DataReceived -= OnData; // 取消订阅事件
            }
        });

        await app.RunAsync("http://0.0.0.0:5000"); // 启动 Web 应用并监听端口 5000
    } // Main 方法结束
} // Program 类结束

