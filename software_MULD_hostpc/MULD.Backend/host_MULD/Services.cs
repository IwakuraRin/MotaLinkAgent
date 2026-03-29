using System.IO.Ports;
using System.Runtime.InteropServices;

namespace host_MULD.Services
{
    public record SerialOpenDto(string PortName, int BaudRate);
    public record SerialSendDto(string Data);

    public class SerialManager
    {
        SerialPort? port;
        readonly object sync = new();
        public event Action<string>? DataReceived;

        public string[] GetPorts() => SerialPort.GetPortNames();

        public bool Open(string portName, int baudRate)
        {
            lock (sync)
            {
                try
                {
                    if (port is not null && port.IsOpen) port.Close();
                    port = new SerialPort(portName, baudRate) { NewLine = "\n", ReadTimeout = 500 };
                    port.DataReceived += Port_DataReceived;
                    port.Open();
                    return true;
                }
                catch
                {
                    port = null;
                    return false;
                }
            }
        }

        private void Port_DataReceived(object? sender, SerialDataReceivedEventArgs e)
        {
            try
            {
                var p = sender as SerialPort;
                if (p is null) return;
                var s = p.ReadExisting();
                if (!string.IsNullOrEmpty(s)) DataReceived?.Invoke(s);
            }
            catch { }
        }

        public void Close()
        {
            lock (sync)
            {
                if (port is not null)
                {
                    try { port.DataReceived -= Port_DataReceived; port.Close(); } catch { }
                    port = null;
                }
            }
        }

        public bool Send(string data)
        {
            lock (sync)
            {
                try
                {
                    if (port is null || !port.IsOpen) return false;
                    port.WriteLine(data);
                    return true;
                }
                catch { return false; }
            }
        }
    }

    public static class FfmpegArgs
    {
        public static string Get()
        {
            var env = Environment.GetEnvironmentVariable("FFMPEG_ARGS");
            if (!string.IsNullOrEmpty(env)) return env;

            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            {
                return "-f dshow -i video=Integrated Camera -r 25 -s 640x480 -f mjpeg pipe:1";
            }
            else
            {
                return "-f v4l2 -framerate 25 -video_size 640x480 -i /dev/video0 -f mjpeg pipe:1";
            }
        }
    }
}
