using System.Collections.Generic;
using System.Linq;
using UnityEngine;
using UnityEngine.UI;
using ValheimSecondScreen.Network;

namespace ValheimSecondScreen.Hooks
{
    /// <summary>
    /// Splits one tall guest framebuffer into equal 16:9 game and HUD panes and
    /// moves Valheim's live UI objects into the lower pane.
    /// </summary>
    /// <remarks>
    /// WEMU exposes one X11 screen to Unity, not two Unity displays. A 1280x1440
    /// guest therefore contains a 1280x720 game pane followed by a 1280x720 HUD
    /// pane. Android presents two source rectangles of the same AHardwareBuffer;
    /// no UI is reconstructed, captured, or copied.
    ///
    /// The old implementation accepted 1280x960 and stretched its 240-row tail
    /// over a 16:9 panel. It also moved only four objects and left every other UI
    /// element positioned against the full 8:9 canvas. This implementation only
    /// advertises readiness after it has established two equal panes, confined the
    /// world cameras and all spatial UI to the top, and moved a validated set of
    /// persistent HUD roots to the bottom.
    /// </remarks>
    public sealed class HudBandMover : MonoBehaviour
    {
        public static HudBandMover Instance { get; private set; }

        public static int BandHeight { get; private set; }
        public static int SourceTop { get; private set; }
        public static int SourceBottom { get; private set; }
        public static bool Moved { get; private set; }

        private readonly Dictionary<Transform, TransformSnapshot> _moved =
            new Dictionary<Transform, TransformSnapshot>();
        private readonly Dictionary<Camera, Rect> _cameraRects =
            new Dictionary<Camera, Rect>();

        private Hud _configuredHud;
        private RectTransform _topPane;
        private RectTransform _bottomPane;
        private float _nextReadyAt;

        private void Awake() => Instance = this;

        private void LateUpdate()
        {
            /*
             * A real second display wins outright.
             *
             * The geometry test below would already decline — a guest with two
             * monitors gives Unity a 16:9 main screen, not a double-height one —
             * but relying on that leaves the two arrangements one screen-size
             * coincidence away from both reparenting the same objects. This is
             * the fallback for hosts that cannot offer a second display, and it
             * should say so rather than be lucky.
             */
            if (SecondDisplayMover.SecondDisplayLive)
            {
                if (Moved) RestoreLayout();
                return;
            }

            int top;
            int bottom;
            if (!TryGetBandGeometry(out top, out bottom))
            {
                if (Moved) RestoreLayout();
                return;
            }

            var gameCamera = GameCamera.instance;
            var hud = Hud.instance;
            if (!gameCamera || !hud || !hud.m_rootObject || Minimap.instance == null)
                return;

            if (_configuredHud != hud || !_topPane || !_bottomPane ||
                SourceTop != top || SourceBottom != bottom)
            {
                RestoreLayout();
                if (!Configure(hud, gameCamera, top, bottom))
                    return;
            }

            ApplyCameraViewport(gameCamera);
            if (Moved && Time.unscaledTime >= _nextReadyAt)
            {
                BroadcastReady();
                _nextReadyAt = Time.unscaledTime + 1f;
            }
        }

        private static bool TryGetBandGeometry(out int top, out int bottom)
        {
            top = Mathf.RoundToInt(Screen.width * 9f / 16f);
            bottom = Screen.height;
            return Screen.width >= 640 && top >= 360 && bottom == top * 2;
        }

