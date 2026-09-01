using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using StardewValley;
using StardewValley.Inventories;

namespace StardewSecondScreen
{
    /// <summary>
    /// Stable entrypoints called directly from the patched game assembly.
    /// Every boundary is fail-soft so dual-screen failures cannot stop vanilla
    /// Stardew or a SMAPI-hosted game from starting and updating normally.
    /// </summary>
    public static class NativeHooks
    {
        private static readonly object Gate = new();
        private static readonly object FailureGate = new();
        private static readonly Dictionary<string, long> LastFailureLog = new();
        private static NativeRuntime? _runtime;
        private static bool _initializeAttempted;
        private static bool _shutdown;
        private static bool _worldReady;
        private static int _lastTime = -1;
        private static int _lastDate = -1;
        private static GameLocation? _lastLocation;
        private static long _nextSecond;
        private static Inventory? _inventory;
        private static bool _inventoryChanged;

        [MethodImpl(MethodImplOptions.NoInlining)]
        public static void Initialize()
        {
            lock (Gate)
            {
                if (_initializeAttempted || _shutdown) return;
                _initializeAttempted = true;
                try
                {
                    _runtime = new NativeRuntime();
                    _nextSecond = Environment.TickCount64 + 1000L;
                    AppDomain.CurrentDomain.ProcessExit += (_, _) => Shutdown();
                    NativeLog.Write(
                        LogLevel.Info,
                        "Direct hooks active for Stardew Valley " + Game1.GetVersionString() + ".");
                }
                catch (Exception failure)
                {
                    NativeLog.Write(LogLevel.Error, "Native initialization failed: " + failure);
                }
            }
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        public static void AfterUpdate()
        {
            try
            {
                Initialize();
                var runtime = _runtime;
                if (runtime == null || _shutdown) return;

                var ready = Context.IsWorldReady;
                if (ready != _worldReady)
                {
                    _worldReady = ready;
                    if (ready)
                    {
                        AttachInventory();
                        _lastTime = Game1.timeOfDay;
                        _lastDate = DateKey();
                        _lastLocation = Game1.currentLocation;
                        runtime.OnSaveLoaded();
                    }
                    else
                    {
                        DetachInventory();
                        _lastTime = -1;
                        _lastDate = -1;
                        _lastLocation = null;
                        runtime.OnReturnedToTitle();
                    }
                }

                if (ready)
                {
                    AttachInventory();
                    var date = DateKey();
                    if (date != _lastDate)
                    {
                        _lastDate = date;
                        runtime.OnDayStarted();
                    }

                    if (Game1.timeOfDay != _lastTime)
                    {
                        _lastTime = Game1.timeOfDay;
                        runtime.OnTimeChanged();
                    }

                    if (!ReferenceEquals(Game1.currentLocation, _lastLocation))
                    {
                        _lastLocation = Game1.currentLocation;
                        runtime.OnWarped();
                    }

                    if (_inventoryChanged)
                    {
                        _inventoryChanged = false;
                        runtime.OnInventoryChanged();
                    }
                }

                var now = Environment.TickCount64;
                if (now >= _nextSecond)
                {
                    _nextSecond = now + 1000L;
                    runtime.OnSecond();
                }

                runtime.OnTick(Game1.ticks);
            }
            catch (Exception failure)
            {
                LogHookFailure("AfterUpdate", failure);
            }
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        public static void AfterDraw()
        {
            try
            {
                _runtime?.OnRendered();
            }
            catch (Exception failure)
            {
                LogHookFailure("AfterDraw", failure);
            }
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        public static void Shutdown()
        {
            lock (Gate)
            {
                if (_shutdown) return;
                _shutdown = true;
                DetachInventory();
                try { _runtime?.Dispose(); }
                catch (Exception failure)
                {
                    NativeLog.Write(LogLevel.Error, "Native shutdown failed: " + failure);
                }
                finally
                {
                    _runtime = null;
                }
            }
        }

        private static int DateKey() =>
            ((Game1.year * 4 + (int)Game1.season) * 32) + Game1.dayOfMonth;

        private static void AttachInventory()
        {
            var current = Game1.player?.Items;
            if (ReferenceEquals(current, _inventory)) return;
            DetachInventory();
            if (current == null) return;

            _inventory = current;
            current.OnSlotChanged += OnInventorySlotChanged;
            current.OnInventoryReplaced += OnInventoryReplaced;
        }

        private static void DetachInventory()
        {
            if (_inventory != null)
            {
                _inventory.OnSlotChanged -= OnInventorySlotChanged;
                _inventory.OnInventoryReplaced -= OnInventoryReplaced;
            }
            _inventory = null;
            _inventoryChanged = false;
        }

        private static void OnInventorySlotChanged(
            Inventory inventory,
            int index,
            Item before,
            Item after) => _inventoryChanged = true;

        private static void OnInventoryReplaced(
            Inventory inventory,
            IList<Item> before,
            IList<Item> after) => _inventoryChanged = true;

        private static void LogHookFailure(string hook, Exception failure)
        {
            var now = Environment.TickCount64;
            lock (FailureGate)
            {
                if (LastFailureLog.TryGetValue(hook, out var previous) && now - previous < 60_000)
                    return;
                LastFailureLog[hook] = now;
            }
            NativeLog.Write(LogLevel.Error, hook + " hook failed: " + failure);
        }
    }
}
