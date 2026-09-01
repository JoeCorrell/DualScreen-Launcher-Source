using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Threading;

namespace ValheimSecondScreen.Network
{
    /// <summary>
    /// Lightweight RFC 6455 WebSocket server bound to 127.0.0.1 only.
    /// Handles multi-client broadcast and targeted single-client sends.
    /// No external library dependencies — uses only .NET Framework 4.x BCL.
    /// </summary>
    public sealed class WebSocketServer : IDisposable
    {
        /// <summary>Fired on the accept thread when a new client completes the WS handshake.</summary>
        public event Action<WebSocketClient> OnClientConnected;
        /// <summary>Fired when a text frame is received from a client.</summary>
        public event Action<WebSocketClient, string> OnTextMessage;

        private readonly int _port;
        private TcpListener _listener;
        private Thread _acceptThread;
        private volatile bool _running;

        private readonly List<WebSocketClient> _clients = new List<WebSocketClient>();
        private readonly object _lock = new object();

        public WebSocketServer(int port) { _port = port; }

        public void Start()
        {
            _running = true;
            _listener = new TcpListener(IPAddress.Loopback, _port);
            _listener.Start();
            _acceptThread = new Thread(AcceptLoop)
            {
                IsBackground = true,
                Name = "SecondScreen-Accept"
            };
            _acceptThread.Start();
            Plugin.Log.LogInfo($"[SecondScreen] WebSocket server listening on ws://127.0.0.1:{_port}");
        }

        public void Stop()
        {
            _running = false;
            try { _listener?.Stop(); } catch { }
            lock (_lock)
            {
                foreach (var c in _clients) c.Close();
                _clients.Clear();
            }
        }

        public bool HasClients()
        {
            lock (_lock) { return _clients.Count > 0; }
        }

        /// <summary>Returns a snapshot of connected clients (safe to iterate outside the lock).</summary>
        public List<WebSocketClient> GetAllClients()
        {
            lock (_lock) { return new List<WebSocketClient>(_clients); }
        }

        /// <summary>Broadcast a UTF-8 text WebSocket frame to every connected client.</summary>
        public void Broadcast(string message)
        {
            if (string.IsNullOrEmpty(message)) return;
            var frame = BuildFrame(0x81, Encoding.UTF8.GetBytes(message));
            SendFrame(frame, target: null, broadcast: true);
        }

        /// <summary>Send a UTF-8 text frame to a single specific client (e.g. on initial connect).</summary>
        public void SendTo(WebSocketClient client, string message)
        {
            if (string.IsNullOrEmpty(message) || client == null) return;
            var frame = BuildFrame(0x81, Encoding.UTF8.GetBytes(message));
            SendFrame(frame, target: client, broadcast: false);
        }

        private void SendFrame(byte[] frame, WebSocketClient target, bool broadcast)
        {
            List<WebSocketClient> recipients;
            lock (_lock)
            {
                recipients = broadcast
                    ? new List<WebSocketClient>(_clients)
                    : new List<WebSocketClient> { target };
            }

            List<WebSocketClient> dead = null;
            // Never hold the client-list lock across a socket write. A slow or
            // disconnected companion must not block connects, disconnects, or
            // every other client while the Unity thread is broadcasting.
            foreach (var c in recipients)
            {
                try { c.Send(frame); }
                catch
                {
                    if (dead == null) dead = new List<WebSocketClient>();
                    dead.Add(c);
                }
            }
            if (dead == null) return;
            lock (_lock)
            {
                foreach (var d in dead) _clients.Remove(d);
            }
            foreach (var d in dead) d.Close();
        }

        private void RemoveClient(WebSocketClient client)
        {
            client.Close();
            lock (_lock) { _clients.Remove(client); }
            Plugin.Log.LogInfo("[SecondScreen] Client disconnected.");
        }

        private void AcceptLoop()
        {
            while (_running)
            {
                try
                {
                    var tcp = _listener.AcceptTcpClient();
                    tcp.NoDelay = true;
                    tcp.SendTimeout = 1000;
                    var client = new WebSocketClient(tcp);

                    new Thread(() =>
                    {
                        try
                        {
                            if (!client.Handshake())
                            {
                                client.Close();
                                return;
                            }
                            lock (_lock) { _clients.Add(client); }
                            Plugin.Log.LogInfo("[SecondScreen] Android client connected.");
                            OnClientConnected?.Invoke(client);
                            client.RunReceiveLoop(c => RemoveClient(c),
                                (c, msg) => OnTextMessage?.Invoke(c, msg));
                        }
                        catch (Exception ex)
                        {
                            Plugin.Log.LogWarning($"[SecondScreen] Client setup error: {ex.Message}");
                            client.Close();
                        }
                    })
                    { IsBackground = true, Name = "SecondScreen-Client" }.Start();
                }
                catch (Exception) when (!_running) { break; }
                catch (Exception ex)
                {
                    Plugin.Log.LogError($"[SecondScreen] Accept error: {ex.Message}");
                }
            }
        }

        /// <summary>
        /// Builds an unmasked WebSocket frame (server-to-client frames are never masked per RFC 6455).
        /// opcode: 0x81 = text, 0x82 = binary
        /// </summary>
        private static byte[] BuildFrame(byte opcode, byte[] payload)
        {
            int len = payload.Length;
            int headerLen = len < 126 ? 2 : len < 65536 ? 4 : 10;
            var frame = new byte[headerLen + len];
            frame[0] = opcode;    // FIN=1 + opcode

            if (len < 126)
            {
                frame[1] = (byte)len;
            }
            else if (len < 65536)
            {
                frame[1] = 126;
                frame[2] = (byte)(len >> 8);
                frame[3] = (byte)(len & 0xFF);
            }
            else
            {
                frame[1] = 127;
                long lenLong = (long)len;            // must be long — int shifts wrap at 32 bits
                for (int i = 0; i < 8; i++)
                    frame[9 - i] = (byte)((lenLong >> (i * 8)) & 0xFF);
            }

            Buffer.BlockCopy(payload, 0, frame, headerLen, len);
            return frame;
        }

