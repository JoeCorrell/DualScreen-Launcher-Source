using System;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using HarmonyLib;
using UnityEngine;

namespace ValheimSecondScreen.Network
{
    public static class CommandHandler
    {
        private static readonly FieldInfo _fPins = AccessTools.Field(typeof(Minimap), "m_pins");

        public static void HandleCommand(WebSocketClient sender, string json)
        {
            try
            {
                string type = GetString(json, "type");
                switch (type)
                {
                    case "place_pin": HandlePlacePin(json); break;
                    case "remove_pin": HandleRemovePin(json); break;
                    case "send_ping": HandleSendPing(json); break;
                    case "use_slot": HandleUseSlot(json); break;
                    case "move_item": HandleMoveItem(json); break;
                    case "set_tab": HandleSetTab(json); break;
                    case "zoom_map": HandleZoomMap(json); break;
                    case "pinch_map": HandlePinchMap(json); break;
                    case "pan_map": HandlePanMap(json); break;
                    case "tap": HandleTap(json); break;
                    case "hold": HandleHold(json); break;
                    case "drag_start": HandleDrag(json, true); break;
                    case "drag_end": HandleDrag(json, false); break;
                    case "centre_map": UI.LargeMap.Centre(); break;

                }
            }
            catch (Exception e)
            {
                Plugin.Log.LogWarning($"[SecondScreen] Command error: {e.Message}");
            }
        }

        /// <summary>
        /// Switches the second screen's page from WEMU's normalized touch stream.
        /// </summary>
        /// <remarks>
        /// Android owns the physical panel input in current WEMU builds, so this
        /// socket command is the authoritative path. Unity pointer handlers remain
        /// available as a fallback for hosts that expose display-one touch directly.
        /// </remarks>
        private static void HandleSetTab(string json)
        {
            var mover = Hooks.SecondDisplayMover.Instance;
            if (mover == null) return;
            mover.SetTab((int)GetFloat(json, "tab", 0f));
        }

        /// <summary>
        /// Zooms the second screen map from WEMU's normalized touch stream.
        /// </summary>
        /// <remarks>
        /// Current WEMU sends button and pinch gestures over the local socket.
        /// The map's wheel handler remains a direct-Unity fallback for other hosts.
        /// </remarks>
        private static void HandleZoomMap(string json)
        {
            // A button press is a pinch by a fixed amount, so both routes end
            // up in one place and cannot drift apart.
            int direction = (int)GetFloat(json, "direction", 1f);
            if (direction == 0) return;
            float factor = direction > 0 ? 1.35f : 1f / 1.35f;
            var mover = Hooks.SecondDisplayMover.Instance;
            if (mover == null) return;
            Plugin.Log.LogInfo("[SecondScreen] zoom_map " + direction
                               + " -> " + mover.ScaleMap(factor));
        }

        /// <summary>Pinch, as the ratio between this frame's span and the last.</summary>
        private static void HandlePinchMap(string json)
        {
            var mover = Hooks.SecondDisplayMover.Instance;
            if (mover != null)
                mover.ScaleMap(GetFloat(json, "factor", 1f));
        }

        /// <summary>
        /// A touch on the panel, as a fraction of it.
        /// </summary>
        /// <remarks>
        /// One message for every control, hit-tested by the side that drew them.
        /// The host used to place its own invisible targets over each one by
        /// agreeing constants across two processes, which is how a tap on the
        /// map's zoom opened the skills tab and a tap on a slot did nothing.
        /// </remarks>
        private static void HandleTap(string json)
        {
            var mover = Hooks.SecondDisplayMover.Instance;
            if (mover == null) return;
            mover.Tap(GetFloat(json, "x", -1f), GetFloat(json, "y", -1f));
        }

        /// <summary>Begins or ends a drag on the panel, in fractions of it.</summary>
        private static void HandleDrag(string json, bool begin)
        {
            var mover = Hooks.SecondDisplayMover.Instance;
            if (mover == null) return;
            mover.Drag(GetFloat(json, "x", -1f), GetFloat(json, "y", -1f), begin);
        }

