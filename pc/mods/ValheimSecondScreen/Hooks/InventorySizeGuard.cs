using System.Collections.Generic;
using System.Reflection;
using HarmonyLib;
using UnityEngine;

namespace ValheimSecondScreen.Hooks
{
    /// <summary>
    /// Keeps the local player's model at Valheim's standard 8x4 capacity even
    /// when another plugin tries to expand the private Inventory dimensions.
    /// </summary>
    public sealed class InventorySizeGuard : MonoBehaviour
    {
        private const int Width = 8;
        private const int Height = 4;
        private const int Capacity = Width * Height;

        private static readonly FieldInfo WidthField =
            AccessTools.Field(typeof(Inventory), "m_width");
        private static readonly FieldInfo HeightField =
            AccessTools.Field(typeof(Inventory), "m_height");
        private static readonly MethodInfo ChangedMethod =
            AccessTools.Method(typeof(Inventory), "Changed");

        private float _nextCheck;
        private float _nextLog;
        private float _nextBlockedLog;

        private void Update()
        {
            if (Time.unscaledTime < _nextCheck) return;
            _nextCheck = Time.unscaledTime + 0.5f;
            Enforce();
        }

        private void Enforce()
        {
            Player player = Player.m_localPlayer;
            if (player == null || player.IsTeleporting() || player.InCutscene()) return;
            Inventory inventory = player.GetInventory();
            if (inventory == null || WidthField == null || HeightField == null) return;

            int rawWidth = (int)WidthField.GetValue(inventory);
            int rawHeight = (int)HeightField.GetValue(inventory);
            if (rawWidth == Width && rawHeight == Height) return;

            var items = new List<ItemDrop.ItemData>(inventory.GetAllItems());
            if (!MakeRoom(player, inventory, items)) return;

            items = new List<ItemDrop.ItemData>(inventory.GetAllItems());
            PlaceInsideStandardGrid(player, items);
            WidthField.SetValue(inventory, Width);
            HeightField.SetValue(inventory, Height);
            ChangedMethod?.Invoke(inventory, null);

            if (Time.unscaledTime >= _nextLog)
            {
                _nextLog = Time.unscaledTime + 30f;
                Plugin.Log.LogInfo("[SecondScreen] Restored player inventory from "
                    + rawWidth + "x" + rawHeight + " to the standard 8x4 grid.");
            }
        }

        /// <summary>
        /// If an expanded inventory is actually full, preserve quest/equipped
        /// items first and drop only ordinary overflow beside the player.
        /// </summary>
        private bool MakeRoom(Player player, Inventory inventory,
                              List<ItemDrop.ItemData> items)
        {
            if (items.Count <= Capacity) return true;

            items.Sort((left, right) => ItemPriority(player, left)
                .CompareTo(ItemPriority(player, right)));
            for (int i = Capacity; i < items.Count; i++)
            {
                if (items[i].m_shared.m_questItem || player.IsItemEquiped(items[i]))
                {
                    WarnBlocked("[SecondScreen] Cannot shrink inventory safely: "
                        + "more than 32 slots are occupied and protected items would overflow.");
                    return false;
                }
            }

            for (int i = items.Count - 1; i >= Capacity; i--)
            {
                ItemDrop.ItemData item = items[i];
                if (!player.DropItem(inventory, item, item.m_stack))
                {
                    WarnBlocked("[SecondScreen] Could not move overflow item "
                        + item.m_shared.m_name + "; inventory resize was postponed.");
                    return false;
                }
            }
            return true;
        }

        private void WarnBlocked(string message)
        {
            if (Time.unscaledTime < _nextBlockedLog) return;
            _nextBlockedLog = Time.unscaledTime + 30f;
            Plugin.Log.LogWarning(message);
        }

        private static int ItemPriority(Player player, ItemDrop.ItemData item)
        {
            if (item.m_shared.m_questItem) return 0;
            if (player.IsItemEquiped(item)) return 1;
            return InBounds(item.m_gridPos) ? 2 : 3;
        }

        private static void PlaceInsideStandardGrid(Player player,
                                                    List<ItemDrop.ItemData> items)
        {
            var occupied = new bool[Capacity];
            var needsPosition = new List<ItemDrop.ItemData>();

            foreach (ItemDrop.ItemData item in items)
            {
                Vector2i position = item.m_gridPos;
                int index = position.y * Width + position.x;
                if (InBounds(position) && !occupied[index])
                    occupied[index] = true;
                else
                    needsPosition.Add(item);
            }

            foreach (ItemDrop.ItemData item in needsPosition)
            {
                int slot = FirstFree(occupied, player.IsItemEquiped(item));
                if (slot < 0) break;
                item.m_gridPos = new Vector2i(slot % Width, slot / Width);
                occupied[slot] = true;
            }
        }

        private static int FirstFree(bool[] occupied, bool preferHotbar)
        {
            int start = preferHotbar ? 0 : Width;
            for (int i = start; i < occupied.Length; i++)
                if (!occupied[i]) return i;
            for (int i = 0; i < start; i++)
                if (!occupied[i]) return i;
            return -1;
        }

        private static bool InBounds(Vector2i position) =>
            position.x >= 0 && position.x < Width
            && position.y >= 0 && position.y < Height;

        [HarmonyPatch(typeof(Inventory), nameof(Inventory.GetWidth))]
        private static class WidthPatch
        {
            [HarmonyPriority(HarmonyLib.Priority.Last)]
            private static void Postfix(Inventory __instance, ref int __result)
            {
                ForceWidth(__instance, ref __result);
            }

            // Finalizers run after postfixes. Inventory expanders normally use
            // a postfix of their own, so repeating the result here makes 8x4
            // survive even when that other postfix was registered later.
            [HarmonyFinalizer]
            [HarmonyPriority(HarmonyLib.Priority.Last)]
            private static void Finalizer(Inventory __instance, ref int __result) =>
                ForceWidth(__instance, ref __result);
        }

        [HarmonyPatch(typeof(Inventory), nameof(Inventory.GetHeight))]
        private static class HeightPatch
        {
            [HarmonyPriority(HarmonyLib.Priority.Last)]
            private static void Postfix(Inventory __instance, ref int __result)
            {
                ForceHeight(__instance, ref __result);
            }

            [HarmonyFinalizer]
            [HarmonyPriority(HarmonyLib.Priority.Last)]
            private static void Finalizer(Inventory __instance, ref int __result) =>
                ForceHeight(__instance, ref __result);
        }

        private static void ForceWidth(Inventory inventory, ref int result)
        {
            Player player = Player.m_localPlayer;
            if (player != null && object.ReferenceEquals(inventory, player.GetInventory()))
                result = Width;
        }

        private static void ForceHeight(Inventory inventory, ref int result)
        {
            Player player = Player.m_localPlayer;
            if (player != null && object.ReferenceEquals(inventory, player.GetInventory()))
                result = Height;
        }
    }
}
