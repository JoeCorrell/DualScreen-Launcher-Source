using UnityEngine;

namespace ValheimSecondScreen.UI
{
    /// <summary>
    /// Compact, consistent item metadata for every lower-screen inventory
    /// surface. Keeping this here prevents the hotbar, inventory and chest
    /// views from quietly disagreeing about what an item badge means.
    /// </summary>
    internal static class ItemPresentation
    {
        public static string QualityStars(ItemDrop.ItemData item)
        {
            return item == null || item.m_shared == null
                ? ""
                : QualityStars(item.m_quality, item.m_shared.m_maxQuality);
        }

        public static string QualityStars(int quality, int maximumQuality)
        {
            if (maximumQuality <= 1) return "";

            quality = Mathf.Max(1, quality);
            // One star plus the level remains readable in the dashboard's
            // compact 4x8 cells. Repeating the glyph up to five times collided
            // with the equipped badge long before the item itself was hard to
            // identify.
            return "\u2605" + quality;
        }

        public static string EquipState(Player player, ItemDrop.ItemData item)
        {
            if (item == null) return "";
            if (player != null && player.IsEquipActionQueued(item)) return "...";
            return item.m_equipped ? "E" : "";
        }

        public static bool HasDurability(ItemDrop.ItemData item)
        {
            return item != null && item.m_shared != null
                   && item.m_shared.m_useDurability;
        }

        public static float Durability(ItemDrop.ItemData item)
        {
            if (!HasDurability(item)) return 1f;
            return Mathf.Clamp01(item.m_durability
                                 / Mathf.Max(1f, item.GetMaxDurability()));
        }

        public static string ConditionBadge(ItemDrop.ItemData item)
        {
            if (!HasDurability(item)) return "";
            float fraction = Durability(item);
            if (item.m_durability <= 0f) return "BROKEN";
            return fraction <= 0.25f
                ? Mathf.CeilToInt(fraction * 100f) + "%"
                : "";
        }

        public static string ConditionDetail(ItemDrop.ItemData item)
        {
            return HasDurability(item)
                ? Mathf.CeilToInt(Durability(item) * 100f) + "%"
                : "";
        }

        public static bool IsFullStack(ItemDrop.ItemData item)
        {
            return item != null && item.m_shared != null
                   && item.m_shared.m_maxStackSize > 1
                   && item.m_stack >= item.m_shared.m_maxStackSize;
        }

        public static bool IsPortalBlocked(ItemDrop.ItemData item)
        {
            if (item == null || item.m_shared == null || item.m_shared.m_teleportable)
                return false;
            return ZoneSystem.instance == null
                   || !ZoneSystem.instance.GetGlobalKey(GlobalKeys.TeleportAll);
        }

        public static Color DurabilityColour(ItemDrop.ItemData item, Color normal)
        {
            if (item != null && item.m_durability <= 0f)
                return new Color(0.95f, 0.15f, 0.10f, 1f);

            return Durability(item) <= 0.30f
                ? new Color(0.85f, 0.35f, 0.28f, 1f)
                : normal;
        }
    }
}
