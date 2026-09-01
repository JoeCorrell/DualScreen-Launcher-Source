using System;
using System.IO;
using System.Reflection;

namespace StardewSecondScreen
{
    /// <summary>
    /// Small bootstrap copied into Stardew Valley.dll by the patcher. The full
    /// native runtime is stored as an assembly resource, so normal and SMAPI
    /// launches use the same self-contained implementation.
    /// </summary>
    internal static class NativeBootstrap
    {
        internal const string ResourceName = "StardewDualScreen.Native.runtime";
        internal const string SymbolsResourceName = "StardewDualScreen.Native.pdb";
        private const string HooksTypeName = "StardewSecondScreen.NativeHooks";

        private static readonly object Gate = new();
        private static bool _loadAttempted;
        private static Action? _initialize;
        private static Action? _afterUpdate;
        private static Action? _afterDraw;
        private static Action? _shutdown;

        public static void Initialize()
        {
            EnsureLoaded();
            Invoke(_initialize, "Initialize");
        }

        public static void AfterUpdate()
        {
            EnsureLoaded();
            Invoke(_afterUpdate, "AfterUpdate");
        }

        public static void AfterDraw()
        {
            EnsureLoaded();
            Invoke(_afterDraw, "AfterDraw");
        }

        public static void Shutdown()
        {
            Invoke(_shutdown, "Shutdown");
        }

        private static void EnsureLoaded()
        {
            if (_loadAttempted) return;
            lock (Gate)
            {
                if (_loadAttempted) return;
                _loadAttempted = true;
                try
                {
                    using var stream = typeof(NativeBootstrap).Assembly
                        .GetManifestResourceStream(ResourceName)
                        ?? throw new InvalidOperationException(
                            "Embedded dual-screen runtime resource is missing.");
                    using var memory = new MemoryStream();
                    stream.CopyTo(memory);
                    Assembly runtime;
                    using var symbols = typeof(NativeBootstrap).Assembly
                        .GetManifestResourceStream(SymbolsResourceName);
                    if (symbols == null)
                    {
                        runtime = Assembly.Load(memory.ToArray());
                    }
                    else
                    {
                        using var symbolMemory = new MemoryStream();
                        symbols.CopyTo(symbolMemory);
                        runtime = Assembly.Load(memory.ToArray(), symbolMemory.ToArray());
                    }
                    var hooks = runtime.GetType(HooksTypeName, throwOnError: true)
                        ?? throw new TypeLoadException(HooksTypeName);

                    _initialize = GetAction(hooks, "Initialize");
                    _afterUpdate = GetAction(hooks, "AfterUpdate");
                    _afterDraw = GetAction(hooks, "AfterDraw");
                    _shutdown = GetAction(hooks, "Shutdown");
                    WriteDiagnostic("Embedded runtime loaded: " + runtime.FullName);
                }
                catch (Exception failure)
                {
                    WriteDiagnostic("Bootstrap failed: " + failure);
                }
            }
        }

        private static Action GetAction(Type type, string name)
        {
            var method = type.GetMethod(
                name,
                BindingFlags.Public | BindingFlags.Static,
                binder: null,
                types: Type.EmptyTypes,
                modifiers: null)
                ?? throw new MissingMethodException(type.FullName, name);
            return (Action)Delegate.CreateDelegate(typeof(Action), method);
        }

        private static void Invoke(Action? callback, string name)
        {
            if (callback == null) return;
            try
            {
                callback();
            }
            catch (Exception failure)
            {
                WriteDiagnostic(name + " callback failed: " + failure);
            }
        }

        private static void WriteDiagnostic(string message)
        {
            try
            {
                var path = Path.Combine(
                    AppContext.BaseDirectory,
                    "StardewDualScreen.Native.bootstrap.log");
                File.AppendAllText(
                    path,
                    DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff")
                    + " " + message + Environment.NewLine);
            }
            catch
            {
                // A logging failure must never affect the game.
            }
        }
    }
}
