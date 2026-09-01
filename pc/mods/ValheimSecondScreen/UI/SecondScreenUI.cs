using System;
using System.Collections.Generic;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace ValheimSecondScreen.UI
{
    /// <summary>The permanent bronze-and-black dashboard on display one.</summary>
    internal sealed class SecondScreenUI
    {
        public const float ReferenceWidth = 1240f;
        public const float ReferenceHeight = 1080f;

        private const float Outer = 14f;
        private const float NavigationWidth = 162f;
        private const float Gap = 10f;

        private static readonly Color Backdrop = new Color(0.16f, 0.115f, 0.075f, 1f);
        internal static readonly Color PanelTint = UiTheme.Panel;
        private static readonly Color Card = PanelTint;
        private static readonly Color CardSelected = new Color(0.66f, 0.36f, 0.07f, 0.78f);
        private static readonly Color NavIdle = new Color(0.025f, 0.018f, 0.012f, 0.58f);
        private static readonly Color GoldText = new Color(0.91f, 0.78f, 0.57f, 1f);
        private static readonly Color SelectedGold = new Color(1f, 0.86f, 0.63f, 1f);
        private static readonly Color IdleIconGold = new Color(0.72f, 0.61f, 0.46f, 0.88f);

        private static TMP_FontAsset _sharedFont;

        public Canvas Canvas { get; private set; }
        public RectTransform Root { get; private set; }
        private GameObject _canvasGO;
        private Image _background;
        private Image _shade;

        public RectTransform LeftRail { get; private set; }
        public RectTransform RightRail { get; private set; }
        public RectTransform BottomBar { get; private set; }
        public RectTransform TopStrip { get; private set; }

        public RectTransform PageMap { get; private set; }
        public RectTransform PageGear { get; private set; }
        public RectTransform PageStats { get; private set; }
        public RectTransform Parked { get; private set; }

        private RectTransform _pageMapRoot;
        private RectTransform _mapToolbarRoot;
        private RectTransform _pageStatsRoot;
        private RectTransform _pageCraftingRoot;
        private RectTransform _pageTrophiesRoot;
        private RectTransform _pageSettingsRoot;
        private RectTransform _pageChatRoot;
        private RectTransform _pageContainerRoot;
        private RectTransform _inventoryPanel;
        private RectTransform _quickbarSlot;
        private RectTransform _statusPanel;
        private RectTransform _buffsPanel;
        private RectTransform _timePanel;
        private Vector2 _inventoryPanelPosition;
        private Vector2 _inventoryPanelSize;
        private bool _playerInventoryOpen;
        private int _tabBeforePlayerInventory = 1;

        public int ActiveTab { get; private set; }
        public bool MapGestureActive { get; private set; }
        public Vector2 PageSize { get; private set; }
        public Vector2 BarSize { get; private set; }
        public float PageCentreY { get; private set; }

        public InventoryPage Gear { get; private set; }
        public SkillsPage Stats { get; private set; }
        public HotbarStrip Hotbar { get; private set; }
        public Dashboard Dashboard { get; private set; }
        public CraftingPage Crafting { get; private set; }
        public TrophiesPage Trophies { get; private set; }
        public SettingsPage Settings { get; private set; }
        public ChatPage Chat { get; private set; }
        public MapToolbar MapTools { get; private set; }
        public ContainerPage Containers { get; private set; }

        private const int ContainerContextPage = 7;
        private int _tabBeforeContainer = 1;

        private sealed class NavEntry
        {
            public GameObject Root;
            public Button Button;
            public Image Frame;
            public Image Accent;
            public Image Shadow;
            public Image Icon;
            public RectTransform IconTransform;
            public TMP_Text Label;
            public string IconName;
            public bool NativeIcon;
            public GameObject? Notification;
            public int Target;
        }

        private readonly List<NavEntry> _navigation = new List<NavEntry>();
        private static readonly int[] NavigationOrder = { 1, 3, 0, 2, 4, 6, 5 };
        private float _nextNavigationIconRefresh;
        private int _navigationIconRefreshAttempts;

        public bool Build()
        {
            var gui = InventoryGui.instance;
            if (gui == null) return false;

            _navigation.Clear();
            _nextNavigationIconRefresh = 0f;
            _navigationIconRefreshAttempts = 0;
            Textures.BindNative(gui);
            _sharedFont = ResolveFont(gui);

            _canvasGO = new GameObject("SecondScreenCanvas");
            UnityEngine.Object.DontDestroyOnLoad(_canvasGO);
            Canvas = _canvasGO.AddComponent<Canvas>();
            Canvas.renderMode = RenderMode.ScreenSpaceOverlay;
            Canvas.targetDisplay = 1;
            Canvas.sortingOrder = 50;
            _canvasGO.AddComponent<TouchSelectionGuard>();

            var scaler = _canvasGO.AddComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scaler.referenceResolution = new Vector2(ReferenceWidth, ReferenceHeight);
            scaler.screenMatchMode = CanvasScaler.ScreenMatchMode.MatchWidthOrHeight;
            scaler.matchWidthOrHeight = 0.5f;
            _canvasGO.AddComponent<GraphicRaycaster>();

            Root = Fill(new GameObject("Panel", typeof(RectTransform), typeof(Image)),
                        _canvasGO.transform);
            _background = Root.GetComponent<Image>();
            UiSkin.Backdrop(_background, Backdrop);
            _background.raycastTarget = false;

            var shadeRect = Fill(new GameObject("LowerScreenTint",
                typeof(RectTransform), typeof(Image)), Root);
            _shade = shadeRect.GetComponent<Image>();
            _shade.color = Color.clear;
            _shade.raycastTarget = false;

            BuildFrame();
            BuildNavigation();

            Gear = new InventoryPage(_inventoryPanel, _sharedFont,
                                     _inventoryPanel.rect.size);
            Stats = new SkillsPage(PageStats, _sharedFont, PageSize);
            Hotbar = new HotbarStrip(_quickbarSlot, _sharedFont, BarSize);
            Dashboard = new Dashboard(_statusPanel, _buffsPanel,
                                      _timePanel, _sharedFont);
            Crafting = new CraftingPage(_pageCraftingRoot, _sharedFont,
                                        _pageCraftingRoot.rect.size);
            Trophies = new TrophiesPage(_pageTrophiesRoot, _sharedFont,
                                        _pageTrophiesRoot.rect.size);
            Settings = new SettingsPage(this, _pageSettingsRoot, _sharedFont,
                                        _pageSettingsRoot.rect.size);
            Chat = new ChatPage(_pageChatRoot, _sharedFont,
                                _pageChatRoot.rect.size);
            MapTools = new MapToolbar(_mapToolbarRoot, PageMap, _sharedFont,
                                      _mapToolbarRoot.rect.size);
            Containers = new ContainerPage(_pageContainerRoot, _sharedFont,
                                           _pageContainerRoot.rect.size);

            // Buttons are created by the individual pages, but their shape is
            // enough to choose the correct shared frame consistently.
            UiSkin.PageSurface(PageGear);
            UiSkin.PageSurface(_pageMapRoot);
            UiSkin.PageSurface(_pageStatsRoot);
            UiSkin.PageSurface(_pageCraftingRoot);
            UiSkin.PageSurface(_pageTrophiesRoot);
            UiSkin.PageSurface(_pageSettingsRoot);
            UiSkin.PageSurface(_pageChatRoot);
            UiSkin.PageSurface(_pageContainerRoot);
            UiSkin.ControlsUnder(Root);

            ApplyPreferences();
            SetTab(SecondScreenPreferences.InitialTab);
            return true;
        }

        public void Destroy()
        {
            CancelDrag();
            Crafting?.Cancel();
            if (_canvasGO != null) UnityEngine.Object.Destroy(_canvasGO);
            _canvasGO = null;
            Canvas = null;
            Root = null;
            _navigation.Clear();
            _playerInventoryOpen = false;
            MapGestureActive = false;
        }

        public void SetVisible(bool visible)
        {
            if (_canvasGO != null && _canvasGO.activeSelf != visible)
                _canvasGO.SetActive(visible);
        }

        private void BuildFrame()
        {
            var outerFrame = Absolute(Root, "OuterFrame",
                new Vector2(Outer, Outer),
                new Vector2(ReferenceWidth - Outer, ReferenceHeight - Outer));

            LeftRail = AbsolutePanel(outerFrame, "Navigation",
                new Vector2(7f, 7f),
                new Vector2(7f + NavigationWidth, ReferenceHeight - Outer * 2f - 7f),
                Card);

            float contentLeft = 7f + NavigationWidth + Gap;
            float contentRight = ReferenceWidth - Outer * 2f - 7f;
            float contentTop = ReferenceHeight - Outer * 2f - 7f;
            var content = Absolute(outerFrame, "Content",
                new Vector2(contentLeft, 7f), new Vector2(contentRight, contentTop));

            float contentWidth = contentRight - contentLeft;
            float contentHeight = contentTop - 7f;
            PageCentreY = 0f;

            PageGear = FullPage(content, "InventoryDashboard");

            // Four columns by eight rows make useful use of the tall inventory
            // card.  The narrower left column also leaves the live status and
            // buff cards enough room to remain readable at the Thor's native
            // lower-screen resolution.
            const float leftWidth = 350f;
            float rightX = leftWidth + Gap;
            float rightWidth = contentWidth - rightX;
            const float lowerHeight = 260f;
            const float quickHeight = 220f;
            float middleY = lowerHeight + Gap;
            float quickY = contentHeight - quickHeight;
            float middleHeight = quickY - Gap - middleY;

            _inventoryPanel = AbsolutePanel(PageGear, "InventoryCard",
                Vector2.zero, new Vector2(leftWidth, quickY - Gap), Card);
            _inventoryPanelPosition = _inventoryPanel.anchoredPosition;
            _inventoryPanelSize = _inventoryPanel.sizeDelta;
            BottomBar = AbsolutePanel(PageGear, "QuickbarCard",
                new Vector2(0f, quickY),
                new Vector2(contentWidth, contentHeight), Card);
            UiSkin.HeaderWash(_inventoryPanel);
            UiSkin.HeaderWash(BottomBar, 54f);

            float half = (rightWidth - Gap) * 0.5f;
            _statusPanel = AbsolutePanel(PageGear, "StatusCard",
                new Vector2(rightX, middleY),
                new Vector2(rightX + half, middleY + middleHeight), Card);
            _buffsPanel = AbsolutePanel(PageGear, "BuffsCard",
                new Vector2(rightX + half + Gap, middleY),
                new Vector2(rightX + rightWidth, middleY + middleHeight), Card);
            RightRail = _buffsPanel;
            _timePanel = AbsolutePanel(PageGear, "TimeCard",
                new Vector2(rightX, 0f),
                new Vector2(rightX + rightWidth, lowerHeight), Card);
            UiSkin.HeaderWash(_statusPanel);
            UiSkin.HeaderWash(_buffsPanel);
            UiSkin.HeaderWash(_timePanel);

            _quickbarSlot = Inset(BottomBar, "Quickbar", 18f, 12f, 18f, 58f);
            BarSize = new Vector2(contentWidth - 36f, quickHeight - 70f);

            var quickTitle = MakeText(BottomBar, "Title", "QUICKBAR", 31f, GoldText);
            TitleAtTop(quickTitle, 50f);

            _pageMapRoot = FullPage(content, "MapPage");
            PageMap = Inset(_pageMapRoot, "MapViewport", 12f, 108f, 12f, 12f);
            _mapToolbarRoot = AbsolutePanel(_pageMapRoot, "MapToolbar",
                new Vector2(12f, 12f), new Vector2(contentWidth - 12f, 100f), Card);
            PageSize = new Vector2(contentWidth - 24f, contentHeight - 24f);

            _pageStatsRoot = FullPage(content, "SkillsPage");
            PageStats = Inset(_pageStatsRoot, "SkillsContent", 14f, 14f, 14f, 14f);

            _pageCraftingRoot = FullPage(content, "CraftingPage");
            _pageTrophiesRoot = FullPage(content, "TrophiesPage");
            _pageSettingsRoot = FullPage(content, "SettingsPage");
            _pageChatRoot = FullPage(content, "ChatPage");
            _pageContainerRoot = FullPage(content, "ContainerPage");

            TopStrip = Absolute(content, "TransientHud",
                new Vector2(12f, contentHeight - 80f),
                new Vector2(contentWidth - 12f, contentHeight - 12f));

            Parked = Absolute(Root, "Parked", Vector2.zero, Vector2.one);
            Parked.gameObject.SetActive(false);
        }

        private void BuildNavigation()
        {
            string[] labels =
                { "INVENTORY", "CRAFTING", "MAP", "SKILLS", "TROPHIES", "CHAT", "SETTINGS" };
            string[] icons =
                { "inventory", "crafting", "map", "skills", "trophies", "chat", "settings" };
            float usableHeight = LeftRail.rect.height - 22f;
            float buttonSize = Mathf.Min(LeftRail.rect.width - 16f,
                (usableHeight - Gap * (labels.Length - 1)) / labels.Length);
            buttonSize = Mathf.Max(80f, buttonSize);
            float spacing = Mathf.Max(0f,
                (usableHeight - buttonSize * labels.Length)
                / Mathf.Max(1, labels.Length - 1));

            for (int i = 0; i < labels.Length; i++)
            {
                int target = NavigationOrder[i];
                var go = new GameObject("Nav_" + labels[i], typeof(RectTransform),
                                        typeof(Image), typeof(Button));
                var rect = (RectTransform)go.transform;
                rect.SetParent(LeftRail, false);
                rect.anchorMin = new Vector2(0.5f, 1f);
                rect.anchorMax = new Vector2(0.5f, 1f);
                rect.pivot = new Vector2(0.5f, 1f);
                rect.sizeDelta = new Vector2(buttonSize, buttonSize);
                rect.anchoredPosition = new Vector2(
                    0f, -11f - i * (buttonSize + spacing));

                var frame = go.GetComponent<Image>();
                UiSkin.Square(frame, NavIdle);
                var accent = UiSkin.SelectionAccent(rect);

                var shadowObject = new GameObject("IconShadow",
                    typeof(RectTransform), typeof(Image));
                var shadowRect = (RectTransform)shadowObject.transform;
                shadowRect.SetParent(rect, false);
                shadowRect.anchorMin = new Vector2(0.5f, 0.5f);
                shadowRect.anchorMax = new Vector2(0.5f, 0.5f);
                shadowRect.pivot = new Vector2(0.5f, 0.5f);
                float iconSize = Mathf.Clamp(buttonSize * 0.56f, 64f, 82f);
                shadowRect.sizeDelta = new Vector2(iconSize, iconSize);
                shadowRect.anchoredPosition = new Vector2(2f, 4f);
                var shadow = shadowObject.GetComponent<Image>();
                Sprite navSprite = UiIcons.GetPageIcon(icons[i]);
                bool nativeIcon = UiIcons.IsNative(navSprite);
                shadow.sprite = navSprite;
                shadow.color = new Color(0f, 0f, 0f, 0.52f);
                shadow.preserveAspect = true;
                shadow.raycastTarget = false;

                var iconObject = new GameObject("Icon", typeof(RectTransform), typeof(Image));
                var iconRect = (RectTransform)iconObject.transform;
                iconRect.SetParent(rect, false);
                iconRect.anchorMin = new Vector2(0.5f, 0.5f);
                iconRect.anchorMax = new Vector2(0.5f, 0.5f);
                iconRect.pivot = new Vector2(0.5f, 0.5f);
                iconRect.sizeDelta = new Vector2(iconSize, iconSize);
                iconRect.anchoredPosition = new Vector2(0f, 7f);
                var icon = iconObject.GetComponent<Image>();
                icon.sprite = navSprite;
                icon.color = nativeIcon
                    ? new Color(0.88f, 0.84f, 0.76f, 0.96f) : IdleIconGold;
                icon.preserveAspect = true;
                icon.raycastTarget = false;

                var label = MakeText(rect, "Label", labels[i], 16f,
                    IdleIconGold, TextAlignmentOptions.Center);
                var labelRect = label.rectTransform;
                labelRect.anchorMin = new Vector2(0f, 0f);
                labelRect.anchorMax = new Vector2(1f, 0f);
                labelRect.pivot = new Vector2(0.5f, 0f);
                labelRect.anchoredPosition = new Vector2(0f, 8f);
                labelRect.sizeDelta = new Vector2(-12f, 25f);
                label.fontStyle = FontStyles.Bold;
                FitText(label, 10f, 16f);

                var button = go.GetComponent<Button>();
                button.targetGraphic = frame;
                button.transition = Selectable.Transition.None;
                button.navigation = new Navigation { mode = Navigation.Mode.None };
                button.onClick.AddListener(() => SetTab(target));

                GameObject? notification = null;
                if (target == 6)
                {
                    notification = new GameObject("Unread",
                        typeof(RectTransform), typeof(Image));
                    var noteRect = (RectTransform)notification.transform;
                    noteRect.SetParent(rect, false);
                    noteRect.anchorMin = noteRect.anchorMax = new Vector2(1f, 1f);
                    noteRect.pivot = new Vector2(1f, 1f);
                    noteRect.anchoredPosition = new Vector2(-9f, -9f);
                    noteRect.sizeDelta = new Vector2(19f, 19f);
                    var note = notification.GetComponent<Image>();
                    UiSkin.Plain(note, new Color(0.96f, 0.47f, 0.12f, 1f));
                    note.raycastTarget = false;
                    notification.SetActive(false);
                }

                _navigation.Add(new NavEntry
                {
                    Root = go,
                    Button = button,
                    Frame = frame,
                    Accent = accent,
                    Shadow = shadow,
                    Icon = icon,
                    IconTransform = iconRect,
                    Label = label,
                    IconName = icons[i],
                    NativeIcon = nativeIcon,
                    Notification = notification,
                    Target = target
                });
            }
        }

        public void SetTab(int tab)
        {
            // A container is a modal game context. Keep its transfer surface on
            // the lower display until Valheim closes the actual container.
            if (Containers != null && Containers.IsOpen) return;

            ActiveTab = Mathf.Clamp(tab, 0, 6);
            MapGestureActive = false;
            Gear?.CancelDrag();

            if (_pageMapRoot != null) _pageMapRoot.gameObject.SetActive(ActiveTab == 0);
            if (PageGear != null) PageGear.gameObject.SetActive(ActiveTab == 1);
            if (_pageStatsRoot != null) _pageStatsRoot.gameObject.SetActive(ActiveTab == 2);
            if (_pageCraftingRoot != null) _pageCraftingRoot.gameObject.SetActive(ActiveTab == 3);
            if (_pageTrophiesRoot != null) _pageTrophiesRoot.gameObject.SetActive(ActiveTab == 4);
            if (_pageSettingsRoot != null) _pageSettingsRoot.gameObject.SetActive(ActiveTab == 5);
            if (_pageChatRoot != null) _pageChatRoot.gameObject.SetActive(ActiveTab == 6);
            if (_pageContainerRoot != null) _pageContainerRoot.gameObject.SetActive(false);

            PaintNavigation(ActiveTab);

            if (ActiveTab == 1) Gear?.Refresh();
            if (ActiveTab == 2) Stats?.Refresh();
            if (ActiveTab == 3) Crafting?.Refresh();
            if (ActiveTab == 4) Trophies?.Refresh(true);
            if (ActiveTab == 5) Settings?.Refresh(true);
            if (ActiveTab == 6) Chat?.Refresh(true);

            if (ActiveTab >= 0 && ActiveTab <= 6
                && SecondScreenPreferences.RememberLastTab)
                SecondScreenPreferences.LastTab = ActiveTab;
        }

        /// <summary>
        /// Presents the player's controller-opened inventory as a full content
        /// page while keeping the navigation rail available.
        /// </summary>
        public void OpenPlayerInventory()
        {
            if (Containers != null && Containers.IsOpen) return;
            if (!_playerInventoryOpen)
                _tabBeforePlayerInventory = ActiveTab >= 0 && ActiveTab <= 6
                    ? ActiveTab : 1;
            _playerInventoryOpen = true;
            SetInventoryExpanded(true);
            SetTab(1);
        }

        public void ClosePlayerInventory()
        {
            if (!_playerInventoryOpen) return;
            _playerInventoryOpen = false;
            SetInventoryExpanded(false);
            if (Containers == null || !Containers.IsOpen)
                SetTab(Mathf.Clamp(_tabBeforePlayerInventory, 0, 6));
        }

        private void SetInventoryExpanded(bool expanded)
        {
            if (_inventoryPanel == null || PageGear == null) return;
            _inventoryPanel.anchoredPosition = expanded
                ? Vector2.zero : _inventoryPanelPosition;
            _inventoryPanel.sizeDelta = expanded
                ? PageGear.rect.size : _inventoryPanelSize;

            if (BottomBar != null) BottomBar.gameObject.SetActive(!expanded);
            if (_statusPanel != null) _statusPanel.gameObject.SetActive(!expanded);
            if (_buffsPanel != null) _buffsPanel.gameObject.SetActive(!expanded);
            if (_timePanel != null) _timePanel.gameObject.SetActive(!expanded);

            Gear?.SetExpanded(expanded, _inventoryPanel.rect.size);
        }

        /// <summary>Shows the context-only, two-inventory container surface.</summary>
        public void OpenContainer(Container container)
        {
            if (container == null || _pageContainerRoot == null || Containers == null)
                return;

            if (_playerInventoryOpen)
            {
                _playerInventoryOpen = false;
                SetInventoryExpanded(false);
            }
            if (!Containers.IsOpen) _tabBeforeContainer = ActiveTab;
            MapGestureActive = false;
            Gear?.CancelDrag();
            Containers.Open(container);
            ActiveTab = ContainerContextPage;

            if (_pageMapRoot != null) _pageMapRoot.gameObject.SetActive(false);
            if (PageGear != null) PageGear.gameObject.SetActive(false);
            if (_pageStatsRoot != null) _pageStatsRoot.gameObject.SetActive(false);
            if (_pageCraftingRoot != null) _pageCraftingRoot.gameObject.SetActive(false);
            if (_pageTrophiesRoot != null) _pageTrophiesRoot.gameObject.SetActive(false);
            if (_pageSettingsRoot != null) _pageSettingsRoot.gameObject.SetActive(false);
            if (_pageChatRoot != null) _pageChatRoot.gameObject.SetActive(false);
            _pageContainerRoot.gameObject.SetActive(true);

            PaintNavigation(-1);
            Containers.Refresh();
        }

        private void PaintNavigation(int selectedTab)
        {
            bool modal = selectedTab == -1;
            foreach (var entry in _navigation)
            {
                bool selected = entry.Target == selectedTab;
                if (entry.Button != null) entry.Button.interactable = !modal;
                entry.Frame.color = modal ? UiTheme.ControlDisabled
                    : selected ? CardSelected : NavIdle;
                UiSkin.SetSelected(entry.Accent, selected);
                if (entry.Icon != null)
                    entry.Icon.color = modal
                        ? new Color(0.46f, 0.43f, 0.38f, 0.56f)
                        : entry.NativeIcon
                        ? (selected ? Color.white
                            : new Color(0.88f, 0.84f, 0.76f, 0.96f))
                        : (selected ? SelectedGold : IdleIconGold);
                if (entry.Label != null)
                    entry.Label.color = modal
                        ? new Color(0.46f, 0.43f, 0.38f, 0.56f)
                        : selected ? SelectedGold : IdleIconGold;
                if (entry.Shadow != null)
                    entry.Shadow.color = modal
                        ? new Color(0f, 0f, 0f, 0.28f)
                        : selected
                        ? new Color(0.16f, 0.08f, 0.01f, 0.70f)
                        : new Color(0f, 0f, 0f, 0.52f);
                if (entry.IconTransform != null)
                    entry.IconTransform.localScale = selected
                        ? new Vector3(1.06f, 1.06f, 1f) : Vector3.one;
            }
        }

        /// <summary>Leaves container context and restores the prior normal tab.</summary>
        public void CloseContainer()
        {
            if (Containers == null || !Containers.IsOpen) return;
            Containers.Close();
            SetTab(Mathf.Clamp(_tabBeforeContainer, 0, 6));
        }

        public bool Tap(float fractionX, float fractionY)
        {
            if (!TryRootPoint(fractionX, fractionY, out var rootPoint)) return false;

            if (ActiveTab == ContainerContextPage && Containers != null)
                return Containers.Tap(Root, rootPoint);

            foreach (var entry in _navigation)
            {
                if (Contains(entry.Root.transform as RectTransform, rootPoint))
                {
                    SetTab(entry.Target);
                    return true;
                }
            }

            if (ActiveTab == 1)
            {
                if (Hotbar != null && Hotbar.Tap(Root, rootPoint)) return true;
                if (Gear != null && TryGearPoint(rootPoint, out var pagePoint))
                    return Gear.Tap(pagePoint);
            }
            else if (ActiveTab == 0 && MapTools != null)
            {
                return MapTools.Tap(Root, rootPoint);
            }
            else if (ActiveTab == 3 && Crafting != null)
            {
                return Crafting.Tap(Root, rootPoint);
            }
            else if (ActiveTab == 2 && Stats != null)
            {
                return Stats.Tap(Root, rootPoint);
            }
            else if (ActiveTab == 4 && Trophies != null)
            {
                return Trophies.Tap(Root, rootPoint);
            }
            else if (ActiveTab == 5 && Settings != null)
            {
                return Settings.Tap(Root, rootPoint);
            }
            else if (ActiveTab == 6 && Chat != null)
            {
                return Chat.Tap(Root, rootPoint);
            }
            return false;
        }

        public bool BeginDrag(float fractionX, float fractionY)
        {
            if (!TryRootPoint(fractionX, fractionY, out var rootPoint)) return false;
            if (ActiveTab == ContainerContextPage && Containers != null)
                return Containers.BeginDrag(Root, rootPoint);
            if (ActiveTab == 0)
            {
                MapGestureActive = Contains(PageMap, rootPoint);
                return MapGestureActive;
            }
            return ActiveTab == 1 && Gear != null
                   && TryGearPoint(rootPoint, out var pagePoint)
                   && Gear.BeginDrag(pagePoint);
        }

        public bool Hold(float fractionX, float fractionY, float duration)
        {
            if (duration < SecondScreenPreferences.HoldSeconds)
                return Tap(fractionX, fractionY);
            if (ActiveTab == ContainerContextPage && Containers != null
                && TryRootPoint(fractionX, fractionY, out var containerPoint))
                return Containers.Use(Root, containerPoint);
            if (ActiveTab != 1 || Gear == null
                || !TryRootPoint(fractionX, fractionY, out var rootPoint)
                || !Contains(_inventoryPanel, rootPoint)
                || !TryGearPoint(rootPoint, out var pagePoint))
                return Tap(fractionX, fractionY);
            return Gear.Use(pagePoint);
        }

        public bool EndDrag(float fractionX, float fractionY)
        {
            if (ActiveTab == ContainerContextPage && Containers != null)
                return TryRootPoint(fractionX, fractionY, out var containerPoint)
                       && Containers.EndDrag(Root, containerPoint);
            if (ActiveTab == 0)
            {
                bool wasMap = MapGestureActive;
                MapGestureActive = false;
                return wasMap;
            }
            return ActiveTab == 1 && Gear != null
                   && TryRootPoint(fractionX, fractionY, out var rootPoint)
                   && TryGearPoint(rootPoint, out var pagePoint)
                   && Gear.EndDrag(pagePoint);
        }

        public void CancelDrag()
        {
            MapGestureActive = false;
            Gear?.CancelDrag();
            Containers?.CancelDrag();
        }

        public void CycleTab(int direction)
        {
            if (Containers != null && Containers.IsOpen) return;
            int index = Array.IndexOf(NavigationOrder, ActiveTab);
            if (index < 0) index = 0;
            index = ((index + direction) % NavigationOrder.Length + NavigationOrder.Length)
                    % NavigationOrder.Length;
            SetTab(NavigationOrder[index]);
        }

        /// <summary>
        /// How often the panel's contents are rebuilt, in seconds.
        /// </summary>
        /// <remarks>
        /// <para>
        /// This was every frame, and it is the single most expensive thing the
        /// mod does. Every refresh writes text and swaps sprites, and any change
        /// to a Unity UI element marks its canvas dirty — so the whole panel was
        /// being re-batched and re-uploaded 60 times a second to show numbers
        /// that change a handful of times a second at most.
        /// </para>
        /// <para>
        /// It is worse than an ordinary UI's cost, because this canvas is on a
        /// second Unity display: rebuilding it makes Unity render and present
        /// another swapchain, through DXVK, the compositor and SurfaceFlinger,
        /// on a GPU the game already saturates. The renderer's own counters show
        /// the price — two presents for every one of the game's.
        /// </para>
        /// <para>
        /// A fifteenth of a second is under the threshold at which a glanced-at
        /// readout looks stale — including the health and stamina bars, which
        /// are the most time-critical thing here — and is four times less work.
        /// </para>
        /// </remarks>
        private const float RefreshInterval = 1f / 15f;

        private float _nextRefresh;

        public void Tick()
        {
            float now = Time.unscaledTime;
            if (now < _nextRefresh) return;
            _nextRefresh = now + RefreshInterval;

            RefreshNavigationIcons(now);

            // Hidden pages stay genuinely idle. The map used to walk every pin
            // and dirty its material at 15 Hz even while another page covered
            // it, which still rebuilt work for the second-display canvas.
            if (ActiveTab == 0)
            {
                LargeMap.Tick();
                MapTools?.Tick();
            }

            // A craft can finish while its page is hidden. Crafting.Tick keeps
            // only that pending operation alive and otherwise returns at once.
            Crafting?.Tick();
            if (ActiveTab == 4) Trophies?.Tick();
            if (ActiveTab == 5) Settings?.Tick();
            Chat?.Poll(ActiveTab == 6);
            foreach (var entry in _navigation)
                if (entry.Target == 6 && entry.Notification != null)
                    entry.Notification.SetActive(Chat != null && Chat.HasUnread);
            if (ActiveTab == ContainerContextPage)
            {
                if (Containers != null && Containers.IsOpen
                    && (InventoryGui.instance == null
                        || !InventoryGui.instance.IsContainerOpen()))
                    CloseContainer();
                else
                    Containers?.Refresh();
            }
            else if (ActiveTab == 1)
            {
                Hotbar?.Refresh();
                Gear?.Refresh();
                Dashboard?.Refresh();
            }
            else if (ActiveTab == 2)
            {
                Stats?.Refresh();
            }
        }

        /// <summary>
        /// Replaces startup fallbacks once ObjectDB, Minimap and player skills
        /// have published their native sprites.  The retry is cheap, bounded,
        /// and stops permanently once every navigation icon is native.
        /// </summary>
        private void RefreshNavigationIcons(float now)
        {
            if (now < _nextNavigationIconRefresh || _navigation.Count == 0) return;
            _nextNavigationIconRefresh = now + 2f;

            // Sprite discovery can walk Unity's loaded resources. Never do it
            // while the game databases are still coming online, and cap the
            // ready-state retries so this polish path cannot become a polling
            // cost in a modded session with no semantic icon asset.
            if (ObjectDB.instance == null || Minimap.instance == null
                || Player.m_localPlayer == null) return;
            if (_navigationIconRefreshAttempts++ >= 2)
            {
                _nextNavigationIconRefresh = float.PositiveInfinity;
                return;
            }

            bool unresolved = false;
            bool changed = false;
            foreach (NavEntry entry in _navigation)
            {
                if (entry.NativeIcon) continue;
                Sprite sprite = UiIcons.GetPageIcon(entry.IconName);
                if (sprite == null || !UiIcons.IsNative(sprite))
                {
                    unresolved = true;
                    continue;
                }

                entry.NativeIcon = true;
                if (entry.Icon != null) entry.Icon.sprite = sprite;
                if (entry.Shadow != null) entry.Shadow.sprite = sprite;
                changed = true;
            }

            if (!unresolved || _navigationIconRefreshAttempts >= 2)
                _nextNavigationIconRefresh = float.PositiveInfinity;
            if (changed) PaintNavigation(ActiveTab == ContainerContextPage ? -1 : ActiveTab);
        }

        public void ApplyPreferences()
        {
            float brightness = SecondScreenPreferences.BackgroundBrightness;
            if (_background != null)
            {
                _background.color = new Color(
                    Mathf.Clamp01(Backdrop.r * brightness),
                    Mathf.Clamp01(Backdrop.g * brightness),
                    Mathf.Clamp01(Backdrop.b * brightness), Backdrop.a);
                UiSkin.SetArtworkBrightness(_background, 0.62f * brightness);
            }
            if (_shade != null)
                _shade.color = new Color(0f, 0f, 0f, SecondScreenPreferences.Shade);
        }

        private bool TryRootPoint(float x, float y, out Vector2 point)
        {
            point = Vector2.zero;
            if (Root == null || float.IsNaN(x) || float.IsNaN(y)
                || x < 0f || x > 1f || y < 0f || y > 1f) return false;
            var rect = Root.rect;
            if (rect.width <= 0f || rect.height <= 0f) return false;
            point = new Vector2(Mathf.Lerp(rect.xMin, rect.xMax, x),
                                Mathf.Lerp(rect.yMax, rect.yMin, y));
            return true;
        }

        private bool Contains(RectTransform target, Vector2 rootPoint)
        {
            if (Root == null || target == null || !target.gameObject.activeInHierarchy)
                return false;
            Vector3 local = target.InverseTransformPoint(Root.TransformPoint(rootPoint));
            return target.rect.Contains(new Vector2(local.x, local.y));
        }

        private bool TryGearPoint(Vector2 rootPoint, out Vector2 pagePoint)
        {
            pagePoint = Vector2.zero;
            if (Root == null || _inventoryPanel == null) return false;
            Vector3 local = _inventoryPanel.InverseTransformPoint(Root.TransformPoint(rootPoint));
            pagePoint = new Vector2(local.x, local.y);
            return true;
        }

        internal static TMP_Text MakeText(Transform parent, string name, string content,
                                          float size, Color colour,
                                          TextAlignmentOptions align = TextAlignmentOptions.Center)
        {
            var go = new GameObject(name, typeof(RectTransform));
            go.SetActive(false);
            go.transform.SetParent(parent, false);
            var value = go.AddComponent<TextMeshProUGUI>();
            var font = _sharedFont ?? ResolveFont(null);
            if (font != null) value.font = font;
            value.text = content;
            value.fontSize = size;
            value.color = colour;
            value.alignment = align;
            value.raycastTarget = false;
            value.enableWordWrapping = false;
            var rect = (RectTransform)go.transform;
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.one;
            rect.offsetMin = Vector2.zero;
            rect.offsetMax = Vector2.zero;
            go.SetActive(true);
            TrySetOutline(value, 0.055f, new Color32(8, 5, 3, 196));
            return value;
        }

        /// <summary>
        /// TMP can leave a newly-created label without a material until its
        /// first enable when Valheim is still constructing InventoryGui. Its
        /// outline setters dereference that material, so decoration must stay
        /// optional rather than aborting the entire lower-screen build.
        /// </summary>
        internal static void TrySetOutline(TMP_Text text, float width, Color32 colour)
        {
            if (text == null || text.font == null) return;
            try
            {
                text.outlineColor = colour;
                text.outlineWidth = width;
            }
            catch (NullReferenceException)
            {
                // The label itself remains valid; TMP will assign its material
                // on a later canvas rebuild, just without this optional outline.
            }
        }

        /// <summary>
        /// Keeps dynamic and localized text inside its assigned rectangle.
        /// The upper bound preserves the intended visual hierarchy while the
        /// lower bound prevents a long label from becoming unreadably tiny.
        /// </summary>
        internal static void FitText(TMP_Text text, float minimum, float maximum,
                                     bool wrap = false)
        {
            if (text == null) return;
            text.enableAutoSizing = true;
            text.fontSizeMin = Mathf.Max(7f, minimum);
            text.fontSizeMax = Mathf.Max(text.fontSizeMin, maximum);
            text.enableWordWrapping = wrap;
            text.overflowMode = wrap
                ? TextOverflowModes.Truncate : TextOverflowModes.Ellipsis;
        }

        private static TMP_FontAsset ResolveFont(InventoryGui gui)
        {
            if (_sharedFont != null) return _sharedFont;

            if (gui != null)
            {
                var buttonText = gui.m_craftButton != null
                    ? gui.m_craftButton.GetComponentInChildren<TMP_Text>(true)
                    : null;
                if (buttonText != null && buttonText.font != null)
                    return _sharedFont = buttonText.font;
                if (gui.m_recipeName != null && gui.m_recipeName.font != null)
                    return _sharedFont = gui.m_recipeName.font;
                if (gui.m_recipeDecription != null && gui.m_recipeDecription.font != null)
                    return _sharedFont = gui.m_recipeDecription.font;
            }

            foreach (var text in Resources.FindObjectsOfTypeAll<TMP_Text>())
                if (text != null && text.font != null)
                    return _sharedFont = text.font;

            foreach (var candidate in Resources.FindObjectsOfTypeAll<TMP_FontAsset>())
                if (candidate != null)
                    return _sharedFont = candidate;
            return null;
        }

        private static RectTransform FullPage(RectTransform parent, string name)
        {
            var go = new GameObject(name, typeof(RectTransform));
            return Fill(go, parent);
        }

        private static RectTransform Absolute(Transform parent, string name,
                                              Vector2 min, Vector2 max)
        {
            var go = new GameObject(name, typeof(RectTransform));
            var rect = (RectTransform)go.transform;
            rect.SetParent(parent, false);
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.zero;
            rect.pivot = Vector2.zero;
            rect.anchoredPosition = min;
            rect.sizeDelta = max - min;
            return rect;
        }

        private static RectTransform AbsolutePanel(Transform parent, string name,
                                                   Vector2 min, Vector2 max, Color tint)
        {
            var rect = Absolute(parent, name, min, max);
            rect.gameObject.AddComponent<Image>();
            StylePanel(rect.gameObject, tint);
            return rect;
        }

        /*
         * A nested Canvas per card was here and has been taken out.
         *
         * It is the right optimisation on paper -- Unity batches a canvas as a
         * unit, so one changed label rebuilds every element beside it -- but it
         * went in at the same time as the refresh throttle and the game then
         * stopped reaching its first logged line. Of the three changes it is the
         * only one that alters the UI's structure rather than how often it is
         * read, so it is the one that goes until it can be put back on its own
         * and watched.
         */

        private static RectTransform Inset(RectTransform parent, string name,
                                           float left, float bottom, float right, float top)
        {
            var go = new GameObject(name, typeof(RectTransform));
            var rect = (RectTransform)go.transform;
            rect.SetParent(parent, false);
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.one;
            rect.offsetMin = new Vector2(left, bottom);
            rect.offsetMax = new Vector2(-right, -top);
            return rect;
        }

        private static void StylePanel(GameObject go, Color tint)
        {
            var image = go.GetComponent<Image>() ?? go.AddComponent<Image>();
            UiSkin.Panel((RectTransform)go.transform,
                raised: tint.a >= UiTheme.PanelRaised.a - 0.01f);
            image.color = tint;
            image.raycastTarget = false;
        }

        private static RectTransform Fill(GameObject go, Transform parent)
        {
            var rect = (RectTransform)go.transform;
            rect.SetParent(parent, false);
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.one;
            rect.offsetMin = Vector2.zero;
            rect.offsetMax = Vector2.zero;
            return rect;
        }

        private static void TitleAtTop(TMP_Text title, float height)
        {
            title.fontStyle = FontStyles.Bold;
            var rect = (RectTransform)title.transform;
            rect.anchorMin = new Vector2(0f, 1f);
            rect.anchorMax = new Vector2(1f, 1f);
            rect.pivot = new Vector2(0.5f, 1f);
            rect.sizeDelta = new Vector2(0f, height);
            rect.anchoredPosition = Vector2.zero;
        }
    }
}
