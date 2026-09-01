using System;
using System.Collections.Generic;
using System.Reflection;
using HarmonyLib;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace ValheimSecondScreen.UI
{
    /// <summary>
    /// The player's carried inventory, as a grid of the game's own item icons.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Read from <c>Player.m_localPlayer.GetInventory()</c> and drawn with each
    /// item's own <c>m_shared.m_icons</c> sprite, which is the same asset the
    /// game's inventory screen uses — so a texture pack or a modded item shows
    /// up here with no work.
    /// </para>
    /// <para>
    /// Built rather than moved, and this is the one place in the plugin that is.
    /// Valheim's inventory grid is a single object owned by
    /// <c>InventoryGui</c>, which positions it, populates it and switches it off
    /// when the inventory closes. Reparenting it would mean fighting that every
    /// frame and taking the real inventory screen away from the player to do it.
    /// Slots are cheap; the inventory is not.
    /// </para>
    /// <para>
    /// The slot frame is the trader panel's category art rather than something
    /// drawn here, so an empty slot and a full one are the same shape as the
    /// game's.
    /// </para>
    /// </remarks>
    internal sealed class InventoryPage
    {
        private const int InventoryColumns = 8;
        private const int CarriedInventoryRows = 4;
        private const int DisplayColumns = 4;
        private const float SlotGap = 5f;
        private const float GridSideInset = 22f;
        // The inventory page is the complete standard inventory, including the
        // eight hotbar positions. The quickbar strip is a larger convenience
        // view of row zero, not a reason to remove those slots from inventory.
        private const int FirstCarriedRow = 0;
        private const float GridTopInset = 66f;
        private const float GridBottomInset = 132f;

        /// <summary>
        /// Slot size, worked out from the page rather than fixed.
        /// </summary>
        /// <remarks>
        /// A fixed size left a small grid adrift in a page drawn at the size the
        /// grid should have been. Limited by the shorter of the two directions
        /// so the slots stay square and the whole inventory fits without
        /// scrolling — four rows is what a player with a full set of bags has.
        /// </remarks>
        private float _slotSize;

        private static readonly Color SlotInk = UiTheme.Slot;
        private static readonly Color EquippedInk = UiTheme.SlotEquipped;
        private static readonly Color GoldText = UiTheme.Gold;
        private static readonly Color Weak = UiTheme.Muted;
        private static readonly Color DragInk = new Color(0.72f, 0.38f, 0.065f, 0.82f);
        private static readonly Color HoldInk = new Color(0.72f, 0.58f, 0.18f, 0.88f);
        private static readonly Color ControllerInk = new Color(0.86f, 0.57f, 0.10f, 0.94f);

        private readonly RectTransform _page;
        private readonly TMP_FontAsset _font;
        private Vector2 _pageSize;
        private readonly List<Slot> _slots = new List<Slot>();
        private static readonly MethodInfo InventoryChanged =
            AccessTools.Method(typeof(Inventory), "Changed");
        private static readonly FieldInfo GridSelection =
            AccessTools.Field(typeof(InventoryGrid), "m_selected");

        private RectTransform _grid;
        private TMP_Text _weightText;
        private GameObject _weightMeterRoot;
        private RectTransform _weightFill;
        private Image _weightFillImage;
        private TMP_Text _readout;
        private RectTransform _sortCard;
        private RectTransform _discardCard;

        /// <summary>The grid's size, for the hit test.</summary>
        private int _width = InventoryColumns;
        private int _displayColumns = DisplayColumns;
        private int _displaySlotCount;
        private int _lastFingerprint;
        private bool _hasPainted;
        private bool _expanded;

        private sealed class Slot
        {
            public GameObject Root;
            public Image Frame;
            public Image Icon;
            public TMP_Text Amount;
            public TMP_Text Quality;
            public TMP_Text Equipped;
            public TMP_Text Condition;
            public Image Durability;
            public InventorySlotInput Input;
        }

        public InventoryPage(RectTransform page, TMP_FontAsset font, Vector2 pageSize)
        {
            _page = page;
            _font = font;
            _pageSize = pageSize;

            int displayRows = Mathf.CeilToInt(
                InventoryColumns * CarriedInventoryRows / (float)DisplayColumns);
            float byWidth = (pageSize.x - GridSideInset * 2f
                             - (DisplayColumns - 1) * SlotGap)
                            / DisplayColumns;
            float byHeight = (pageSize.y - GridTopInset - GridBottomInset
                              - (displayRows - 1) * SlotGap)
                             / displayRows;
            _slotSize = Mathf.Max(1f, Mathf.Min(byWidth, byHeight));

            Build();
        }

        /// <summary>
        /// Switches between the dashboard's 4x8 presentation and the
        /// controller inventory's full-width native 8x4 presentation.
        /// </summary>
        public void SetExpanded(bool expanded, Vector2 pageSize)
        {
            _expanded = expanded;
            _pageSize = pageSize;
            LayoutActions();
            _hasPainted = false;
            UpdateGridMetrics(_displaySlotCount > 0
                ? _displaySlotCount : InventoryColumns * CarriedInventoryRows);
            Refresh(true);
        }

        private void Build()
        {
            var title = SecondScreenUI.MakeText(_page, "Title", "INVENTORY", 31f, GoldText,
                                                 TextAlignmentOptions.Center);
            if (_font != null) title.font = _font;
            var titleRect = (RectTransform)title.transform;
            titleRect.anchorMin = new Vector2(0f, 1f);
            titleRect.anchorMax = new Vector2(1f, 1f);
            titleRect.pivot = new Vector2(0.5f, 1f);
            titleRect.sizeDelta = new Vector2(0f, 56f);
            titleRect.anchoredPosition = Vector2.zero;
            title.fontStyle = FontStyles.Bold;
            UiSkin.PageHeading(_page, title, 70f);

            float actionWidth = Mathf.Max(220f, _pageSize.x - 32f);
            const float actionGap = 8f;
            float actionButtonWidth = (actionWidth - actionGap) * 0.5f;
            float sortWidth = actionButtonWidth;
            float discardWidth = actionButtonWidth;
            float discardX = 16f + sortWidth + actionGap;

            _readout = SecondScreenUI.MakeText(_page, "Readout", "", 22f, Weak,
                                               TextAlignmentOptions.Center);
            if (_font != null) _readout.font = _font;
            var readoutRect = (RectTransform)_readout.transform;
            readoutRect.anchorMin = new Vector2(0f, 1f);
            readoutRect.anchorMax = new Vector2(1f, 1f);
            readoutRect.pivot = new Vector2(0.5f, 1f);
            readoutRect.sizeDelta = new Vector2(-16f, 21f);
            readoutRect.anchoredPosition = new Vector2(0f, -42f);
            _readout.fontSize = 13f;
            _readout.enableAutoSizing = true;
            _readout.fontSizeMin = 10f;
            _readout.fontSizeMax = 13f;
            _readout.enableWordWrapping = false;

            _sortCard = ActionCard("SORT", 16f, sortWidth, SortInventory);
            _discardCard = ActionCard("DISCARD", discardX, discardWidth, DiscardSelected);
            BuildWeightMeter();
            LayoutActions();

            var gridGO = new GameObject("Grid", typeof(RectTransform));
            _grid = (RectTransform)gridGO.transform;
            _grid.SetParent(_page, false);
            _grid.anchorMin = new Vector2(0.5f, 1f);
            _grid.anchorMax = new Vector2(0.5f, 1f);
            _grid.pivot = new Vector2(0.5f, 1f);
            _grid.anchoredPosition = new Vector2(0f, -GridTopInset);
            _grid.sizeDelta = new Vector2(
                DisplayColumns * _slotSize + (DisplayColumns - 1) * SlotGap, 0f);
        }

        /// <summary>
        /// Redraws the grid from the live inventory.
        /// </summary>
        /// <remarks>
        /// Slots are grown to fit and then reused: the inventory's size changes
        /// when a bag is equipped, and destroying and rebuilding the grid on
        /// every refresh would allocate a few hundred objects a second for a
        /// panel that mostly does not change.
        /// </remarks>
        public void Refresh(bool force = false)
        {
            var player = Player.m_localPlayer;
            var inventory = player != null ? player.GetInventory() : null;
            if (inventory == null)
            {
                _hasPainted = false;
                foreach (var slot in _slots) slot.Root.SetActive(false);
                if (_weightText != null) _weightText.text = "";
                if (_weightFill != null)
                    _weightFill.anchorMax = new Vector2(0f, 1f);
                return;
            }

            int fingerprint = UiStateFingerprint.Inventory(inventory);
            fingerprint = UiStateFingerprint.Add(
                fingerprint, player.GetMaxCarryWeight().GetHashCode());
            fingerprint = UiStateFingerprint.Add(
                fingerprint, SecondScreenPreferences.ShowItemBadges ? 1 : 0);
            fingerprint = UiStateFingerprint.Add(
                fingerprint, SecondScreenPreferences.ShowCarryMeter ? 1 : 0);
            fingerprint = UiStateFingerprint.Add(
                fingerprint, SecondScreenPreferences.ShowInteractionHints ? 1 : 0);
            fingerprint = UiStateFingerprint.Add(fingerprint, IsDiscardArmed() ? 1 : 0);
            fingerprint = UiStateFingerprint.Add(fingerprint, _dragColumn);
            fingerprint = UiStateFingerprint.Add(fingerprint, _dragRow);
            int controllerColumn = -1;
            int controllerRow = -1;
            if (TryControllerSelection(out var selected))
            {
                controllerColumn = selected.x;
                controllerRow = selected.y;
                fingerprint = UiStateFingerprint.Add(fingerprint, controllerColumn);
                fingerprint = UiStateFingerprint.Add(fingerprint, controllerRow);
            }
            if (!force && _hasPainted && fingerprint == _lastFingerprint) return;
            _lastFingerprint = fingerprint;
            _hasPainted = true;

            int width = Mathf.Max(1, inventory.GetWidth());
            int height = Mathf.Max(1, inventory.GetHeight());
            _width = width;
            // Show the complete 8x4 model. It is rearranged visually as four
            // columns by eight rows so all 32 real positions fit this panel.
            int carriedRows = Mathf.Max(0, height - FirstCarriedRow);
            int wanted = width * carriedRows;
            UpdateGridMetrics(wanted);
            while (_slots.Count < wanted) _slots.Add(CreateSlot());

            for (int i = 0; i < _slots.Count; i++)
            {
                var slot = _slots[i];
                if (i >= wanted)
                {
                    slot.Root.SetActive(false);
                    continue;
                }

                int column = i % width;
                int row = FirstCarriedRow + i / width;
                int displayColumn = i % _displayColumns;
                int displayRow = i / _displayColumns;
                slot.Root.SetActive(true);
                slot.Input.Bind(this, column, row);
                ((RectTransform)slot.Root.transform).anchoredPosition = new Vector2(
                    displayColumn * (_slotSize + SlotGap),
                    -displayRow * (_slotSize + SlotGap));

                var item = inventory.GetItemAt(column, row);
                Paint(slot, item);
            }

            if (controllerColumn >= 0)
                HighlightController(controllerColumn, controllerRow);
            if (_dragColumn >= 0) Highlight(_dragColumn, _dragRow, true);
            if (_holdProgress >= 0f)
                ApplyHoldProgress(_holdColumn, _holdRow, _holdProgress);

            RefreshReadout(player);

            _weightText.text = Mathf.CeilToInt(inventory.GetTotalWeight())
                + " / " + Mathf.CeilToInt(player.GetMaxCarryWeight());
            _weightText.color = inventory.GetTotalWeight() > player.GetMaxCarryWeight()
                ? new Color(0.85f, 0.35f, 0.28f, 1f)
                : Weak;
            float carryFraction = player.GetMaxCarryWeight() > 0f
                ? Mathf.Clamp01(inventory.GetTotalWeight() / player.GetMaxCarryWeight())
                : 0f;
            if (_weightFill != null)
                _weightFill.anchorMax = new Vector2(carryFraction, 1f);
            if (_weightFillImage != null)
                _weightFillImage.color = inventory.GetTotalWeight() > player.GetMaxCarryWeight()
                    ? new Color(0.82f, 0.18f, 0.12f, 0.92f)
                    : new Color(0.82f, 0.46f, 0.08f, 0.92f);
            if (_weightMeterRoot != null)
                _weightMeterRoot.SetActive(SecondScreenPreferences.ShowCarryMeter);
        }

        private void BuildWeightMeter()
        {
            var trackObject = new GameObject("CapacityTrack",
                typeof(RectTransform), typeof(Image));
            _weightMeterRoot = trackObject;
            var track = (RectTransform)trackObject.transform;
            track.SetParent(_page, false);
            track.anchorMin = new Vector2(0f, 0f);
            track.anchorMax = new Vector2(1f, 0f);
            track.pivot = new Vector2(0.5f, 0f);
            track.offsetMin = new Vector2(16f, 88f);
            track.offsetMax = new Vector2(-16f, 116f);
            UiSkin.Plain(trackObject.GetComponent<Image>(),
                new Color(0f, 0f, 0f, 0.58f));

            var fillObject = new GameObject("Fill",
                typeof(RectTransform), typeof(Image));
            _weightFill = (RectTransform)fillObject.transform;
            _weightFill.SetParent(track, false);
            _weightFill.anchorMin = Vector2.zero;
            _weightFill.anchorMax = new Vector2(0f, 1f);
            _weightFill.pivot = new Vector2(0f, 0.5f);
            _weightFill.offsetMin = Vector2.zero;
            _weightFill.offsetMax = Vector2.zero;
            _weightFillImage = fillObject.GetComponent<Image>();
            _weightFillImage.color = new Color(0.82f, 0.46f, 0.08f, 0.92f);
            _weightFillImage.raycastTarget = false;

            _weightText = SecondScreenUI.MakeText(track, "Weight", "", 18f, Weak,
                                                  TextAlignmentOptions.Center);
            if (_font != null) _weightText.font = _font;
            _weightText.fontStyle = FontStyles.Bold;
            _weightText.rectTransform.anchorMin = Vector2.zero;
            _weightText.rectTransform.anchorMax = Vector2.one;
            _weightText.rectTransform.offsetMin = new Vector2(8f, 1f);
            _weightText.rectTransform.offsetMax = new Vector2(-8f, -1f);
            _weightText.raycastTarget = false;
            SecondScreenUI.FitText(_weightText, 11f, 18f);
        }

        /// <summary>
        /// Uses whatever is at a point on this page.
        /// </summary>
        /// <remarks>
        /// <para>
        /// The point arrives in the page's own space, and the hit test is done
        /// here rather than on the host. The host cannot know where the slots
        /// are — the grid is sized from the page at build time and its columns
        /// change when a bag is equipped — and every attempt to agree on that by
        /// copying constants into both processes has produced a control that
        /// silently misses. Only the side that drew it can say what was hit.
        /// </para>
        /// <para>
        /// <c>UseItem</c> with the inventory-screen flag, which is what makes a
        /// tap do the thing the item is for: armour and weapons equip, food is
        /// eaten, mead is drunk, and a stack of wood does nothing because
        /// nothing is what using wood means.
        /// </para>
        /// </remarks>
        public bool Tap(Vector2 pagePoint)
        {
            var player = Player.m_localPlayer;
            var inventory = player != null ? player.GetInventory() : null;
            if (inventory == null) return false;
            if (Contains(_sortCard, pagePoint))
            {
                SortInventory();
                return true;
            }
            if (Contains(_discardCard, pagePoint))
            {
                DiscardSelected();
                return true;
            }
            if (!Locate(pagePoint, out int column, out int row)) return false;

            return TapAt(column, row);
        }

        public bool Use(Vector2 pagePoint)
        {
            if (!Locate(pagePoint, out int column, out int row)) return false;
            return UseAt(column, row);
        }

        /// <summary>
        /// Selects a source on the first tap and moves it on the second. Tapping
        /// the selected source again is an explicit cancel, which makes the mode
        /// safe on an inventory full of small targets.
        /// </summary>
        internal bool TapAt(int column, int row)
        {
            var player = Player.m_localPlayer;
            var inventory = player != null ? player.GetInventory() : null;
            if (inventory == null || player.IsTeleporting() || player.InCutscene())
                return false;
            if (column < 0 || row < 0 || column >= inventory.GetWidth()
                || row >= inventory.GetHeight()) return false;

            if (_dragColumn >= 0)
            {
                if (column == _dragColumn && row == _dragRow)
                {
                    CancelDrag();
                    return true;
                }
                return EndDragAt(column, row);
            }

            if (inventory.GetItemAt(column, row) == null) return false;
            return BeginDragAt(column, row);
        }

        internal bool UseAt(int column, int row)
        {
            var player = Player.m_localPlayer;
            var inventory = player != null ? player.GetInventory() : null;
            if (inventory == null || player.IsTeleporting() || player.InCutscene()) return false;
            if (column < 0 || row < 0 || column >= inventory.GetWidth()
                || row >= inventory.GetHeight()) return false;

            var item = inventory.GetItemAt(column, row);
            if (item == null) return false;

            // A hold is a use command, never a move command. Clear a previously
            // picked source first so the next tap cannot unexpectedly move it.
            if (_dragColumn >= 0)
            {
                ClearDiscardConfirmation();
                _dragColumn = -1;
                _dragRow = -1;
            }
            _holdColumn = -1;
            _holdRow = -1;
            _holdProgress = -1f;

            // This is the same action InventoryGui uses for a right-click. It
            // consumes food and mead, or queues the correct equip/unequip path
            // (including items with a non-zero equip duration).
            player.UseItem(inventory, item, true);
            Refresh();
            return true;
        }

        /// <summary>The slot a drag started on, or -1 between drags.</summary>
        private int _dragColumn = -1;
        private int _dragRow = -1;
        private int _holdColumn = -1;
        private int _holdRow = -1;
        private float _holdProgress = -1f;
        private int _discardConfirmColumn = -1;
        private int _discardConfirmRow = -1;
        private float _discardConfirmUntil;

        /// <summary>
        /// Remembers where a drag began, so its end can be a move.
        /// </summary>
        /// <remarks>
        /// Held as a grid position rather than an item. An item reference would
        /// go stale the moment something else moved it -- a container closing,
        /// another player taking it in multiplayer -- and dropping a stale
        /// reference onto a slot is how an item ends up duplicated or gone.
        /// </remarks>
        public bool BeginDrag(Vector2 pagePoint)
        {
            if (!Locate(pagePoint, out int column, out int row)) return false;
            return BeginDragAt(column, row);
        }

        internal bool BeginDragAt(int column, int row)
        {
            var player = Player.m_localPlayer;
            var inventory = player != null ? player.GetInventory() : null;
            if (inventory == null || player.IsTeleporting() || player.InCutscene()
                || column < 0 || row < 0
                || column >= inventory.GetWidth() || row >= inventory.GetHeight()
                || inventory.GetItemAt(column, row) == null) return false;

            // A tap-selected source stays authoritative until it is dropped or
            // cancelled. A second finger beginning a drag on another slot used
            // to silently replace that source, so releasing the first finger
            // could move an entirely different item.
            if (_dragColumn >= 0)
                return _dragColumn == column && _dragRow == row;

            ClearDiscardConfirmation();
            _dragColumn = column;
            _dragRow = row;
            Highlight(column, row, true);
            RefreshReadout(player);
            return true;
        }

        /// <summary>
        /// Finishes a drag by moving the held item to the slot under the finger.
        /// </summary>
        /// <remarks>
        /// The move itself is <c>InventoryGrid.DropItem</c>, which is the game's
        /// own same-inventory implementation: it stacks matching items, swaps
        /// unlike items, and refuses invalid moves. Deciding any of that here
        /// would be a second opinion about the player's own rules.
        /// </remarks>
        public bool EndDrag(Vector2 pagePoint)
        {
            int fromColumn = _dragColumn, fromRow = _dragRow;
            if (fromColumn < 0) return false;
            ClearDiscardConfirmation();
            Highlight(fromColumn, fromRow, false);
            _dragColumn = -1;
            _dragRow = -1;

            if (!Locate(pagePoint, out int column, out int row)) return false;
            return EndDragAt(column, row, fromColumn, fromRow);
        }

        internal bool EndDragAt(int column, int row)
        {
            int fromColumn = _dragColumn, fromRow = _dragRow;
            if (fromColumn < 0) return false;
            ClearDiscardConfirmation();
            Highlight(fromColumn, fromRow, false);
            _dragColumn = -1;
            _dragRow = -1;
            bool moved = EndDragAt(column, row, fromColumn, fromRow);
            if (!moved && SourceStillExists(fromColumn, fromRow))
            {
                _dragColumn = fromColumn;
                _dragRow = fromRow;
                Refresh(true);
            }
            return moved;
        }

        private static bool SourceStillExists(int column, int row)
        {
            Player player = Player.m_localPlayer;
            Inventory inventory = player != null ? player.GetInventory() : null;
            return inventory != null && column >= 0 && row >= 0
                   && column < inventory.GetWidth() && row < inventory.GetHeight()
                   && inventory.GetItemAt(column, row) != null;
        }

        private bool EndDragAt(int column, int row, int fromColumn, int fromRow)
        {
            if (column == fromColumn && row == fromRow) return true;

            var player = Player.m_localPlayer;
            var inventory = player != null ? player.GetInventory() : null;
            var grid = InventoryGui.instance != null ? InventoryGui.instance.m_playerGrid : null;
            if (inventory == null || grid == null || player.IsTeleporting()
                || player.InCutscene()) return false;
            if (column < 0 || row < 0 || column >= inventory.GetWidth()
                || row >= inventory.GetHeight()) return false;

            var item = inventory.GetItemAt(fromColumn, fromRow);
            if (item == null) return false;

            // InventoryGui normally performs this binding only while its own
            // screen is visible. The bottom panel must work before that screen
            // has ever been opened, so initialise the stock grid on demand.
            if (grid.GetInventory() != inventory)
                grid.UpdateInventory(inventory, player, null);

            /*
             * Inventory.MoveItemToThis alone cannot swap two unlike items in
             * the same inventory. InventoryGrid.DropItem is Valheim's own swap
             * implementation. Mirror InventoryGui's equip preservation around
             * it because the move clones stacks and the equipped reference may
             * otherwise point at the item that was removed.
             */
            var destination = inventory.GetItemAt(column, row);
            bool sourceEquipped = player.IsItemEquiped(item);
            bool destinationEquipped = destination != null && player.IsItemEquiped(destination);
            var sourcePosition = item.m_gridPos;

            player.RemoveEquipAction(item);
            player.RemoveEquipAction(destination);
            player.UnequipItem(item, false);
            player.UnequipItem(destination, false);

            bool moved = grid.DropItem(
                inventory, item, item.m_stack, new Vector2i(column, row));

            // Mirror InventoryGui.OnSelectedItem exactly. DropItem may merge a
            // partial stack and leave the original object in the inventory, so
            // preserving only the object now at the destination is not enough.
            if (sourceEquipped)
            {
                var movedItem = inventory.GetItemAt(column, row);
                if (movedItem != null) player.EquipItem(movedItem, false);
                if (inventory.ContainsItem(item)) player.EquipItem(item, false);
            }
            if (destinationEquipped)
            {
                var swappedItem = inventory.GetItemAt(sourcePosition.x, sourcePosition.y);
                if (swappedItem != null) player.EquipItem(swappedItem, false);
                if (inventory.ContainsItem(destination)) player.EquipItem(destination, false);
            }

            Refresh();
            return moved;
        }

        /// <summary>Cancels a drag that ended outside the grid.</summary>
        public void CancelDrag()
        {
            bool changed = _dragColumn >= 0 || _holdProgress >= 0f
                           || _discardConfirmColumn >= 0;
            ClearDiscardConfirmation();
            if (_dragColumn >= 0) Highlight(_dragColumn, _dragRow, false);
            _dragColumn = -1;
            _dragRow = -1;
            _holdColumn = -1;
            _holdRow = -1;
            _holdProgress = -1f;
            if (changed) Refresh();
        }

        private void Highlight(int column, int row, bool on)
        {
            int index = (row - FirstCarriedRow) * _width + column;
            if (index < 0 || index >= _slots.Count) return;
            if (on)
            {
                _slots[index].Frame.color = DragInk;
                return;
            }

            var player = Player.m_localPlayer;
            var inventory = player != null ? player.GetInventory() : null;
            Paint(_slots[index], inventory != null
                ? inventory.GetItemAt(column, row) : null);
        }

        private void HighlightController(int column, int row)
        {
            int index = (row - FirstCarriedRow) * _width + column;
            if (index >= 0 && index < _slots.Count)
                _slots[index].Frame.color = ControllerInk;
        }

        private bool TryControllerSelection(out Vector2i selection)
        {
            selection = new Vector2i(-1, -1);
            var gui = InventoryGui.instance;
            if (!_expanded || gui == null || !InventoryGui.IsVisible()
                || gui.ActiveGroup != 1 || gui.m_playerGrid == null
                || GridSelection == null)
                return false;

            object value = GridSelection.GetValue(gui.m_playerGrid);
            if (!(value is Vector2i current)) return false;
            selection = current;
            return current.x >= 0 && current.y >= FirstCarriedRow;
        }

        /// <summary>Shows the native long-press threshold directly on the slot.</summary>
        internal void ShowHoldProgress(int column, int row, float progress)
        {
            if (!SecondScreenPreferences.ShowHoldProgress) progress = -1f;
            if (progress < 0f)
            {
                _holdColumn = -1;
                _holdRow = -1;
                _holdProgress = -1f;
            }
            else
            {
                _holdColumn = column;
                _holdRow = row;
                _holdProgress = Mathf.Clamp01(progress);
            }
            ApplyHoldProgress(column, row, progress);
        }

        private void ApplyHoldProgress(int column, int row, float progress)
        {
            int index = (row - FirstCarriedRow) * _width + column;
            if (index < 0 || index >= _slots.Count) return;
            var player = Player.m_localPlayer;
            var inventory = player != null ? player.GetInventory() : null;
            var item = inventory != null ? inventory.GetItemAt(column, row) : null;
            if (item == null) return;
            if (progress < 0f)
            {
                _slots[index].Frame.color = column == _dragColumn && row == _dragRow
                    ? DragInk : (item.m_equipped ? EquippedInk : SlotInk);
                return;
            }
            Color baseColour = column == _dragColumn && row == _dragRow
                ? DragInk : (item.m_equipped ? EquippedInk : SlotInk);
            _slots[index].Frame.color = Color.Lerp(baseColour, HoldInk,
                                                    Mathf.Clamp01(progress));
        }

        /// <summary>Turns a point on the page into a grid position.</summary>
        private bool Locate(Vector2 pagePoint, out int column, out int row)
        {
            column = row = -1;
            if (_grid == null || _page == null) return false;

            // The grid is anchored to the page's top while the page itself has
            // a centred pivot. GridTop is therefore not a page-local Y value.
            // Convert through the live transforms and measure from the grid's
            // actual upper-left corner instead of duplicating its anchoring
            // arithmetic here.
            Vector3 gridPoint = _grid.InverseTransformPoint(
                _page.TransformPoint(pagePoint));
            Rect gridRect = _grid.rect;
            float localX = gridPoint.x - gridRect.xMin;
            float localY = gridRect.yMax - gridPoint.y;
            if (localX < 0f || localY < 0f) return false;

            int displayColumn = (int)(localX / (_slotSize + SlotGap));
            int displayRow = (int)(localY / (_slotSize + SlotGap));
            if (displayColumn < 0 || displayRow < 0
                || displayColumn >= _displayColumns)
                return false;

            // Do not turn the visual spacing between two cells into a press on
            // whichever cell happens to precede it.
            float withinX = localX - displayColumn * (_slotSize + SlotGap);
            float withinY = localY - displayRow * (_slotSize + SlotGap);
            if (withinX > _slotSize || withinY > _slotSize) return false;

            int index = displayRow * _displayColumns + displayColumn;
            if (index < 0 || index >= _displaySlotCount) return false;
            column = index % _width;
            row = FirstCarriedRow + index / _width;
            return true;
        }

        /// <summary>Fits the live carried inventory inside its card.</summary>
        private void UpdateGridMetrics(int wanted)
        {
            _displaySlotCount = Mathf.Max(0, wanted);
            int layoutColumns = _expanded ? InventoryColumns : DisplayColumns;
            _displayColumns = wanted > 0
                ? Mathf.Max(1, Mathf.Min(layoutColumns, wanted))
                : layoutColumns;
            int displayRows = wanted > 0
                ? Mathf.CeilToInt(wanted / (float)_displayColumns) : 0;

            float byWidth = (_pageSize.x - GridSideInset * 2f
                - (_displayColumns - 1) * SlotGap) / _displayColumns;
            float availableHeight = Mathf.Max(1f,
                _pageSize.y - GridTopInset - GridBottomInset);
            float byHeight = displayRows > 0
                ? (availableHeight - (displayRows - 1) * SlotGap) / displayRows
                : byWidth;
            _slotSize = Mathf.Max(1f, Mathf.Min(byWidth, byHeight));

            float gridWidth = displayRows > 0
                ? _displayColumns * _slotSize
                    + (_displayColumns - 1) * SlotGap : 0f;
            float gridHeight = displayRows > 0
                ? displayRows * _slotSize + (displayRows - 1) * SlotGap : 0f;
            _grid.sizeDelta = new Vector2(gridWidth, gridHeight);
            _grid.anchoredPosition = new Vector2(0f,
                -GridTopInset - Mathf.Max(0f, (availableHeight - gridHeight) * 0.5f));

            foreach (Slot slot in _slots)
            {
                ((RectTransform)slot.Root.transform).sizeDelta =
                    new Vector2(_slotSize, _slotSize);
                LayoutSlotVisuals(slot);
            }
        }

        private void LayoutActions()
        {
            if (_sortCard == null || _discardCard == null) return;
            const float gap = 8f;
            float available = Mathf.Max(220f, _pageSize.x - 32f);
            float width = (available - gap) * 0.5f;
            _sortCard.anchoredPosition = new Vector2(16f, 12f);
            _sortCard.sizeDelta = new Vector2(width, 68f);
            _discardCard.anchoredPosition = new Vector2(16f + width + gap, 12f);
            _discardCard.sizeDelta = new Vector2(width, 68f);
        }

        /// <summary>
        /// The numbers that used to be columns on the rails.
        /// </summary>
        /// <remarks>
        /// Food and eitr were bars beside the map and were taken off it: food
        /// only moves when you eat and eitr is zero until the Mistlands, so both
        /// were teaching the eye to ignore that side of the panel. They are
        /// still worth knowing, so they are written here, where you come to look
        /// at what you are carrying — along with armour, which the game only
        /// shows on a screen this replaces.
        /// </remarks>
        private void RefreshReadout(Player player)
        {
            if (_readout == null) return;

            var inventory = player.GetInventory();
            if (IsDiscardArmed())
            {
                _readout.text = "TAP DISCARD AGAIN TO CONFIRM";
                _readout.color = new Color(0.88f, 0.38f, 0.30f, 1f);
                return;
            }
            if (_dragColumn >= 0 && inventory != null)
            {
                var selected = inventory.GetItemAt(_dragColumn, _dragRow);
                if (selected != null)
                {
                    string stars = ItemPresentation.QualityStars(selected);
                    string durability = ItemPresentation.ConditionDetail(selected);
                    string stack = selected.m_shared != null
                                   && selected.m_shared.m_maxStackSize > 1
                        ? "  ·  " + selected.m_stack + "/"
                          + selected.m_shared.m_maxStackSize
                        : "";
                    string portal = ItemPresentation.IsPortalBlocked(selected)
                        ? "  ·  NO PORTAL" : "";
                    _readout.text = Localize(selected.m_shared.m_name)
                        + (stars.Length > 0 ? "  " + stars : "")
                        + stack
                        + (durability.Length > 0 ? "  ·  " + durability : "")
                        + portal
                        + (SecondScreenPreferences.ShowInteractionHints
                            ? "  ·  TAP A SLOT TO MOVE" : "");
                    _readout.color = GoldText;
                    return;
                }
                _dragColumn = -1;
                _dragRow = -1;
            }
            _readout.text = SecondScreenPreferences.ShowInteractionHints
                ? "TAP TO MOVE  ·  HOLD TO EQUIP, EAT OR USE" : "";
            _readout.color = Weak;
        }

        private void Paint(Slot slot, ItemDrop.ItemData item)
        {
            if (item == null)
            {
                slot.Frame.color = SlotInk;
                slot.Icon.enabled = false;
                slot.Amount.text = "";
                slot.Quality.text = "";
                slot.Equipped.text = "";
                slot.Condition.text = "";
                slot.Durability.transform.parent.gameObject.SetActive(false);
                return;
            }

            slot.Frame.color = item.m_equipped ? EquippedInk : SlotInk;

            Sprite icon = item.GetIcon();
            slot.Icon.sprite = icon;
            slot.Icon.enabled = icon != null;

            slot.Amount.text = item.m_shared != null && item.m_shared.m_maxStackSize > 1
                ? item.m_stack.ToString()
                : "";
            bool showBadges = SecondScreenPreferences.ShowItemBadges;
            slot.Quality.text = showBadges ? ItemPresentation.QualityStars(item) : "";
            slot.Equipped.text = showBadges
                ? ItemPresentation.EquipState(Player.m_localPlayer, item) : "";
            slot.Condition.text = showBadges
                ? ItemPresentation.ConditionBadge(item) : "";
            slot.Amount.color = ItemPresentation.IsFullStack(item)
                ? GoldText : new Color(0.90f, 0.82f, 0.69f, 1f);

            // Only for things that wear out: a bar under every apple would read
            // as damage on food that cannot take any.
            bool wears = item.m_shared != null && item.m_shared.m_useDurability;
            slot.Durability.transform.parent.gameObject.SetActive(wears);
            if (wears)
            {
                float fraction = ItemPresentation.Durability(item);
                slot.Durability.rectTransform.anchorMax = new Vector2(fraction, 1f);
                if (item.m_durability <= 0f)
                    slot.Durability.rectTransform.anchorMax = Vector2.one;
                slot.Durability.color = ItemPresentation.DurabilityColour(item,
                    new Color(0.83f, 0.64f, 0.31f, 1f));
            }
        }

        private Slot CreateSlot()
        {
            var go = new GameObject("Slot", typeof(RectTransform), typeof(Image),
                                    typeof(CanvasGroup), typeof(InventorySlotInput));
            var rect = (RectTransform)go.transform;
            rect.SetParent(_grid, false);
            rect.anchorMin = new Vector2(0f, 1f);
            rect.anchorMax = new Vector2(0f, 1f);
            rect.pivot = new Vector2(0f, 1f);
            rect.sizeDelta = new Vector2(_slotSize, _slotSize);

            var frame = go.GetComponent<Image>();
            UiSkin.Slot(frame, SlotInk);
            frame.raycastTarget = true;

            var iconGO = new GameObject("Icon", typeof(RectTransform), typeof(Image));
            var iconRect = (RectTransform)iconGO.transform;
            iconRect.SetParent(rect, false);
            iconRect.anchorMin = Vector2.zero;
            iconRect.anchorMax = Vector2.one;
            iconRect.offsetMin = new Vector2(6f, 6f);
            iconRect.offsetMax = new Vector2(-6f, -6f);
            var icon = iconGO.GetComponent<Image>();
            icon.preserveAspect = true;
            icon.raycastTarget = false;

            var amount = SecondScreenUI.MakeText(rect, "Amount", "", 13f,
                                                 new Color(0.90f, 0.82f, 0.69f, 1f),
                                                 TextAlignmentOptions.BottomRight);
            if (_font != null) amount.font = _font;
            var amountRect = (RectTransform)amount.transform;
            amountRect.offsetMin = new Vector2(1f, 8f);
            amountRect.offsetMax = new Vector2(-5f, 0f);

            var quality = SecondScreenUI.MakeText(rect, "Quality", "", 12f, GoldText,
                                                   TextAlignmentOptions.TopLeft);
            if (_font != null) quality.font = _font;
            var qualityRect = (RectTransform)quality.transform;
            qualityRect.offsetMin = new Vector2(5f, 0f);
            qualityRect.offsetMax = new Vector2(-5f, -4f);
            quality.fontStyle = FontStyles.Bold;

            var equipped = SecondScreenUI.MakeText(rect, "Equipped", "", 12f,
                new Color(1f, 0.73f, 0.30f, 1f), TextAlignmentOptions.TopRight);
            if (_font != null) equipped.font = _font;
            var equippedRect = (RectTransform)equipped.transform;
            equippedRect.offsetMin = new Vector2(5f, 0f);
            equippedRect.offsetMax = new Vector2(-5f, -4f);
            equipped.fontStyle = FontStyles.Bold;

            var condition = SecondScreenUI.MakeText(rect, "Condition", "", 10f,
                new Color(0.96f, 0.42f, 0.32f, 1f), TextAlignmentOptions.BottomLeft);
            if (_font != null) condition.font = _font;
            var conditionRect = (RectTransform)condition.transform;
            conditionRect.offsetMin = new Vector2(5f, 8f);
            conditionRect.offsetMax = new Vector2(-5f, 0f);
            condition.fontStyle = FontStyles.Bold;

            var barGO = new GameObject("Durability", typeof(RectTransform), typeof(Image));
            var barRect = (RectTransform)barGO.transform;
            barRect.SetParent(rect, false);
            barRect.anchorMin = new Vector2(0f, 0f);
            barRect.anchorMax = new Vector2(1f, 0f);
            barRect.pivot = new Vector2(0f, 0f);
            barRect.offsetMin = new Vector2(4f, 3f);
            barRect.offsetMax = new Vector2(-4f, 7f);
            UiSkin.Plain(barGO.GetComponent<Image>(),
                new Color(0f, 0f, 0f, 0.6f));

            var fillGO = new GameObject("Fill", typeof(RectTransform), typeof(Image));
            var fillRect = (RectTransform)fillGO.transform;
            fillRect.SetParent(barRect, false);
            fillRect.anchorMin = Vector2.zero;
            fillRect.anchorMax = Vector2.one;
            fillRect.offsetMin = Vector2.zero;
            fillRect.offsetMax = Vector2.zero;

            var slot = new Slot
            {
                Root = go,
                Frame = frame,
                Icon = icon,
                Amount = amount,
                Quality = quality,
                Equipped = equipped,
                Condition = condition,
                Durability = fillGO.GetComponent<Image>(),
                Input = go.GetComponent<InventorySlotInput>(),
            };
            LayoutSlotVisuals(slot);
            return slot;
        }

        /// <summary>Keeps badges and icon padding proportional in both layouts.</summary>
        private void LayoutSlotVisuals(Slot slot)
        {
            if (slot == null || slot.Icon == null) return;
            float iconInset = Mathf.Clamp(_slotSize * 0.085f, 5f, 10f);
            RectTransform icon = slot.Icon.rectTransform;
            icon.offsetMin = new Vector2(iconInset, iconInset);
            icon.offsetMax = new Vector2(-iconInset, -iconInset);

            slot.Amount.fontSize = Mathf.Clamp(_slotSize * 0.18f, 11f, 18f);
            slot.Quality.fontSize = Mathf.Clamp(_slotSize * 0.16f, 10f, 16f);
            slot.Equipped.fontSize = Mathf.Clamp(_slotSize * 0.16f, 10f, 16f);
            slot.Condition.fontSize = Mathf.Clamp(_slotSize * 0.135f, 9f, 14f);

            var durability = slot.Durability != null
                ? slot.Durability.transform.parent as RectTransform : null;
            if (durability != null)
            {
                float edge = Mathf.Clamp(_slotSize * 0.055f, 3f, 7f);
                float height = Mathf.Clamp(_slotSize * 0.055f, 4f, 7f);
                durability.offsetMin = new Vector2(edge, 3f);
                durability.offsetMax = new Vector2(-edge, 3f + height);
            }
        }

        private RectTransform ActionCard(string label, float x, float width, Action action)
        {
            var go = action != null
                ? new GameObject("Action_" + label, typeof(RectTransform), typeof(Image), typeof(Button))
                : new GameObject("Action_" + label, typeof(RectTransform), typeof(Image));
            var rect = (RectTransform)go.transform;
            rect.SetParent(_page, false);
            rect.anchorMin = new Vector2(0f, 0f);
            rect.anchorMax = new Vector2(0f, 0f);
            rect.pivot = new Vector2(0f, 0f);
            rect.anchoredPosition = new Vector2(x, 12f);
            rect.sizeDelta = new Vector2(width, 68f);
            var image = go.GetComponent<Image>();
            UiSkin.Rectangle(image,
                new Color(0.025f, 0.018f, 0.012f, 0.62f));
            image.raycastTarget = action != null;

            if (!string.IsNullOrEmpty(label))
            {
                var text = SecondScreenUI.MakeText(rect, "Label", label, 20f, Weak);
                text.fontStyle = FontStyles.Bold;
                var textRect = (RectTransform)text.transform;
                textRect.offsetMin = new Vector2(6f, 4f);
                textRect.offsetMax = new Vector2(-6f, -4f);
                SecondScreenUI.FitText(text, 12f, 20f);
            }
            if (action != null)
            {
                var button = go.GetComponent<Button>();
                button.targetGraphic = image;
                button.transition = Selectable.Transition.ColorTint;
                button.navigation = new Navigation { mode = Navigation.Mode.None };
                button.onClick.AddListener(() => action());
            }
            return rect;
        }

        private void SortInventory()
        {
            var player = Player.m_localPlayer;
            var inventory = player != null ? player.GetInventory() : null;
            if (inventory == null || player.IsTeleporting() || player.InCutscene()) return;

            CancelDrag();
            var items = new List<ItemDrop.ItemData>();
            foreach (var item in inventory.GetAllItems())
                if (item != null && item.m_gridPos.y > 0) items.Add(item);

            items.Sort((a, b) =>
            {
                int type = a.m_shared.m_itemType.CompareTo(b.m_shared.m_itemType);
                if (type != 0) return type;
                int name = string.Compare(Localize(a.m_shared.m_name),
                    Localize(b.m_shared.m_name), StringComparison.CurrentCultureIgnoreCase);
                if (name != 0) return name;
                int quality = b.m_quality.CompareTo(a.m_quality);
                return quality != 0 ? quality : b.m_stack.CompareTo(a.m_stack);
            });

            int width = inventory.GetWidth();
            for (int i = 0; i < items.Count; i++)
                items[i].m_gridPos = new Vector2i(i % width, 1 + i / width);
            InventoryChanged?.Invoke(inventory, null);
            Refresh();
        }

        private void DiscardSelected()
        {
            var player = Player.m_localPlayer;
            var inventory = player != null ? player.GetInventory() : null;
            if (inventory == null || player.IsTeleporting() || player.InCutscene()) return;
            if (_dragColumn < 0)
            {
                ClearDiscardConfirmation();
                _readout.text = "SELECT AN ITEM, THEN TAP DISCARD";
                _readout.color = GoldText;
                return;
            }

            var item = inventory.GetItemAt(_dragColumn, _dragRow);
            if (item != null && item.m_shared.m_questItem)
            {
                ClearDiscardConfirmation();
                _readout.text = "QUEST ITEMS CANNOT BE DISCARDED";
                _readout.color = new Color(0.88f, 0.38f, 0.30f, 1f);
                return;
            }
            if (item != null && SecondScreenPreferences.ConfirmDiscard
                && !IsDiscardArmed())
            {
                _discardConfirmColumn = _dragColumn;
                _discardConfirmRow = _dragRow;
                _discardConfirmUntil = Time.unscaledTime + 2.5f;
                Refresh(true);
                return;
            }
            ClearDiscardConfirmation();
            if (item != null && !player.DropItem(inventory, item, item.m_stack))
            {
                _readout.text = "ITEM COULD NOT BE DISCARDED";
                _readout.color = new Color(0.88f, 0.38f, 0.30f, 1f);
                return;
            }
            _dragColumn = -1;
            _dragRow = -1;
            Refresh();
        }

        private bool IsDiscardArmed()
        {
            return SecondScreenPreferences.ConfirmDiscard
                   && _discardConfirmColumn == _dragColumn
                   && _discardConfirmRow == _dragRow
                   && _discardConfirmColumn >= 0
                   && Time.unscaledTime < _discardConfirmUntil;
        }

        private void ClearDiscardConfirmation()
        {
            _discardConfirmColumn = -1;
            _discardConfirmRow = -1;
            _discardConfirmUntil = 0f;
        }

        private bool Contains(RectTransform target, Vector2 pagePoint)
        {
            if (target == null || _page == null) return false;
            Vector3 local = target.InverseTransformPoint(_page.TransformPoint(pagePoint));
            return target.rect.Contains(new Vector2(local.x, local.y));
        }

        private static string Localize(string value)
        {
            if (string.IsNullOrEmpty(value)) return "";
            try
            {
                return Localization.instance != null
                    ? Localization.instance.Localize(value) : value;
            }
            catch { return value; }
        }
    }
}
