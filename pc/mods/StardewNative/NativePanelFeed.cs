using System;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace StardewSecondScreen
{
    /// <summary>
    /// In-process replacement for the temporary WebSocket transport.
    ///
    /// The existing publishers still emit the same versioned JSON messages, but
    /// those messages now go straight into the native renderer in this process.
    /// Keeping that model boundary preserves the proven game-state extraction
    /// while removing every network/client lifecycle from the display path.
    /// </summary>
    internal sealed class NativePanelFeed : IDisposable
    {
        private const int InputLimit = 256;

        private readonly Action<string> _log;
        private readonly NativePanelRenderer _renderer;
        private readonly ConcurrentQueue<NativePanelInput> _input = new();
        private readonly CancellationTokenSource _cancel = new();
        private NativePanelWindow? _window;
        private Thread? _udpThread;
        private UdpClient? _udp;
        private volatile bool _started;
        private int _inputCount;
        private int _inputDropLogged;
        private long _nextUdpFailureLogAt;

        public NativePanelFeed(Action<string> log)
        {
            _log = log;
            _renderer = new NativePanelRenderer(log);
        }

        public bool HasClients => _started && _window is { IsReady: true };

        public event Action<string>? MessageReceived;

        public void Start()
        {
            if (_started) return;
            _window = new NativePanelWindow(_log, QueueInput);
            if (!_window.Start())
            {
                _window.Dispose();
                _window = null;
                _log("Native lower-panel window could not be created.");
                return;
            }

            _started = true;
            StartUdpInput();
            _log("Native lower-panel window is ready; no WebSocket client is required.");
        }

        public void Send(string payload) => _renderer.Accept(payload);

        public void Broadcast(string kind, string payload) => _renderer.Accept(payload);

        public void ResetWorldState()
        {
            while (_input.TryDequeue(out _)) Interlocked.Decrement(ref _inputCount);
            _renderer.ResetWorldState();
        }

        /// <summary>Runs native-panel mutations on Stardew's update thread.</summary>
        public void PumpInput()
        {
            while (_input.TryDequeue(out var input))
            {
                Interlocked.Decrement(ref _inputCount);
                try
                {
                    var command = _renderer.HandleInput(input);
                    if (!string.IsNullOrEmpty(command)) MessageReceived?.Invoke(command);
                }
                catch (Exception failure)
                {
                    _log("Native panel input failed: " + failure.Message);
                }
            }
            if (Volatile.Read(ref _inputCount) < InputLimit / 2)
                Interlocked.Exchange(ref _inputDropLogged, 0);
        }

        /// <summary>Draws with Stardew's GraphicsDevice after the main frame.</summary>
        public void RenderOnGameThread()
        {
            var window = _window;
            if (!_started || window is not { IsReady: true }) return;
            _renderer.RenderIfNeeded(window.Present);
        }

        private void StartUdpInput()
        {
            try
            {
                _udp = new UdpClient(new IPEndPoint(IPAddress.Loopback, NativePanelWindow.InputPort));
                _udp.Client.ReceiveTimeout = 250;
                _udpThread = new Thread(UdpInputLoop)
                {
                    IsBackground = true,
                    Name = "Stardew native panel input",
                    Priority = ThreadPriority.BelowNormal,
                };
                _udpThread.Start();
            }
            catch (Exception failure)
            {
                _log("Native panel UDP input is unavailable; window touch remains active: "
                    + failure.Message);
            }
        }

        private void UdpInputLoop()
        {
            var endpoint = new IPEndPoint(IPAddress.Loopback, 0);
            while (!_cancel.IsCancellationRequested && _udp != null)
            {
                try
                {
                    var packet = _udp.Receive(ref endpoint);
                    if (!IPAddress.IsLoopback(endpoint.Address) || packet.Length == 0) continue;
                    ParseInput(Encoding.ASCII.GetString(packet));
                }
                catch (SocketException failure)
                    when (failure.SocketErrorCode == SocketError.TimedOut ||
                          failure.SocketErrorCode == SocketError.Interrupted)
                {
                }
                catch (ObjectDisposedException)
                {
                    return;
                }
                catch (Exception failure)
                {
                    var now = Environment.TickCount64;
                    if (!_cancel.IsCancellationRequested && now >= _nextUdpFailureLogAt)
                    {
                        _nextUdpFailureLogAt = now + 60_000;
                        _log("Native panel input receiver failed: " + failure.Message);
                    }
                }
            }
        }

        private void ParseInput(string raw)
        {
            var parts = raw.Trim().Split(' ', StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length == 0) return;

            if ((parts[0] == "tap" || parts[0] == "hold" || parts[0] == "down" ||
                 parts[0] == "move" || parts[0] == "up") && parts.Length >= 3 &&
                float.TryParse(parts[1], System.Globalization.NumberStyles.Float,
                    System.Globalization.CultureInfo.InvariantCulture, out var x) &&
                float.TryParse(parts[2], System.Globalization.NumberStyles.Float,
                    System.Globalization.CultureInfo.InvariantCulture, out var y))
            {
                var kind = parts[0] switch
                {
                    "hold" => NativePanelInputKind.Hold,
                    "down" => NativePanelInputKind.Down,
                    "move" => NativePanelInputKind.Move,
                    "up" => NativePanelInputKind.Up,
                    _ => NativePanelInputKind.Tap,
                };
                QueueInput(new NativePanelInput(kind, x, y, 0, normalized: true));
                return;
            }

            if (parts[0] == "pinch" && parts.Length >= 4 &&
                float.TryParse(parts[1], System.Globalization.NumberStyles.Float,
                    System.Globalization.CultureInfo.InvariantCulture, out x) &&
                float.TryParse(parts[2], System.Globalization.NumberStyles.Float,
                    System.Globalization.CultureInfo.InvariantCulture, out y) &&
                float.TryParse(parts[3], System.Globalization.NumberStyles.Float,
                    System.Globalization.CultureInfo.InvariantCulture, out var scale))
            {
                QueueInput(new NativePanelInput(
                    NativePanelInputKind.Scroll, x, y, scale >= 1f ? -1 : 1, normalized: true));
                return;
            }

            if (parts[0] == "page" && parts.Length >= 2)
            {
                var previous = parts[1].Equals("previous", StringComparison.OrdinalIgnoreCase) ||
                    (int.TryParse(parts[1], out var page) && page < 0);
                QueueInput(new NativePanelInput(
                    previous ? NativePanelInputKind.PreviousPage : NativePanelInputKind.NextPage));
                return;
            }

            if (parts[0] == "nav" && parts.Length >= 2)
            {
                parts = parts[1..];
            }

            if (parts[0] == "toolbar" && parts.Length >= 2)
            {
                QueueInput(new NativePanelInput(
                    parts[1].Equals("previous", StringComparison.OrdinalIgnoreCase)
                        ? NativePanelInputKind.PreviousToolbar
                        : NativePanelInputKind.NextToolbar));
                return;
            }

            var command = parts[0].ToLowerInvariant() switch
            {
                "previous" => NativePanelInputKind.PreviousPage,
                "next" => NativePanelInputKind.NextPage,
                "up" => NativePanelInputKind.UpKey,
                "down" => NativePanelInputKind.DownKey,
                "left" => NativePanelInputKind.Left,
                "right" => NativePanelInputKind.Right,
                "sort" => NativePanelInputKind.Sort,
                "use" => NativePanelInputKind.Use,
                "activate" => NativePanelInputKind.Activate,
                _ => NativePanelInputKind.None,
            };
            if (command != NativePanelInputKind.None) QueueInput(new NativePanelInput(command));
        }

        private void QueueInput(NativePanelInput input)
        {
            if (float.IsNaN(input.X) || float.IsInfinity(input.X) ||
                float.IsNaN(input.Y) || float.IsInfinity(input.Y) ||
                float.IsNaN(input.Value) || float.IsInfinity(input.Value))
                return;

            if (Interlocked.Increment(ref _inputCount) > InputLimit)
            {
                Interlocked.Decrement(ref _inputCount);
                if (Interlocked.Exchange(ref _inputDropLogged, 1) == 0)
                    _log($"Native panel input queue reached {InputLimit}; dropping excess input.");
                return;
            }
            _input.Enqueue(input);
        }

        public void Dispose()
        {
            _started = false;
            _cancel.Cancel();
            try { _udp?.Close(); } catch { }
            if (_udpThread is { IsAlive: true }) _udpThread.Join(1000);
            DisposePart("UDP input", () => _udp?.Dispose());
            DisposePart("lower-panel window", () => _window?.Dispose());
            DisposePart("renderer", _renderer.Dispose);
            if (_udpThread is not { IsAlive: true })
                DisposePart("input cancellation", _cancel.Dispose);
        }

        private void DisposePart(string part, Action dispose)
        {
            try { dispose(); }
            catch (Exception failure) { _log($"Native panel {part} shutdown failed: {failure}"); }
        }
    }

    internal enum NativePanelInputKind
    {
        None,
        Tap,
        Hold,
        Down,
        Move,
        Up,
        Scroll,
        PreviousPage,
        NextPage,
        Left,
        Right,
        UpKey,
        DownKey,
        Activate,
        Sort,
        Use,
        PreviousToolbar,
        NextToolbar,
    }

    internal readonly struct NativePanelInput
    {
        public NativePanelInput(
            NativePanelInputKind kind,
            float x = 0,
            float y = 0,
            float value = 0,
            bool normalized = false)
        {
            Kind = kind;
            X = x;
            Y = y;
            Value = value;
            Normalized = normalized;
        }

        public NativePanelInputKind Kind { get; }
        public float X { get; }
        public float Y { get; }
        public float Value { get; }
        public bool Normalized { get; }
    }

    /// <summary>Private CPU/GDI guest window routed by WEMU to the lower panel.</summary>
    internal sealed class NativePanelWindow : IDisposable
    {
        public const int LogicalWidth = 1240;
        public const int LogicalHeight = 1080;
        public const int InputPort = 7788;

        private const string WindowClass = "StardewValleySecondScreenPanel";
        private const string WindowTitle = "Stardew Valley Dual Screen";
        private const uint WsPopup = 0x80000000;
        private const uint WsExToolWindow = 0x00000080;
        private const uint WsExNoActivate = 0x08000000;
        private const int SwShowNoActivate = 4;
        private const uint WmPaint = 0x000F;
        private const uint WmDestroy = 0x0002;
        private const uint WmClose = 0x0010;
        private const uint WmEraseBackground = 0x0014;
        private const uint WmLButtonDown = 0x0201;
        private const uint WmLButtonUp = 0x0202;
        private const uint WmRButtonUp = 0x0205;
        private const uint WmMouseMove = 0x0200;
        private const uint WmMouseWheel = 0x020A;
        private const uint WmKeyDown = 0x0100;
        private const uint WmAppPresent = 0x8001;
        private const uint DibRgbColors = 0;
        private const uint SrcCopy = 0x00CC0020;
        private const uint MonitorInfoPrimary = 1;

        private readonly Action<string> _log;
        private readonly Action<NativePanelInput> _input;
        private readonly ManualResetEventSlim _ready = new(false);
        private readonly object _frameGate = new();
        private readonly WndProc _windowProc;
        private Thread? _thread;
        private IntPtr _window;
        private byte[]? _frame;
        private int _width = LogicalWidth;
        private int _height = LogicalHeight;
        private bool _pointerDown;
        private volatile bool _disposed;

        public NativePanelWindow(Action<string> log, Action<NativePanelInput> input)
        {
            _log = log;
            _input = input;
            _windowProc = WindowProcedure;
        }

        public bool IsReady => _window != IntPtr.Zero && !_disposed;

        public bool Start()
        {
            _thread = new Thread(WindowThread)
            {
                IsBackground = true,
                Name = "Stardew native lower panel",
                Priority = ThreadPriority.BelowNormal,
            };
            _thread.Start();
            return _ready.Wait(4000) && IsReady;
        }

        public void Present(byte[] bgra)
        {
            if (!IsReady || bgra.Length != LogicalWidth * LogicalHeight * 4) return;
            lock (_frameGate)
            {
                if (_frame == null || _frame.Length != bgra.Length)
                    _frame = new byte[bgra.Length];
                Buffer.BlockCopy(bgra, 0, _frame, 0, bgra.Length);
            }
            PostMessage(_window, WmAppPresent, IntPtr.Zero, IntPtr.Zero);
        }

        private void WindowThread()
        {
            try
            {
                var bounds = ChooseMonitor();
                _width = Math.Max(1, bounds.Right - bounds.Left);
                _height = Math.Max(1, bounds.Bottom - bounds.Top);
                var instance = GetModuleHandle(null);
                var windowClass = new WndClassEx
                {
                    Size = (uint)Marshal.SizeOf<WndClassEx>(),
                    WindowProcedure = Marshal.GetFunctionPointerForDelegate(_windowProc),
                    Instance = instance,
                    Cursor = LoadCursor(IntPtr.Zero, new IntPtr(32512)),
                    ClassName = WindowClass,
                };
                var atom = RegisterClassEx(ref windowClass);
                if (atom == 0 && Marshal.GetLastWin32Error() != 1410)
                    throw new InvalidOperationException(
                        "RegisterClassEx failed with " + Marshal.GetLastWin32Error());

                _window = CreateWindowEx(
                    WsExNoActivate | WsExToolWindow,
                    WindowClass,
                    WindowTitle,
                    WsPopup,
                    bounds.Left,
                    bounds.Top,
                    _width,
                    _height,
                    IntPtr.Zero,
                    IntPtr.Zero,
                    instance,
                    IntPtr.Zero);
                if (_window == IntPtr.Zero)
                    throw new InvalidOperationException(
                        "CreateWindowEx failed with " + Marshal.GetLastWin32Error());

                ShowWindow(_window, SwShowNoActivate);
                UpdateWindow(_window);
                _ready.Set();
                _log($"Native panel created at {bounds.Left},{bounds.Top} {_width}x{_height}.");

                while (!_disposed && GetMessage(out var message, IntPtr.Zero, 0, 0) > 0)
                {
                    TranslateMessage(ref message);
                    DispatchMessage(ref message);
                }
            }
            catch (Exception failure)
            {
                _log("Native panel window thread failed: " + failure);
                _ready.Set();
            }
            finally
            {
                var window = _window;
                _window = IntPtr.Zero;
                if (window != IntPtr.Zero && IsWindow(window)) DestroyWindow(window);
            }
        }

        private IntPtr WindowProcedure(IntPtr window, uint message, IntPtr wParam, IntPtr lParam)
        {
            switch (message)
            {
                case WmPaint:
                    Paint(window);
                    return IntPtr.Zero;
                case WmEraseBackground:
                    return new IntPtr(1);
                case WmAppPresent:
                    InvalidateRect(window, IntPtr.Zero, false);
                    return IntPtr.Zero;
                case WmLButtonDown:
                    _pointerDown = true;
                    EmitPointer(NativePanelInputKind.Down, lParam);
                    return IntPtr.Zero;
                case WmMouseMove:
                    if (_pointerDown) EmitPointer(NativePanelInputKind.Move, lParam);
                    return IntPtr.Zero;
                case WmLButtonUp:
                    EmitPointer(_pointerDown ? NativePanelInputKind.Up : NativePanelInputKind.Tap,
                        lParam);
                    _pointerDown = false;
                    return IntPtr.Zero;
                case WmRButtonUp:
                    EmitPointer(NativePanelInputKind.Hold, lParam);
                    return IntPtr.Zero;
                case WmMouseWheel:
                    var delta = unchecked((short)((long)wParam >> 16));
                    _input(new NativePanelInput(
                        NativePanelInputKind.Scroll, value: delta > 0 ? -1 : 1));
                    return IntPtr.Zero;
                case WmKeyDown:
                    EmitKey(wParam.ToInt32());
                    return IntPtr.Zero;
                case WmClose:
                    DestroyWindow(window);
                    return IntPtr.Zero;
                case WmDestroy:
                    PostQuitMessage(0);
                    return IntPtr.Zero;
                default:
                    return DefWindowProc(window, message, wParam, lParam);
            }
        }

        private void Paint(IntPtr window)
        {
            var dc = BeginPaint(window, out var paint);
            if (dc == IntPtr.Zero) return;
            try
            {
                lock (_frameGate)
                {
                    var frame = _frame;
                    if (frame == null)
                    {
                        var brush = CreateSolidBrush(0x000B0909);
                        FillRect(dc, ref paint.Paint, brush);
                        DeleteObject(brush);
                        return;
                    }

                    var info = BitmapInfo.TopDown(LogicalWidth, LogicalHeight);
                    var handle = GCHandle.Alloc(frame, GCHandleType.Pinned);
                    try
                    {
                        StretchDIBits(
                            dc, 0, 0, _width, _height,
                            0, 0, LogicalWidth, LogicalHeight,
                            handle.AddrOfPinnedObject(), ref info, DibRgbColors, SrcCopy);
                    }
                    finally
                    {
                        handle.Free();
                    }
                }
            }
            finally
            {
                EndPaint(window, ref paint);
            }
        }

        private void EmitPointer(NativePanelInputKind kind, IntPtr packed)
        {
            var value = packed.ToInt64();
            var x = unchecked((short)(value & 0xFFFF));
            var y = unchecked((short)((value >> 16) & 0xFFFF));
            _input(new NativePanelInput(
                kind,
                Math.Clamp(x / (float)Math.Max(1, _width), 0f, 1f),
                Math.Clamp(y / (float)Math.Max(1, _height), 0f, 1f),
                normalized: true));
        }

        private void EmitKey(int virtualKey)
        {
            var kind = virtualKey switch
            {
                0x25 or 0x21 => NativePanelInputKind.PreviousPage,
                0x27 or 0x22 => NativePanelInputKind.NextPage,
                0x26 => NativePanelInputKind.UpKey,
                0x28 => NativePanelInputKind.DownKey,
                0x0D or 0x20 => NativePanelInputKind.Activate,
                _ => NativePanelInputKind.None,
            };
            if (kind != NativePanelInputKind.None) _input(new NativePanelInput(kind));
        }

        private static Rect ChooseMonitor()
        {
            var selected = default(Rect);
            var found = false;
            EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero,
                (IntPtr monitor, IntPtr deviceContext, ref Rect monitorRect, IntPtr userData) =>
                {
                    var info = new MonitorInfo { Size = Marshal.SizeOf<MonitorInfo>() };
                    if (!GetMonitorInfo(monitor, ref info)) return true;
                    if ((info.Flags & MonitorInfoPrimary) != 0) return true;
                    selected = info.Monitor;
                    found = true;
                    return false;
                }, IntPtr.Zero);
            return found ? selected : new Rect(0, 720, LogicalWidth, 720 + LogicalHeight);
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            var window = _window;
            if (window != IntPtr.Zero) PostMessage(window, WmClose, IntPtr.Zero, IntPtr.Zero);
            if (_thread is { IsAlive: true }) _thread.Join(1000);
        }

        private delegate IntPtr WndProc(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
        private delegate bool MonitorProc(
            IntPtr monitor, IntPtr dc, ref Rect monitorRect, IntPtr data);

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct WndClassEx
        {
            public uint Size;
            public uint Style;
            public IntPtr WindowProcedure;
            public int ClassExtra;
            public int WindowExtra;
            public IntPtr Instance;
            public IntPtr Icon;
            public IntPtr Cursor;
            public IntPtr Background;
            public string? MenuName;
            public string ClassName;
            public IntPtr SmallIcon;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct Message
        {
            public IntPtr Window;
            public uint Value;
            public IntPtr WParam;
            public IntPtr LParam;
            public uint Time;
            public Point Point;
            public uint Private;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct Point { public int X; public int Y; }

        [StructLayout(LayoutKind.Sequential)]
        private struct Rect
        {
            public int Left;
            public int Top;
            public int Right;
            public int Bottom;
            public Rect(int left, int top, int right, int bottom)
            {
                Left = left; Top = top; Right = right; Bottom = bottom;
            }
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct PaintStruct
        {
            public IntPtr Dc;
            public int Erase;
            public Rect Paint;
            public int Restore;
            public int IncUpdate;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)]
            public byte[] Reserved;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct MonitorInfo
        {
            public int Size;
            public Rect Monitor;
            public Rect Work;
            public uint Flags;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct BitmapInfoHeader
        {
            public uint Size;
            public int Width;
            public int Height;
            public ushort Planes;
            public ushort BitCount;
            public uint Compression;
            public uint SizeImage;
            public int XPelsPerMeter;
            public int YPelsPerMeter;
            public uint ColorsUsed;
            public uint ColorsImportant;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct BitmapInfo
        {
            public BitmapInfoHeader Header;
            public uint Colors;
            public static BitmapInfo TopDown(int width, int height) => new()
            {
                Header = new BitmapInfoHeader
                {
                    Size = (uint)Marshal.SizeOf<BitmapInfoHeader>(),
                    Width = width,
                    Height = -height,
                    Planes = 1,
                    BitCount = 32,
                    Compression = 0,
                    SizeImage = (uint)(width * height * 4),
                },
            };
        }

        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern ushort RegisterClassEx(ref WndClassEx windowClass);
        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CreateWindowEx(
            uint exStyle, string className, string title, uint style,
            int x, int y, int width, int height, IntPtr parent, IntPtr menu,
            IntPtr instance, IntPtr parameter);
        [DllImport("user32.dll")]
        private static extern bool DestroyWindow(IntPtr window);
        [DllImport("user32.dll")]
        private static extern bool IsWindow(IntPtr window);
        [DllImport("user32.dll")]
        private static extern bool ShowWindow(IntPtr window, int command);
        [DllImport("user32.dll")]
        private static extern bool UpdateWindow(IntPtr window);
        [DllImport("user32.dll")]
        private static extern int GetMessage(out Message message, IntPtr window, uint min, uint max);
        [DllImport("user32.dll")]
        private static extern bool TranslateMessage(ref Message message);
        [DllImport("user32.dll")]
        private static extern IntPtr DispatchMessage(ref Message message);
        [DllImport("user32.dll")]
        private static extern IntPtr DefWindowProc(
            IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
        [DllImport("user32.dll")]
        private static extern bool PostMessage(
            IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
        [DllImport("user32.dll")]
        private static extern void PostQuitMessage(int exitCode);
        [DllImport("user32.dll")]
        private static extern IntPtr BeginPaint(IntPtr window, out PaintStruct paint);
        [DllImport("user32.dll")]
        private static extern bool EndPaint(IntPtr window, ref PaintStruct paint);
        [DllImport("user32.dll")]
        private static extern bool InvalidateRect(IntPtr window, IntPtr rect, bool erase);
        [DllImport("user32.dll")]
        private static extern int FillRect(IntPtr dc, ref Rect rect, IntPtr brush);
        [DllImport("gdi32.dll")]
        private static extern IntPtr CreateSolidBrush(uint color);
        [DllImport("gdi32.dll")]
        private static extern bool DeleteObject(IntPtr value);
        [DllImport("gdi32.dll")]
        private static extern int StretchDIBits(
            IntPtr dc, int x, int y, int width, int height,
            int sourceX, int sourceY, int sourceWidth, int sourceHeight,
            IntPtr bits, ref BitmapInfo info, uint usage, uint operation);
        [DllImport("user32.dll")]
        private static extern bool EnumDisplayMonitors(
            IntPtr dc, IntPtr clip, MonitorProc callback, IntPtr data);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern bool GetMonitorInfo(IntPtr monitor, ref MonitorInfo info);
        [DllImport("user32.dll")]
        private static extern IntPtr LoadCursor(IntPtr instance, IntPtr cursor);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr GetModuleHandle(string? moduleName);
    }
}
