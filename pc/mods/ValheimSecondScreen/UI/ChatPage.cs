using System;
using System.Collections.Generic;
using System.Reflection;
using System.Text;
using HarmonyLib;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace ValheimSecondScreen.UI
{
    /// <summary>
    /// Native lower-display chat history, composer and touch keyboard. It reads
    /// Valheim's real chat buffer and sends through Chat itself, preserving the
    /// game's platform permission checks, filtering, ranges and slash commands.
    /// </summary>
    internal sealed class ChatPage
    {
        private const int MaxDraftLength = 200;
        private const int HistoryLines = 16;

        private static readonly FieldInfo ChatBufferField =
            AccessTools.Field(typeof(Terminal), "m_chatBuffer");

        private static readonly Color HistoryInk =
            new Color(0.018f, 0.013f, 0.009f, 0.68f);
        private static readonly Color InputInk =
            new Color(0.018f, 0.013f, 0.009f, 0.88f);

        private readonly RectTransform _page;
        private readonly TMP_FontAsset _font;
        private readonly Vector2 _size;
        private readonly List<Button> _buttons = new List<Button>();
        private readonly List<Button> _channelButtons = new List<Button>();
        private readonly List<Image> _channelAccents = new List<Image>();
        private readonly StringBuilder _builder = new StringBuilder(2048);

        private TMP_Text _history = null!;
        private TMP_Text _draftText = null!;
        private TMP_Text _draftCount = null!;
        private TMP_Text _status = null!;
        private Button _older = null!;
        private Button _newer = null!;
        private Button _shiftButton = null!;
        private Image _shiftAccent = null!;
        private Talker.Type _channel = Talker.Type.Normal;
        private string _draft = "";
        private bool _shift;
        private int _scrollOffset;
        private int _lastCount = -1;
        private int _lastHash;
        private int _paintedOffset = -1;
        private int _seenCount;
        private int _seenHash;
        private float _nextPoll;

        public bool HasUnread { get; private set; }

        public ChatPage(RectTransform page, TMP_FontAsset font, Vector2 size)
        {
            _page = page;
            _font = font;
            _size = size;
            Build();
            var initial = Buffer();
            _seenCount = initial.Count;
            _seenHash = BufferHash(initial);
            Refresh(true);
        }

        private void Build()
        {
            var title = Label(_page, "Title", "CHAT", 34f, UiTheme.Gold,
                              TextAlignmentOptions.Center);
            Place(title.rectTransform, 0f, 5f, _size.x, 44f);
            title.fontStyle = FontStyles.Bold;
            UiSkin.PageHeading(_page, title, 70f);

            var historyCard = Panel(_page, "History", 12f, 78f,
                                    _size.x - 24f, 392f, HistoryInk);
            // The two structural chat cards share Valheim's cropped panel grain;
            // inputs and individual keys remain clean control surfaces.
            UiSkin.Panel(historyCard, 48f);
            var historyLabel = Label(historyCard, "Heading", "WORLD CHAT", 19f,
                UiTheme.Gold, TextAlignmentOptions.Left);
            Place(historyLabel.rectTransform, 18f, 9f,
                  historyCard.rect.width - 150f, 30f);
            historyLabel.fontStyle = FontStyles.Bold;

            _history = Label(historyCard, "Messages", "", 20f, UiTheme.Text,
                             TextAlignmentOptions.BottomLeft);
            Place(_history.rectTransform, 18f, 50f,
                  historyCard.rect.width - 98f, historyCard.rect.height - 64f);
            _history.textWrappingMode = TextWrappingModes.Normal;
            _history.richText = true;
            _history.overflowMode = TextOverflowModes.Truncate;
            _history.lineSpacing = -2f;

            _older = ActionButton(historyCard, "Older", "▲",
                historyCard.rect.width - 70f, 54f, 54f, 54f, () => Scroll(1), true);
            _newer = ActionButton(historyCard, "Newer", "▼",
                historyCard.rect.width - 70f, historyCard.rect.height - 70f,
                54f, 54f, () => Scroll(-1), true);

            const float channelTop = 480f;
            const float channelHeight = 54f;
            string[] channelNames = { "SAY", "WHISPER", "SHOUT" };
            Talker.Type[] channels =
                { Talker.Type.Normal, Talker.Type.Whisper, Talker.Type.Shout };
            float channelWidth = 150f;
            for (int i = 0; i < channelNames.Length; i++)
            {
                int index = i;
                Button button = ActionButton(_page, "Channel_" + channelNames[i],
                    channelNames[i], 12f + i * (channelWidth + 8f), channelTop,
                    channelWidth, channelHeight, () => SetChannel(channels[index]));
                _channelButtons.Add(button);
                var buttonRect = button.transform as RectTransform;
                _channelAccents.Add(buttonRect != null
                    ? UiSkin.SelectionAccent(buttonRect, true) : null!);
            }

            _status = Label(_page, "Status", "LOCAL RANGE", 15f, UiTheme.Muted,
                            TextAlignmentOptions.Right);
            Place(_status.rectTransform, 490f, channelTop + 7f,
                  _size.x - 502f, 38f);
            SecondScreenUI.FitText(_status, 10f, 15f);

            const float composerTop = 542f;
            var composer = Panel(_page, "Composer", 12f, composerTop,
                                 _size.x - 24f, 74f, UiTheme.PanelRaised);
            var input = Panel(composer, "Input", 10f, 9f,
                              composer.rect.width - 174f, 56f, InputInk);
            _draftText = Label(input, "Draft", "TAP KEYS BELOW TO CHAT…", 21f,
                UiTheme.Muted, TextAlignmentOptions.Left);
            _draftText.richText = false;
            Place(_draftText.rectTransform, 14f, 4f, input.rect.width - 116f, 48f);
            SecondScreenUI.FitText(_draftText, 12f, 21f);
            _draftCount = Label(input, "CharacterCount", "0 / 200", 13f,
                UiTheme.Muted, TextAlignmentOptions.Right);
            Place(_draftCount.rectTransform, input.rect.width - 98f, 6f, 84f, 44f);
            SecondScreenUI.FitText(_draftCount, 10f, 13f);
            ActionButton(composer, "Send", "SEND", composer.rect.width - 154f,
                         9f, 144f, 56f, Send);
            UiSkin.Panel(composer, raised: true);

            BuildKeyboard(628f, _size.y - 640f);
            PaintChannels();
        }

        private void BuildKeyboard(float top, float height)
        {
            string[][] rows =
            {
                new[] { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
                new[] { "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P" },
                new[] { "A", "S", "D", "F", "G", "H", "J", "K", "L" },
                new[] { "SHIFT", "Z", "X", "C", "V", "B", "N", "M", "BACK" },
                new[] { "/", ",", "'", "SPACE", "-", ".", "?", "!", "CLEAR" }
            };
            float[] rowIndent = { 0f, 0f, 48f, 0f, 0f };
            float[] weightsLast = { 1f, 1f, 1f, 3.6f, 1f, 1f, 1f, 1f, 1.5f };
            const float margin = 12f;
            const float gap = 7f;
            float rowHeight = Mathf.Max(UiTheme.MinimumTouch,
                (height - gap * (rows.Length - 1)) / rows.Length);

            for (int row = 0; row < rows.Length; row++)
            {
                string[] keys = rows[row];
                float indent = rowIndent[row];
                float available = _size.x - margin * 2f - indent * 2f
                                  - gap * (keys.Length - 1);
                float totalWeight = 0f;
                for (int i = 0; i < keys.Length; i++)
                    totalWeight += row == rows.Length - 1 ? weightsLast[i]
                        : (keys[i] == "SHIFT" || keys[i] == "BACK" ? 1.45f : 1f);

                float x = margin + indent;
                for (int i = 0; i < keys.Length; i++)
                {
                    string key = keys[i];
                    float weight = row == rows.Length - 1 ? weightsLast[i]
                        : (key == "SHIFT" || key == "BACK" ? 1.45f : 1f);
                    float width = available * weight / totalWeight;
                    Action action = () => Press(key);
                    Button button = ActionButton(_page, "Key_" + row + "_" + i, KeyLabel(key),
                        x, top + row * (rowHeight + gap), width, rowHeight, action,
                        width <= rowHeight * 1.18f);
                    if (key == "SHIFT")
                    {
                        _shiftButton = button;
                        _shiftAccent = UiSkin.SelectionAccent(
                            (RectTransform)button.transform, true);
                    }
                    x += width + gap;
                }
            }
        }

        public void Refresh(bool force = false)
        {
            List<string> buffer = Buffer();
            _scrollOffset = Mathf.Clamp(_scrollOffset, 0, Mathf.Max(0, buffer.Count - 1));
            int hash = BufferHash(buffer);
            if (!force && buffer.Count == _lastCount && hash == _lastHash
                && _paintedOffset == _scrollOffset) return;

            _lastCount = buffer.Count;
            _lastHash = hash;
            _paintedOffset = _scrollOffset;
            int end = Mathf.Clamp(buffer.Count - _scrollOffset, 0, buffer.Count);
            int start = Mathf.Max(0, end - HistoryLines);
            _builder.Length = 0;
            for (int i = start; i < end; i++)
            {
                if (string.IsNullOrWhiteSpace(buffer[i])) continue;
                _builder.Append(buffer[i]);
                if (i + 1 < end) _builder.Append('\n');
            }
            _history.text = _builder.Length > 0
                ? _builder.ToString() : "No chat messages yet.";
            _older.interactable = start > 0;
            _newer.interactable = _scrollOffset > 0;
            PaintDraft();
        }

        /// <summary>Tracks new messages without rebuilding a hidden page.</summary>
        public void Poll(bool visible)
        {
            float now = Time.unscaledTime;
            if (now < _nextPoll && !(visible && HasUnread)) return;
            _nextPoll = now + (visible ? 0.10f : 0.40f);

            List<string> buffer = Buffer();
            int hash = BufferHash(buffer);
            if (visible)
            {
                _seenCount = buffer.Count;
                _seenHash = hash;
                HasUnread = false;
                Refresh();
                return;
            }
            HasUnread = buffer.Count != _seenCount || hash != _seenHash;
        }

        public bool Tap(RectTransform root, Vector2 rootPoint)
        {
            foreach (Button button in _buttons)
            {
                if (button == null || !button.gameObject.activeInHierarchy
                    || !button.interactable) continue;
                var buttonRect = button.transform as RectTransform;
                if (buttonRect == null || !Contains(root, buttonRect, rootPoint)) continue;
                button.onClick.Invoke();
                return true;
            }
            return false;
        }

        private void Press(string key)
        {
            switch (key)
            {
                case "SHIFT":
                    _shift = !_shift;
                    break;
                case "BACK":
                    if (_draft.Length > 0)
                        _draft = _draft.Substring(0, _draft.Length - 1);
                    break;
                case "CLEAR":
                    _draft = "";
                    break;
                case "SPACE":
                    Append(" ");
                    break;
                default:
                    Append(_shift ? key.ToUpperInvariant() : key.ToLowerInvariant());
                    _shift = false;
                    break;
            }
            PaintDraft();
        }

        private void Append(string value)
        {
            if (_draft.Length >= MaxDraftLength) return;
            value = value.Replace("<", "").Replace(">", "");
            int room = MaxDraftLength - _draft.Length;
            _draft += value.Length <= room ? value : value.Substring(0, room);
        }

        private void Send()
        {
            string text = _draft.Trim();
            Chat chat = Chat.instance;
            if (chat == null)
            {
                _status.text = "CHAT IS NOT READY";
                return;
            }
            if (text.Length == 0) return;

            try
            {
                if (text[0] == '/')
                    chat.TryRunCommand(text.Substring(1));
                else
                    chat.SendText(_channel, text);
                _draft = "";
                _shift = false;
                _scrollOffset = 0;
                Refresh(true);
            }
            catch (Exception error)
            {
                Plugin.Log.LogWarning("[SecondScreen] Chat send failed: " + error.Message);
                _status.text = "MESSAGE COULD NOT BE SENT";
            }
        }

        private void SetChannel(Talker.Type channel)
        {
            _channel = channel;
            PaintChannels();
        }

        private void PaintChannels()
        {
            for (int i = 0; i < _channelButtons.Count; i++)
            {
                bool selected = (i == 0 && _channel == Talker.Type.Normal)
                    || (i == 1 && _channel == Talker.Type.Whisper)
                    || (i == 2 && _channel == Talker.Type.Shout);
                Image image = _channelButtons[i].GetComponent<Image>();
                image.color = selected ? UiTheme.ControlSelected : UiTheme.Control;
                UiSkin.SetSelected(_channelAccents[i], selected);
            }
            _status.text = _channel == Talker.Type.Shout ? "WIDE RANGE"
                : _channel == Talker.Type.Whisper ? "NEARBY ONLY" : "LOCAL RANGE";
        }

        private void PaintDraft()
        {
            if (_draftText == null) return;
            if (_draftCount != null)
            {
                _draftCount.text = _draft.Length + " / " + MaxDraftLength;
                _draftCount.color = _draft.Length >= MaxDraftLength
                    ? UiTheme.Bad : _draft.Length >= MaxDraftLength * 0.9f
                        ? UiTheme.Gold : UiTheme.Muted;
            }
            if (_shiftButton != null)
            {
                Image image = _shiftButton.GetComponent<Image>();
                if (image != null)
                    image.color = _shift ? UiTheme.ControlSelected : UiTheme.Control;
                UiSkin.SetSelected(_shiftAccent, _shift);
            }
            if (_draft.Length == 0)
            {
                _draftText.text = "TAP KEYS BELOW TO CHAT…";
                _draftText.color = UiTheme.Muted;
            }
            else
            {
                _draftText.text = _draft + (_shift ? "  ⇧" : "");
                _draftText.color = UiTheme.Text;
            }
        }

        private void Scroll(int direction)
        {
            List<string> buffer = Buffer();
            _scrollOffset = Mathf.Clamp(_scrollOffset + direction * 6,
                                        0, Mathf.Max(0, buffer.Count - 1));
            Refresh(true);
        }

        private static List<string> Buffer()
        {
            try
            {
                return Chat.instance != null
                    ? ChatBufferField?.GetValue(Chat.instance) as List<string>
                      ?? EmptyBuffer
                    : EmptyBuffer;
            }
            catch { return EmptyBuffer; }
        }

        private static readonly List<string> EmptyBuffer = new List<string>();

        private static int BufferHash(List<string> buffer)
        {
            unchecked
            {
                int hash = buffer.Count;
                int start = Mathf.Max(0, buffer.Count - 24);
                for (int i = start; i < buffer.Count; i++)
                    hash = hash * 31 + (buffer[i]?.GetHashCode() ?? 0);
                return hash;
            }
        }

        private Button ActionButton(Transform parent, string name, string label,
                                    float left, float top, float width, float height,
                                    Action action, bool square = false)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Image), typeof(Button));
            go.transform.SetParent(parent, false);
            var rect = (RectTransform)go.transform;
            Place(rect, left, top, width, height);
            var image = go.GetComponent<Image>();
            image.color = UiTheme.Control;
            if (square) UiSkin.Square(image, image.color);
            else UiSkin.Rectangle(image, image.color);
            var button = go.GetComponent<Button>();
            button.targetGraphic = image;
            button.navigation = new Navigation { mode = Navigation.Mode.None };
            button.onClick.AddListener(() => action());
            var text = Label(rect, "Label", label, 19f, UiTheme.Gold,
                             TextAlignmentOptions.Center);
            text.rectTransform.offsetMin = new Vector2(5f, 3f);
            text.rectTransform.offsetMax = new Vector2(-5f, -3f);
            SecondScreenUI.FitText(text, 10f, 19f);
            _buttons.Add(button);
            return button;
        }

        private RectTransform Panel(Transform parent, string name, float left,
                                    float top, float width, float height, Color tint)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Image));
            go.transform.SetParent(parent, false);
            var rect = (RectTransform)go.transform;
            Place(rect, left, top, width, height);
            var image = go.GetComponent<Image>();
            image.color = tint;
            image.raycastTarget = false;
            return rect;
        }

        private TMP_Text Label(Transform parent, string name, string value, float size,
                               Color colour, TextAlignmentOptions alignment)
        {
            var go = new GameObject(name, typeof(RectTransform));
            go.transform.SetParent(parent, false);
            var text = go.AddComponent<TextMeshProUGUI>();
            if (_font != null) text.font = _font;
            text.text = value;
            text.fontSize = size;
            text.color = colour;
            text.alignment = alignment;
            text.raycastTarget = false;
            var rect = (RectTransform)go.transform;
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.one;
            rect.offsetMin = Vector2.zero;
            rect.offsetMax = Vector2.zero;
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

        private static bool Contains(RectTransform root, RectTransform target,
                                     Vector2 rootPoint)
        {
            if (root == null || target == null) return false;
            Vector3 local = target.InverseTransformPoint(root.TransformPoint(rootPoint));
            return target.rect.Contains(new Vector2(local.x, local.y));
        }

        private static string KeyLabel(string key)
        {
            if (key == "BACK") return "⌫";
            if (key == "SHIFT") return "⇧";
            return key;
        }
    }
}
