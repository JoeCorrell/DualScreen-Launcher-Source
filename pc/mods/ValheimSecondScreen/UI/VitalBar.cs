using System;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace ValheimSecondScreen.UI
{
    /// <summary>
    /// One vital as a tall column: a frame, a fill that rises, and the number.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Drawn rather than moved, unlike the rest of the HUD. Valheim's bars are
    /// wide and short because they live along the bottom of a 16:9 screen; this
    /// panel has height to spare and no width, so the shape has to change, and a
    /// prefab rotated ninety degrees would take its text and its notches with it.
    /// </para>
    /// <para>
    /// Filling upward is the point of the shape. Four columns of the same height
    /// standing side by side can be read against each other at a glance — which
    /// is low, how low — in a way four stacked horizontal bars cannot.
    /// </para>
    /// </remarks>
    internal sealed class VitalBar
    {
        private static readonly Color FrameInk = new Color(0.06f, 0.05f, 0.04f, 0.92f);
        private static readonly Color TrackInk = new Color(0f, 0f, 0f, 0.55f);
        private static readonly Color Weak = UiTheme.Muted;

        private readonly Image _fill;
        private readonly Image _ghost;
        private readonly VitalGradient _fillGradient;
        private readonly VitalGradient _ghostGradient;
        private readonly TMP_Text _value;
        private readonly TMP_Text _caption;
        private readonly GameObject _root;
        private readonly Color _colour;
        private float _lastFraction = -1f;
        private int _lastCurrent = int.MinValue;
        private int _lastMaximum = int.MinValue;

        public RectTransform Rect { get; }

        public VitalBar(RectTransform parent, string caption, Color colour,
                        TMP_FontAsset font, float width)
        {
            _colour = colour;

            var go = new GameObject("Vital_" + caption, typeof(RectTransform), typeof(Image));
            _root = go;
            Rect = (RectTransform)go.transform;
            Rect.SetParent(parent, false);
            Rect.anchorMin = new Vector2(0.5f, 0f);
            Rect.anchorMax = new Vector2(0.5f, 1f);
            Rect.pivot = new Vector2(0.5f, 0.5f);
            Rect.sizeDelta = new Vector2(width, 0f);

            var frame = go.GetComponent<Image>();
            UiSkin.Panel(Rect, raised: true);
            frame.color = FrameInk;
            frame.raycastTarget = false;

            var track = Child(Rect, "Track", TrackInk);
            UiSkin.Plain(track, TrackInk);
            track.rectTransform.offsetMin = new Vector2(6f, 34f);
            track.rectTransform.offsetMax = new Vector2(-6f, -30f);

            /*
             * The ghost sits behind the fill and lags it.
             *
             * Valheim's own bars do this and it is the only part of them worth
             * reproducing: a hit that takes a third of your health is a number
             * you cannot read in the tenth of a second it takes to happen, and
             * the trailing edge is what makes the size of it visible after the
             * fact.
             */
            _ghost = Child(track.rectTransform, "Ghost",
                           new Color(colour.r, colour.g, colour.b, 0.30f));
            Bottom(_ghost.rectTransform);
            _ghostGradient = _ghost.gameObject.AddComponent<VitalGradient>();

            _fill = Child(track.rectTransform, "Fill", colour);
            Bottom(_fill.rectTransform);
            _fillGradient = _fill.gameObject.AddComponent<VitalGradient>();
            ApplyGradient(colour);

            _caption = SecondScreenUI.MakeText(Rect, "Caption", caption, 19f, Weak,
                                               TextAlignmentOptions.Center);
            if (font != null) _caption.font = font;
            var capRect = (RectTransform)_caption.transform;
            capRect.anchorMin = new Vector2(0f, 1f);
            capRect.anchorMax = new Vector2(1f, 1f);
            capRect.pivot = new Vector2(0.5f, 1f);
            capRect.sizeDelta = new Vector2(0f, 28f);
            capRect.anchoredPosition = new Vector2(0f, -2f);

            _value = SecondScreenUI.MakeText(Rect, "Value", "0 / 0", 20f, Color.white,
                                             TextAlignmentOptions.Center);
            if (font != null) _value.font = font;
            var valRect = (RectTransform)_value.transform;
            valRect.anchorMin = new Vector2(0f, 0f);
            valRect.anchorMax = new Vector2(1f, 0f);
            valRect.pivot = new Vector2(0.5f, 0f);
            valRect.sizeDelta = new Vector2(0f, 32f);
            valRect.anchoredPosition = new Vector2(0f, 3f);
            SecondScreenUI.FitText(_value, 10f, 20f);
        }

        public void SetVisible(bool visible)
        {
            if (_root.activeSelf != visible) _root.SetActive(visible);
        }

        /// <summary>
        /// Updates the column. The ghost catches up over about a second.
        /// </summary>
        public void Set(float current, float max)
        {
            float fraction = max > 0f ? Mathf.Clamp01(current / max) : 0f;
            if (!Mathf.Approximately(_lastFraction, fraction))
            {
                _lastFraction = fraction;
                _fill.rectTransform.anchorMax = new Vector2(1f, fraction);
            }

            float ghost = _ghost.rectTransform.anchorMax.y;
            // Only trails downward: a bar being refilled should show the gain
            // immediately, and a ghost lagging upward would read as the opposite.
            ghost = fraction > ghost
                ? fraction
                : Mathf.MoveTowards(ghost, fraction, Time.unscaledDeltaTime);
            _ghost.rectTransform.anchorMax = new Vector2(1f, ghost);

            int currentText = Mathf.CeilToInt(current);
            int maximumText = Mathf.CeilToInt(max);
            if (_lastCurrent != currentText || _lastMaximum != maximumText)
            {
                _lastCurrent = currentText;
                _lastMaximum = maximumText;
                _value.text = currentText + " / " + maximumText;
            }
            var activeColour = fraction < 0.25f
                ? Color.Lerp(_colour, new Color(0.86f, 0.24f, 0.20f, 1f), 0.6f)
                : _colour;
            ApplyGradient(activeColour);
        }

        private void ApplyGradient(Color colour)
        {
            // Valheim's bars read as illuminated material rather than a flat
            // block: a deep lower edge carries the body colour and the upper
            // edge catches a warm highlight. Vertex colours keep that treatment
            // sharp as the vertical fill changes height.
            var bottom = new Color(
                colour.r * 0.48f,
                colour.g * 0.48f,
                colour.b * 0.48f,
                colour.a);
            var top = Color.Lerp(colour, Color.white, 0.24f);
            top.a = colour.a;

            _fill.color = Color.white;
            _fillGradient.SetColors(bottom, top);

            bottom.a = 0.30f;
            top.a = 0.30f;
            _ghost.color = Color.white;
            _ghostGradient.SetColors(bottom, top);
        }

        private static Image Child(RectTransform parent, string name, Color colour)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Image));
            var rect = (RectTransform)go.transform;
            rect.SetParent(parent, false);
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.one;
            rect.offsetMin = Vector2.zero;
            rect.offsetMax = Vector2.zero;
            var image = go.GetComponent<Image>();
            image.color = colour;
            image.raycastTarget = false;
            return image;
        }

        /// <summary>Anchors a fill to the bottom edge so it grows upward.</summary>
        private static void Bottom(RectTransform rect)
        {
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = new Vector2(1f, 0f);
            rect.pivot = new Vector2(0.5f, 0f);
            rect.offsetMin = Vector2.zero;
            rect.offsetMax = Vector2.zero;
        }
    }
}
