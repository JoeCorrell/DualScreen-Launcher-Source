using HarmonyLib;
using UnityEngine;
using ValheimSecondScreen.Network;

namespace ValheimSecondScreen.Hooks
{
    /// <summary>
    /// Harmony patches that observe Valheim's Minimap and Chat systems
    /// to detect state changes and push them to connected Android clients.
    ///
    /// All patches are postfix so they never interfere with game logic.
    /// </summary>
    [HarmonyPatch]
    internal static class MinimapHooks
    {
        // ── Fog dirty flag ─────────────────────────────────────────────────────

        private static volatile bool _fogDirty;

        /// <summary>
        /// Returns true and clears the flag if fog exploration changed since the last call.
        /// Called from the main thread by <see cref="Senders.MapDataSender"/>.
        /// </summary>
        public static bool ConsumeFogDirty()
        {
            if (!_fogDirty) return false;
            _fogDirty = false;
            return true;
        }

        // ── Fog exploration ────────────────────────────────────────────────────

        /// <summary>
        /// Valheim calls UpdateExplore once every m_exploreInterval (~2 s) while in a world.
        /// We mark the fog dirty so MapDataSender sends a new fog PNG after a debounce.
        /// Matching "UpdateExplore" by name patches the private method — Harmony supports this.
        /// </summary>
        [HarmonyPostfix]
        [HarmonyPatch(typeof(Minimap), "UpdateExplore")]
        private static void UpdateExplore_Postfix()
        {
            // The native second display reads Valheim's live minimap texture.
            // Dirtying the legacy exporter here would eventually trigger a
            // 2048x2048 readback/PNG encode for data nobody consumes.
            if (SecondDisplayMover.SecondDisplayLive) return;
            _fogDirty = true;
        }

        // ── Pin add / remove ───────────────────────────────────────────────────

        /// <summary>
        /// Fires after any pin is added (boss pins, beds, user pins, death markers, etc.).
        /// We skip ephemeral runtime pins (Ping, Player) as they cycle rapidly.
        /// AddPin returns the new PinData as __result.
        /// </summary>
        [HarmonyPostfix]
        [HarmonyPatch(typeof(Minimap), nameof(Minimap.AddPin))]
        private static void AddPin_Postfix(Minimap.PinData __result)
        {
            if (__result == null) return;
            if (__result.m_type == Minimap.PinType.Ping || __result.m_type == Minimap.PinType.Player)
                return;
            if (!LegacyOutputActive()) return;
            Plugin.Instance.Server.Broadcast(Protocol.PinAdded(__result));
        }

        /// <summary>
        /// Fires after the PinData overload of RemovePin (used when the player
        /// explicitly deletes a pin from the map UI). The vector overload
        /// (<see cref="Minimap.RemovePin(UnityEngine.Vector3, float)"/>) is for
        /// radius-based removal and also covered below.
        /// </summary>
        [HarmonyPostfix]
        [HarmonyPatch(typeof(Minimap), nameof(Minimap.RemovePin),
            new[] { typeof(Minimap.PinData) })]
        private static void RemovePinData_Postfix(Minimap.PinData pin)
        {
            if (pin == null) return;
            if (pin.m_type == Minimap.PinType.Ping || pin.m_type == Minimap.PinType.Player)
                return;
            if (!LegacyOutputActive()) return;
            Plugin.Instance.Server.Broadcast(Protocol.PinRemoved(pin));
        }

        // ── Map pings ──────────────────────────────────────────────────────────

        /// <summary>
        /// Intercepts the local player's own pings (middle-click on large map).
        /// Chat.SendPing is called just before the RPC is dispatched, so we capture
        /// the position here and forward it to the Android client with the player name.
        /// </summary>
        [HarmonyPostfix]
        [HarmonyPatch(typeof(Chat), nameof(Chat.SendPing))]
        private static void SendPing_Postfix(Vector3 position)
        {
            if (!LegacyOutputActive()) return;
            string name = Player.m_localPlayer?.GetPlayerName() ?? "";
            Plugin.Instance.Server.Broadcast(Protocol.MapPing(position, name));
        }

        private static bool LegacyOutputActive()
        {
            return !SecondDisplayMover.SecondDisplayLive
                   && Plugin.Instance?.Server != null
                   && Plugin.Instance.Server.HasClients();
        }
    }
}
