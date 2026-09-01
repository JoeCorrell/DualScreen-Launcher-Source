using System.Collections.Generic;
using System.Reflection;
using HarmonyLib;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace ValheimSecondScreen.UI
{
    /// <summary>
    /// The eight hotbar slots, drawn to match the rest of the panel.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Valheim's hotbar is the first row of the player's inventory, so this reads
    /// the same eight items the game does and draws each with its own icon. Built
    /// rather than moved for the same reason the inventory page is: the real
    /// <c>HotkeyBar</c> is sized and positioned for the bottom of a wide screen,
    /// and it carries key hints for keys this panel never receives.
    /// </para>
    /// <para>
    /// The selected slot is outlined rather than filled. A filled highlight
    /// behind a dark item icon hides the item, which on the one slot you are
    /// about to use is the wrong thing to hide.
    /// </para>
    /// </remarks>
    internal sealed class HotbarStrip
    {
        private const int Slots = 8;
        private const float SlotGap = 4f;

        /// <summary>
        /// Slot size, worked out from the bar rather than fixed.
        /// </summary>
        /// <remarks>
        /// Eight fixed-size slots inside a bar drawn the full width of the panel
        /// left a strip of backdrop either side that read as an empty frame. The
        /// slots now take whatever the bar has, limited by its height so they
        /// stay square.
        /// </remarks>
        private readonly float _slotSize;

        private static readonly Color SlotInk = UiTheme.Slot;
        private static readonly Color Selected = UiTheme.ControlSelected;
        private static readonly Color Equipped = UiTheme.SlotEquipped;
        private static readonly Color Gold = UiTheme.Gold;
        private static readonly Color Weak = UiTheme.Muted;

        private readonly List<Cell> _cells = new List<Cell>();
        private readonly TMP_FontAsset _font;
        private static readonly FieldInfo VanillaSelection =
            AccessTools.Field(typeof(HotkeyBar), "m_selected");
        private HotkeyBar _vanillaHotbar;
        private int _lastFingerprint;
        private bool _hasPainted;

        private sealed class Cell
        {
            public Image Frame;
            public GameObject Selection;
            public Image Icon;
            public TMP_Text Amount;
            public TMP_Text Key;
            public TMP_Text Quality;
            public TMP_Text Equipped;
            public TMP_Text Condition;
            public RectTransform Durability;
            public Image DurabilityFill;
        }

        public HotbarStrip(RectTransform parent, TMP_FontAsset font, Vector2 barSize)
        {
            _font = font;

            float byWidth = (barSize.x - (Slots - 1) * SlotGap) / Slots;
            _slotSize = Mathf.Max(40f, Mathf.Min(byWidth, barSize.y - 6f));

            var stripGO = new GameObject("Hotbar", typeof(RectTransform));
            var strip = (RectTransform)stripGO.transform;
            strip.SetParent(parent, false);
            strip.anchorMin = new Vector2(0.5f, 0.5f);
            strip.anchorMax = new Vector2(0.5f, 0.5f);
            strip.pivot = new Vector2(0.5f, 0.5f);

            float span = Slots * _slotSize + (Slots - 1) * SlotGap;
            strip.sizeDelta = new Vector2(span, _slotSize);

            for (int i = 0; i < Slots; i++)
            {
                _cells.Add(CreateCell(strip, i, -span * 0.5f + i * (_slotSize + SlotGap)));
            }
        }

        /// <summary>
        /// Uses the slot at a horizontal position, measured from the panel's
        /// centre.
        /// </summary>
        /// <remarks>
        /// Only the horizontal matters: the strip is one row spanning the panel,
        /// so anything inside the bottom bar is a hotbar press and the question
        /// is only which of the eight.
        /// </remarks>
        public bool Tap(float x)
        {
            float span = Slots * _slotSize + (Slots - 1) * SlotGap;
            float local = x + span * 0.5f;
            float step = _slotSize + SlotGap;
            int index = Mathf.FloorToInt(local / step);
            if (index < 0 || index >= Slots) return false;
            if (local - index * step > _slotSize) return false;

            return UseSlot(index);
        }

        /// <summary>Uses the slot whose live Unity rectangle contains the touch.</summary>
        public bool Tap(RectTransform panelRoot, Vector2 panelPoint)
        {
            if (panelRoot == null) return false;
            Vector3 worldPoint = panelRoot.TransformPoint(panelPoint);
            for (int i = 0; i < _cells.Count; i++)
            {
                var rect = _cells[i].Frame != null
                    ? _cells[i].Frame.rectTransform
                    : null;
                if (rect != null)
                {
                    Vector3 local = rect.InverseTransformPoint(worldPoint);
                    if (rect.rect.Contains(new Vector2(local.x, local.y)))
                        return UseSlot(i);
                }
            }
            return false;
        }

        private static bool UseSlot(int index)
        {
            var player = Player.m_localPlayer;
            var inventory = player != null ? player.GetInventory() : null;
            if (inventory == null || player.IsTeleporting() || player.InCutscene()) return false;

            var item = inventory.GetItemAt(index, 0);
            if (item == null) return false;

            // UseItem owns both halves of the toggle, including delayed equip
            // and unequip actions. This is an inventory surface, not the world
            // under the player's reticle, so fromInventoryGui also prevents a
            // touch from trying the item on whatever the character is facing.
            player.UseItem(inventory, item, true);
            return true;
        }

        public void Refresh()
        {
            var player = Player.m_localPlayer;
            var inventory = player != null ? player.GetInventory() : null;
            if (inventory == null) return;

            // The row the game reserves for the hotbar, and which of its slots
            // is currently live.
            bool gamepadActive = ZInput.IsGamepadActive();
            int selected = GetSelectedSlot(gamepadActive);
            int fingerprint = 17;
            for (int i = 0; i < Slots; i++)
            {
                var item = inventory.GetItemAt(i, 0);
                fingerprint = UiStateFingerprint.Add(
                    fingerprint, UiStateFingerprint.Item(item));
                fingerprint = UiStateFingerprint.Add(fingerprint,
                    item != null && player.IsEquipActionQueued(item) ? 1 : 0);
            }
            fingerprint = UiStateFingerprint.Add(fingerprint, selected);
            fingerprint = UiStateFingerprint.Add(fingerprint, gamepadActive ? 1 : 0);
            fingerprint = UiStateFingerprint.Add(
                fingerprint, SecondScreenPreferences.ShowItemBadges ? 1 : 0);
            if (_hasPainted && fingerprint == _lastFingerprint) return;
            _lastFingerprint = fingerprint;
            _hasPainted = true;

            for (int i = 0; i < _cells.Count; i++)
            {
                Paint(_cells[i], inventory.GetItemAt(i, 0), i == selected,
                      gamepadActive);
            }
        }

        /// <summary>
        /// Which hotbar slot the player has chosen, or -1 for none.
        /// </summary>
        /// <remarks>
        /// The gamepad cursor belongs to Valheim's live HotkeyBar. Equipment is
        /// deliberately rendered separately because weapons, shields and armour
        /// can all be equipped at once and none of them alone identifies the
        /// controller's current slot.
        /// </remarks>
        private int GetSelectedSlot(bool gamepadActive)
        {
            if (!gamepadActive || VanillaSelection == null) return -1;

            if (_vanillaHotbar == null && Hud.instance != null)
                _vanillaHotbar = Hud.instance.GetComponentInChildren<HotkeyBar>(true);
            if (_vanillaHotbar == null) return -1;

            try
            {
                object value = VanillaSelection.GetValue(_vanillaHotbar);
                if (value is int selected && selected >= 0 && selected < Slots)
                    return selected;
            }
            catch { }
            return -1;
        }

        private void Paint(Cell cell, ItemDrop.ItemData item, bool selected,
                           bool gamepadActive)
        {
            if (cell.Selection.activeSelf != selected)
                cell.Selection.SetActive(selected);
            if (cell.Key.gameObject.activeSelf == gamepadActive)
                cell.Key.gameObject.SetActive(!gamepadActive);

            if (item == null)
            {
                cell.Frame.color = SlotInk;
                cell.Icon.enabled = false;
                cell.Amount.text = "";
                cell.Quality.text = "";
                cell.Equipped.text = "";
                cell.Condition.text = "";
                cell.Durability.gameObject.SetActive(false);
                return;
            }

            cell.Frame.color = item.m_equipped ? Equipped : SlotInk;

            Sprite icon = item.GetIcon();
            cell.Icon.sprite = icon;
            cell.Icon.enabled = icon != null;

            cell.Amount.text = item.m_stack > 1 ? item.m_stack.ToString() : "";
            bool badges = SecondScreenPreferences.ShowItemBadges;
            cell.Quality.text = badges ? ItemPresentation.QualityStars(item) : "";
            cell.Equipped.text = badges
                ? ItemPresentation.EquipState(Player.m_localPlayer, item) : "";
            cell.Condition.text = badges ? ItemPresentation.ConditionBadge(item) : "";
            cell.Amount.color = ItemPresentation.IsFullStack(item) ? Gold : Color.white;

            bool wears = item.m_shared != null && item.m_shared.m_useDurability;
            cell.Durability.gameObject.SetActive(wears);
            if (wears)
            {
                float fraction = ItemPresentation.Durability(item);
                cell.DurabilityFill.rectTransform.anchorMax = new Vector2(fraction, 1f);
                if (item.m_durability <= 0f)
                    cell.DurabilityFill.rectTransform.anchorMax = Vector2.one;
                cell.DurabilityFill.color = ItemPresentation.DurabilityColour(item, Gold);
            }
        }

        private Cell CreateCell(RectTransform strip, int index, float x)
        {
            var go = new GameObject("Slot" + (index + 1),
                                    typeof(RectTransform), typeof(Image), typeof(Button));
            var rect = (RectTransform)go.transform;
            rect.SetParent(strip, false);
            rect.anchorMin = new Vector2(0.5f, 0.5f);
            rect.anchorMax = new Vector2(0.5f, 0.5f);
            rect.pivot = new Vector2(0f, 0.5f);
            rect.sizeDelta = new Vector2(_slotSize, _slotSize);
            rect.anchoredPosition = new Vector2(x, 0f);

            var frame = go.GetComponent<Image>();
            UiSkin.Slot(frame, SlotInk);
            frame.raycastTarget = true;

            var button = go.GetComponent<Button>();
            button.targetGraphic = frame;
            button.transition = Selectable.Transition.None;
            button.navigation = new Navigation { mode = Navigation.Mode.None };
            int slot = index;
            button.onClick.AddListener(() => UseSlot(slot));

            // Use Valheim's selected-slot sprite where the inventory prefab
            // exposes it.  Older prefabs fall back to four slim bronze edges,
            // which preserves icon contrast instead of washing the whole cell.
            var outlineGO = new GameObject("Outline", typeof(RectTransform), typeof(Image));
            var outline = (RectTransform)outlineGO.transform;
            outline.SetParent(rect, false);
            outline.anchorMin = Vector2.zero;
            outline.anchorMax = Vector2.one;
            outline.offsetMin = Vector2.zero;
            outline.offsetMax = Vector2.zero;
            var outlineImage = outlineGO.GetComponent<Image>();
            UiSkin.SlotSelection(outlineImage);
            if (outlineImage.sprite == null)
                AddSelectionEdges(outline);
            outlineGO.SetActive(false);

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

            var key = SecondScreenUI.MakeText(rect, "Key", (index + 1).ToString(), 16f, Weak,
                                              TextAlignmentOptions.TopLeft);
            if (_font != null) key.font = _font;
            ((RectTransform)key.transform).offsetMin = new Vector2(7f, 0f);
            ((RectTransform)key.transform).offsetMax = new Vector2(0f, -4f);

            var quality = SecondScreenUI.MakeText(rect, "Quality", "", 13f, Gold,
                                                  TextAlignmentOptions.TopRight);
            if (_font != null) quality.font = _font;
            ((RectTransform)quality.transform).offsetMin = new Vector2(0f, 0f);
            ((RectTransform)quality.transform).offsetMax = new Vector2(-7f, -4f);
            quality.fontStyle = FontStyles.Bold;

            var equipped = SecondScreenUI.MakeText(rect, "Equipped", "", 13f,
                new Color(1f, 0.73f, 0.30f, 1f), TextAlignmentOptions.Top);
            if (_font != null) equipped.font = _font;
            ((RectTransform)equipped.transform).offsetMin = new Vector2(30f, 0f);
            ((RectTransform)equipped.transform).offsetMax = new Vector2(-30f, -4f);
            equipped.fontStyle = FontStyles.Bold;

            var amount = SecondScreenUI.MakeText(rect, "Amount", "", 22f, Color.white,
                                                 TextAlignmentOptions.BottomRight);
            if (_font != null) amount.font = _font;
            ((RectTransform)amount.transform).offsetMin = new Vector2(0f, 8f);
            ((RectTransform)amount.transform).offsetMax = new Vector2(-7f, 0f);

            var condition = SecondScreenUI.MakeText(rect, "Condition", "", 10f,
                new Color(0.96f, 0.42f, 0.32f, 1f), TextAlignmentOptions.BottomLeft);
            if (_font != null) condition.font = _font;
            ((RectTransform)condition.transform).offsetMin = new Vector2(7f, 9f);
            ((RectTransform)condition.transform).offsetMax = new Vector2(0f, 0f);
            condition.fontStyle = FontStyles.Bold;

            var barGO = new GameObject("Durability", typeof(RectTransform), typeof(Image));
            var bar = (RectTransform)barGO.transform;
            bar.SetParent(rect, false);
            bar.anchorMin = new Vector2(0f, 0f);
            bar.anchorMax = new Vector2(1f, 0f);
            bar.pivot = new Vector2(0.5f, 0f);
            bar.offsetMin = new Vector2(6f, 4f);
            bar.offsetMax = new Vector2(-6f, 9f);
            UiSkin.Plain(barGO.GetComponent<Image>(),
                new Color(0f, 0f, 0f, 0.65f));

            var fillGO = new GameObject("Fill", typeof(RectTransform), typeof(Image));
            var fill = (RectTransform)fillGO.transform;
            fill.SetParent(bar, false);
            fill.anchorMin = Vector2.zero;
            fill.anchorMax = Vector2.one;
            fill.offsetMin = Vector2.zero;
            fill.offsetMax = Vector2.zero;

            return new Cell
            {
                Frame = frame,
                Selection = outlineGO,
                Icon = icon,
                Amount = amount,
                Key = key,
                Quality = quality,
                Equipped = equipped,
                Condition = condition,
                Durability = bar,
                DurabilityFill = fillGO.GetComponent<Image>(),
            };
        }

        private static void AddSelectionEdges(RectTransform parent)
        {
            Edge(parent, "Top", new Vector2(0f, 1f), new Vector2(1f, 1f),
                 new Vector2(0f, -3f), Vector2.zero);
            Edge(parent, "Bottom", new Vector2(0f, 0f), new Vector2(1f, 0f),
                 new Vector2(0f, 0f), new Vector2(0f, 3f));
            Edge(parent, "Left", new Vector2(0f, 0f), new Vector2(0f, 1f),
                 new Vector2(0f, 3f), new Vector2(3f, -3f));
            Edge(parent, "Right", new Vector2(1f, 0f), new Vector2(1f, 1f),
                 new Vector2(-3f, 3f), new Vector2(0f, -3f));
        }

        private static void Edge(RectTransform parent, string name,
                                 Vector2 anchorMin, Vector2 anchorMax,
                                 Vector2 offsetMin, Vector2 offsetMax)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Image));
            var rect = (RectTransform)go.transform;
            rect.SetParent(parent, false);
            rect.anchorMin = anchorMin;
            rect.anchorMax = anchorMax;
            rect.offsetMin = offsetMin;
            rect.offsetMax = offsetMax;
            var image = go.GetComponent<Image>();
            image.color = Selected;
            image.raycastTarget = false;
        }
    }
}
