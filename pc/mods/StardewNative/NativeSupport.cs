using System;
using System.IO;
using System.Text.Json;
using StardewValley;

namespace StardewSecondScreen
{
    internal enum LogLevel
    {
        Trace,
        Debug,
        Info,
        Warn,
        Error,
    }

    internal sealed class NativeMonitor
    {
        public void Log(string message, LogLevel level = LogLevel.Info) =>
            NativeLog.Write(level, message);
    }

    internal static class NativeLog
    {
        private const long RotateAtBytes = 4L * 1024L * 1024L;
        private static readonly object Gate = new();
        private static readonly string Path = System.IO.Path.Combine(
            AppContext.BaseDirectory,
            "StardewDualScreen.Native.log");
        private static bool _opened;

        public static void Write(LogLevel level, string message)
        {
            var line = $"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff} [{level.ToString().ToUpperInvariant()}] "
                + $"[T{Environment.CurrentManagedThreadId}] {message}";
            lock (Gate)
            {
                try
                {
                    OpenLog();
                    File.AppendAllText(Path, line + Environment.NewLine);
                }
                catch
                {
                    // Diagnostics must never become a new game-start failure.
                }
            }

            try { Console.Error.WriteLine("[StardewDualScreen.Native] " + line); }
            catch { }
        }

        private static void OpenLog()
        {
            if (_opened) return;
            _opened = true;

            try
            {
                if (File.Exists(Path) && new FileInfo(Path).Length >= RotateAtBytes)
                {
                    var previous = System.IO.Path.ChangeExtension(Path, ".previous.log");
                    File.Move(Path, previous, overwrite: true);
                }
            }
            catch
            {
                // Keep appending if rotation is unavailable under Wine.
            }

            File.AppendAllText(
                Path,
                Environment.NewLine + "=== Native dual-screen session ===" + Environment.NewLine);
        }
    }

    internal sealed class NativeConfigStore
    {
        private static readonly JsonSerializerOptions JsonOptions = new()
        {
            PropertyNameCaseInsensitive = true,
            WriteIndented = true,
        };

        private readonly string _path = System.IO.Path.Combine(
            AppContext.BaseDirectory,
            "StardewDualScreen.Native.json");

        public T ReadConfig<T>() where T : new()
        {
            try
            {
                if (!File.Exists(_path)) return new T();
                return JsonSerializer.Deserialize<T>(File.ReadAllText(_path), JsonOptions) ?? new T();
            }
            catch (Exception failure)
            {
                NativeLog.Write(LogLevel.Warn, "Could not read native config: " + failure);
                return new T();
            }
        }

        public void WriteConfig<T>(T config)
        {
            try
            {
                var temporary = _path + ".tmp";
                File.WriteAllText(temporary, JsonSerializer.Serialize(config, JsonOptions));
                File.Move(temporary, _path, overwrite: true);
            }
            catch (Exception failure)
            {
                NativeLog.Write(LogLevel.Warn, "Could not write native config: " + failure);
            }
        }
    }

    internal static class Context
    {
        public static bool IsWorldReady
        {
            get
            {
                try
                {
                    return Game1.hasLoadedGame
                        && Game1.player != null
                        && Game1.currentLocation != null;
                }
                catch
                {
                    return false;
                }
            }
        }
    }
}
