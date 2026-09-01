using System;
using System.Collections.Generic;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace ValheimSecondScreen.UI
{
    /// <summary>Touch tools for the independent lower-screen map.</summary>
    internal sealed class MapToolbar
    {
        private enum MapTool
        {
            None,
            Pin,
            Ping,
            Remove
        }

        private static readonly Color Idle = UiTheme.Control;
        private static readonly Color Selected = UiTheme.ControlSelected;
        private static readonly Color Public = new Color(0.22f, 0.46f, 0.16f, 0.72f);
        private static readonly Color Gold = UiTheme.Gold;
        private static readonly Color Text = UiTheme.Text;

        private readonly RectTransform _root;
        private readonly RectTransform _viewport;
        private readonly TMP_FontAsset _font;
        private readonly List<ButtonVisual> _buttons = new List<ButtonVisual>();
        private readonly List<ButtonVisual> _pinButtons = new List<ButtonVisual>();

        private MapTool _tool;
        private Minimap.PinType _pinType = Minimap.PinType.Icon0;
        private ButtonVisual _ping;
        private ButtonVisual _remove;
        private ButtonVisual _publicPosition;
        private float _nextPaint;

        private sealed class ButtonVisual
        {
            public Button Button;
            public Image Background;
            public Image Accent;
            public TMP_Text Label;
            public Minimap.PinType PinType;
        }

        public MapToolbar(RectTransform root, RectTransform viewport,
                          TMP_FontAsset font, Vector2 size)
        {
            _root = root;
            _viewport = viewport;
            _font = font;
            Build(size);
            Paint();
        }

        private void Build(Vector2 size)
        {
            const float margin = 8f;
            const float gap = 6f;
            const float height = 76f;
            const float pinWidth = 64f;
            float x = margin;

            Minimap.PinType[] pinTypes =
            {
                Minimap.PinType.Icon0,
                Minimap.PinType.Icon1,
                Minimap.PinType.Icon2,
                Minimap.PinType.Icon3,
                Minimap.PinType.Icon4
            };
            for (int i = 0; i < pinTypes.Length; i++)
            {
                Minimap.PinType type = pinTypes[i];
                var button = CreateButton("Pin" + (i + 1), x, 6f, pinWidth, height,
                    "PIN " + (i + 1), FindPinSprite(type), () => SelectPin(type));
                button.PinType = type;
                _pinButtons.Add(button);
                x += pinWidth + gap;
            }

            _ping = CreateButton("Ping", x, 6f, 88f, height, "PING", null,
                                  () => SelectTool(MapTool.Ping));
            x += 94f;
            _remove = CreateButton("Remove", x, 6f, 126f, height, "REMOVE", null,
                                     () => SelectTool(MapTool.Remove));
            x += 132f;
            CreateButton("Centre", x, 6f, 126f, height, "CENTER", null,
                         LargeMap.Centre);
            x += 132f;

            float publicWidth = Mathf.Max(150f, size.x - x - margin);
            _publicPosition = CreateButton("PublicPosition", x, 6f, publicWidth, height,
                                           "VISIBLE TO PLAYERS\nOFF", null,
                                           TogglePublicPosition);

            // Keep zoom on the map itself, aligned above the public-position
            // control. This leaves the action strip less cramped and matches
            // the familiar vertical map-control placement.
            const float zoomSize = 62f;
            const float zoomMargin = 12f;
            float zoomX = Mathf.Max(zoomMargin,
                _viewport.rect.width - zoomMargin - zoomSize);
            CreateButton(_viewport, "ZoomIn", zoomX,
                zoomMargin + zoomSize + gap, zoomSize, zoomSize, "+", null,
                () => MapZoom.Zoom(-1));
            CreateButton(_viewport, "ZoomOut", zoomX,
                zoomMargin, zoomSize, zoomSize, "−", null,
                () => MapZoom.Zoom(1));
        }

        public void Tick()
        {
            if (!_root.gameObject.activeInHierarchy || Time.unscaledTime < _nextPaint) return;
            _nextPaint = Time.unscaledTime + 0.25f;
            Paint();
        }

        public bool Tap(RectTransform sourceRoot, Vector2 sourcePoint)
        {
            if (!_root.gameObject.activeInHierarchy || sourceRoot == null) return false;
            Vector3 worldPoint = sourceRoot.TransformPoint(sourcePoint);
            foreach (ButtonVisual visual in _buttons)
            {
                if (visual.Button == null || !visual.Button.gameObject.activeInHierarchy
                    || !visual.Button.interactable) continue;
                var rect = visual.Button.transform as RectTransform;
                Vector3 local = rect.InverseTransformPoint(worldPoint);
                if (!rect.rect.Contains(new Vector2(local.x, local.y))) continue;
                visual.Button.onClick.Invoke();
                return true;
            }

            Vector3 mapLocal = _viewport.InverseTransformPoint(worldPoint);
            Rect mapRect = _viewport.rect;
            if (!mapRect.Contains(new Vector2(mapLocal.x, mapLocal.y))
                || _tool == MapTool.None) return false;

            float nx = Mathf.InverseLerp(mapRect.xMin, mapRect.xMax, mapLocal.x);
            float ny = Mathf.InverseLerp(mapRect.yMin, mapRect.yMax, mapLocal.y);
            if (!LargeMap.TryWorldPoint(nx, ny, out Vector3 world)) return false;

            if (_tool == MapTool.Pin) AddPin(world);
            else if (_tool == MapTool.Ping) Ping(world);
            else if (_tool == MapTool.Remove) RemovePin(world);
            if (!SecondScreenPreferences.KeepMapToolSelected)
            {
                _tool = MapTool.None;
                Paint();
            }
            return true;
        }

        private void SelectPin(Minimap.PinType type)
        {
            if (_tool == MapTool.Pin && _pinType == type)
                _tool = MapTool.None;
            else
            {
                _tool = MapTool.Pin;
                _pinType = type;
            }
            Paint();
        }

        private void SelectTool(MapTool tool)
        {
            _tool = _tool == tool ? MapTool.None : tool;
            Paint();
        }

        private void AddPin(Vector3 world)
        {
            var minimap = Minimap.instance;
            if (minimap == null || Game.m_noMap) return;
            var pin = minimap.AddPin(world, _pinType, "", true, false, 0L);
            var player = Player.m_localPlayer;
            if (player != null)
                player.Message(MessageHud.MessageType.TopLeft, "$msg_pin_added", 0,
                               pin != null ? pin.m_icon : null);
        }

        private static void Ping(Vector3 world)
        {
            if (Chat.instance != null) Chat.instance.SendPing(world);
        }

        private static void RemovePin(Vector3 world)
        {
            LargeMap.RemovePinAt(world);
        }

        private void TogglePublicPosition()
        {
            var network = ZNet.instance;
            if (network == null) return;
            network.SetPublicReferencePosition(!network.IsReferencePositionPublic());
            Paint();
        }

        private void Paint()
        {
            foreach (ButtonVisual pin in _pinButtons)
            {
                pin.Background.color = _tool == MapTool.Pin && pin.PinType == _pinType
                    ? Selected : Idle;
                UiSkin.SetSelected(pin.Accent,
                    _tool == MapTool.Pin && pin.PinType == _pinType);
            }
            if (_ping != null)
            {
                _ping.Background.color = _tool == MapTool.Ping ? Selected : Idle;
                UiSkin.SetSelected(_ping.Accent, _tool == MapTool.Ping);
            }
            if (_remove != null)
            {
                _remove.Background.color = _tool == MapTool.Remove ? Selected : Idle;
                UiSkin.SetSelected(_remove.Accent, _tool == MapTool.Remove);
            }

            bool isPublic = ZNet.instance != null
                            && ZNet.instance.IsReferencePositionPublic();
            if (_publicPosition != null)
            {
                _publicPosition.Background.color = isPublic ? Public : Idle;
                UiSkin.SetSelected(_publicPosition.Accent, isPublic);
                _publicPosition.Label.text = "VISIBLE TO PLAYERS\n"
                                             + (isPublic ? "ON" : "OFF");
            }
        }

        private ButtonVisual CreateButton(string name, float left, float bottom,
                                          float width, float height, string label,
                                          Sprite icon, Action action)
        {
            return CreateButton(_root, name, left, bottom, width, height,
                                label, icon, action);
        }

        private ButtonVisual CreateButton(RectTransform parent, string name,
                                          float left, float bottom,
                                          float width, float height, string label,
                                          Sprite icon, Action action)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Image), typeof(Button));
            var rect = (RectTransform)go.transform;
            rect.SetParent(parent, false);
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.zero;
            rect.pivot = Vector2.zero;
            rect.anchoredPosition = new Vector2(left, bottom);
            rect.sizeDelta = new Vector2(width, height);

            var background = go.GetComponent<Image>();
            UiSkin.Control(background, rect);
            var accent = UiSkin.SelectionAccent(rect, true);
            background.color = Idle;
            background.raycastTarget = true;
            var button = go.GetComponent<Button>();
            button.targetGraphic = background;
            button.transition = Selectable.Transition.None;
            button.navigation = new Navigation { mode = Navigation.Mode.None };
            button.onClick.AddListener(() => action());

            var text = SecondScreenUI.MakeText(rect, "Label", label, icon != null ? 14f : 16f,
                                               Text, TextAlignmentOptions.Center);
            if (_font != null) text.font = _font;
            text.fontStyle = FontStyles.Bold;
            if (icon != null)
            {
                text.rectTransform.anchorMin = Vector2.zero;
                text.rectTransform.anchorMax = new Vector2(1f, 0f);
                text.rectTransform.pivot = new Vector2(0.5f, 0f);
                text.rectTransform.anchoredPosition = new Vector2(0f, 4f);
                text.rectTransform.sizeDelta = new Vector2(-10f, 20f);
            }
            else
            {
                text.rectTransform.anchorMin = Vector2.zero;
                text.rectTransform.anchorMax = Vector2.one;
                text.rectTransform.offsetMin = new Vector2(5f, 3f);
                text.rectTransform.offsetMax = new Vector2(-5f, -3f);
            }
            SecondScreenUI.FitText(text, icon != null ? 9f : 10f,
                                   icon != null ? 14f : 16f, label.Contains("\n"));

            if (icon != null)
            {
                var iconObject = new GameObject("Icon", typeof(RectTransform), typeof(Image));
                var iconRect = (RectTransform)iconObject.transform;
                iconRect.SetParent(rect, false);
                iconRect.anchorMin = new Vector2(0.5f, 1f);
                iconRect.anchorMax = new Vector2(0.5f, 1f);
                iconRect.pivot = new Vector2(0.5f, 1f);
                iconRect.anchoredPosition = new Vector2(0f, -5f);
                float iconSize = Mathf.Min(42f, Mathf.Max(28f, height - 34f));
                iconRect.sizeDelta = new Vector2(iconSize, iconSize);
                var image = iconObject.GetComponent<Image>();
                image.sprite = icon;
                image.color = Gold;
                image.preserveAspect = true;
                image.raycastTarget = false;
            }

            var result = new ButtonVisual
            {
                Button = button,
                Background = background,
                Accent = accent,
                Label = text
            };
            _buttons.Add(result);
            return result;
        }

        private static Sprite FindPinSprite(Minimap.PinType type)
        {
            var minimap = Minimap.instance;
            if (minimap == null || minimap.m_icons == null) return null;
            foreach (var icon in minimap.m_icons)
                if (icon.m_name == type) return icon.m_icon;
            return null;
        }
    }
}
