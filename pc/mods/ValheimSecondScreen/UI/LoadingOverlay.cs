using UnityEngine;
using UnityEngine.UI;

namespace ValheimSecondScreen.UI
{
    /// <summary>
    /// An unadorned black curtain for display one while gameplay is unavailable.
    /// </summary>
    /// <remarks>
    /// No loading art, tips, progress, or dashboard content is mirrored onto the
    /// lower screen. Keeping a real canvas here instead of merely disabling the
    /// dashboard also prevents the display compositor from retaining the last
    /// gameplay frame during teleports and world changes.
    /// </remarks>
    internal sealed class LoadingOverlay
    {
        private readonly GameObject _canvasGO;
        private bool _visible = true;

        public LoadingOverlay()
        {
            _canvasGO = new GameObject("SecondScreenBlankingCurtain");
            Object.DontDestroyOnLoad(_canvasGO);

            var canvas = _canvasGO.AddComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;
            canvas.targetDisplay = 1;
            canvas.sortingOrder = 1000;

            var scaler = _canvasGO.AddComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scaler.referenceResolution = new Vector2(
                SecondScreenUI.ReferenceWidth, SecondScreenUI.ReferenceHeight);
            scaler.matchWidthOrHeight = 0.5f;

            var curtain = new GameObject("Black", typeof(RectTransform), typeof(Image));
            var rect = (RectTransform)curtain.transform;
            rect.SetParent(_canvasGO.transform, false);
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.one;
            rect.offsetMin = Vector2.zero;
            rect.offsetMax = Vector2.zero;
            var image = curtain.GetComponent<Image>();
            image.color = Color.black;
            image.raycastTarget = false;
        }

        // Retaining the old signature keeps the caller small. Loading visuals
        // are deliberately ignored; only the curtain's visibility matters.
        public void Set(bool visible, Sprite art, string tip, float progress)
        {
            if (_visible == visible) return;
            _visible = visible;
            _canvasGO.SetActive(visible);
        }

        public void Destroy()
        {
            if (_canvasGO != null) Object.Destroy(_canvasGO);
        }
    }
}
