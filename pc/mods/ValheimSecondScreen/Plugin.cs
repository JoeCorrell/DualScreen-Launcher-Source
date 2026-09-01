using System.Collections.Concurrent;
using BepInEx;
using BepInEx.Logging;
using HarmonyLib;
using UnityEngine;
using ValheimSecondScreen.Network;
using ValheimSecondScreen.Senders;

namespace ValheimSecondScreen
{
    [BepInPlugin(PluginInfo.PLUGIN_GUID, PluginInfo.PLUGIN_NAME, PluginInfo.PLUGIN_VERSION)]
    public sealed class Plugin : BaseUnityPlugin
    {
        public static Plugin Instance { get; private set; }
        internal static new ManualLogSource Log { get; private set; }

        public WebSocketServer Server { get; private set; }

        // Actions enqueued from background threads that need to run on Unity's main thread
        private readonly ConcurrentQueue<System.Action> _mainThreadQueue =
            new ConcurrentQueue<System.Action>();

        private Harmony _harmony;
        private GameObject _driver;
        private const int MaxMainThreadActionsPerFrame = 128;

        private void Awake()
        {
            Instance = this;
            Log      = base.Logger;

            // Start the WebSocket server (binds to 127.0.0.1:7786)
            Server = new WebSocketServer(PluginInfo.WS_PORT);
            Server.OnClientConnected += OnClientConnected;
            Server.OnTextMessage += OnTextMessage;
            Server.Start();

            // Create a persistent GameObject that drives the update loop
            _driver = new GameObject("SecondScreen_Sender");
            DontDestroyOnLoad(_driver);
            _driver.AddComponent<MapDataSender>();
            _driver.AddComponent<Hooks.UIHider>();
            _driver.AddComponent<Hooks.InventorySizeGuard>();
            // Retain the validated double-height framebuffer fallback for
            // hosts that cannot expose Unity display 1. It self-disables as
            // soon as the real second-display route is available.
            _driver.AddComponent<Hooks.HudBandMover>();
            // Moves the real HUD onto the host's second display. This is the
            // whole of the second screen now: wemu reports two monitors through
            // RandR, Unity sees two displays, and the HUD's own RectTransforms
            // are reparented onto the second one. Nothing is captured, nothing
            // is redrawn, and a texture pack or a game patch needs no work here.
            _driver.AddComponent<Hooks.SecondDisplayMover>();

            // Apply all Harmony patches
            _harmony = new Harmony(PluginInfo.PLUGIN_GUID);
            _harmony.PatchAll();

            Log.LogInfo($"[SecondScreen] v{PluginInfo.PLUGIN_VERSION} loaded; " +
                        $"legacy socket fallback on ws://127.0.0.1:{PluginInfo.WS_PORT}");
        }

        private void Update()
        {
            // Drain the main-thread action queue (filled from the WS accept thread)
            int handled = 0;
            while (handled++ < MaxMainThreadActionsPerFrame
                   && _mainThreadQueue.TryDequeue(out var action))
            {
                try { action?.Invoke(); }
                catch (System.Exception exception)
                {
                    Log.LogError("[SecondScreen] Main-thread action failed: " + exception);
                }
            }
        }

        private void OnDestroy()
        {
            Hooks.InventoryGuiHooks.Restore();
            Server?.Stop();
            if (_driver != null) Destroy(_driver);
            _harmony?.UnpatchSelf();
            if (Instance == this) Instance = null;
        }

        /// <summary>
        /// Called from the WS accept background thread when a client connects.
        /// Unity APIs (texture reads, etc.) must run on the main thread, so we
        /// marshal the initial-data send through the main-thread queue.
        /// </summary>
        private void OnClientConnected(WebSocketClient client)
        {
            _mainThreadQueue.Enqueue(() =>
            {
                /*
                 * The socket is only an input transport while Unity owns the
                 * physical lower panel. Sending the legacy mirror here would
                 * read back and PNG-encode the map and every HUD sprite even
                 * though Android is already displaying Unity's second window.
                 */
                if (!Hooks.SecondDisplayMover.SecondDisplayLive)
                    MapDataSender.Instance?.SendInitialData(client);
            });
        }

        private void OnTextMessage(WebSocketClient client, string message)
        {
            _mainThreadQueue.Enqueue(() => CommandHandler.HandleCommand(client, message));
        }

        /// <summary>Convenience method for other subsystems to run work on the main thread.</summary>
        public void EnqueueMainThread(System.Action action) =>
            _mainThreadQueue.Enqueue(action);
    }

    internal static class PluginInfo
    {
        public const string PLUGIN_GUID    = "com.secondscreen.valheim";
        public const string PLUGIN_NAME    = "ValheimSecondScreen";
        public const string PLUGIN_VERSION = "1.3.0";
        public const int    WS_PORT        = 7786;
    }
}
