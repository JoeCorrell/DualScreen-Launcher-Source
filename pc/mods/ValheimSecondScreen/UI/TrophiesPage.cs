using System;
using System.Collections.Generic;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace ValheimSecondScreen.UI
{
    /// <summary>The player's live Valheim trophy encyclopedia.</summary>
    internal sealed class TrophiesPage
    {
        private const int Columns = 4;
        private const int Rows = 4;
        private const int VisibleCount = Columns * Rows;

        private static readonly Color Card = UiTheme.Panel;
        private static readonly Color Idle = UiTheme.Control;
        private static readonly Color Selected = UiTheme.ControlSelected;
        private static readonly Color Gold = UiTheme.Gold;
        private static readonly Color Text = UiTheme.Text;
        private static readonly Color Weak = UiTheme.Muted;

        private readonly RectTransform _page;
        private readonly TMP_FontAsset _font;
        private readonly Vector2 _size;
        private readonly List<Entry> _entries = new List<Entry>();
        private readonly List<Cell> _cells = new List<Cell>();
        private readonly List<Button> _buttons = new List<Button>();

        private TMP_Text _count;
        private TMP_Text _pageText;
        private Image _detailIcon;
        private TMP_Text _detailName;
        private TMP_Text _detailLore;
        private TMP_Text _detailPosition;
        private RectTransform _progressFill;
        private Button _previousPageButton;
        private Button _nextPageButton;
        private int _selected = -1;
        private int _first;
        private float _nextRefresh;
        private Player _lastPlayer;
        private ObjectDB _lastDatabase;
        private int _lastTrophyFingerprint = int.MinValue;
        private int _cachedTotal;

        private sealed class Entry
        {
            public string PrefabName;
            public ItemDrop Item;
            public string Name;
            public string Lore;
            public Vector2Int Position;
        }

        private sealed class Cell
        {
            public GameObject Root;
            public Image Background;
            public Image Accent;
            public Image Icon;
            public TMP_Text Name;
            public Button Button;
        }

        public TrophiesPage(RectTransform page, TMP_FontAsset font, Vector2 size)
        {
            _page = page;
            _font = font;
            _size = size;
            Build();
            Refresh(true);
        }

        private void Build()
        {
            var title = Label(_page, "Title", "TROPHIES", 36f, Gold,
                              TextAlignmentOptions.Center);
            Place(title.rectTransform, 0f, 8f, _size.x, 48f);
            title.fontStyle = FontStyles.Bold;
            UiSkin.PageHeading(_page, title, 84f);

            _count = Label(_page, "Count", "", 19f, Weak,
                           TextAlignmentOptions.Center);
            Place(_count.rectTransform, 12f, 54f, _size.x - 24f, 30f);
            SecondScreenUI.FitText(_count, 11f, 19f);

            var progressObject = new GameObject("CollectionProgress",
                typeof(RectTransform), typeof(Image));
            var progress = (RectTransform)progressObject.transform;
            progress.SetParent(_page, false);
            Place(progress, _size.x * 0.27f, 84f, _size.x * 0.46f, 7f);
            UiSkin.Plain(progressObject.GetComponent<Image>(),
                new Color(0f, 0f, 0f, 0.52f));

            var fillObject = new GameObject("Fill",
                typeof(RectTransform), typeof(Image));
            _progressFill = (RectTransform)fillObject.transform;
            _progressFill.SetParent(progress, false);
            _progressFill.anchorMin = Vector2.zero;
            _progressFill.anchorMax = new Vector2(0f, 1f);
            _progressFill.pivot = new Vector2(0f, 0.5f);
            _progressFill.offsetMin = Vector2.zero;
            _progressFill.offsetMax = Vector2.zero;
            var progressImage = fillObject.GetComponent<Image>();
            progressImage.color = new Color(0.88f, 0.49f, 0.09f, 0.92f);
            progressImage.raycastTarget = false;

            float bodyTop = 104f;
            float bodyHeight = _size.y - bodyTop - 14f;
            float gridWidth = Mathf.Clamp(_size.x * 0.64f, 620f, 680f);
            var gridCard = Panel(_page, "Collection", 12f, bodyTop, gridWidth,
                                 bodyHeight, Card);
            var detail = Panel(_page, "Details", gridWidth + 20f, bodyTop,
                               _size.x - gridWidth - 32f, bodyHeight, Card);
            // These are the page's two structural cards. Give only them the
            // shared cropped Valheim panel grain; trophy cells and nested lore
            // surfaces keep their quieter slot/control treatment.
            UiSkin.Panel(gridCard);
            UiSkin.Panel(detail);

            const float gap = 7f;
            const float footer = 82f;
            const float arrowSize = 64f;
            float availableWidth = gridWidth - 20f - gap * (Columns - 1);
            float availableHeight = bodyHeight - footer - 20f - gap * (Rows - 1);
            float cellSize = Mathf.Min(availableWidth / Columns,
                                       availableHeight / Rows);
            float gridSpan = cellSize * Columns + gap * (Columns - 1);
            float gridLeft = (gridWidth - gridSpan) * 0.5f;
            float gridTop = Mathf.Max(10f,
                (bodyHeight - footer - gridSpan) * 0.5f);
            for (int i = 0; i < VisibleCount; i++)
            {
                int visible = i;
                int column = i % Columns;
                int row = i / Columns;
                var cell = CreateCell(gridCard,
                    gridLeft + column * (cellSize + gap),
                    gridTop + row * (cellSize + gap), cellSize, cellSize,
                    () => SelectVisible(visible));
                _cells.Add(cell);
            }

            float footerTop = bodyHeight - footer + 9f;
            _previousPageButton = ActionButton(gridCard, "Previous", "<", 10f,
                footerTop, arrowSize, arrowSize, () => ChangePage(-1));
            _nextPageButton = ActionButton(gridCard, "Next", ">",
                gridWidth - 10f - arrowSize, footerTop,
                arrowSize, arrowSize, () => ChangePage(1));
            _pageText = Label(gridCard, "Page", "", 19f, Weak,
                              TextAlignmentOptions.Center);
            Place(_pageText.rectTransform, 82f, footerTop,
                  gridWidth - 164f, arrowSize);
            SecondScreenUI.FitText(_pageText, 11f, 19f);

            float detailWidth = detail.rect.width;
            var iconCard = Panel(detail, "TrophyIcon", (detailWidth - 210f) * 0.5f,
                                 28f, 210f, 210f,
                                 new Color(0.015f, 0.010f, 0.007f, 0.76f));
            _detailIcon = SpriteImage(iconCard, "Icon", 14f, 14f, 182f, 182f);
            _detailIcon.preserveAspect = true;

            _detailName = Label(detail, "Name", "", 30f, Gold,
                                TextAlignmentOptions.Center);
            Place(_detailName.rectTransform, 16f, 254f, detailWidth - 32f, 78f);
            _detailName.enableWordWrapping = true;
            _detailName.fontStyle = FontStyles.Bold;
            SecondScreenUI.FitText(_detailName, 14f, 30f, true);

            _detailPosition = Label(detail, "Position", "", 17f, Weak,
                                    TextAlignmentOptions.Center);
            Place(_detailPosition.rectTransform, 16f, 332f, detailWidth - 32f, 30f);

            var loreCard = Panel(detail, "Lore", 16f, 374f, detailWidth - 32f,
                                 bodyHeight - 392f,
                                 new Color(0.015f, 0.010f, 0.007f, 0.38f));
            loreCard.gameObject.AddComponent<RectMask2D>();
            _detailLore = Label(loreCard, "Text", "Select a trophy.", 20f, Text,
                                TextAlignmentOptions.TopLeft);
            Place(_detailLore.rectTransform, 14f, 12f,
                  loreCard.rect.width - 28f, loreCard.rect.height - 24f);
            _detailLore.enableWordWrapping = true;
            _detailLore.enableAutoSizing = true;
            _detailLore.fontSizeMin = 13f;
            _detailLore.fontSizeMax = 20f;
            _detailLore.lineSpacing = -2f;
            _detailLore.overflowMode = TextOverflowModes.Truncate;
        }

        public void Refresh(bool force = false)
        {
            if (!force && Time.unscaledTime < _nextRefresh) return;
            _nextRefresh = Time.unscaledTime + 1.5f;

            var player = Player.m_localPlayer;
            var database = ObjectDB.instance;
            List<string> trophyNames = player != null
                ? player.GetTrophies() : EmptyTrophies;
            int fingerprint = TrophyFingerprint(trophyNames);
            if (!force && player == _lastPlayer && database == _lastDatabase
                && fingerprint == _lastTrophyFingerprint)
                return;

            _lastPlayer = player;
            _lastTrophyFingerprint = fingerprint;
            if (database != _lastDatabase)
            {
                _lastDatabase = database;
                _cachedTotal = CountAvailableTrophies(database);
            }

            string selectedName = SelectedEntry?.PrefabName;
            _entries.Clear();
            if (player != null && database != null)
            {
                foreach (string prefabName in trophyNames)
                {
                    var prefab = database.GetItemPrefab(prefabName);
                    var item = prefab != null ? prefab.GetComponent<ItemDrop>() : null;
                    if (item == null) continue;
                    string name = Localize(item.m_itemData.m_shared.m_name);
                    if (name.EndsWith(" trophy", StringComparison.OrdinalIgnoreCase))
                        name = name.Substring(0, name.Length - 7);
                    _entries.Add(new Entry
                    {
                        PrefabName = prefabName,
                        Item = item,
                        Name = name,
                        Lore = LocalizedLore(item.m_itemData.m_shared.m_name),
                        Position = item.m_itemData.m_shared.m_trophyPos
                    });
                }
            }
            _entries.Sort((left, right) =>
            {
                int row = left.Position.y.CompareTo(right.Position.y);
                if (row != 0) return row;
                int column = left.Position.x.CompareTo(right.Position.x);
                return column != 0 ? column
                    : string.Compare(left.Name, right.Name,
                                     StringComparison.CurrentCultureIgnoreCase);
            });

            _selected = -1;
            if (!string.IsNullOrEmpty(selectedName))
                _selected = _entries.FindIndex(entry => entry.PrefabName == selectedName);
            if (_selected < 0 && _entries.Count > 0) _selected = 0;
            int last = LastPageStart(_entries.Count);
            _first = Mathf.Clamp(_first, 0, last);
            if (_selected >= 0 && (_selected < _first
                || _selected >= _first + VisibleCount))
                _first = Mathf.Clamp((_selected / VisibleCount) * VisibleCount, 0, last);

            int total = _cachedTotal;
            total = Mathf.Max(total, _entries.Count);
            _count.text = _entries.Count + " DISCOVERED"
                + (total > 0 ? "  ·  " + total + " TOTAL" : "");
            if (_progressFill != null)
                _progressFill.anchorMax = new Vector2(total > 0
                    ? Mathf.Clamp01(_entries.Count / (float)total) : 0f, 1f);
            Paint();
        }

        public void Tick()
        {
            if (_page.gameObject.activeInHierarchy) Refresh();
        }

        public bool Tap(RectTransform sourceRoot, Vector2 sourcePoint)
        {
            if (!_page.gameObject.activeInHierarchy || sourceRoot == null) return false;
            Vector3 world = sourceRoot.TransformPoint(sourcePoint);
            foreach (var button in _buttons)
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

        private void SelectVisible(int visible)
        {
            int index = _first + visible;
            if (index < 0 || index >= _entries.Count) return;
            _selected = index;
            Paint();
        }

        private void ChangePage(int direction)
        {
            if (_entries.Count <= VisibleCount) return;
            int last = LastPageStart(_entries.Count);
            int next = _first + direction * VisibleCount;
            _first = next < 0 ? last : next > last ? 0 : next;
            if (_selected < _first || _selected >= _first + VisibleCount)
                _selected = _first;
            Paint();
        }

        private void Paint()
        {
            for (int i = 0; i < _cells.Count; i++)
            {
                int index = _first + i;
                var cell = _cells[i];
                bool active = index >= 0 && index < _entries.Count;
                cell.Root.SetActive(active);
                if (!active) continue;
                var entry = _entries[index];
                cell.Icon.sprite = entry.Item.m_itemData.GetIcon();
                cell.Icon.enabled = cell.Icon.sprite != null;
                cell.Name.text = entry.Name;
                cell.Background.color = index == _selected ? Selected : Idle;
                UiSkin.SetSelected(cell.Accent, index == _selected);
            }

            int lastPage = LastPageStart(_entries.Count);
            if (_previousPageButton != null)
                _previousPageButton.interactable = lastPage > 0;
            if (_nextPageButton != null)
                _nextPageButton.interactable = lastPage > 0;

            _pageText.text = _entries.Count == 0 ? "NO TROPHIES DISCOVERED"
                : (_first + 1) + "–" + Mathf.Min(_first + VisibleCount, _entries.Count)
                  + " / " + _entries.Count;

            var selected = SelectedEntry;
            if (selected == null)
            {
                _detailIcon.enabled = false;
                _detailName.text = "UNDISCOVERED";
                _detailPosition.text = "";
                _detailLore.text = "Defeat creatures and collect their trophies to fill this page.";
                return;
            }
            _detailIcon.sprite = selected.Item.m_itemData.GetIcon();
            _detailIcon.enabled = _detailIcon.sprite != null;
            _detailName.text = selected.Name;
            _detailPosition.text = "COLLECTION " + (selected.Position.x + 1)
                                   + " · " + (selected.Position.y + 1);
            _detailLore.text = selected.Lore;
        }

        private Entry SelectedEntry => _selected >= 0 && _selected < _entries.Count
            ? _entries[_selected] : null;

        private static int LastPageStart(int count)
        {
            return count <= 0 ? 0 : ((count - 1) / VisibleCount) * VisibleCount;
        }

        private static readonly List<string> EmptyTrophies = new List<string>();

        private static int TrophyFingerprint(List<string> names)
        {
            unchecked
            {
                int hash = names.Count;
                foreach (string name in names)
                    hash = hash * 31 + (name?.GetHashCode() ?? 0);
                return hash;
            }
        }

        private static int CountAvailableTrophies(ObjectDB database)
        {
            if (database == null) return 0;
            var names = new HashSet<string>();
            foreach (var prefab in database.m_items)
            {
                var item = prefab != null ? prefab.GetComponent<ItemDrop>() : null;
                if (item != null && item.m_itemData.m_shared.m_itemType
                    == ItemDrop.ItemData.ItemType.Trophy)
                    names.Add(item.m_itemData.m_shared.m_name);
            }
            return names.Count;
        }

        private static string LocalizedLore(string itemToken)
        {
            string token = itemToken + "_lore";
            string lore = Localize(token);
            return string.IsNullOrWhiteSpace(lore) || lore == token
                ? "No lore has been recorded for this trophy."
                : lore;
        }

        private Cell CreateCell(Transform parent, float left, float top,
                                float width, float height, Action action)
        {
            var root = Panel(parent, "Trophy", left, top, width, height, Idle);
            var background = root.GetComponent<Image>();
            UiSkin.Control(background, root);
            var accent = UiSkin.SelectionAccent(root);
            background.raycastTarget = true;
            var button = root.gameObject.AddComponent<Button>();
            button.targetGraphic = background;
            button.transition = Selectable.Transition.None;
            button.navigation = new Navigation { mode = Navigation.Mode.None };
            button.onClick.AddListener(() => action());
            _buttons.Add(button);

            float iconSize = Mathf.Min(width - 20f, height - 48f);
            var icon = SpriteImage(root, "Icon", (width - iconSize) * 0.5f, 8f,
                                   iconSize, iconSize);
            icon.preserveAspect = true;
            var name = Label(root, "Name", "", 16f, Text,
                             TextAlignmentOptions.Center);
            Place(name.rectTransform, 6f, height - 40f, width - 12f, 34f);
            name.enableWordWrapping = true;
            name.enableAutoSizing = true;
            name.fontSizeMin = 11f;
            name.fontSizeMax = 16f;
            name.overflowMode = TextOverflowModes.Truncate;
            return new Cell
            {
                Root = root.gameObject,
                Background = background,
                Accent = accent,
                Icon = icon,
                Name = name,
                Button = button
            };
        }

        private Button ActionButton(Transform parent, string name, string label,
                                    float left, float top, float width, float height,
                                    Action action)
        {
            var root = Panel(parent, name, left, top, width, height, Idle);
            var image = root.GetComponent<Image>();
            bool arrow = label == "<" || label == ">"
                         || label == "\u2039" || label == "\u203a";
            if (arrow) UiSkin.Square(image, Idle);
            else UiSkin.Control(image, root);
            image.raycastTarget = true;
            var button = root.gameObject.AddComponent<Button>();
            button.targetGraphic = image;
            button.transition = Selectable.Transition.None;
            button.navigation = new Navigation { mode = Navigation.Mode.None };
            button.onClick.AddListener(() => action());
            var text = Label(root, "Label", label, arrow ? 27f : 19f, Gold,
                             TextAlignmentOptions.Center);
            text.fontStyle = FontStyles.Bold;
            text.rectTransform.offsetMin = new Vector2(7f, 4f);
            text.rectTransform.offsetMax = new Vector2(-7f, -4f);
            SecondScreenUI.FitText(text, arrow ? 18f : 10f,
                                   arrow ? 27f : 19f);
            _buttons.Add(button);
            return button;
        }

        private RectTransform Panel(Transform parent, string name, float left,
                                    float top, float width, float height, Color color)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Image));
            var rect = (RectTransform)go.transform;
            rect.SetParent(parent, false);
            Place(rect, left, top, width, height);
            var image = go.GetComponent<Image>();
            image.color = color;
            image.raycastTarget = false;
            return rect;
        }

        private Image SpriteImage(Transform parent, string name, float left,
                                  float top, float width, float height)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Image));
            var rect = (RectTransform)go.transform;
            rect.SetParent(parent, false);
            Place(rect, left, top, width, height);
            var image = go.GetComponent<Image>();
            image.raycastTarget = false;
            return image;
        }

        private TMP_Text Label(Transform parent, string name, string content,
                               float size, Color color,
                               TextAlignmentOptions alignment)
        {
            var text = SecondScreenUI.MakeText(parent, name, content, size, color,
                                               alignment);
            if (_font != null) text.font = _font;
            return text;
        }

        private static void Place(RectTransform rect, float left, float top,
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