        public void Dispose() => Stop();
    }

    /// <summary>Represents a single connected Android client over WebSocket.</summary>
    public sealed class WebSocketClient
    {
        private static readonly string WS_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        private readonly TcpClient _tcp;
        private readonly NetworkStream _stream;
        private volatile bool _closed;

        public WebSocketClient(TcpClient tcp)
        {
            _tcp = tcp;
            _stream = tcp.GetStream();
        }

        /// <summary>
        /// Performs the HTTP → WebSocket upgrade handshake.
        /// Returns false if the request is not a valid WebSocket upgrade.
        /// </summary>
        public bool Handshake()
        {
            // Read until we see the blank line that ends HTTP headers
            var sb = new StringBuilder(512);
            var buf = new byte[1];
            while (true)
            {
                int n = _stream.Read(buf, 0, 1);
                if (n == 0) return false;
                sb.Append((char)buf[0]);
                int l = sb.Length;
                if (l >= 4 &&
                    sb[l - 4] == '\r' && sb[l - 3] == '\n' &&
                    sb[l - 2] == '\r' && sb[l - 1] == '\n') break;
                if (l > 8192) return false; // Reject oversized headers
            }

            string wsKey = null;
            foreach (var line in sb.ToString().Split(new[] { "\r\n" }, StringSplitOptions.None))
            {
                if (line.StartsWith("Sec-WebSocket-Key:", StringComparison.OrdinalIgnoreCase))
                    wsKey = line.Substring("Sec-WebSocket-Key:".Length).Trim();
            }
            if (wsKey == null) return false;

            string accept;
            using (var sha = SHA1.Create())
                accept = Convert.ToBase64String(
                    sha.ComputeHash(Encoding.UTF8.GetBytes(wsKey + WS_MAGIC)));

            var response = Encoding.UTF8.GetBytes(
                "HTTP/1.1 101 Switching Protocols\r\n" +
                "Upgrade: websocket\r\n" +
                "Connection: Upgrade\r\n" +
                "Sec-WebSocket-Accept: " + accept + "\r\n\r\n");
            _stream.Write(response, 0, response.Length);
            return true;
        }

        /// <summary>Thread-safe: send a pre-built WebSocket frame.</summary>
        public void Send(byte[] frame)
        {
            if (_closed) return;
            lock (_stream) { _stream.Write(frame, 0, frame.Length); }
        }

        /// <summary>
        /// Blocks on the network stream reading incoming frames.
        /// Responds to WebSocket ping frames (0x09) with pong frames (0x0A) — required by RFC 6455.
        /// OkHttp sends pings every 20 s and drops the connection if no pong arrives.
        /// Calls <paramref name="onDisconnect"/> when the connection ends.
        /// Runs on its own background thread.
        /// </summary>
        public void RunReceiveLoop(Action<WebSocketClient> onDisconnect,
            Action<WebSocketClient, string> onText = null)
        {
            var drain = new byte[65536];
            try
            {
                while (!_closed)
                {
                    int b0 = _stream.ReadByte(); if (b0 < 0) break;
                    int b1 = _stream.ReadByte(); if (b1 < 0) break;
                    int opcode  = b0 & 0x0F;
                    bool masked = (b1 & 0x80) != 0;
                    long payloadLen = b1 & 0x7F;

                    if (payloadLen == 126)
                        payloadLen = (_stream.ReadByte() << 8) | _stream.ReadByte();
                    else if (payloadLen == 127)
                    {
                        payloadLen = 0;
                        for (int i = 0; i < 8; i++)
                            payloadLen = (payloadLen << 8) | (byte)_stream.ReadByte();
                    }

                    byte[] maskKey = null;
                    if (masked)
                    {
                        maskKey = new byte[4];
                        ReadFull(_stream, maskKey, 4);
                    }

                    if ((opcode == 0x01 || opcode == 0x09) && payloadLen > 0 && payloadLen <= 65536)
                    {
                        var payload = new byte[payloadLen];
                        ReadFull(_stream, payload, (int)payloadLen);
                        if (masked && maskKey != null)
                            for (int i = 0; i < payload.Length; i++)
                                payload[i] ^= maskKey[i % 4];

                        if (opcode == 0x01)
                            onText?.Invoke(this, Encoding.UTF8.GetString(payload));
                        else
                        {
                            var pong = new byte[2 + payload.Length];
                            pong[0] = 0x8A;
                            pong[1] = (byte)payload.Length;
                            if (payload.Length > 0) Buffer.BlockCopy(payload, 0, pong, 2, payload.Length);
                            Send(pong);
                        }
                    }
                    else
                    {
                        long rem = payloadLen;
                        while (rem > 0)
                        {
                            int n = _stream.Read(drain, 0, (int)Math.Min(rem, drain.Length));
                            if (n == 0) goto disconnect;
                            rem -= n;
                        }
                    }

                    if (opcode == 0x08) break;
                }
            }
            catch { /* TCP disconnected */ }
            disconnect:
            Close();
            onDisconnect?.Invoke(this);
        }

        public void Close()
        {
            if (_closed) return;
            _closed = true;
            try { _tcp.Close(); } catch { }
        }

        private static void ReadFull(Stream s, byte[] buf, int count)
        {
            int read = 0;
            while (read < count)
            {
                int n = s.Read(buf, read, count - read);
                if (n == 0) throw new EndOfStreamException();
                read += n;
            }
        }
    }
}