        private bool Configure(Hud hud, GameCamera gameCamera, int top, int bottom)
        {
            _configuredHud = hud;
            SourceTop = top;
            SourceBottom = bottom;
            BandHeight = bottom - top;

            var root = hud.m_rootObject.transform as RectTransform;
            if (!root)
            {
                Plugin.Log.LogError(
                    "[SecondScreen] Hud.m_rootObject is not a RectTransform; refusing HUD crop.");
                return false;
            }

            var originalChildren = new List<Transform>();
            for (int i = 0; i < root.childCount; i++)
                originalChildren.Add(root.GetChild(i));

            _topPane = CreatePane(
                "SecondScreen_TopGameUI", root, new Vector2(0f, 0.5f), Vector2.one);
            _bottomPane = CreatePane(
                "SecondScreen_BottomRealHUD", root, Vector2.zero, new Vector2(1f, 0.5f));
            CreateBottomBackground(_bottomPane);

            // All UI starts in the upper 16:9 pane. Crosshair, damage overlays,
            // hover labels, build UI and menus therefore stay aligned with the
            // upper world camera instead of landing on the 8:9 screen midpoint.
            foreach (var child in originalChildren)
                MoveToPane(child, _topPane);

            var lower = CollectLowerHudRoots(hud, root)
                .Where(candidate => candidate && candidate != root)
                .Distinct()
                .ToList();

            // Preserve prefab hierarchy: when one candidate contains another,
            // moving the ancestor already carries the descendant with it.
            lower = lower.Where(candidate =>
                    !lower.Any(other => other != candidate && candidate.IsChildOf(other)))
                .ToList();

            foreach (var item in lower)
                MoveToPane(item, _bottomPane);

            if (lower.Count < 5)
            {
                Plugin.Log.LogError(
                    "[SecondScreen] Valheim HUD topology mismatch (only " + lower.Count +
                    " movable roots); refusing HUD crop.");
                RestoreLayout();
                return false;
            }

            ApplyCameraViewport(gameCamera);
            Moved = true;
            _nextReadyAt = 0f;
            Plugin.Log.LogInfo(
                "[SecondScreen] Real HUD moved to rows " + top + ".." + bottom +
                " using " + lower.Count + " live Valheim roots.");
            foreach (var item in lower)
                Plugin.Log.LogInfo("[SecondScreen] HUD root: " + HierarchyPath(item));
            return true;
        }

        private static RectTransform CreatePane(
            string name,
            RectTransform parent,
            Vector2 anchorMin,
            Vector2 anchorMax)
        {
            var go = new GameObject(name, typeof(RectTransform));
            var pane = (RectTransform)go.transform;
            pane.SetParent(parent, false);
            pane.anchorMin = anchorMin;
            pane.anchorMax = anchorMax;
            pane.pivot = new Vector2(0.5f, 0.5f);
            pane.offsetMin = Vector2.zero;
            pane.offsetMax = Vector2.zero;
            pane.localScale = Vector3.one;
            return pane;
        }

        private static void CreateBottomBackground(RectTransform parent)
        {
            var go = new GameObject(
                "SecondScreen_HudBackground",
                typeof(RectTransform),
                typeof(CanvasRenderer),
                typeof(Image));
            var rect = (RectTransform)go.transform;
            rect.SetParent(parent, false);
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.one;
            rect.offsetMin = Vector2.zero;
            rect.offsetMax = Vector2.zero;
            var image = go.GetComponent<Image>();
            image.color = Color.black;
            image.raycastTarget = false;
            rect.SetAsFirstSibling();
        }

        /// <summary>
        /// The HUD roots that belong on the second screen, whichever way they get
        /// there.
        /// </summary>
        /// <remarks>
        /// Shared with <see cref="SecondDisplayMover"/>, which does the same job by
        /// reparenting onto a real second Unity display rather than into the lower
        /// half of one tall screen. The destination differs; what counts as "the
        /// HUD" does not, and two lists would drift apart on the first Valheim
        /// patch that renamed a field.
        /// </remarks>
        internal static IEnumerable<Transform> CollectLowerHudRoots(Hud hud, RectTransform root)
        {
            var roots = new List<Transform>();
            Add(roots, hud.m_healthBarRoot);
            Add(roots, hud.m_foodBarRoot);
            Add(roots, hud.m_gpRoot);
            Add(roots, hud.m_staminaBar2Root);
            Add(roots, hud.m_adrenalineBarRoot);
            Add(roots, hud.m_eitrBarRoot);
            Add(roots, hud.m_statusEffectListRoot);
            Add(roots, hud.m_actionBarRoot);
            Add(roots, hud.m_staggerAnimator ? hud.m_staggerAnimator.transform : null);
            Add(roots, hud.m_mountPanel);
            Add(roots, hud.m_shipHudRoot);
            Add(roots, hud.m_eventBar);
            Add(roots, hud.m_saveIcon);
            Add(roots, hud.m_badConnectionIcon);

            var minimap = Minimap.instance;
            if (minimap && minimap.m_smallRoot)
                Add(roots, minimap.m_smallRoot);

            foreach (var hotkeyBar in root.GetComponentsInChildren<HotkeyBar>(true))
                Add(roots, hotkeyBar);

            return roots;
        }

        private static void Add(ICollection<Transform> roots, Component component)
        {
            if (component) roots.Add(component.transform);
        }