        /// <summary>Drags the map, in fractions of the panel.</summary>
        private static void HandlePanMap(string json)
        {
            var mover = Hooks.SecondDisplayMover.Instance;
            if (mover != null)
                mover.PanMap(GetFloat(json, "dx", 0f), GetFloat(json, "dy", 0f));
        }

        private static void HandleHold(string json)
        {
            var mover = Hooks.SecondDisplayMover.Instance;
            if (mover == null) return;
            mover.Hold(GetFloat(json, "x", -1f), GetFloat(json, "y", -1f),
                       GetFloat(json, "duration", 0f));
        }


        private static void HandlePlacePin(string json)
        {
            if (Minimap.instance == null) return;
            float x = GetFloat(json, "x", 0f);
            float z = GetFloat(json, "z", 0f);
            if (!Finite(x) || !Finite(z)) return;
            string pinType = GetString(json, "pinType") ?? "Icon3";
            string name = GetString(json, "name") ?? "";

            if (!Enum.TryParse<Minimap.PinType>(pinType, out var pt))
                pt = Minimap.PinType.Icon3;
            if (pt != Minimap.PinType.Icon0 && pt != Minimap.PinType.Icon1
                && pt != Minimap.PinType.Icon2 && pt != Minimap.PinType.Icon3
                && pt != Minimap.PinType.Icon4)
                pt = Minimap.PinType.Icon3;

            float halfWorld = Minimap.instance.m_textureSize
                              * Minimap.instance.m_pixelSize * 0.5f;
            x = Mathf.Clamp(x, -halfWorld, halfWorld);
            z = Mathf.Clamp(z, -halfWorld, halfWorld);

            Minimap.instance.AddPin(new Vector3(x, 0f, z), pt, name, true, false, 0L);
        }

        private static void HandleRemovePin(string json)
        {
            if (Minimap.instance == null || _fPins == null) return;
            string pinId = GetString(json, "pinId");
            if (string.IsNullOrEmpty(pinId)) return;

            var pins = (List<Minimap.PinData>)_fPins.GetValue(Minimap.instance);
            if (pins == null) return;

            Minimap.PinData target = null;
            foreach (var p in pins)
                if (Protocol.PinStableId(p) == pinId) { target = p; break; }

            if (target != null)
                Minimap.instance.RemovePin(target);
        }

        private static void HandleSendPing(string json)
        {
            if (Chat.instance == null) return;
            float x = GetFloat(json, "x", 0f);
            float z = GetFloat(json, "z", 0f);
            if (!Finite(x) || !Finite(z)) return;
            Chat.instance.SendPing(new Vector3(x, 0f, z));
        }

        /// <summary>
        /// Equips or consumes whatever is in a hotbar slot, as pressing its key would.
        /// </summary>
        /// <remarks>
        /// Routed through <c>Player.UseItem</c> rather than by setting the equipped
        /// item directly, because that one call is what the game itself uses: it
        /// equips a weapon, eats food, drinks a mead and refuses when the player is
        /// mid-animation or the item is not usable. Reimplementing that decision
        /// tree here would be a second opinion about the player's own rules, and it
        /// would drift on the next patch.
        ///
        /// Hotbar slots are the first row of the inventory, so the slot number maps
        /// to grid position (slot - 1, 0). Bounds are checked because this arrives
        /// off a socket: a slot number from a stale UI must not throw inside the
        /// game's update loop.
        /// </remarks>
        private static void HandleUseSlot(string json)
        {
            var player = Player.m_localPlayer;
            if (player == null) return;
            if (player.IsTeleporting() || player.InCutscene()) return;

            int slot = (int)GetFloat(json, "slot", 0f);
            var inventory = player.GetInventory();
            if (inventory == null) return;
            if (slot < 1 || slot > inventory.GetWidth()) return;

            var item = inventory.GetItemAt(slot - 1, 0);
            if (item == null) return;

            player.UseItem(inventory, item, true);
        }

