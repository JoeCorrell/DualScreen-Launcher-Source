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
    /// A touch-first view over Valheim's real crafting rules and completion path.
    /// </summary>
    /// <remarks>
    /// The list and detail panel are ours because the stock InventoryGui cannot be
    /// shown on display one without also opening it on the game display.  The act
    /// of crafting is deliberately not reimplemented: after the lower-screen
    /// timer finishes, the private InventoryGui.DoCrafting method is invoked with
    /// the selected recipe, item, variant and multiplier.  That keeps station and
    /// DLC checks, resource consumption, upgrades, bonus crafts, effects, skill
    /// gain and profile statistics on Valheim's own code path.
    /// </remarks>
    internal sealed class CraftingPage
    {
        private const int VisibleRecipes = 10;
        private const int RequirementRows = 8;

        private static readonly Color Card = UiTheme.Panel;
        private static readonly Color CardStrong = UiTheme.PanelRaised;
        private static readonly Color Idle = UiTheme.Control;
        private static readonly Color Selected = UiTheme.ControlSelected;
        private static readonly Color Disabled = UiTheme.ControlDisabled;
        private static readonly Color Gold = UiTheme.Gold;
        private static readonly Color Text = UiTheme.Text;
        private static readonly Color Weak = UiTheme.Muted;
        private static readonly Color Good = UiTheme.Good;
        private static readonly Color Bad = UiTheme.Bad;

        private static readonly FieldInfo CraftRecipeField =
            AccessTools.Field(typeof(InventoryGui), "m_craftRecipe");
        private static readonly FieldInfo CraftUpgradeField =
            AccessTools.Field(typeof(InventoryGui), "m_craftUpgradeItem");
        private static readonly FieldInfo CraftVariantField =
            AccessTools.Field(typeof(InventoryGui), "m_craftVariant");
        private static readonly FieldInfo MultiCraftField =
            AccessTools.Field(typeof(InventoryGui), "m_multiCrafting");
        private static readonly FieldInfo StockCraftTimerField =
            AccessTools.Field(typeof(InventoryGui), "m_craftTimer");
        private static readonly MethodInfo DoCraftingMethod =
            AccessTools.Method(typeof(InventoryGui), "DoCrafting", new[] { typeof(Player) });
        private static readonly MethodInfo RepairOneMethod =
            AccessTools.Method(typeof(InventoryGui), "RepairOneItem");
        private static readonly MethodInfo HaveRepairableMethod =
            AccessTools.Method(typeof(InventoryGui), "HaveRepairableItems");

        private readonly RectTransform _page;
        private readonly TMP_FontAsset _font;
        private readonly Vector2 _size;
        private readonly List<Entry> _entries = new List<Entry>();
        private List<Recipe> _recipes = new List<Recipe>();
        private readonly List<ItemDrop.ItemData> _upgradeItems =
            new List<ItemDrop.ItemData>();
        private readonly List<RecipeRow> _recipeRows = new List<RecipeRow>();
        private readonly List<RequirementRow> _requirementRows =
            new List<RequirementRow>();
        private readonly List<Button> _touchButtons = new List<Button>();

        private RectTransform _listCard;
        private RectTransform _detailCard;
        private RectTransform _descriptionMask;
        private TMP_Text _station;
        private TMP_Text _listHeading;
        private TMP_Text _name;
        private TMP_Text _description;
        private TMP_Text _quality;
        private TMP_Text _requirementsHeading;
        private TMP_Text _status;
        private TMP_Text _variantReadout;
        private TMP_Text _multiReadout;
        private TMP_Text _craftLabel;
        private TMP_Text _repairLabel;
        private RectTransform _statusFrame;
        private Image _itemIcon;
        private Button _newButton;
        private Button _upgradeButton;
        private Image _newAccent;
        private Image _upgradeAccent;
        private Button _repairButton;
        private Button _previousPageButton;
        private Button _nextPageButton;
        private Button _variantPrevious;
        private Button _variantNext;
        private Button _multiPrevious;
        private Button _multiNext;
        private Button _craftButton;
        private float _craftFullWidth;
        private float _craftQuantityWidth;
        private float _detailControlsTop;
        private float _detailDescriptionTop;
        private float _requirementHeight;
        private float _detailWidth;

        private bool _upgradeMode;
        private bool _multiCraft;
        private int _selectedIndex = -1;
        private int _firstIndex;
        private int _variant;
        private float _nextRefresh;
        private PendingCraft _pending;
        private string _notice = "";
        private Color _noticeColour;
        private float _noticeUntil;

        private sealed class Entry
        {
            public Recipe Recipe;
            public ItemDrop.ItemData UpgradeItem;
            public string Name;
            public bool CanCraft;
            public string Reason;
        }

        private sealed class PendingCraft
        {
            public Recipe Recipe;
            public ItemDrop.ItemData UpgradeItem;
            public int Variant;
            public bool Multi;
            public float StartedAt;
            public float Duration;
        }

        private sealed class RecipeRow
        {
            public GameObject Root;
            public Image Background;
            public Image Accent;
            public Image Icon;
            public TMP_Text Name;
            public TMP_Text Meta;
            public Button Button;
        }

        private sealed class RequirementRow
        {
            public GameObject Root;
            public Image Icon;
            public TMP_Text Name;
            public TMP_Text Amount;
        }

        public CraftingPage(RectTransform page, TMP_FontAsset font, Vector2 size)
        {
            _page = page;
            _font = font;
            _size = size;
            Build();
            Refresh();
        }

        private void Build()
        {
            var title = Label(_page, "Title", "CRAFTING", 28f, Gold,
                              TextAlignmentOptions.Center);
            PlaceTop(title.rectTransform, 0f, 1f, _size.x, 30f);
            title.fontStyle = FontStyles.Bold;
            UiSkin.PageHeading(_page, title, 58f);

            _station = Label(_page, "Station", "", 15f, Weak,
                             TextAlignmentOptions.Center);
            PlaceTop(_station.rectTransform, 12f, 31f, _size.x - 24f, 21f);
            SecondScreenUI.FitText(_station, 10f, 15f);

            float listWidth = Mathf.Clamp(_size.x * 0.385f, 370f, 408f);
            const float toolbarTop = 58f;
            const float toolbarHeight = 54f;
            const float modeGap = 8f;
            float modeWidth = (listWidth - modeGap) * 0.5f;
            _newButton = ActionButton(_page, "NewMode", "NEW", 12f, toolbarTop,
                                      modeWidth, toolbarHeight,
                                      () => SetMode(false), out _);
            _upgradeButton = ActionButton(_page, "UpgradeMode", "UPGRADE",
                                          12f + modeWidth + modeGap, toolbarTop,
                                          modeWidth, toolbarHeight,
                                          () => SetMode(true), out _);
            _newAccent = UiSkin.SelectionAccent(
                (RectTransform)_newButton.transform, true);
            _upgradeAccent = UiSkin.SelectionAccent(
                (RectTransform)_upgradeButton.transform, true);

            _repairButton = ActionButton(_page, "Repair", "REPAIR", _size.x - 210f,
                                         toolbarTop, 198f, toolbarHeight, RepairOne,
                                         out _repairLabel);

            float bodyTop = 118f;
            float bodyHeight = Mathf.Max(660f, _size.y - bodyTop - 14f);
            _listCard = Panel(_page, "RecipeList", 12f, bodyTop, listWidth, bodyHeight,
                              Card);
            _detailCard = Panel(_page, "RecipeDetail", listWidth + 20f, bodyTop,
                                _size.x - listWidth - 32f, bodyHeight, Card);
            UiSkin.HeaderWash(_listCard, 48f);
            UiSkin.HeaderWash(_detailCard, 166f);

            _listHeading = Label(_listCard, "Heading", "RECIPES", 22f, Gold,
                                 TextAlignmentOptions.Center);
            PlaceTop(_listHeading.rectTransform, 8f, 6f, listWidth - 16f, 32f);
            _listHeading.fontStyle = FontStyles.Bold;

            const float rowTop = 48f;
            const float pageArrowSize = 60f;
            const float pageArrowGap = 12f;
            float footerTop = bodyHeight - 76f;
            float rowHeight = Mathf.Max(48f,
                (footerTop - rowTop - (VisibleRecipes - 1) * 4f) / VisibleRecipes);
            for (int i = 0; i < VisibleRecipes; i++)
            {
                int visible = i;
                var row = CreateRecipeRow(_listCard, 8f,
                    rowTop + i * (rowHeight + 4f), listWidth - 16f, rowHeight,
                    () => SelectVisible(visible));
                _recipeRows.Add(row);
            }

            float pageArrowLeft = (listWidth - pageArrowSize * 2f - pageArrowGap) * 0.5f;
            _previousPageButton = ActionButton(_listCard, "PreviousPage", "<",
                pageArrowLeft,
                footerTop + 8f, pageArrowSize, pageArrowSize,
                () => PageRecipes(-1), out _);
            _nextPageButton = ActionButton(_listCard, "NextPage", ">",
                pageArrowLeft + pageArrowSize + pageArrowGap, footerTop + 8f,
                pageArrowSize, pageArrowSize, () => PageRecipes(1), out _);

            BuildDetails(bodyHeight, _detailCard.rect.width);
        }

        private void BuildDetails(float height, float width)
        {
            _detailWidth = width;
            var iconRoot = Panel(_detailCard, "ItemIcon", 16f, 14f, 148f, 148f,
                                 CardStrong);
            _itemIcon = SpriteImage(iconRoot, "Icon", 10f, 10f, 128f, 128f);
            _itemIcon.preserveAspect = true;

            _name = Label(_detailCard, "Name", "", 32f, Gold,
                          TextAlignmentOptions.TopLeft);
            PlaceTop(_name.rectTransform, 180f, 16f, width - 196f, 48f);
            _name.fontStyle = FontStyles.Bold;
            SecondScreenUI.FitText(_name, 15f, 32f);

            _quality = Label(_detailCard, "Quality", "", 22f, Weak,
                             TextAlignmentOptions.TopLeft);
            PlaceTop(_quality.rectTransform, 180f, 69f, width - 196f, 82f);
            SecondScreenUI.FitText(_quality, 12f, 22f, true);

            float controlsTop = Mathf.Max(690f, height - 196f);
            _detailControlsTop = controlsTop;
            const int requirementColumns = 1;
            const float requirementHeight = 40f;
            _requirementHeight = requirementHeight;
            int requirementDisplayRows = Mathf.CeilToInt(
                RequirementRows / (float)requirementColumns);
            float requirementTop = controlsTop
                                   - requirementDisplayRows * requirementHeight;
            float requirementHeadingTop = requirementTop - 38f;
            const float descriptionTop = 174f;
            _detailDescriptionTop = descriptionTop;
            float descriptionHeight = Mathf.Max(
                180f, requirementHeadingTop - descriptionTop - 8f);

            _descriptionMask = Panel(_detailCard, "DescriptionMask",
                                     16f, descriptionTop,
                                     width - 32f, descriptionHeight,
                                     new Color(0f, 0f, 0f, 0.18f));
            _descriptionMask.gameObject.AddComponent<RectMask2D>();
            _description = Label(_descriptionMask, "Description", "", 20f, Text,
                                 TextAlignmentOptions.TopLeft);
            PlaceTop(_description.rectTransform, 10f, 8f,
                     _descriptionMask.rect.width - 20f, _descriptionMask.rect.height - 16f);
            _description.textWrappingMode = TextWrappingModes.Normal;
            _description.enableAutoSizing = true;
            _description.fontSizeMin = 12f;
            _description.fontSizeMax = 20f;
            _description.lineSpacing = -3f;
            _description.overflowMode = TextOverflowModes.Truncate;

            _requirementsHeading = Label(_detailCard, "RequirementsHeading",
                                         "REQUIREMENTS", 24f, Gold,
                                         TextAlignmentOptions.Left);
            PlaceTop(_requirementsHeading.rectTransform, 18f,
                     requirementHeadingTop, width - 36f, 34f);
            _requirementsHeading.fontStyle = FontStyles.Bold;
            SecondScreenUI.FitText(_requirementsHeading, 14f, 24f);

            const float requirementGap = 0f;
            float requirementWidth = (width - 32f
                - requirementGap * (requirementColumns - 1)) / requirementColumns;
            for (int i = 0; i < RequirementRows; i++)
            {
                int column = i % requirementColumns;
                int rowIndex = i / requirementColumns;
                var row = CreateRequirementRow(_detailCard,
                    16f + column * (requirementWidth + requirementGap),
                    requirementTop + rowIndex * requirementHeight,
                    requirementWidth, requirementHeight - 2f);
                _requirementRows.Add(row);
            }

            _variantPrevious = ActionButton(_detailCard, "VariantPrevious", "<", 16f,
                controlsTop, 64f, 64f, () => ChangeVariant(-1), out _);
            _variantReadout = Label(_detailCard, "Variant", "VARIANT 1 / 1", 22f, Text,
                                    TextAlignmentOptions.Center);
            PlaceTop(_variantReadout.rectTransform, 88f, controlsTop,
                     Mathf.Max(126f, width * 0.40f - 88f), 64f);
            SecondScreenUI.FitText(_variantReadout, 13f, 22f);
            _variantNext = ActionButton(_detailCard, "VariantNext", ">",
                Mathf.Max(222f, width * 0.40f) + 8f, controlsTop, 64f, 64f,
                () => ChangeVariant(1), out _);

            // Quantity belongs with the action it modifies. Keep the arrows,
            // count and Craft button on one footer row instead of spending a
            // second row above it and leaving a wide dead gap around the count.
            const float quantityArrowSize = 56f;
            const float quantityReadoutWidth = 64f;
            const float quantityGap = 2f;
            const float craftQuantityGap = 10f;
            float footerTop = height - 78f;
            float quantityTop = footerTop + 4f;
            float quantityWidth = quantityArrowSize * 2f
                + quantityReadoutWidth + quantityGap * 2f;
            float multiLeft = width - 16f - quantityWidth;
            _multiPrevious = ActionButton(_detailCard, "MultiPrevious", "<", multiLeft,
                quantityTop, quantityArrowSize, quantityArrowSize,
                () => SetMulti(false), out _);
            _multiReadout = Label(_detailCard, "Multi", "x1", 24f, Text,
                                  TextAlignmentOptions.Center);
            PlaceTop(_multiReadout.rectTransform,
                     multiLeft + quantityArrowSize + quantityGap, quantityTop,
                     quantityReadoutWidth, quantityArrowSize);
            SecondScreenUI.FitText(_multiReadout, 14f, 24f);
            _multiNext = ActionButton(_detailCard, "MultiNext", ">",
                multiLeft + quantityArrowSize + quantityGap + quantityReadoutWidth
                    + quantityGap,
                quantityTop, quantityArrowSize, quantityArrowSize,
                () => SetMulti(true), out _);

            _statusFrame = Panel(_detailCard, "StatusMessage", 16f,
                                 controlsTop + 70f, width - 32f, 44f, Color.clear);
            _status = Label(_statusFrame, "Status", "", 19f, Weak,
                            TextAlignmentOptions.Center);
            PlaceTop(_status.rectTransform, 10f, 3f, width - 52f, 36f);
            SecondScreenUI.FitText(_status, 11f, 19f, true);

            float craftWidth = Mathf.Max(180f,
                multiLeft - craftQuantityGap - 16f);
            _craftFullWidth = width - 32f;
            _craftQuantityWidth = craftWidth;
            _craftButton = ActionButton(_detailCard, "Craft", "CRAFT", 16f,
                footerTop, craftWidth, 64f, CraftOrCancel, out _craftLabel);
            _craftLabel.fontSize = 22f;
            SecondScreenUI.FitText(_craftLabel, 12f, 22f);
        }

        public void Refresh()
        {
            _nextRefresh = 0f;
            RebuildEntries();
        }

        public void Tick()
        {
            TickPendingCraft();
            if (!_page.gameObject.activeInHierarchy && _pending == null) return;
            if (Time.unscaledTime >= _nextRefresh)
            {
                _nextRefresh = Time.unscaledTime + 0.20f;
                RebuildEntries();
            }
            PaintProgress();
        }

        public bool Tap(RectTransform sourceRoot, Vector2 sourcePoint)
        {
            if (!_page.gameObject.activeInHierarchy || sourceRoot == null) return false;
            Vector3 world = sourceRoot.TransformPoint(sourcePoint);
            foreach (var button in _touchButtons)
            {
                if (button == null || !button.gameObject.activeInHierarchy
                    || !button.interactable) continue;
                var rect = button.transform as RectTransform;
                Vector3 local = rect.InverseTransformPoint(world);
                if (!rect.rect.Contains(new Vector2(local.x, local.y))) continue;
                button.onClick.Invoke();
                return true;
            }
            return false;
        }

        public void Cancel()
        {
            if (_pending == null) return;
            _pending = null;
            ShowNotice("Craft cancelled.", Weak);
            PaintProgress();
        }

        private void SetMode(bool upgrade)
        {
            if (_pending != null || _upgradeMode == upgrade) return;
            _upgradeMode = upgrade;
            _multiCraft = false;
            _selectedIndex = -1;
            _firstIndex = 0;
            _variant = 0;
            Refresh();
        }

        private void SetMulti(bool multi)
        {
            if (_pending != null || _upgradeMode) return;
            _multiCraft = multi;
            RebuildEntries();
        }

        private void ChangeVariant(int direction)
        {
            var entry = SelectedEntry;
            if (_pending != null || entry == null || entry.UpgradeItem != null) return;
            int count = VariantCount(entry.Recipe);
            if (count <= 1) return;
            _variant = ((_variant + direction) % count + count) % count;
            PaintDetails();
        }

        private void SelectVisible(int visibleIndex)
        {
            if (_pending != null) return;
            int index = _firstIndex + visibleIndex;
            if (index < 0 || index >= _entries.Count) return;
            _selectedIndex = index;
            _variant = 0;
            PaintAll();
        }

        private void PageRecipes(int direction)
        {
            if (_entries.Count <= VisibleRecipes) return;
            int last = LastPageStart(_entries.Count);
            _firstIndex = Mathf.Clamp(_firstIndex + direction * VisibleRecipes, 0, last);
            PaintList();
        }

        private void RebuildEntries()
        {
            var player = Player.m_localPlayer;
            var selected = SelectedEntry;
            Recipe selectedRecipe = selected?.Recipe;
            ItemDrop.ItemData selectedItem = selected?.UpgradeItem;
            Vector2i selectedPosition = selectedItem != null
                ? selectedItem.m_gridPos : new Vector2i(-1, -1);

            _entries.Clear();
            if (player != null && ObjectDB.instance != null)
            {
                _recipes.Clear();
                player.GetAvailableRecipes(ref _recipes);
                foreach (var recipe in _recipes)
                {
                    if (recipe == null || recipe.m_item == null) continue;
                    if (!_upgradeMode)
                    {
                        _entries.Add(CreateEntry(player, recipe, null));
                        continue;
                    }

                    if (recipe.m_item.m_itemData.m_shared.m_maxQuality <= 1) continue;
                    _upgradeItems.Clear();
                    player.GetInventory().GetAllItems(
                        recipe.m_item.m_itemData.m_shared.m_name, _upgradeItems);
                    foreach (var item in _upgradeItems)
                        _entries.Add(CreateEntry(player, recipe, item));
                }
            }

            _entries.Sort(CompareEntries);
            _selectedIndex = FindSelection(selectedRecipe, selectedItem, selectedPosition);
            if (_selectedIndex < 0 && _entries.Count > 0) _selectedIndex = 0;
            if (_entries.Count == 0) _selectedIndex = -1;

            int last = LastPageStart(_entries.Count);
            _firstIndex = Mathf.Clamp(_firstIndex, 0, last);
            if (_selectedIndex >= 0 && _selectedIndex < _firstIndex)
                _firstIndex = (_selectedIndex / VisibleRecipes) * VisibleRecipes;
            if (_selectedIndex >= _firstIndex + VisibleRecipes)
                _firstIndex = (_selectedIndex / VisibleRecipes) * VisibleRecipes;
            _firstIndex = Mathf.Clamp(_firstIndex, 0, last);
            _variant = Mathf.Clamp(_variant, 0,
                Mathf.Max(0, VariantCount(SelectedEntry?.Recipe) - 1));

            PaintAll();
        }

        private Entry CreateEntry(Player player, Recipe recipe,
                                  ItemDrop.ItemData upgradeItem)
        {
            var entry = new Entry
            {
                Recipe = recipe,
                UpgradeItem = upgradeItem,
                Name = Localize(recipe.m_item.m_itemData.m_shared.m_name)
            };
            entry.CanCraft = Evaluate(player, entry, out entry.Reason);
            return entry;
        }

        private int CompareEntries(Entry left, Entry right)
        {
            int craftable = right.CanCraft.CompareTo(left.CanCraft);
            if (craftable != 0) return craftable;
            int weight = left.Recipe.m_listSortWeight.CompareTo(right.Recipe.m_listSortWeight);
            if (weight != 0) return weight;
            int name = string.Compare(left.Name, right.Name,
                                      StringComparison.CurrentCultureIgnoreCase);
            if (name != 0) return name;
            int leftQuality = left.UpgradeItem?.m_quality ?? 0;
            int rightQuality = right.UpgradeItem?.m_quality ?? 0;
            return rightQuality.CompareTo(leftQuality);
        }

        private int FindSelection(Recipe recipe, ItemDrop.ItemData item, Vector2i position)
        {
            if (recipe == null) return -1;
            for (int i = 0; i < _entries.Count; i++)
                if (_entries[i].Recipe == recipe && _entries[i].UpgradeItem == item)
                    return i;
            if (item != null)
                for (int i = 0; i < _entries.Count; i++)
                    if (_entries[i].Recipe == recipe && _entries[i].UpgradeItem != null
                        && _entries[i].UpgradeItem.m_gridPos == position)
                        return i;
            for (int i = 0; i < _entries.Count; i++)
                if (_entries[i].Recipe == recipe) return i;
            return -1;
        }

        private void PaintAll()
        {
            _newButton.GetComponent<Image>().color = !_upgradeMode ? Selected : Idle;
            _upgradeButton.GetComponent<Image>().color = _upgradeMode ? Selected : Idle;
            UiSkin.SetSelected(_newAccent, !_upgradeMode);
            UiSkin.SetSelected(_upgradeAccent, _upgradeMode);
            _listHeading.text = _upgradeMode ? "UPGRADABLE ITEMS" : "RECIPES";

            var player = Player.m_localPlayer;
            var station = player != null ? player.GetCurrentCraftingStation() : null;
            if (station != null)
                _station.text = Localize(station.m_name) + "  ·  LEVEL " + station.GetLevel();
            else
                _station.text = Localize("$hud_crafting") + "  ·  NO STATION";

            PaintList();
            PaintDetails();
            PaintRepair();
            PaintProgress();
        }

        private void PaintList()
        {
            for (int visible = 0; visible < _recipeRows.Count; visible++)
            {
                int index = _firstIndex + visible;
                var row = _recipeRows[visible];
                bool active = index >= 0 && index < _entries.Count;
                row.Root.SetActive(active);
                if (!active) continue;

                var entry = _entries[index];
                var item = entry.UpgradeItem ?? entry.Recipe.m_item.m_itemData;
                row.Icon.sprite = item.GetIcon();
                row.Icon.enabled = row.Icon.sprite != null;
                row.Name.text = entry.Name
                    + (!_upgradeMode && entry.Recipe.m_amount > 1
                        ? " x" + entry.Recipe.m_amount : "");
                int maxQuality = item.m_shared != null
                    ? item.m_shared.m_maxQuality : 1;
                string baseStars = ItemPresentation.QualityStars(1, maxQuality);
                row.Meta.text = entry.UpgradeItem != null
                    ? ItemPresentation.QualityStars(entry.UpgradeItem.m_quality, maxQuality)
                      + " → "
                      + ItemPresentation.QualityStars(entry.UpgradeItem.m_quality + 1,
                                                      maxQuality)
                    : (baseStars.Length > 0 ? baseStars + "  ·  " : "")
                      + (entry.CanCraft ? "READY" : "MISSING");
                row.Meta.color = entry.CanCraft ? Good : Weak;
                row.Background.color = index == _selectedIndex
                    ? Selected : (entry.CanCraft ? Idle : Disabled);
                UiSkin.SetSelected(row.Accent, index == _selectedIndex);
                row.Button.interactable = _pending == null;
            }

            int lastPage = LastPageStart(_entries.Count);
            bool multiplePages = _entries.Count > VisibleRecipes;
            if (_previousPageButton != null)
            {
                _previousPageButton.gameObject.SetActive(multiplePages);
                _previousPageButton.interactable = _pending == null
                                                   && _firstIndex > 0;
            }
            if (_nextPageButton != null)
            {
                _nextPageButton.gameObject.SetActive(multiplePages);
                _nextPageButton.interactable = _pending == null
                                               && _firstIndex < lastPage;
            }

        }

        private void PaintDetails()
        {
            var entry = SelectedEntry;
            var player = Player.m_localPlayer;
            if (entry == null || player == null)
            {
                _itemIcon.enabled = false;
                _name.text = "";
                _quality.text = "";
                _description.text = "Select a recipe.";
                _requirementsHeading.text = "REQUIREMENTS";
                foreach (var row in _requirementRows) row.Root.SetActive(false);
                LayoutRequirements(0);
                SetVariantControls(false, 1);
                SetMultiControls(false);
                _craftButton.interactable = false;
                _craftLabel.text = "CRAFT";
                if (HasNotice()) SetStatus(_notice, _noticeColour);
                else SetStatus("", Weak);
                return;
            }

            int quality = entry.UpgradeItem == null ? 1 : entry.UpgradeItem.m_quality + 1;
            int multiplier = entry.UpgradeItem == null && _multiCraft
                ? Mathf.Max(1, InventoryGui.instance?.m_multiCraftAmount ?? 5) : 1;
            int variant = entry.UpgradeItem != null
                ? entry.UpgradeItem.m_variant : _variant;
            var template = entry.Recipe.m_item.m_itemData;
            int variants = VariantCount(entry.Recipe);
            variant = Mathf.Clamp(variant, 0, variants - 1);
            _itemIcon.sprite = template.m_shared.m_icons[variant];
            _itemIcon.enabled = _itemIcon.sprite != null;
            _name.text = entry.Name + (entry.Recipe.m_amount * multiplier > 1
                ? " x" + entry.Recipe.m_amount * multiplier : "");

            var required = entry.Recipe.GetRequiredStation(quality);
            string stationText = required != null
                ? Localize(required.m_name) + " LEVEL "
                  + entry.Recipe.GetRequiredStationLevel(quality)
                : "HAND CRAFTING";
            int maxQuality = template.m_shared != null
                ? template.m_shared.m_maxQuality : 1;
            string resultStars = ItemPresentation.QualityStars(quality, maxQuality);
            _quality.text = entry.UpgradeItem == null
                ? (resultStars.Length > 0 ? resultStars : "QUALITY 1")
                  + "  ·  " + stationText
                : ItemPresentation.QualityStars(entry.UpgradeItem.m_quality, maxQuality)
                  + " → " + resultStars + "  ·  " + stationText;

            try
            {
                _description.text = Localize(ItemDrop.ItemData.GetTooltip(
                    template, quality, true, Game.m_worldLevel,
                    entry.Recipe.m_amount * multiplier));
            }
            catch
            {
                _description.text = Localize(template.m_shared.m_description);
            }
            if (entry.Recipe.m_requireOnlyOneIngredient)
                _description.text += "\n\n" + Localize("$inventory_onlyoneingredient");

            PaintRequirements(player, entry, quality, multiplier);
            SetVariantControls(entry.UpgradeItem == null && variants > 1, variants);
            SetMultiControls(entry.UpgradeItem == null);

            entry.CanCraft = Evaluate(player, entry, out entry.Reason);
            bool busy = IsStockCrafting();
            _craftButton.interactable = _pending != null || (entry.CanCraft && !busy);
            _craftLabel.text = _pending != null ? "CANCEL"
                : (entry.UpgradeItem != null ? "UPGRADE" : "CRAFT"
                   + (multiplier > 1 ? " x" + multiplier : ""));
            if (_pending == null)
            {
                // The enabled CRAFT button already says the recipe is ready.
                // Blockers remain as plain red text instead of resembling a
                // second, disabled craft button.
                if (HasNotice()) SetStatus(_notice, _noticeColour);
                else if (!busy && entry.CanCraft) SetStatus("", Good);
                else SetStatus(busy ? "Crafting is already in progress."
                                    : entry.Reason, Bad);
            }
        }

        private void PaintRequirements(Player player, Entry entry, int quality,
                                       int multiplier)
        {
            var requirements = new List<Piece.Requirement>();
            foreach (var requirement in entry.Recipe.m_resources)
            {
                if (requirement == null || requirement.m_resItem == null
                    || requirement.GetAmount(quality) <= 0) continue;
                if (entry.Recipe.m_requireOnlyOneIngredient
                    && !player.IsKnownMaterial(
                        requirement.m_resItem.m_itemData.m_shared.m_name)) continue;
                requirements.Add(requirement);
            }

            _requirementsHeading.text = entry.Recipe.m_requireOnlyOneIngredient
                ? "REQUIREMENTS  ·  USE ANY ONE" : "REQUIREMENTS";
            LayoutRequirements(Mathf.Min(requirements.Count, _requirementRows.Count));
            bool noCost = IsNoCost(player);
            for (int i = 0; i < _requirementRows.Count; i++)
            {
                var row = _requirementRows[i];
                bool active = i < requirements.Count;
                row.Root.SetActive(active);
                if (!active) continue;
                var requirement = requirements[i];
                var item = requirement.m_resItem.m_itemData;
                int owned = player.GetInventory().CountItems(item.m_shared.m_name);
                int needed = requirement.GetAmount(quality) * multiplier;
                row.Icon.sprite = item.GetIcon();
                row.Icon.enabled = row.Icon.sprite != null;
                row.Name.text = Localize(item.m_shared.m_name);
                row.Amount.text = owned + " / " + needed;
                row.Amount.color = noCost || owned >= needed ? Good : Bad;
            }
            if (requirements.Count > _requirementRows.Count)
                _requirementsHeading.text += "  (" + requirements.Count + " TOTAL)";
        }

        /// <summary>
        /// Gives unused ingredient rows back to the item tooltip. Most Valheim
        /// recipes use two to four materials, so permanently reserving all eight
        /// rows made weapon stats tiny while the lower detail card stayed empty.
        /// </summary>
        private void LayoutRequirements(int visibleRows)
        {
            visibleRows = Mathf.Clamp(visibleRows, 0, _requirementRows.Count);
            float requirementTop = _detailControlsTop
                                   - visibleRows * _requirementHeight;
            float headingTop = requirementTop - 40f;
            PlaceTop(_requirementsHeading.rectTransform, 18f, headingTop,
                     _detailWidth - 36f, 34f);

            float descriptionHeight = Mathf.Max(120f,
                headingTop - _detailDescriptionTop - 8f);
            PlaceTop(_descriptionMask, 16f, _detailDescriptionTop,
                     _detailWidth - 32f, descriptionHeight);
            PlaceTop(_description.rectTransform, 10f, 8f,
                     _detailWidth - 52f, descriptionHeight - 16f);

            float rowWidth = _detailWidth - 32f;
            for (int i = 0; i < _requirementRows.Count; i++)
            {
                PlaceTop((RectTransform)_requirementRows[i].Root.transform,
                         16f, requirementTop + i * _requirementHeight,
                         rowWidth, _requirementHeight - 3f);
            }
        }

        private void SetVariantControls(bool visible, int count)
        {
            _variantPrevious.gameObject.SetActive(visible);
            _variantNext.gameObject.SetActive(visible);
            _variantReadout.gameObject.SetActive(visible);
            if (visible) _variantReadout.text = "VARIANT " + (_variant + 1) + " / " + count;
        }

        private void SetMultiControls(bool visible)
        {
            _multiPrevious.gameObject.SetActive(visible);
            _multiNext.gameObject.SetActive(visible);
            _multiReadout.gameObject.SetActive(visible);
            if (_craftButton != null)
            {
                RectTransform craft = (RectTransform)_craftButton.transform;
                craft.sizeDelta = new Vector2(
                    visible ? _craftQuantityWidth : _craftFullWidth,
                    craft.sizeDelta.y);
            }
            if (visible)
            {
                int amount = Mathf.Max(1, InventoryGui.instance?.m_multiCraftAmount ?? 5);
                _multiReadout.text = _multiCraft ? "x" + amount : "x1";
                _multiPrevious.interactable = _multiCraft;
                _multiNext.interactable = !_multiCraft;
            }
        }

        private void PaintRepair()
        {
            bool available = false;
            var gui = InventoryGui.instance;
            if (gui != null && HaveRepairableMethod != null)
            {
                try { available = (bool)HaveRepairableMethod.Invoke(gui, null); }
                catch { available = false; }
            }
            bool stationOpen = InventoryGui.IsVisible()
                               && Player.m_localPlayer != null
                               && Player.m_localPlayer.GetCurrentCraftingStation() != null;
            _repairButton.gameObject.SetActive(stationOpen);
            if (!stationOpen) return;

            _repairButton.interactable = available && _pending == null;
            _repairButton.GetComponent<Image>().color = available ? Idle : Disabled;
            _repairLabel.text = "REPAIR";
        }

        private void CraftOrCancel()
        {
            if (_pending != null)
            {
                Cancel();
                return;
            }

            var player = Player.m_localPlayer;
            var entry = SelectedEntry;
            if (player == null || entry == null) return;
            entry.CanCraft = Evaluate(player, entry, out entry.Reason);
            if (!entry.CanCraft || IsStockCrafting())
            {
                SetStatus(IsStockCrafting() ? "Crafting is already in progress."
                                            : entry.Reason, Bad);
                return;
            }

            var gui = InventoryGui.instance;
            if (gui == null) return;
            bool multi = entry.UpgradeItem == null && _multiCraft;
            float duration = multi ? gui.m_multiCraftDuration : gui.m_craftDuration;
            var station = player.GetCurrentCraftingStation();
            if (station != null && station.m_craftingSkill != Skills.SkillType.None)
                duration *= 1f - player.GetSkillFactor(station.m_craftingSkill)
                            * gui.m_craftDurationSkillMaxDecrease;

            _pending = new PendingCraft
            {
                Recipe = entry.Recipe,
                UpgradeItem = entry.UpgradeItem,
                Variant = entry.UpgradeItem?.m_variant ?? _variant,
                Multi = multi,
                StartedAt = Time.time,
                Duration = Mathf.Max(0.05f, duration)
            };

            try
            {
                if (entry.Recipe.m_craftingStation != null && station != null)
                    station.m_craftItemEffects.Create(player.transform.position,
                                                      Quaternion.identity);
                else
                    gui.m_craftItemEffects.Create(player.transform.position,
                                                  Quaternion.identity);
            }
            catch (Exception exception)
            {
                Plugin.Log.LogDebug("[SecondScreen] Craft start effect: "
                                    + exception.Message);
            }
            ClearNotice();
            SetStatus("Crafting " + entry.Name + "…", Good);
            PaintAll();
        }

        private void TickPendingCraft()
        {
            if (_pending == null) return;
            var player = Player.m_localPlayer;
            if (player == null || player.IsDead() || player.IsTeleporting()
                || player.InCutscene())
            {
                Cancel();
                return;
            }

            // This page is intentionally polled at 15 Hz. Adding a single
            // render frame's delta here made the timer advance only once every
            // several frames and stretched a normal craft far beyond Valheim's
            // configured duration. Time.time is the same scaled clock as the
            // stock timer and remains correct regardless of presentation rate.
            if (Elapsed(_pending) < _pending.Duration) return;
            CompleteCraft(player);
        }

        private void CompleteCraft(Player player)
        {
            var pending = _pending;
            _pending = null;
            var entry = new Entry
            {
                Recipe = pending.Recipe,
                UpgradeItem = pending.UpgradeItem,
                Name = pending.Recipe != null && pending.Recipe.m_item != null
                    ? Localize(pending.Recipe.m_item.m_itemData.m_shared.m_name) : "Item"
            };
            string reason = "Recipe unavailable.";
            if (pending.Recipe == null || IsStockCrafting()
                || !Evaluate(player, entry, out reason))
            {
                ShowNotice(IsStockCrafting() ? "Crafting is already in progress."
                                             : reason, Bad);
                Refresh();
                return;
            }

            var gui = InventoryGui.instance;
            if (gui == null || DoCraftingMethod == null || CraftRecipeField == null
                || CraftUpgradeField == null || CraftVariantField == null
                || MultiCraftField == null)
            {
                ShowNotice("Valheim's crafting service is unavailable.", Bad);
                return;
            }

            object previousRecipe = null;
            object previousUpgrade = null;
            object previousVariant = null;
            object previousMulti = null;
            bool capturedStockState = false;
            try
            {
                previousRecipe = CraftRecipeField.GetValue(gui);
                previousUpgrade = CraftUpgradeField.GetValue(gui);
                previousVariant = CraftVariantField.GetValue(gui);
                previousMulti = MultiCraftField.GetValue(gui);
                capturedStockState = true;
                CraftRecipeField.SetValue(gui, pending.Recipe);
                CraftUpgradeField.SetValue(gui, pending.UpgradeItem);
                CraftVariantField.SetValue(gui, pending.Variant);
                MultiCraftField.SetValue(gui, pending.Multi);
                DoCraftingMethod.Invoke(gui, new object[] { player });
                ShowNotice((pending.UpgradeItem != null ? "Upgraded " : "Crafted ")
                           + entry.Name + ".", Good);
            }
            catch (TargetInvocationException exception)
            {
                Exception cause = exception.InnerException ?? exception;
                Plugin.Log.LogError("[SecondScreen] Craft failed: " + cause);
                ShowNotice("Craft failed: " + cause.Message, Bad);
            }
            catch (Exception exception)
            {
                Plugin.Log.LogError("[SecondScreen] Craft failed: " + exception);
                ShowNotice("Craft failed: " + exception.Message, Bad);
            }
            finally
            {
                if (capturedStockState)
                {
                    try
                    {
                        CraftRecipeField.SetValue(gui, previousRecipe);
                        CraftUpgradeField.SetValue(gui, previousUpgrade);
                        CraftVariantField.SetValue(gui, previousVariant);
                        MultiCraftField.SetValue(gui, previousMulti);
                    }
                    catch (Exception exception)
                    {
                        Plugin.Log.LogWarning(
                            "[SecondScreen] Could not restore stock crafting state: "
                            + exception.Message);
                    }
                }
            }
            Refresh();
        }

        private bool Evaluate(Player player, Entry entry, out string reason)
        {
            reason = "Ready to craft.";
            if (player == null || entry?.Recipe == null || entry.Recipe.m_item == null)
            {
                reason = "Recipe unavailable.";
                return false;
            }
            if (player.IsDead() || player.IsTeleporting() || player.InCutscene())
            {
                reason = "Player is busy.";
                return false;
            }
            if (entry.UpgradeItem != null
                && !player.GetInventory().ContainsItem(entry.UpgradeItem))
            {
                reason = "Item is no longer in the inventory.";
                return false;
            }

            int quality = entry.UpgradeItem == null ? 1 : entry.UpgradeItem.m_quality + 1;
            if (quality > entry.Recipe.m_item.m_itemData.m_shared.m_maxQuality)
            {
                reason = "Maximum quality reached.";
                return false;
            }
            int multiplier = entry.UpgradeItem == null && _multiCraft
                ? Mathf.Max(1, InventoryGui.instance?.m_multiCraftAmount ?? 5) : 1;
            bool noCost = IsNoCost(player);
            var required = entry.Recipe.GetRequiredStation(quality);
            if (!player.NoCostCheat() && required != null)
            {
                var current = player.GetCurrentCraftingStation();
                if (current == null || current.m_name != required.m_name)
                {
                    reason = "Required station: " + Localize(required.m_name) + ".";
                    return false;
                }
                if (!current.CheckUsable(player, false))
                {
                    reason = "Crafting station is not usable.";
                    return false;
                }
            }
            if (!player.HaveRequirements(entry.Recipe, false, quality, multiplier)
                && !noCost)
            {
                reason = "Missing requirements.";
                return false;
            }

            // Recipe.GetAmount assumes a matching one-ingredient item exists.
            // Guard it before asking for the output count, including in no-cost
            // mode, so a malformed or empty recipe cannot throw from a touch.
            if (entry.Recipe.m_requireOnlyOneIngredient)
            {
                int requiredAmount;
                int extraAmount;
                var source = player.GetFirstRequiredItem(player.GetInventory(), entry.Recipe,
                    quality, out requiredAmount, out extraAmount, multiplier);
                if (source == null)
                {
                    reason = "One listed ingredient is required.";
                    return false;
                }
            }

            if (entry.UpgradeItem == null)
            {
                try
                {
                    int need;
                    ItemDrop.ItemData source;
                    int amount = entry.Recipe.GetAmount(quality, out need, out source,
                                                        multiplier);
                    if (!player.GetInventory().CanAddItem(entry.Recipe.m_item.gameObject,
                                                          amount))
                    {
                        reason = Localize("$inventory_full");
                        return false;
                    }
                }
                catch
                {
                    reason = "Recipe output is unavailable.";
                    return false;
                }
            }
            return true;
        }

        private void RepairOne()
        {
            if (_pending != null || InventoryGui.instance == null
                || RepairOneMethod == null) return;
            try
            {
                RepairOneMethod.Invoke(InventoryGui.instance, null);
                ShowNotice("Repair complete.", Good);
            }
            catch (TargetInvocationException exception)
            {
                Exception cause = exception.InnerException ?? exception;
                Plugin.Log.LogWarning("[SecondScreen] Repair failed: " + cause.Message);
                ShowNotice("Repair failed: " + cause.Message, Bad);
            }
            Refresh();
        }

        private bool IsStockCrafting()
        {
            var gui = InventoryGui.instance;
            if (gui == null || StockCraftTimerField == null) return false;
            try { return (float)StockCraftTimerField.GetValue(gui) >= 0f; }
            catch { return false; }
        }

        private static bool IsNoCost(Player player)
        {
            return player != null && (player.NoCostCheat()
                || (ZoneSystem.instance != null
                    && ZoneSystem.instance.GetGlobalKey(GlobalKeys.NoCraftCost)));
        }

        private static int VariantCount(Recipe recipe)
        {
            if (recipe == null || recipe.m_item == null) return 1;
            var shared = recipe.m_item.m_itemData.m_shared;
            int icons = shared.m_icons != null ? shared.m_icons.Length : 0;
            return Mathf.Max(1, Mathf.Min(Mathf.Max(1, shared.m_variants),
                                          Mathf.Max(1, icons)));
        }

        private Entry SelectedEntry => _selectedIndex >= 0 && _selectedIndex < _entries.Count
            ? _entries[_selectedIndex] : null;

        private static int LastPageStart(int count)
        {
            return count <= 0 ? 0 : ((count - 1) / VisibleRecipes) * VisibleRecipes;
        }

        private void PaintProgress()
        {
            if (_pending != null)
            {
                _craftLabel.text = "CANCEL  ·  " + Mathf.CeilToInt(
                    Mathf.Max(0f, _pending.Duration - Elapsed(_pending))) + "s";
                _craftButton.interactable = true;
            }
        }

        private static float Elapsed(PendingCraft craft)
        {
            return craft == null ? 0f : Mathf.Max(0f, Time.time - craft.StartedAt);
        }

        private bool HasNotice()
        {
            if (string.IsNullOrEmpty(_notice) || Time.unscaledTime >= _noticeUntil)
            {
                ClearNotice();
                return false;
            }
            return true;
        }

        private void ShowNotice(string value, Color colour, float seconds = 2.5f)
        {
            _notice = value ?? "";
            _noticeColour = colour;
            _noticeUntil = Time.unscaledTime + Mathf.Max(0.1f, seconds);
            SetStatus(_notice, _noticeColour);
        }

        private void ClearNotice()
        {
            _notice = "";
            _noticeUntil = 0f;
        }

        private void SetStatus(string value, Color colour)
        {
            if (_status == null) return;
            _status.text = value ?? "";
            _status.color = colour;
            if (_statusFrame != null)
                _statusFrame.gameObject.SetActive(!string.IsNullOrEmpty(_status.text));
        }

        private RecipeRow CreateRecipeRow(Transform parent, float left, float top,
                                          float width, float height, Action action)
        {
            var root = Panel(parent, "Recipe", left, top, width, height, Idle);
            var button = root.gameObject.AddComponent<Button>();
            var background = root.GetComponent<Image>();
            UiSkin.Control(background, root);
            var accent = UiSkin.SelectionAccent(root, true);
            button.targetGraphic = background;
            button.targetGraphic.raycastTarget = true;
            button.transition = Selectable.Transition.None;
            button.navigation = new Navigation { mode = Navigation.Mode.None };
            button.onClick.AddListener(() => action());
            _touchButtons.Add(button);

            var icon = SpriteImage(root, "Icon", 6f, 5f, height - 10f, height - 10f);
            icon.preserveAspect = true;
            var name = Label(root, "Name", "", 21f, Text, TextAlignmentOptions.Left);
            PlaceTop(name.rectTransform, height + 3f, 2f,
                     width - height - 128f, height - 4f);
            SecondScreenUI.FitText(name, 11f, 21f);
            var meta = Label(root, "Meta", "", 16f, Weak, TextAlignmentOptions.Right);
            PlaceTop(meta.rectTransform, width - 122f, 2f, 104f, height - 4f);
            SecondScreenUI.FitText(meta, 10f, 16f);
            return new RecipeRow
            {
                Root = root.gameObject,
                Background = background,
                Accent = accent,
                Icon = icon,
                Name = name,
                Meta = meta,
                Button = button
            };
        }

        private RequirementRow CreateRequirementRow(Transform parent, float left,
            float top, float width, float height)
        {
            var root = Panel(parent, "Requirement", left, top, width, height,
                             new Color(0.02f, 0.015f, 0.01f, 0.36f));
            var icon = SpriteImage(root, "Icon", 5f, 4f, height - 8f, height - 8f);
            icon.preserveAspect = true;
            var name = Label(root, "Name", "", 21f, Text, TextAlignmentOptions.Left);
            PlaceTop(name.rectTransform, height + 3f, 0f,
                     width - height - 132f, height);
            SecondScreenUI.FitText(name, 12f, 21f);
            var amount = Label(root, "Amount", "", 22f, Weak,
                               TextAlignmentOptions.Right);
            PlaceTop(amount.rectTransform, width - 132f, 0f, 122f, height);
            SecondScreenUI.FitText(amount, 12f, 22f);
            return new RequirementRow
            {
                Root = root.gameObject,
                Icon = icon,
                Name = name,
                Amount = amount
            };
        }

        private Button ActionButton(Transform parent, string name, string label,
            float left, float top, float width, float height, Action action,
            out TMP_Text text)
        {
            var root = Panel(parent, name, left, top, width, height, Idle);
            var button = root.gameObject.AddComponent<Button>();
            var background = root.GetComponent<Image>();
            bool arrow = label == "<" || label == ">"
                         || label == "\u2039" || label == "\u203a";
            if (arrow) UiSkin.Square(background, Idle);
            else UiSkin.Control(background, root);
            button.targetGraphic = background;
            button.targetGraphic.raycastTarget = true;
            button.transition = Selectable.Transition.None;
            button.navigation = new Navigation { mode = Navigation.Mode.None };
            button.onClick.AddListener(() => action());
            text = Label(root, "Label", label, arrow ? 27f : 18f, Gold,
                         TextAlignmentOptions.Center);
            text.fontStyle = FontStyles.Bold;
            text.rectTransform.offsetMin = new Vector2(7f, 4f);
            text.rectTransform.offsetMax = new Vector2(-7f, -4f);
            SecondScreenUI.FitText(text, arrow ? 18f : 10f,
                                   arrow ? 27f : 18f);
            _touchButtons.Add(button);
            return button;
        }

        private RectTransform Panel(Transform parent, string name, float left,
                                    float top, float width, float height, Color colour)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Image));
            var rect = (RectTransform)go.transform;
            rect.SetParent(parent, false);
            PlaceTop(rect, left, top, width, height);
            var image = go.GetComponent<Image>();
            image.sprite = null;
            image.type = Image.Type.Simple;
            image.color = colour;
            image.raycastTarget = false;
            return rect;
        }

        private Image SpriteImage(Transform parent, string name, float left, float top,
                                  float width, float height)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Image));
            var rect = (RectTransform)go.transform;
            rect.SetParent(parent, false);
            PlaceTop(rect, left, top, width, height);
            var image = go.GetComponent<Image>();
            image.raycastTarget = false;
            return image;
        }

        private TMP_Text Label(Transform parent, string name, string content,
                               float size, Color colour, TextAlignmentOptions alignment)
        {
            var value = SecondScreenUI.MakeText(parent, name, content, size, colour,
                                                alignment);
            if (_font != null) value.font = _font;
            return value;
        }

        private static void PlaceTop(RectTransform rect, float left, float top,
                                     float width, float height)
        {
            rect.anchorMin = new Vector2(0f, 1f);
            rect.anchorMax = new Vector2(0f, 1f);
            rect.pivot = new Vector2(0f, 1f);
            rect.anchoredPosition = new Vector2(left, -top);
            rect.sizeDelta = new Vector2(width, height);
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