        private static void Add(ICollection<Transform> roots, GameObject gameObject)
        {
            if (gameObject) roots.Add(gameObject.transform);
        }

        private void MoveToPane(Transform item, RectTransform pane)
        {
            TransformSnapshot snapshot;
            if (!_moved.TryGetValue(item, out snapshot))
            {
                snapshot = new TransformSnapshot(item);
                _moved.Add(item, snapshot);
            }
            item.SetParent(pane, false);
            snapshot.ApplyLayoutOnly();
        }

        private void ApplyCameraViewport(GameCamera gameCamera)
        {
            foreach (var camera in gameCamera.GetComponentsInChildren<Camera>(true))
            {
                if (!camera || camera.targetTexture) continue;
                if (!_cameraRects.ContainsKey(camera)) _cameraRects.Add(camera, camera.rect);
                camera.rect = new Rect(0f, 0.5f, 1f, 0.5f);
            }

            if (gameCamera.m_skyCamera)
            {
                var sky = gameCamera.m_skyCamera;
                if (!_cameraRects.ContainsKey(sky)) _cameraRects.Add(sky, sky.rect);
                sky.rect = new Rect(0f, 0.5f, 1f, 0.5f);
            }
        }

        private static void BroadcastReady()
        {
            if (Plugin.Instance?.Server == null || !Moved) return;
            Plugin.Instance.Server.Broadcast(Protocol.HudBandReady(SourceTop, SourceBottom));
        }

        private void RestoreLayout()
        {
            Moved = false;

            foreach (var pair in _moved.ToArray())
                if (pair.Key) pair.Value.Restore();
            _moved.Clear();

            foreach (var pair in _cameraRects.ToArray())
                if (pair.Key) pair.Key.rect = pair.Value;
            _cameraRects.Clear();

            if (_topPane) Destroy(_topPane.gameObject);
            if (_bottomPane) Destroy(_bottomPane.gameObject);
            _topPane = null;
            _bottomPane = null;
            _configuredHud = null;
            BandHeight = 0;
            SourceTop = 0;
            SourceBottom = 0;
        }

        private void OnDestroy()
        {
            RestoreLayout();
            if (Instance == this) Instance = null;
        }

        private static string HierarchyPath(Transform transform)
        {
            var names = new Stack<string>();
            for (var current = transform; current; current = current.parent)
                names.Push(current.name);
            return string.Join("/", names.ToArray());
        }

        private sealed class TransformSnapshot
        {
            private readonly Transform _transform;
            private readonly Transform _parent;
            private readonly int _siblingIndex;
            private readonly Vector3 _localPosition;
            private readonly Quaternion _localRotation;
            private readonly Vector3 _localScale;
            private readonly bool _isRect;
            private readonly Vector2 _anchorMin;
            private readonly Vector2 _anchorMax;
            private readonly Vector2 _pivot;
            private readonly Vector2 _sizeDelta;
            private readonly Vector3 _anchoredPosition;

            public TransformSnapshot(Transform value)
            {
                _transform = value;
                _parent = value.parent;
                _siblingIndex = value.GetSiblingIndex();
                _localPosition = value.localPosition;
                _localRotation = value.localRotation;
                _localScale = value.localScale;

                var rect = value as RectTransform;
                _isRect = rect;
                if (rect)
                {
                    _anchorMin = rect.anchorMin;
                    _anchorMax = rect.anchorMax;
                    _pivot = rect.pivot;
                    _sizeDelta = rect.sizeDelta;
                    _anchoredPosition = rect.anchoredPosition3D;
                }
            }

            public void ApplyLayoutOnly()
            {
                _transform.localPosition = _localPosition;
                _transform.localRotation = _localRotation;
                _transform.localScale = _localScale;
                if (!_isRect) return;
                var rect = (RectTransform)_transform;
                rect.anchorMin = _anchorMin;
                rect.anchorMax = _anchorMax;
                rect.pivot = _pivot;
                rect.sizeDelta = _sizeDelta;
                rect.anchoredPosition3D = _anchoredPosition;
            }

            public void Restore()
            {
                if (!_transform || !_parent) return;
                _transform.SetParent(_parent, false);
                ApplyLayoutOnly();
                _transform.SetSiblingIndex(Mathf.Min(_siblingIndex, _parent.childCount - 1));
            }
        }
    }
}
