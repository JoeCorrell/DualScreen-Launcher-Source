using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

namespace ValheimSecondScreen.UI
{
    /// <summary>
    /// Small, event-driven touch feedback for the lower display's controls.
    /// </summary>
    /// <remarks>
    /// This deliberately has no Update loop. Pointer events briefly compress
    /// the control and reveal a warm tint, then restore it immediately. That
    /// makes a touchscreen press feel acknowledged without continuously
    /// animating (and rebuilding) the second-display canvas.
    /// </remarks>
    internal sealed class UiInteractionFeedback : MonoBehaviour,
        IPointerEnterHandler, IPointerExitHandler,
        IPointerDownHandler, IPointerUpHandler
    {
        private static readonly Color HoverTint =
            new Color(0.96f, 0.63f, 0.22f, 0.055f);
        private static readonly Color PressedTint =
            new Color(1f, 0.55f, 0.12f, 0.14f);

        private RectTransform _rect;
        private Button _button;
        private Image _overlay;
        private Vector3 _restScale = Vector3.one;
        private bool _hovered;
        private bool _pressed;

        public void Configure(Button button, Image overlay)
        {
            _rect = transform as RectTransform;
            _button = button;
            _overlay = overlay;
            if (_rect != null) _restScale = _rect.localScale;
            Paint();
        }

        public void OnPointerEnter(PointerEventData eventData)
        {
            if (!CanInteract()) return;
            _hovered = true;
            Paint();
        }

        public void OnPointerExit(PointerEventData eventData)
        {
            _hovered = false;
            _pressed = false;
            Paint();
        }

        public void OnPointerDown(PointerEventData eventData)
        {
            if (!CanInteract()) return;
            _pressed = true;
            Paint();
        }

        public void OnPointerUp(PointerEventData eventData)
        {
            _pressed = false;
            Paint();
        }

        private bool CanInteract()
        {
            return SecondScreenPreferences.ButtonFeedback
                   && isActiveAndEnabled && _button != null && _button.IsInteractable();
        }

        private void Paint()
        {
            bool enabled = CanInteract();
            if (_rect != null)
                _rect.localScale = enabled && _pressed
                    ? _restScale * 0.975f : _restScale;
            if (_overlay != null)
                _overlay.color = !enabled ? Color.clear
                    : (_pressed ? PressedTint
                       : (_hovered ? HoverTint : Color.clear));
        }

        private void OnDisable()
        {
            _hovered = false;
            _pressed = false;
            if (_rect != null) _rect.localScale = _restScale;
            if (_overlay != null) _overlay.color = Color.clear;
        }
    }
}
