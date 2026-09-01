using System.Collections.Generic;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace ValheimSecondScreen.UI
{
    /// <summary>
    /// Context-only view of the player's inventory and an open container. All
    /// moves go through Valheim's live Inventory/InventoryGrid objects.
    /// </summary>
    internal sealed class ContainerPage
    {
        internal enum Side { Player, Container }

        private const float Outer = 12f;
        private const float Gap = 12f;
        private const float SlotGap = 4f;
        private const float HeaderHeight = 62f;
        private const float FooterHeight = 68f;
        private const int DisplayColumns = 4;
        private const int DisplayRows = 8;
        private const int DisplaySlots = DisplayColumns * DisplayRows;

        private static readonly Color CardInk = UiTheme.Panel;
        private static readonly Color SlotInk = UiTheme.Slot;
        private static readonly Color UnavailableInk = UiTheme.ControlDisabled;
        private static readonly Color EquippedInk = UiTheme.SlotEquipped;
        private static readonly Color SelectedInk = UiTheme.ControlSelected;
        private static readonly Color HoldInk =
            new Color(0.72f, 0.58f, 0.18f, 0.88f);
        private static readonly Color Gold = UiTheme.Gold;
        private static readonly Color Weak = UiTheme.Muted;
        private static readonly Color Good = UiTheme.Good;
        private static readonly Color Bad = UiTheme.Bad;

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
            public ContainerSlotInput Input;
        }

        private readonly RectTransform _page;
        private readonly TMP_FontAsset _font;
        private readonly Vector2 _pageSize;
        private readonly List<Slot> _playerSlots = new List<Slot>();
        private readonly List<Slot> _containerSlots = new List<Slot>();

        private RectTransform _playerCard;
        private RectTransform _containerCard;
        private RectTransform _playerGrid;
        private RectTransform _containerGrid;
        private TMP_Text _playerTitle;
        private TMP_Text _containerTitle;
        private TMP_Text _playerCount;
        private TMP_Text _containerCount;
        private TMP_Text _hint;
        private Button _previousContainerPage;
        private Button _nextContainerPage;
        private Button _stackButton;
        private Button _takeAllButton;
        private readonly List<Button> _touchButtons = new List<Button>();
        private Container _container;
        private float _slotSize = 54f;
        private int _playerWidth;
        private int _playerHeight;
        private int _containerWidth;
        private int _containerHeight;
        private int _containerPage;
        private int _sourceSide = -1;
        private int _sourceColumn = -1;
        private int _sourceRow = -1;
        private int _holdSide = -1;
        private int _holdColumn = -1;
        private int _holdRow = -1;
        private float _holdProgress = -1f;
        private int _lastFingerprint;
        private bool _hasPainted;
        private string _notice = "";
        private Color _noticeColour = Weak;
        private float _noticeUntil;

        public bool IsOpen => _container != null;

        public ContainerPage(RectTransform page, TMP_FontAsset font, Vector2 pageSize)
        {
            _page = page;
            _font = font;
            _pageSize = pageSize;
            Build();
        }

        public void Open(Container container)
        {
            CancelDrag();
            _container = container;
            _containerPage = 0;
            ClearNotice();
            _hasPainted = false;
            Refresh(true);
        }

        public void Close()
        {
            CancelDrag();
            _container = null;
            _containerPage = 0;
            ClearNotice();
        }

        private void Build()
        {
            float panelWidth = (_pageSize.x - Outer * 2f - Gap) * 0.5f;
            float panelHeight = _pageSize.y - Outer * 2f;
            _playerCard = MakeCard("PlayerInventory", Outer, panelWidth, panelHeight);
            _containerCard = MakeCard("ContainerInventory",
                Outer + panelWidth + Gap, panelWidth, panelHeight);

            _playerTitle = MakeTitle(_playerCard, "INVENTORY");
            _containerTitle = MakeTitle(_containerCard, "CONTAINER");
            _containerTitle.enableAutoSizing = true;
            _containerTitle.fontSizeMin = 18f;
            _containerTitle.fontSizeMax = 30f;
            _containerTitle.overflowMode = TextOverflowModes.Truncate;
            SetTitleInsets(_containerTitle, 58f);
            _playerCount = MakeHeaderMeta(_playerCard, "PlayerCount");
            _containerCount = MakeHeaderMeta(_containerCard, "ContainerCount");
            _playerGrid = MakeGrid(_playerCard, "PlayerGrid");
            _containerGrid = MakeGrid(_containerCard, "ContainerGrid");

            const float pageButtonSize = 48f;
            _previousContainerPage = MakeActionButton(_containerCard,
                "PreviousContainerPage", "<", 6f, 7f, pageButtonSize,
                pageButtonSize, () => PageContainer(-1));
            _nextContainerPage = MakeActionButton(_containerCard,
                "NextContainerPage", ">", panelWidth - pageButtonSize - 6f, 7f,
                pageButtonSize, pageButtonSize, () => PageContainer(1));

            const float actionInset = 10f;
            const float actionGap = 8f;
            const float actionHeight = 52f;
            float actionWidth = (panelWidth - actionInset * 2f - actionGap) * 0.5f;
            float actionTop = panelHeight - actionHeight - 8f;
            _stackButton = MakeActionButton(_containerCard, "Stack", "STACK",
                actionInset, actionTop, actionWidth, actionHeight, StackAll);
            _takeAllButton = MakeActionButton(_containerCard, "TakeAll", "TAKE ALL",
                actionInset + actionWidth + actionGap, actionTop, actionWidth,
                actionHeight, TakeAll);

            _hint = SecondScreenUI.MakeText(_page, "TransferHint",
                "TAP OR DRAG TO MOVE ITEMS  ·  HOLD A CHEST ITEM TO TAKE IT",
                17f, Weak, TextAlignmentOptions.Center);
            if (_font != null) _hint.font = _font;
            var hintRect = (RectTransform)_hint.transform;
            hintRect.anchorMin = Vector2.zero;
            hintRect.anchorMax = Vector2.zero;
            hintRect.pivot = Vector2.zero;
            hintRect.anchoredPosition = new Vector2(Outer + 12f, Outer + 8f);
            hintRect.sizeDelta = new Vector2(panelWidth - 24f, FooterHeight - 16f);
            _hint.enableAutoSizing = true;
            _hint.fontSizeMin = 11f;
            _hint.fontSizeMax = 17f;
            _hint.textWrappingMode = TextWrappingModes.Normal;
            _hint.overflowMode = TextOverflowModes.Truncate;
        }

        private RectTransform MakeCard(string name, float x, float width, float height)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Image));
            var rect = (RectTransform)go.transform;
            rect.SetParent(_page, false);
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.zero;
            rect.pivot = Vector2.zero;
            rect.anchoredPosition = new Vector2(x, Outer);
            rect.sizeDelta = new Vector2(width, height);
            var image = go.GetComponent<Image>();
            image.color = CardInk;
            image.raycastTarget = false;
            UiSkin.Panel(rect, HeaderHeight);
            return rect;
        }

        private TMP_Text MakeTitle(RectTransform parent, string text)
        {
            var title = SecondScreenUI.MakeText(parent, "Title", text, 30f, Gold,
                                                 TextAlignmentOptions.Center);
            if (_font != null) title.font = _font;
            title.fontStyle = FontStyles.Bold;
            var rect = (RectTransform)title.transform;
            rect.anchorMin = new Vector2(0f, 1f);
            rect.anchorMax = new Vector2(1f, 1f);
            rect.pivot = new Vector2(0.5f, 1f);
            rect.anchoredPosition = Vector2.zero;
            rect.sizeDelta = new Vector2(0f, 38f);
            return title;
        }

        private TMP_Text MakeHeaderMeta(RectTransform parent, string name)
        {
            TMP_Text value = SecondScreenUI.MakeText(parent, name, "", 15f, Weak,
                TextAlignmentOptions.Center);
            if (_font != null) value.font = _font;
            value.fontStyle = FontStyles.Bold;
            var rect = (RectTransform)value.transform;
            rect.anchorMin = new Vector2(0f, 1f);
            rect.anchorMax = new Vector2(1f, 1f);
            rect.pivot = new Vector2(0.5f, 1f);
            rect.anchoredPosition = new Vector2(0f, -35f);
            rect.sizeDelta = new Vector2(0f, 23f);
            SecondScreenUI.FitText(value, 10f, 15f);
            return value;
        }

        private static void SetTitleInsets(TMP_Text title, float horizontal)
        {
            if (title == null) return;
            var rect = (RectTransform)title.transform;
            rect.offsetMin = new Vector2(horizontal, rect.offsetMin.y);
            rect.offsetMax = new Vector2(-horizontal, rect.offsetMax.y);
        }

        private static RectTransform MakeGrid(RectTransform parent, string name)
        {
            var go = new GameObject(name, typeof(RectTransform));
            var rect = (RectTransform)go.transform;
            rect.SetParent(parent, false);
            rect.anchorMin = new Vector2(0.5f, 1f);
            rect.anchorMax = new Vector2(0.5f, 1f);
            rect.pivot = new Vector2(0.5f, 1f);
            rect.anchoredPosition = new Vector2(0f, -HeaderHeight);
            return rect;
        }

        public void Refresh(bool force = false)
        {
            if (!IsOpen) return;
            Player player = Player.m_localPlayer;
            Inventory playerInventory = player != null ? player.GetInventory() : null;
            Inventory containerInventory = _container != null
                ? _container.GetInventory() : null;
            if (playerInventory == null || containerInventory == null) return;

            int playerWidth = Mathf.Max(1, playerInventory.GetWidth());
            int playerHeight = Mathf.Max(1, playerInventory.GetHeight());
            int containerWidth = Mathf.Max(1, containerInventory.GetWidth());
            int containerHeight = Mathf.Max(1, containerInventory.GetHeight());
            int containerPages = PageCount(containerWidth * containerHeight);
            int clampedPage = Mathf.Clamp(_containerPage, 0, containerPages - 1);
            if (clampedPage != _containerPage)
            {
                _containerPage = clampedPage;
                ResetInteractionState();
            }
            if (playerWidth != _playerWidth || playerHeight != _playerHeight
                || containerWidth != _containerWidth
                || containerHeight != _containerHeight)
            {
                _playerWidth = playerWidth;
                _playerHeight = playerHeight;
                _containerWidth = containerWidth;
                _containerHeight = containerHeight;
                UpdateLayout();
            }

            bool hasNotice = HasNotice();

            int fingerprint = UiStateFingerprint.Inventory(playerInventory);
            fingerprint = UiStateFingerprint.Add(
                fingerprint, UiStateFingerprint.Inventory(containerInventory));
            fingerprint = UiStateFingerprint.Add(
                fingerprint, containerInventory.GetName()?.GetHashCode() ?? 0);
            fingerprint = UiStateFingerprint.Add(
                fingerprint, SecondScreenPreferences.ShowItemBadges ? 1 : 0);
            fingerprint = UiStateFingerprint.Add(fingerprint, _sourceSide);
            fingerprint = UiStateFingerprint.Add(fingerprint, _sourceColumn);
            fingerprint = UiStateFingerprint.Add(fingerprint, _sourceRow);
            fingerprint = UiStateFingerprint.Add(fingerprint, _holdSide);
            fingerprint = UiStateFingerprint.Add(fingerprint, _holdColumn);
            fingerprint = UiStateFingerprint.Add(fingerprint, _holdRow);
            fingerprint = UiStateFingerprint.Add(
                fingerprint, Mathf.RoundToInt(_holdProgress * 20f));
            fingerprint = UiStateFingerprint.Add(fingerprint, _containerPage);
            fingerprint = UiStateFingerprint.Add(fingerprint, hasNotice ? 1 : 0);
            if (!force && _hasPainted && fingerprint == _lastFingerprint) return;
            _lastFingerprint = fingerprint;
            _hasPainted = true;

            _playerTitle.text = "INVENTORY";
            _containerTitle.text = Localize(containerInventory.GetName()).ToUpperInvariant();
            _playerCount.text = playerInventory.NrOfItems() + " / "
                + (_playerWidth * _playerHeight) + " SLOTS";
            _containerCount.text = containerInventory.NrOfItems() + " / "
                + (_containerWidth * _containerHeight) + " SLOTS"
                + (containerPages > 1
                    ? "  ·  PAGE " + (_containerPage + 1) + " / " + containerPages
                    : "");
            bool paged = containerPages > 1;
            _previousContainerPage.gameObject.SetActive(paged);
            _nextContainerPage.gameObject.SetActive(paged);
            _previousContainerPage.interactable = paged;
            _nextContainerPage.interactable = paged;
            PaintSide(Side.Player, playerInventory, _playerSlots,
                      _playerWidth, _playerHeight);
            PaintSide(Side.Container, containerInventory, _containerSlots,
                      _containerWidth, _containerHeight);

            if (_sourceSide >= 0)
            {
                Inventory source = Resolve((Side)_sourceSide);
                ItemDrop.ItemData item = source != null
                    ? source.GetItemAt(_sourceColumn, _sourceRow) : null;
                if (item == null)
                    CancelDrag();
                else
                {
                    string stars = ItemPresentation.QualityStars(item);
                    string durability = ItemPresentation.ConditionDetail(item);
                    string portal = ItemPresentation.IsPortalBlocked(item)
                        ? "  ·  NO PORTAL" : "";
                    _hint.text = Localize(item.m_shared.m_name).ToUpperInvariant()
                        + (stars.Length > 0 ? "  " + stars : "")
                        + (durability.Length > 0 ? "  ·  " + durability : "")
                        + portal
                        + "  ·  TAP A SLOT TO MOVE  ·  TAP AGAIN TO CANCEL";
                    _hint.color = Gold;
                }
            }
            else if (hasNotice)
            {
                _hint.text = _notice;
                _hint.color = _noticeColour;
            }
            else
            {
                _hint.text = "TAP OR DRAG TO MOVE  ·  HOLD TO USE OR QUICK-TAKE";
                _hint.color = Weak;
            }
        }

        private void UpdateLayout()
        {
            float panelWidth = _playerCard.rect.width;
            float gridHeight = _playerCard.rect.height - HeaderHeight - FooterHeight;
            float byWidth = (panelWidth - 24f - (DisplayColumns - 1) * SlotGap)
                            / DisplayColumns;
            float byHeight = (gridHeight - (DisplayRows - 1) * SlotGap)
                             / DisplayRows;
            _slotSize = Mathf.Max(32f, Mathf.Min(byWidth, byHeight));

            SizeGrid(_playerGrid, gridHeight);
            SizeGrid(_containerGrid, gridHeight);
            ResizeSlots(_playerSlots);
            ResizeSlots(_containerSlots);
        }

        private void SizeGrid(RectTransform grid, float availableHeight)
        {
            float width = DisplayColumns * _slotSize
                          + (DisplayColumns - 1) * SlotGap;
            float height = DisplayRows * _slotSize
                           + (DisplayRows - 1) * SlotGap;
            grid.sizeDelta = new Vector2(width, height);
            grid.anchoredPosition = new Vector2(
                0f, -HeaderHeight - Mathf.Max(0f, (availableHeight - height) * 0.5f));
        }

        private void ResizeSlots(List<Slot> slots)
        {
            foreach (Slot slot in slots)
            {
                ((RectTransform)slot.Root.transform).sizeDelta =
                    new Vector2(_slotSize, _slotSize);
                LayoutSlotVisuals(slot);
            }
        }

        private void PaintSide(Side side, Inventory inventory, List<Slot> slots,
                               int width, int height)
        {
            int capacity = width * height;
            int wanted = DisplaySlots;
            RectTransform grid = side == Side.Player ? _playerGrid : _containerGrid;
            while (slots.Count < wanted) slots.Add(CreateSlot(grid, side));

            for (int i = 0; i < slots.Count; i++)
            {
                Slot slot = slots[i];
                if (i >= wanted)
                {
                    slot.Root.SetActive(false);
                    continue;
                }

                int inventoryIndex = side == Side.Container
                    ? _containerPage * DisplaySlots + i : i;
                bool available = inventoryIndex < capacity;
                int column = available ? inventoryIndex % width : -1;
                int row = available ? inventoryIndex / width : -1;
                slot.Root.SetActive(true);
                slot.Input.Bind(this, side, column, row);
                slot.Frame.raycastTarget = available;
                var rect = (RectTransform)slot.Root.transform;
                rect.sizeDelta = new Vector2(_slotSize, _slotSize);
                rect.anchoredPosition = new Vector2(
                    (i % DisplayColumns) * (_slotSize + SlotGap),
                    -(i / DisplayColumns) * (_slotSize + SlotGap));
                Paint(slot, available ? inventory.GetItemAt(column, row) : null,
                      available,
                      _sourceSide == (int)side
                      && _sourceColumn == column && _sourceRow == row,
                      _holdSide == (int)side
                      && _holdColumn == column && _holdRow == row
                          ? _holdProgress : -1f);
            }
        }

        private Slot CreateSlot(RectTransform grid, Side side)
        {
            var go = new GameObject("Slot", typeof(RectTransform), typeof(Image),
                typeof(CanvasGroup), typeof(ContainerSlotInput));
            var rect = (RectTransform)go.transform;
            rect.SetParent(grid, false);
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
            iconRect.offsetMin = new Vector2(5f, 5f);
            iconRect.offsetMax = new Vector2(-5f, -5f);
            var icon = iconGO.GetComponent<Image>();
            icon.preserveAspect = true;
            icon.raycastTarget = false;

            TMP_Text amount = MakeSlotText(rect, "Amount", 12f,
                TextAlignmentOptions.BottomRight, Weak);
            TMP_Text quality = MakeSlotText(rect, "Quality", 11f,
                TextAlignmentOptions.TopLeft, Gold);
            TMP_Text equipped = MakeSlotText(rect, "Equipped", 11f,
                TextAlignmentOptions.TopRight, Gold);
            TMP_Text condition = MakeSlotText(rect, "Condition", 9f,
                TextAlignmentOptions.BottomLeft,
                new Color(0.96f, 0.42f, 0.32f, 1f));
            condition.rectTransform.offsetMin = new Vector2(4f, 7f);

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
                Input = go.GetComponent<ContainerSlotInput>()
            };
            LayoutSlotVisuals(slot);
            return slot;
        }

        /// <summary>Keeps badges and icon padding readable at the 4x8 scale.</summary>
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

        private TMP_Text MakeSlotText(RectTransform parent, string name, float size,
                                      TextAlignmentOptions align, Color colour)
        {
            TMP_Text text = SecondScreenUI.MakeText(parent, name, "", size, colour, align);
            if (_font != null) text.font = _font;
            var rect = (RectTransform)text.transform;
            rect.offsetMin = new Vector2(4f, 3f);
            rect.offsetMax = new Vector2(-4f, -3f);
            text.fontStyle = FontStyles.Bold;
            return text;
        }

        private static void Paint(Slot slot, ItemDrop.ItemData item, bool available,
                                  bool selected, float holdProgress)
        {
            if (item == null)
            {
                slot.Frame.color = available ? SlotInk : UnavailableInk;
                slot.Icon.enabled = false;
                slot.Amount.text = "";
                slot.Quality.text = "";
                slot.Equipped.text = "";
                slot.Condition.text = "";
                slot.Durability.transform.parent.gameObject.SetActive(false);
                return;
            }

            Color colour = selected ? SelectedInk
                : item.m_equipped ? EquippedInk : SlotInk;
            if (holdProgress >= 0f)
                colour = Color.Lerp(colour, HoldInk, Mathf.Clamp01(holdProgress));
            slot.Frame.color = colour;

            Sprite icon = item.GetIcon();
            slot.Icon.sprite = icon;
            slot.Icon.enabled = icon != null;
            slot.Amount.text = item.m_shared.m_maxStackSize > 1
                ? item.m_stack + "/" + item.m_shared.m_maxStackSize : "";
            bool badges = SecondScreenPreferences.ShowItemBadges;
            slot.Quality.text = badges ? ItemPresentation.QualityStars(item) : "";
            slot.Equipped.text = badges
                ? ItemPresentation.EquipState(Player.m_localPlayer, item) : "";
            slot.Condition.text = badges ? ItemPresentation.ConditionBadge(item) : "";
            slot.Amount.color = ItemPresentation.IsFullStack(item) ? Gold : Weak;

            bool wears = item.m_shared.m_useDurability;
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

        public bool Tap(RectTransform root, Vector2 rootPoint)
        {
            if (TapButton(root, rootPoint)) return true;
            return Locate(root, rootPoint, out Side side, out int column, out int row)
                   && TapAt(side, column, row);
        }

        private bool TapButton(RectTransform root, Vector2 rootPoint)
        {
            if (root == null) return false;
            Vector3 world = root.TransformPoint(rootPoint);
            foreach (Button button in _touchButtons)
            {
                if (button == null || !button.gameObject.activeInHierarchy
                    || !button.interactable) continue;
                var rect = button.transform as RectTransform;
                if (rect == null) continue;
                Vector3 local = rect.InverseTransformPoint(world);
                if (!rect.rect.Contains(new Vector2(local.x, local.y))) continue;
                button.onClick.Invoke();
                return true;
            }
            return false;
        }

        public bool BeginDrag(RectTransform root, Vector2 rootPoint)
        {
            return Locate(root, rootPoint, out Side side, out int column, out int row)
                   && BeginDragAt(side, column, row);
        }

        public bool EndDrag(RectTransform root, Vector2 rootPoint)
        {
            if (!Locate(root, rootPoint, out Side side, out int column, out int row))
            {
                CancelDrag();
                return false;
            }
            return EndDragAt(side, column, row);
        }

        public bool Use(RectTransform root, Vector2 rootPoint)
        {
            return Locate(root, rootPoint, out Side side, out int column, out int row)
                   && UseAt(side, column, row);
        }

        internal bool TapAt(Side side, int column, int row)
        {
            if (_sourceSide >= 0)
            {
                if (_sourceSide == (int)side
                    && _sourceColumn == column && _sourceRow == row)
                {
                    CancelDrag();
                    return true;
                }
                return EndDragAt(side, column, row);
            }
            return BeginDragAt(side, column, row);
        }

        internal bool BeginDragAt(Side side, int column, int row)
        {
            Inventory inventory = Resolve(side);
            Player player = Player.m_localPlayer;
            if (inventory == null || player == null || player.IsTeleporting()
                || player.InCutscene() || !Valid(inventory, column, row)
                || inventory.GetItemAt(column, row) == null) return false;

            // Do not let a second finger replace an item already selected by a
            // tap or another drag. The next ordinary tap is the destination;
            // another drag can begin after the current transaction finishes.
            if (_sourceSide >= 0)
                return _sourceSide == (int)side
                       && _sourceColumn == column && _sourceRow == row;
            _sourceSide = (int)side;
            _sourceColumn = column;
            _sourceRow = row;
            Refresh();
            return true;
        }

        internal bool EndDragAt(Side side, int column, int row)
        {
            if (_sourceSide < 0) return false;
            Side sourceSide = (Side)_sourceSide;
            int sourceColumn = _sourceColumn;
            int sourceRow = _sourceRow;
            _sourceSide = -1;
            _sourceColumn = _sourceRow = -1;

            if (sourceSide == side && sourceColumn == column && sourceRow == row)
            {
                Refresh();
                return true;
            }
            bool moved = Move(sourceSide, sourceColumn, sourceRow, side, column, row);
            if (!moved && SourceStillExists(sourceSide, sourceColumn, sourceRow))
            {
                _sourceSide = (int)sourceSide;
                _sourceColumn = sourceColumn;
                _sourceRow = sourceRow;
                Refresh(true);
            }
            return moved;
        }

        internal bool UseAt(Side side, int column, int row)
        {
            Inventory inventory = Resolve(side);
            Player player = Player.m_localPlayer;
            if (inventory == null || player == null || player.IsTeleporting()
                || player.InCutscene() || !Valid(inventory, column, row)) return false;
            ItemDrop.ItemData item = inventory.GetItemAt(column, row);
            if (item == null) return false;

            CancelDrag();
            if (side == Side.Player)
            {
                player.UseItem(inventory, item, true);
            }
            else
            {
                if (item.m_shared.m_questItem) return false;
                int previousStack = item.m_stack;
                player.GetInventory().MoveItemToThis(inventory, item);
                bool moved = !inventory.ContainsItem(item) || item.m_stack < previousStack;
                if (moved) PlayMoveEffect();
                else player.Message(MessageHud.MessageType.Center, "$inventory_full");
                Refresh();
                return true;
            }
            Refresh();
            return true;
        }

        private bool Move(Side sourceSide, int sourceColumn, int sourceRow,
                          Side destinationSide, int column, int row)
        {
            Player player = Player.m_localPlayer;
            Inventory source = Resolve(sourceSide);
            Inventory destination = Resolve(destinationSide);
            Inventory playerInventory = player != null ? player.GetInventory() : null;
            InventoryGui gui = InventoryGui.instance;
            if (player == null || source == null || destination == null || gui == null
                || player.IsTeleporting() || player.InCutscene()
                || !Valid(source, sourceColumn, sourceRow)
                || !Valid(destination, column, row))
            {
                Refresh();
                return false;
            }

            ItemDrop.ItemData item = source.GetItemAt(sourceColumn, sourceRow);
            if (item == null)
            {
                Refresh();
                return false;
            }
            InventoryGrid grid = destinationSide == Side.Player
                ? gui.m_playerGrid : gui.ContainerGrid;
            if (grid == null)
            {
                Refresh();
                return false;
            }
            if (grid.GetInventory() != destination)
                grid.UpdateInventory(destination,
                    destinationSide == Side.Player ? player : null, null);

            ItemDrop.ItemData replaced = destination.GetItemAt(column, row);
            if (source != destination
                && (item.m_shared.m_questItem
                    || (replaced != null && replaced.m_shared.m_questItem)))
            {
                Refresh();
                return false;
            }
            bool sourceEquipped = player.IsItemEquiped(item);
            bool replacedEquipped = replaced != null && player.IsItemEquiped(replaced);
            Vector2i sourcePosition = item.m_gridPos;

            player.RemoveEquipAction(item);
            player.RemoveEquipAction(replaced);
            player.UnequipItem(item, false);
            player.UnequipItem(replaced, false);

            bool moved = grid.DropItem(source, item, item.m_stack,
                                       new Vector2i(column, row));

            if (sourceEquipped)
            {
                ItemDrop.ItemData movedItem = destination.GetItemAt(column, row);
                if (movedItem != null && playerInventory.ContainsItem(movedItem))
                    player.EquipItem(movedItem, false);
                if (playerInventory.ContainsItem(item)) player.EquipItem(item, false);
            }
            if (replacedEquipped)
            {
                ItemDrop.ItemData swapped = source.GetItemAt(
                    sourcePosition.x, sourcePosition.y);
                if (swapped != null && playerInventory.ContainsItem(swapped))
                    player.EquipItem(swapped, false);
                if (replaced != null && playerInventory.ContainsItem(replaced))
                    player.EquipItem(replaced, false);
            }

            if (moved) PlayMoveEffect();
            Refresh();
            return moved;
        }

        public void CancelDrag()
        {
            _sourceSide = -1;
            _sourceColumn = _sourceRow = -1;
            _holdSide = -1;
            _holdColumn = _holdRow = -1;
            _holdProgress = -1f;
            if (IsOpen) Refresh();
        }

        internal void ShowHoldProgress(Side side, int column, int row, float progress)
        {
            if (!SecondScreenPreferences.ShowHoldProgress) progress = -1f;
            if (progress < 0f)
            {
                _holdSide = -1;
                _holdColumn = _holdRow = -1;
                _holdProgress = -1f;
            }
            else
            {
                _holdSide = (int)side;
                _holdColumn = column;
                _holdRow = row;
                _holdProgress = Mathf.Clamp01(progress);
            }
            Refresh();
        }

        private Inventory Resolve(Side side)
        {
            if (side == Side.Player)
                return Player.m_localPlayer != null
                    ? Player.m_localPlayer.GetInventory() : null;
            return _container != null ? _container.GetInventory() : null;
        }

        private bool SourceStillExists(Side side, int column, int row)
        {
            Inventory inventory = Resolve(side);
            return Valid(inventory, column, row)
                   && inventory.GetItemAt(column, row) != null;
        }

        private bool Locate(RectTransform root, Vector2 rootPoint, out Side side,
                            out int column, out int row)
        {
            if (LocateGrid(root, rootPoint, _playerGrid,
                           _playerWidth, _playerHeight, out column, out row))
            {
                side = Side.Player;
                return true;
            }
            if (LocateGrid(root, rootPoint, _containerGrid,
                           _containerWidth, _containerHeight, out column, out row))
            {
                side = Side.Container;
                return true;
            }
            side = Side.Player;
            column = row = -1;
            return false;
        }

        private bool LocateGrid(RectTransform root, Vector2 rootPoint,
                                RectTransform grid, int width, int height,
                                out int column, out int row)
        {
            column = row = -1;
            if (root == null || grid == null || width <= 0 || height <= 0) return false;
            Vector3 local = grid.InverseTransformPoint(root.TransformPoint(rootPoint));
            Rect rect = grid.rect;
            float x = local.x - rect.xMin;
            float y = rect.yMax - local.y;
            if (x < 0f || y < 0f) return false;
            int displayColumn = (int)(x / (_slotSize + SlotGap));
            int displayRow = (int)(y / (_slotSize + SlotGap));
            if (displayColumn < 0 || displayRow < 0
                || displayColumn >= DisplayColumns || displayRow >= DisplayRows)
                return false;
            float cellX = x - displayColumn * (_slotSize + SlotGap);
            float cellY = y - displayRow * (_slotSize + SlotGap);
            if (cellX > _slotSize || cellY > _slotSize)
            {
                column = row = -1;
                return false;
            }
            int index = displayRow * DisplayColumns + displayColumn;
            if (index >= width * height) return false;
            column = index % width;
            row = index / width;
            return true;
        }

        private static bool Valid(Inventory inventory, int column, int row)
        {
            return inventory != null && column >= 0 && row >= 0
                   && column < inventory.GetWidth() && row < inventory.GetHeight();
        }

        private static void PlayMoveEffect()
        {
            InventoryGui gui = InventoryGui.instance;
            Player player = Player.m_localPlayer;
            if (gui != null && player != null)
                gui.m_moveItemEffects.Create(player.transform.position, Quaternion.identity);
        }

        private static string Localize(string value)
        {
            if (string.IsNullOrEmpty(value)) return "CONTAINER";
            try
            {
                return Localization.instance != null
                    ? Localization.instance.Localize(value) : value;
            }
            catch { return value; }
        }
    }
}
