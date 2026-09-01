using UnityEngine;

namespace ValheimSecondScreen.UI
{
    /// <summary>
    /// Cheap fingerprints for UI models that are polled from Valheim.
    /// </summary>
    /// <remarks>
    /// Valheim exposes inventory change callbacks, but durability, equipment
    /// state and modded item fields can change without going through every one
    /// of them. A small hash keeps polling robust while making an unchanged
    /// poll free of Unity UI writes, which is the expensive part: those writes
    /// dirty and rebuild the complete second-display canvas.
    /// </remarks>
    internal static class UiStateFingerprint
    {
        public static int Inventory(Inventory inventory)
        {
            if (inventory == null) return 0;

            unchecked
            {
                int hash = 17;
                int width = Mathf.Max(1, inventory.GetWidth());
                int height = Mathf.Max(1, inventory.GetHeight());
                hash = Add(hash, width);
                hash = Add(hash, height);
                hash = Add(hash, inventory.GetTotalWeight().GetHashCode());

                for (int row = 0; row < height; row++)
                    for (int column = 0; column < width; column++)
                        hash = Add(hash, Item(inventory.GetItemAt(column, row)));
                return hash;
            }
        }

        public static int Item(ItemDrop.ItemData item)
        {
            if (item == null) return 0;

            unchecked
            {
                int hash = item.GetHashCode();
                hash = Add(hash, item.m_stack);
                hash = Add(hash, item.m_quality);
                hash = Add(hash, item.m_variant);
                hash = Add(hash, item.m_equipped ? 1 : 0);
                hash = Add(hash, item.m_durability.GetHashCode());
                hash = Add(hash, item.m_gridPos.x);
                hash = Add(hash, item.m_gridPos.y);
                hash = Add(hash, item.m_shared != null
                    ? item.m_shared.GetHashCode() : 0);
                return hash;
            }
        }

        public static int Add(int hash, int value)
        {
            unchecked { return hash * 31 + value; }
        }
    }
}
