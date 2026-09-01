using UnityEngine;
using UnityEngine.EventSystems;

namespace ValheimSecondScreen.UI
{
    /// <summary>Native touchscreen gestures for either side of ContainerPage.</summary>
    internal sealed class ContainerSlotInput : MonoBehaviour,
        IPointerDownHandler, IPointerUpHandler, IPointerClickHandler,
        IBeginDragHandler, IDragHandler, IEndDragHandler, IDropHandler
    {
        private ContainerPage _owner;
        private ContainerPage.Side _side;
        private int _column;
        private int _row;
        private CanvasGroup _group;
        private bool _pointerDown;
        private bool _suppressClick;
        private bool _nativeDragging;
        private bool _dropAttempted;
        private float _pressedAt;
        private int _pointerId = int.MinValue;

        public void Bind(ContainerPage owner, ContainerPage.Side side,
                         int column, int row)
        {
            _owner = owner;
            _side = side;
            _column = column;
            _row = row;
            if (_group == null) _group = GetComponent<CanvasGroup>();
        }

        public void OnPointerDown(PointerEventData eventData)
        {
            if (eventData.button != PointerEventData.InputButton.Left
                || _pointerDown || _nativeDragging) return;
            _pointerId = eventData.pointerId;
            _pointerDown = true;
            _suppressClick = false;
            _pressedAt = Time.unscaledTime;
            _owner?.ShowHoldProgress(_side, _column, _row, 0f);
        }

        public void OnPointerUp(PointerEventData eventData)
        {
            if (eventData.button != PointerEventData.InputButton.Left
                || eventData.pointerId != _pointerId) return;
            _pointerDown = false;
            _pointerId = int.MinValue;
            if (!_nativeDragging)
                _owner?.ShowHoldProgress(_side, _column, _row, -1f);
        }

        private void Update()
        {
            if (!_pointerDown || _nativeDragging || _owner == null) return;
            float progress = (Time.unscaledTime - _pressedAt)
                             / SecondScreenPreferences.HoldSeconds;
            _owner.ShowHoldProgress(_side, _column, _row, Mathf.Clamp01(progress));
            if (progress < 1f) return;

            _pointerDown = false;
            _suppressClick = true;
            _owner.ShowHoldProgress(_side, _column, _row, -1f);
            _owner.UseAt(_side, _column, _row);
        }

        public void OnPointerClick(PointerEventData eventData)
        {
            if (eventData.button != PointerEventData.InputButton.Left) return;
            if (_suppressClick)
            {
                _suppressClick = false;
                return;
            }
            _owner?.TapAt(_side, _column, _row);
        }

        public void OnBeginDrag(PointerEventData eventData)
        {
            if (eventData.button != PointerEventData.InputButton.Left
                || eventData.pointerId != _pointerId) return;
            if (_owner == null || !_owner.BeginDragAt(_side, _column, _row))
            {
                _pointerDown = false;
                _pointerId = int.MinValue;
                _suppressClick = true;
                _owner?.ShowHoldProgress(_side, _column, _row, -1f);
                return;
            }
            _pointerDown = false;
            _pointerId = int.MinValue;
            _suppressClick = true;
            _nativeDragging = true;
            _dropAttempted = false;
            if (_group != null) _group.blocksRaycasts = false;
        }

        public void OnDrag(PointerEventData eventData) { }

        public void OnDrop(PointerEventData eventData)
        {
            var source = eventData.pointerDrag != null
                ? eventData.pointerDrag.GetComponent<ContainerSlotInput>() : null;
            if (source != null && source._owner == _owner)
            {
                source._dropAttempted = true;
                _owner.EndDragAt(_side, _column, _row);
            }
        }

        public void OnEndDrag(PointerEventData eventData)
        {
            if (_group != null) _group.blocksRaycasts = true;
            bool ownedDrag = _nativeDragging;
            _nativeDragging = false;
            if (ownedDrag && !_dropAttempted) _owner?.CancelDrag();
            _dropAttempted = false;
        }

        private void OnDisable()
        {
            if (_group != null) _group.blocksRaycasts = true;
            _pointerDown = false;
            _nativeDragging = false;
            _dropAttempted = false;
            _pointerId = int.MinValue;
            _owner?.CancelDrag();
        }
    }
}
