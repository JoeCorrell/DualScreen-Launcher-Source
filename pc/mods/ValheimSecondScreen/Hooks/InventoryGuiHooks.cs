using HarmonyLib;
using System.Reflection;
using UnityEngine;

namespace ValheimSecondScreen.Hooks
{
    /// <summary>
    /// Keeps the lower display in step with Valheim's native crafting flow.
    /// The game's InventoryGui still opens normally so station range, recipes,
    /// upgrades, and close handling remain owned by Valheim.
    /// </summary>
    [HarmonyPatch]
    internal static class InventoryGuiHooks
    {
        private static InventoryGui _hiddenGui;
        private static CanvasGroup _topGroup;
        private static CanvasGroup _ownedGroup;
        private static float _savedAlpha;
        private static bool _savedInteractable;
        private static bool _savedBlocksRaycasts;
        private static readonly MethodInfo SetActiveGroup =
            AccessTools.Method(typeof(InventoryGui), "SetActiveGroup",
                new[] { typeof(int), typeof(bool) });

        /// <summary>
        /// A crafting station calls InventoryGui.Show after assigning itself to
        /// the local player. Waiting until the postfix also lets Valheim finish
        /// refreshing its recipe list before our crafting page reads it.
        /// </summary>
        [HarmonyPostfix]
        [HarmonyPatch(typeof(InventoryGui), nameof(InventoryGui.Show),
            new[] { typeof(Container), typeof(int) })]
        private static void Show_Postfix(InventoryGui __instance, Container container)
        {
            if (!SecondDisplayMover.CanRouteUi)
            {
                RestoreTopInventory();
                return;
            }

            HideTopInventory(__instance);
            if (container != null)
            {
                SecondDisplayMover.Instance?.ClosePlayerInventory();
                SecondDisplayMover.Instance?.OpenContainer(container);
                return;
            }

            Player player = Player.m_localPlayer;
            if (player == null) return;

            if (player.GetCurrentCraftingStation() != null)
            {
                SecondDisplayMover.Instance?.ClosePlayerInventory();
                SecondDisplayMover.Instance?.SetTab(3);
            }
            else
            {
                SecondDisplayMover.Instance?.OpenPlayerInventory();
            }
        }

        [HarmonyPostfix]
        [HarmonyPatch(typeof(InventoryGui), nameof(InventoryGui.Hide))]
        private static void Hide_Postfix()
        {
            RestoreTopInventory();
            SecondDisplayMover.Instance?.CloseContainer();
            SecondDisplayMover.Instance?.ClosePlayerInventory();
        }

        // Valheim can close only the container portion (for example when the
        // player walks out of range) while leaving InventoryGui itself open.
        [HarmonyPostfix]
        [HarmonyPatch(typeof(InventoryGui), "CloseContainer")]
        private static void CloseContainer_Postfix()
        {
            SecondDisplayMover.Instance?.CloseContainer();
            Player player = Player.m_localPlayer;
            if (SecondDisplayMover.CanRouteUi && InventoryGui.IsVisible()
                && player != null && player.GetCurrentCraftingStation() == null)
                SecondDisplayMover.Instance?.OpenPlayerInventory();
        }

        // InventoryGui's animator may write its CanvasGroup while the open
        // animation runs. Reapply our visibility after its own Update, leaving
        // every data and range check on the active InventoryGui untouched.
        [HarmonyPostfix]
        [HarmonyPatch(typeof(InventoryGui), "Update")]
        private static void Update_Postfix(InventoryGui __instance)
        {
            // If display one disappears while this menu is open, immediately
            // return the stock UI to display zero. Otherwise the player is left
            // with an active but completely transparent inventory until it is
            // closed and reopened.
            if (!SecondDisplayMover.CanRouteUi)
            {
                if (_hiddenGui == __instance) RestoreTopInventory();
                return;
            }
            if (_hiddenGui != __instance || _topGroup == null) return;
            _topGroup.alpha = 0f;
            _topGroup.interactable = false;
            _topGroup.blocksRaycasts = false;

            // The full lower-screen inventory exposes only the player's 8x4
            // grid. Keep shoulder input from switching the invisible stock GUI
            // into a crafting group that the player cannot see.
            Player player = Player.m_localPlayer;
            if (!__instance.IsContainerOpen() && player != null
                && player.GetCurrentCraftingStation() == null
                && __instance.ActiveGroup != 1 && SetActiveGroup != null)
                SetActiveGroup.Invoke(__instance, new object[] { 1, false });
        }

        private static void HideTopInventory(InventoryGui gui)
        {
            if (gui == null || gui.m_inventoryRoot == null) return;
            if (_hiddenGui == gui && _topGroup != null) return;
            RestoreTopInventory();

            _hiddenGui = gui;
            _topGroup = gui.m_inventoryRoot.GetComponent<CanvasGroup>();
            if (_topGroup == null)
            {
                _topGroup = gui.m_inventoryRoot.gameObject.AddComponent<CanvasGroup>();
                _ownedGroup = _topGroup;
            }

            _savedAlpha = _topGroup.alpha;
            _savedInteractable = _topGroup.interactable;
            _savedBlocksRaycasts = _topGroup.blocksRaycasts;
            _topGroup.alpha = 0f;
            _topGroup.interactable = false;
            _topGroup.blocksRaycasts = false;
        }

        private static void RestoreTopInventory(bool removeOwnedGroup = false)
        {
            if (_topGroup != null)
            {
                _topGroup.alpha = _savedAlpha;
                _topGroup.interactable = _savedInteractable;
                _topGroup.blocksRaycasts = _savedBlocksRaycasts;
            }
            _hiddenGui = null;
            _topGroup = null;

            // Destroy is deferred until the end of the frame. Removing an
            // owned group on every close allowed a same-frame reopen to find
            // and reuse a component that was already scheduled to disappear,
            // exposing the stock top-screen inventory on the next frame. A
            // neutral CanvasGroup has no behaviour, so retain it between opens
            // and remove it only when the plugin itself unloads.
            if (removeOwnedGroup && _ownedGroup != null)
            {
                Object.Destroy(_ownedGroup);
                _ownedGroup = null;
            }
        }

        /// <summary>Restores stock UI state when the plugin is unloaded.</summary>
        internal static void Restore() => RestoreTopInventory(true);
    }
}
