using System.Collections.Generic;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace ValheimSecondScreen.UI
{
    /// <summary>
    /// Every skill the player has raised, with how far into the next level it is.
    /// </summary>
    /// <remarks>
    /// <para>
    /// The game shows this behind two clicks and a panel that covers the screen,
    /// which is exactly the sort of thing a second screen is for. The numbers are
    /// the live ones from <c>Skills.GetSkillList</c>, and the names come from
    /// <c>Localization</c>, so this reads in whatever language the game is in.
    /// </para>
    /// <para>
    /// Every skill the game has, sorted by level, highest first — the ones at
    /// zero greyed at the bottom rather than absent, because "what else is there
    /// to train" is a question this page should answer.
    /// </para>
    /// </remarks>
    internal sealed class SkillsPage
    {
        private const float RowHeight = 66f;
        private const float RowGap = 6f;
        private const int PageCapacity = 24;

        private static readonly Color RowInk = UiTheme.Control;
        private static readonly Color GoldText = UiTheme.GoldBright;
        private static readonly Color Gold = UiTheme.Gold;
        private static readonly Color Weak = UiTheme.Muted;

        private readonly RectTransform _page;
        private readonly TMP_FontAsset _font;
        private readonly Vector2 _pageSize;
        private readonly List<Row> _rows = new List<Row>();
        private readonly List<Button> _buttons = new List<Button>();

        private RectTransform _list;
        private TMP_Text _pageText;
        private TMP_Text _summary;
        private Button _previousButton;
        private Button _nextButton;
        private int _pageIndex;
        private int _pageCount = 1;
        private float _nextRefreshAt;

        /// <summary>One skill's numbers, whether or not it has been used.</summary>
        private struct Entry
        {
            public Skills.SkillType Type;
            public float Level;
            public float Progress;
            public Sprite Icon;
        }

        private sealed class Row
        {
            public GameObject Root;
            public Image Icon;
            public TMP_Text Name;
            public TMP_Text Level;
            public RectTransform Fill;
        }

        public SkillsPage(RectTransform page, TMP_FontAsset font, Vector2 pageSize)
        {
            _page = page;
            _font = font;
            _pageSize = pageSize;
            Build();
        }

        private void Build()
        {
            var title = SecondScreenUI.MakeText(_page, "Title", "SKILLS", 36f, GoldText);
            if (_font != null) title.font = _font;
            var titleRect = (RectTransform)title.transform;
            titleRect.anchorMin = new Vector2(0f, 1f);
            titleRect.anchorMax = new Vector2(1f, 1f);
            titleRect.pivot = new Vector2(0.5f, 1f);
            titleRect.sizeDelta = new Vector2(0f, 58f);
            titleRect.anchoredPosition = Vector2.zero;
            title.fontStyle = FontStyles.Bold;
            UiSkin.PageHeading(_page, title, 70f);

            _summary = SecondScreenUI.MakeText(_page, "Summary",
                "0 TRAINED  ·  TOTAL LEVEL 0  ·  HIGHEST 0", 15f, Weak);
            if (_font != null) _summary.font = _font;
            var summaryRect = _summary.rectTransform;
            summaryRect.anchorMin = new Vector2(0f, 1f);
            summaryRect.anchorMax = new Vector2(1f, 1f);
            summaryRect.pivot = new Vector2(0.5f, 1f);
            summaryRect.anchoredPosition = new Vector2(0f, -43f);
            summaryRect.sizeDelta = new Vector2(-220f, 22f);
            _summary.fontStyle = FontStyles.Bold;
            SecondScreenUI.FitText(_summary, 10f, 15f);

            _previousButton = NavButton("Previous", "\u2039",
                _pageSize.x - 188f, () => ChangePage(-1));
            _pageText = SecondScreenUI.MakeText(_page, "Page", "1 / 1", 18f, Weak);
            if (_font != null) _pageText.font = _font;
            var pageRect = (RectTransform)_pageText.transform;
            pageRect.anchorMin = new Vector2(1f, 1f);
            pageRect.anchorMax = new Vector2(1f, 1f);
            pageRect.pivot = new Vector2(1f, 1f);
            pageRect.anchoredPosition = new Vector2(-70f, 0f);
            pageRect.sizeDelta = new Vector2(112f, 58f);
            SecondScreenUI.FitText(_pageText, 11f, 18f);
            _nextButton = NavButton("Next", "\u203a",
                _pageSize.x - 62f, () => ChangePage(1));

            var listGO = new GameObject("List", typeof(RectTransform));
            _list = (RectTransform)listGO.transform;
            _list.SetParent(_page, false);
            _list.anchorMin = new Vector2(0f, 1f);
            _list.anchorMax = new Vector2(0f, 1f);
            _list.pivot = new Vector2(0f, 1f);
            _list.sizeDelta = new Vector2(_pageSize.x - 32f, _pageSize.y - 86f);
            _list.anchoredPosition = new Vector2(16f, -76f);
        }

        /// <summary>
        /// Rebuilds the list, at most twice a second.
        /// </summary>
        /// <remarks>
        /// A skill moves by a fraction of a percent per swing. Reading and
        /// sorting the whole list every frame would cost more than the panel is
        /// worth, and nothing on it would look any different.
        /// </remarks>
        public void Refresh()
        {
            if (Time.unscaledTime < _nextRefreshAt) return;
            _nextRefreshAt = Time.unscaledTime + 0.5f;

            var player = Player.m_localPlayer;
            var skills = player != null ? player.GetSkills() : null;
            if (skills == null)
            {
                foreach (var row in _rows) row.Root.SetActive(false);
                return;
            }

            /*
             * Every skill in the game, not only the ones already raised.
             *
             * GetSkillList returns what the player has actually used, so a fresh
             * character's page was nearly empty and a skill appeared out of
             * nowhere the first time it was touched. The enum is the full set;
             * the ones at zero are shown greyed, which is also the answer to
             * "what else is there to train".
             */
            var live = new Dictionary<Skills.SkillType, Skills.Skill>();
            foreach (var skill in skills.GetSkillList())
                if (skill != null && skill.m_info != null)
                    live[skill.m_info.m_skill] = skill;

            var list = new List<Entry>();
            var added = new HashSet<Skills.SkillType>();
            foreach (Skills.SkillDef definition in skills.m_skills)
            {
                if (definition == null) continue;
                Skills.SkillType type = definition.m_skill;
                if (type == Skills.SkillType.None || type == Skills.SkillType.All) continue;
                if (!added.Add(type)) continue;
                live.TryGetValue(type, out var exact);
                float level = skills.GetSkillLevel(type);
                list.Add(new Entry
                {
                    Type = type,
                    Level = level,
                    Progress = level >= Skills.c_MaxSkillLevel
                        ? 1f : exact != null ? exact.GetLevelPercentage() : 0f,
                    Icon = definition.m_icon,
                });
            }
            list.Sort((a, b) =>
            {
                int level = b.Level.CompareTo(a.Level);
                return level != 0 ? level : a.Type.CompareTo(b.Type);
            });

            _pageCount = Mathf.Max(1, Mathf.CeilToInt(list.Count / (float)PageCapacity));
            _pageIndex = Mathf.Clamp(_pageIndex, 0, _pageCount - 1);
            bool multiplePages = _pageCount > 1;
            if (_pageText != null)
            {
                _pageText.text = (_pageIndex + 1) + " / " + _pageCount;
                _pageText.gameObject.SetActive(multiplePages);
            }
            if (_previousButton != null)
            {
                _previousButton.gameObject.SetActive(multiplePages);
                _previousButton.interactable = _pageIndex > 0;
            }
            if (_nextButton != null)
            {
                _nextButton.gameObject.SetActive(multiplePages);
                _nextButton.interactable = _pageIndex < _pageCount - 1;
            }

            int trained = 0;
            int totalLevel = 0;
            int highest = 0;
            foreach (Entry entry in list)
            {
                int level = Mathf.FloorToInt(entry.Level);
                if (level > 0) trained++;
                totalLevel += level;
                highest = Mathf.Max(highest, level);
            }
            if (_summary != null)
                _summary.text = trained + " TRAINED  ·  TOTAL LEVEL " + totalLevel
                                + "  ·  HIGHEST " + highest;
            int first = _pageIndex * PageCapacity;
            int count = Mathf.Min(PageCapacity, list.Count - first);

            while (_rows.Count < PageCapacity) _rows.Add(CreateRow());

            for (int i = 0; i < _rows.Count; i++)
            {
                var row = _rows[i];
                if (i >= count)
                {
                    row.Root.SetActive(false);
                    continue;
                }

                var skill = list[first + i];
                row.Root.SetActive(true);
                int column = i / 12;
                int line = i % 12;
                float width = (_pageSize.x - 40f) * 0.5f;
                var rowRect = (RectTransform)row.Root.transform;
                rowRect.sizeDelta = new Vector2(width, RowHeight);
                rowRect.anchoredPosition = new Vector2(
                    column * (width + 8f), -line * (RowHeight + RowGap));

                row.Name.text = Localization.instance.Localize(
                    "$skill_" + skill.Type.ToString().ToLower());
                row.Icon.sprite = skill.Icon;
                row.Icon.enabled = skill.Icon != null;
                row.Level.text = skill.Level >= Skills.c_MaxSkillLevel
                    ? "LV 100  ·  MAX"
                    : "LV " + (int)skill.Level + "  ·  "
                      + Mathf.RoundToInt(skill.Progress * 100f) + "%";
                row.Fill.anchorMax = new Vector2(Mathf.Clamp01(skill.Progress), 1f);
                row.Name.color = skill.Level > 0f ? GoldText : Weak;
            }

            _list.sizeDelta = new Vector2(_pageSize.x - 32f, _pageSize.y - 86f);
        }

        public bool Tap(RectTransform sourceRoot, Vector2 sourcePoint)
        {
            if (!_page.gameObject.activeInHierarchy || sourceRoot == null) return false;
            Vector3 world = sourceRoot.TransformPoint(sourcePoint);
            foreach (Button button in _buttons)
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

        private void ChangePage(int direction)
        {
            if (_pageCount <= 1) return;
            _pageIndex = Mathf.Clamp(_pageIndex + direction, 0, _pageCount - 1);
            _nextRefreshAt = 0f;
            Refresh();
        }

        private Button NavButton(string name, string text, float x, System.Action action)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Image), typeof(Button));
            var rect = (RectTransform)go.transform;
            rect.SetParent(_page, false);
            rect.anchorMin = new Vector2(0f, 1f);
            rect.anchorMax = new Vector2(0f, 1f);
            rect.pivot = new Vector2(0f, 1f);
            rect.anchoredPosition = new Vector2(x, 0f);
            rect.sizeDelta = new Vector2(56f, 56f);
            var image = go.GetComponent<Image>();
            UiSkin.Square(image,
                new Color(0.055f, 0.039f, 0.025f, 0.78f));
            var button = go.GetComponent<Button>();
            button.targetGraphic = image;
            button.navigation = new Navigation { mode = Navigation.Mode.None };
            button.onClick.AddListener(() => action());
            var label = SecondScreenUI.MakeText(rect, "Label", text, 31f, GoldText);
            if (_font != null) label.font = _font;
            label.rectTransform.offsetMin = new Vector2(7f, 5f);
            label.rectTransform.offsetMax = new Vector2(-7f, -5f);
            SecondScreenUI.FitText(label, 20f, 31f);
            _buttons.Add(button);
            return button;
        }

        private Row CreateRow()
        {
            var go = new GameObject("Skill", typeof(RectTransform), typeof(Image));
            var rect = (RectTransform)go.transform;
            rect.SetParent(_list, false);
            rect.anchorMin = new Vector2(0f, 1f);
            rect.anchorMax = new Vector2(0f, 1f);
            rect.pivot = new Vector2(0f, 1f);
            rect.sizeDelta = new Vector2((_pageSize.x - 40f) * 0.5f, RowHeight);

            var frame = go.GetComponent<Image>();
            UiSkin.Rectangle(frame,
                new Color(0.025f, 0.018f, 0.012f, 0.62f));
            frame.raycastTarget = false;

            // The progress bar is behind the text rather than beside it: a row
            // that fills as you level reads without having to be measured.
            var barGO = new GameObject("Progress", typeof(RectTransform), typeof(Image));
            var barRect = (RectTransform)barGO.transform;
            barRect.SetParent(rect, false);
            barRect.anchorMin = Vector2.zero;
            barRect.anchorMax = new Vector2(0f, 1f);
            barRect.pivot = new Vector2(0f, 0.5f);
            barRect.offsetMin = new Vector2(2f, 2f);
            barRect.offsetMax = new Vector2(0f, -2f);
            var bar = barGO.GetComponent<Image>();
            bar.color = new Color(Gold.r, Gold.g, Gold.b, 0.22f);
            bar.raycastTarget = false;

            var iconGO = new GameObject("Icon", typeof(RectTransform), typeof(Image));
            var iconRect = (RectTransform)iconGO.transform;
            iconRect.SetParent(rect, false);
            iconRect.anchorMin = new Vector2(0f, 0.5f);
            iconRect.anchorMax = new Vector2(0f, 0.5f);
            iconRect.pivot = new Vector2(0f, 0.5f);
            iconRect.anchoredPosition = new Vector2(10f, 0f);
            iconRect.sizeDelta = new Vector2(48f, 48f);
            var icon = iconGO.GetComponent<Image>();
            icon.preserveAspect = true;
            icon.raycastTarget = false;

            var name = SecondScreenUI.MakeText(rect, "Name", "", 26f, GoldText,
                                               TextAlignmentOptions.Left);
            if (_font != null) name.font = _font;
            var nameRect = (RectTransform)name.transform;
            nameRect.offsetMin = new Vector2(70f, 0f);
            nameRect.offsetMax = new Vector2(-150f, 0f);
            SecondScreenUI.FitText(name, 12f, 26f);

            var level = SecondScreenUI.MakeText(rect, "Level", "", 21f, Color.white,
                                                TextAlignmentOptions.Right);
            if (_font != null) level.font = _font;
            var levelRect = (RectTransform)level.transform;
            levelRect.offsetMin = new Vector2(0f, 0f);
            levelRect.offsetMax = new Vector2(-16f, 0f);
            SecondScreenUI.FitText(level, 11f, 21f);

            return new Row
            {
                Root = go,
                Icon = icon,
                Name = name,
                Level = level,
                Fill = barRect,
            };
        }
    }
}