        /// <summary>
        /// Moves a stack from one inventory square to another.
        /// </summary>
        /// <remarks>
        /// <c>InventoryGrid.DropItem</c> is the same implementation used by the
        /// game's inventory screen. It merges, swaps and refuses invalid moves;
        /// equip state is preserved around any cloned or swapped item objects.
        /// </remarks>
        private static void HandleMoveItem(string json)
        {
            var player = Player.m_localPlayer;
            if (player == null || player.IsTeleporting() || player.InCutscene()) return;

            var inventory = player.GetInventory();
            if (inventory == null) return;

            int fromX = (int)GetFloat(json, "fromX", -1f);
            int fromY = (int)GetFloat(json, "fromY", -1f);
            int toX = (int)GetFloat(json, "toX", -1f);
            int toY = (int)GetFloat(json, "toY", -1f);

            int w = inventory.GetWidth(), h = inventory.GetHeight();
            if (fromX < 0 || fromY < 0 || toX < 0 || toY < 0) return;
            if (fromX >= w || toX >= w || fromY >= h || toY >= h) return;
            if (fromX == toX && fromY == toY) return;

            var item = inventory.GetItemAt(fromX, fromY);
            if (item == null) return;
            var grid = InventoryGui.instance != null ? InventoryGui.instance.m_playerGrid : null;
            if (grid == null) return;
            if (grid.GetInventory() != inventory)
                grid.UpdateInventory(inventory, player, null);

            var destination = inventory.GetItemAt(toX, toY);
            bool sourceEquipped = player.IsItemEquiped(item);
            bool destinationEquipped = destination != null && player.IsItemEquiped(destination);
            var sourcePosition = item.m_gridPos;

            player.RemoveEquipAction(item);
            player.RemoveEquipAction(destination);
            player.UnequipItem(item, false);
            player.UnequipItem(destination, false);

            grid.DropItem(inventory, item, item.m_stack, new Vector2i(toX, toY));

            if (sourceEquipped)
            {
                var movedItem = inventory.GetItemAt(toX, toY);
                if (movedItem != null) player.EquipItem(movedItem, false);
                if (inventory.ContainsItem(item)) player.EquipItem(item, false);
            }
            if (destinationEquipped)
            {
                var swappedItem = inventory.GetItemAt(sourcePosition.x, sourcePosition.y);
                if (swappedItem != null) player.EquipItem(swappedItem, false);
                if (inventory.ContainsItem(destination)) player.EquipItem(destination, false);
            }
        }

        private static string GetString(string json, string key)
        {
            string search = "\"" + key + "\":\"";
            int idx = json.IndexOf(search, StringComparison.Ordinal);
            if (idx < 0) return null;
            idx += search.Length;
            int end = json.IndexOf('"', idx);
            return end < 0 ? null : json.Substring(idx, end - idx);
        }

        private static float GetFloat(string json, string key, float def)
        {
            string search = "\"" + key + "\":";
            int idx = json.IndexOf(search, StringComparison.Ordinal);
            if (idx < 0) return def;
            idx += search.Length;
            int end = idx;
            // Kotlin uses scientific notation for sub-pixel normalised values
            // (for example 8.064516E-4 for one pixel on the Thor). Stopping at
            // E turned that into 8.064516 and made a light map drag jump miles.
            while (end < json.Length && (char.IsDigit(json[end]) || json[end] == '.'
                   || json[end] == '-' || json[end] == '+'
                   || json[end] == 'e' || json[end] == 'E'))
                end++;
            return float.TryParse(json.Substring(idx, end - idx), NumberStyles.Float,
                CultureInfo.InvariantCulture, out float v) ? v : def;
        }

        private static bool Finite(float value) =>
            !float.IsNaN(value) && !float.IsInfinity(value);
    }
}
