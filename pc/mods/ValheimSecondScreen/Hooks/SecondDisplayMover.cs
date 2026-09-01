using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using HarmonyLib;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

namespace ValheimSecondScreen.Hooks
{
    /// <summary>
    /// Moves the game's own HUD onto a second Unity display, rather than
    /// reproducing it somewhere else.
    /// </summary>
    /// <remarks>
    /// <para>
    /// The second screen has so far been fed by describing the HUD over a socket
    /// and drawing a lookalike in Compose. That can never match: it re-implements
    /// layout the game already does, it goes stale on every patch, and it cannot
    /// pick up a texture pack. This does the opposite — <c>SetParent</c> moves the
    /// *live* RectTransforms. Same objects, same scripts, same cooldown sweeps and
    /// durability bars, still driven by the game's own Update. Nothing is copied.
    /// </para>
    /// <para>
    /// Whether it is possible at all rests on one unknown: Unity's multi-display
    /// support is a Windows-standalone feature, and here it is running under Wine
    /// with DXVK translating to Vulkan. So this is written as a probe first. It
    /// reports what Unity sees, tries to activate display 1, and only reparents
    /// anything once that has actually succeeded. A refusal costs a log line and
    /// changes nothing — the socket path keeps working exactly as before.
    /// </para>
    /// <para>
    /// Valheim itself never touches <c>Display.displays</c>, so display 1 is free
    /// and nothing in the game will fight us for it.
    /// </para>
    /// </remarks>
    public sealed class SecondDisplayMover : MonoBehaviour
    {
        public static SecondDisplayMover Instance { get; private set; }

        private static readonly FieldInfo _fSmallRoot =
            AccessTools.Field(typeof(Minimap), "m_smallRoot");
        private static readonly FieldInfo _fHealthPanel =
            AccessTools.Field(typeof(Hud), "m_healthPanel");
        private static readonly FieldInfo _fStaminaBar =
            AccessTools.Field(typeof(Hud), "m_staminaBar2Root");

        /// <summary>True once display 1 reported itself active.</summary>
        public static bool SecondDisplayLive { get; private set; }
        internal static bool CanRouteUi => SecondDisplayLive && Instance != null
                                           && Instance._gameplayReady
                                           && Instance._ui != null;

        private Canvas _canvas;
        private UI.SecondScreenUI _ui;
        private UI.LoadingOverlay _loading;
        private bool _probed;
        private bool _moved;
        private Hud _movedHud;
        private Minimap _movedMinimap;
        private float _readySince = -1f;
        private bool _gameplayReady;
        private float _nextMoveAttempt;
        private readonly Dictionary<Transform, TransformSnapshot> _movedRoots =
            new Dictionary<Transform, TransformSnapshot>();
        private readonly List<CanvasGroup> _suppressedVitalGroups =
            new List<CanvasGroup>();
        private GameObject _pauseSettingsCanvas;
        private RectTransform _pauseSettingsStage;
        private Settings _routedSettings;
        private TransformSnapshot _routedSettingsSnapshot;
        private readonly List<CanvasTargetSnapshot> _routedSettingsCanvases =
            new List<CanvasTargetSnapshot>();
        private Menu _routedMenu;
        private bool _pauseMenuDialogWasActive;
        private PointerEventData _pausePointer;
        private GameObject _pausePressTarget;
        private GameObject _pauseDragTarget;
        private int _renderWidth;
        private int _renderHeight;

        private void Awake() => Instance = this;

        private void Update()
        {
            if (!_probed)
            {
                Probe();
                return;
            }
            if (!SecondDisplayLive) return;

            // Display one stays behind an unadorned black curtain until a live
            // player, HUD and minimap have all remained ready long enough to
            // avoid flashing a half-built dashboard during scene transitions.
            if (_loading == null) _loading = new UI.LoadingOverlay();

            if (_moved && (!_movedHud || !_movedMinimap
                           || Hud.instance != _movedHud
                           || Minimap.instance != _movedMinimap))
                ResetPanel();

            _gameplayReady = UpdateLoading();
            if (!_gameplayReady)
            {
                _ui?.SetVisible(false);
                return;
            }

            if (!_moved && Time.unscaledTime >= _nextMoveAttempt) TryMove();
            if (_ui == null) return;
            _ui.SetVisible(true);

            if (_routedSettings != null) RetargetPauseCanvases();
            else if (_pauseSettingsCanvas != null) HidePauseSettings();

            // WEMU forwards lower-panel touch as normalized local commands.
            // These keys remain a convenient non-touch fallback on desktop hosts.
            if (Input.GetKeyDown(CycleKey)) _ui.CycleTab(1);
            else if (Input.GetKeyDown(BackKey)) _ui.CycleTab(-1);

            _ui.Tick();
        }

        private void LateUpdate()
        {
            if (!SecondDisplayLive || !_moved) return;
            // Hud animators can write CanvasGroup alpha after the initial
            // layout pass. Reassert only the stock vital groups in LateUpdate
            // so their ornaments and food overlays cannot flash back onto the
            // top display while the custom status card remains live below.
            foreach (var group in _suppressedVitalGroups)
            {
                if (group == null) continue;
                group.alpha = 0f;
                group.interactable = false;
                group.blocksRaycasts = false;
            }
        }

        /// <summary>
        /// Keeps display one blank until gameplay is genuinely ready.
        /// </summary>
        /// <remarks>
        /// <para>
        /// Readiness requires the world-facing singletons, an active local
        /// player, and Valheim's loading fade to be gone. The short settling
        /// interval prevents the UI from appearing in the one or two frames
        /// between those objects being constructed and their data being filled.
        /// </para>
        /// </remarks>
        private bool UpdateLoading()
        {
            var hud = Hud.instance;
            var player = Player.m_localPlayer;
            var screen = hud != null ? hud.m_loadingScreen : null;
            bool loading = screen != null && screen.gameObject.activeInHierarchy
                           && screen.alpha > 0.01f;
            bool rawReady = hud != null
                            && Minimap.instance != null
                            && InventoryGui.instance != null
                            && ZNetScene.instance != null
                            && player != null
                            && player.gameObject.activeInHierarchy
                            && !player.IsTeleporting()
                            && !player.InCutscene()
                            && !loading;

            if (!rawReady)
            {
                _readySince = -1f;
                _loading.Set(true, null, null, 0f);
                return false;
            }

            if (_readySince < 0f) _readySince = Time.unscaledTime;
            bool ready = Time.unscaledTime - _readySince >= 0.35f;
            _loading.Set(!ready, null, null, 0f);
            return ready;
        }

        /// <summary>Handles a touch on the panel, in fractions of it.</summary>
        public void Tap(float x, float y)
        {
            if (!_gameplayReady || _ui == null || x < 0f || y < 0f) return;
            if (_routedSettings != null)
            {
                TapPauseSettings(x, y);
                return;
            }
            _ui.Tap(x, y);
        }

        /// <summary>Performs the lower panel's hold gesture without a mouse click.</summary>
        public void Hold(float x, float y, float duration)
        {
            if (!_gameplayReady || _ui == null || x < 0f || y < 0f) return;
            if (_routedSettings != null)
            {
                TapPauseSettings(x, y);
                return;
            }
            _ui.Hold(x, y, duration);
        }

        /// <summary>Begins or ends a drag on the panel.</summary>
        public void Drag(float x, float y, bool begin)
        {
            if (!_gameplayReady || _ui == null) return;
            if (_routedSettings != null)
            {
                if (x < 0f || y < 0f) CancelPausePointer();
                else if (begin) BeginPauseDrag(x, y);
                else EndPauseDrag(x, y);
                return;
            }
            if (x < 0f || y < 0f) { _ui.CancelDrag(); return; }
            if (begin) _ui.BeginDrag(x, y);
            else _ui.EndDrag(x, y);
        }

        /// <summary>Moves the bottom map only while its tab is actually open.</summary>
        public void PanMap(float dx, float dy)
        {
            if (_routedSettings != null && _pausePointer != null)
            {
                ContinuePauseDrag(dx, dy);
                return;
            }
            if (_gameplayReady && _ui != null
                && _ui.ActiveTab == 0 && _ui.MapGestureActive)
                UI.LargeMap.Pan(dx, dy);
        }

        /// <summary>Zooms the bottom map only while its tab is actually open.</summary>
        public string ScaleMap(float factor)
        {
            return _routedSettings == null && _gameplayReady
                && _ui != null && _ui.ActiveTab == 0
                ? UI.LargeMap.Scale(factor)
                : "map tab inactive";
        }

        /// <summary>Selects a tab. Reachable from the host over the socket.</summary>
        public void SetTab(int tab)
        {
            if (_gameplayReady && _ui != null) _ui.SetTab(tab);
        }

        public void OpenPlayerInventory()
        {
            if (CanRouteUi) _ui.OpenPlayerInventory();
        }

        public void ClosePlayerInventory()
        {
            _ui?.ClosePlayerInventory();
        }

        /// <summary>Enters the lower display's modal container transfer page.</summary>
        public void OpenContainer(Container container)
        {
            if (_gameplayReady && _ui != null && container != null)
                _ui.OpenContainer(container);
        }

        /// <summary>Restores the page shown before a container was opened.</summary>
        public void CloseContainer()
        {
            if (_ui != null) _ui.CloseContainer();
        }

        /// <summary>Moves Valheim's live pause settings onto display one.</summary>
        internal void RoutePauseSettings(Settings settings)
        {
            if (!CanRouteUi || settings == null || settings.transform == null) return;
            if (_routedSettings == settings) return;
            HidePauseSettings();

            _routedSettings = settings;
            _routedSettingsSnapshot = new TransformSnapshot(settings.transform);
            _routedMenu = Menu.instance;
            if (_routedMenu != null && _routedMenu.m_menuDialog != null)
            {
                _pauseMenuDialogWasActive =
                    _routedMenu.m_menuDialog.gameObject.activeSelf;
                _routedMenu.m_menuDialog.gameObject.SetActive(false);
            }

            _pauseSettingsCanvas = new GameObject("SecondScreen_PauseSettings",
                typeof(RectTransform), typeof(Canvas), typeof(CanvasScaler),
                typeof(GraphicRaycaster));
            _pauseSettingsCanvas.transform.SetParent(transform, false);
            var canvas = _pauseSettingsCanvas.GetComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;
            canvas.targetDisplay = 1;
            canvas.sortingOrder = 500;
            var scaler = _pauseSettingsCanvas.GetComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scaler.referenceResolution = new Vector2(
                UI.SecondScreenUI.ReferenceWidth, UI.SecondScreenUI.ReferenceHeight);
            scaler.screenMatchMode = CanvasScaler.ScreenMatchMode.MatchWidthOrHeight;
            scaler.matchWidthOrHeight = 0.5f;

            var curtainObject = new GameObject("Curtain",
                typeof(RectTransform), typeof(Image));
            var curtain = (RectTransform)curtainObject.transform;
            curtain.SetParent(_pauseSettingsCanvas.transform, false);
            curtain.anchorMin = Vector2.zero;
            curtain.anchorMax = Vector2.one;
            curtain.offsetMin = Vector2.zero;
            curtain.offsetMax = Vector2.zero;
            var curtainImage = curtainObject.GetComponent<Image>();
            curtainImage.color = new Color(0.01f, 0.008f, 0.006f, 1f);
            curtainImage.raycastTarget = true;

            var stageObject = new GameObject("SettingsStage", typeof(RectTransform));
            _pauseSettingsStage = (RectTransform)stageObject.transform;
            _pauseSettingsStage.SetParent(_pauseSettingsCanvas.transform, false);
            _pauseSettingsStage.anchorMin = new Vector2(0.5f, 0.5f);
            _pauseSettingsStage.anchorMax = new Vector2(0.5f, 0.5f);
            _pauseSettingsStage.pivot = new Vector2(0.5f, 0.5f);
            _pauseSettingsStage.anchoredPosition = Vector2.zero;
            _pauseSettingsStage.sizeDelta = new Vector2(1920f, 1080f);
            float fit = Mathf.Min(UI.SecondScreenUI.ReferenceWidth / 1920f,
                                  UI.SecondScreenUI.ReferenceHeight / 1080f);
            _pauseSettingsStage.localScale = new Vector3(fit, fit, 1f);

            var settingsRect = settings.transform as RectTransform;
            settings.transform.SetParent(_pauseSettingsStage, false);
            if (settingsRect != null)
            {
                settingsRect.anchorMin = Vector2.zero;
                settingsRect.anchorMax = Vector2.one;
                settingsRect.pivot = new Vector2(0.5f, 0.5f);
                settingsRect.offsetMin = Vector2.zero;
                settingsRect.offsetMax = Vector2.zero;
                settingsRect.localScale = Vector3.one;
            }
            RetargetPauseCanvases();
            settings.SettingsClosed += HidePauseSettings;
            _ui.CancelDrag();
        }

        /// <summary>
        /// Keeps every override canvas owned by the stock Settings prefab on
        /// display one. Reparenting a nested Canvas does not change its explicit
        /// targetDisplay, and dropdowns can create additional canvases after the
        /// initial route, so this is also refreshed while Settings is open.
        /// </summary>
        private void RetargetPauseCanvases()
        {
            if (_routedSettings == null) return;
            foreach (Canvas canvas in _routedSettings.GetComponentsInChildren<Canvas>(true))
            {
                if (canvas == null) continue;
                if (!_routedSettingsCanvases.Any(snapshot => snapshot.Matches(canvas)))
                    _routedSettingsCanvases.Add(new CanvasTargetSnapshot(canvas));
                canvas.targetDisplay = 1;
            }
        }

        /// <summary>
        /// Forwards WEMU's normalized lower-panel input into the stock Settings
        /// hierarchy. Android owns the physical SurfaceView touch stream, so a
        /// moved Unity canvas cannot receive these pointer events by itself.
        /// </summary>
        private void TapPauseSettings(float x, float y)
        {
            if (!TryPausePointer(x, y, out PointerEventData pointer,
                                 out GameObject hit)) return;

            pointer.eligibleForClick = true;
            pointer.pressPosition = pointer.position;
            pointer.rawPointerPress = hit;
            GameObject pressed = ExecuteEvents.ExecuteHierarchy(
                hit, pointer, ExecuteEvents.pointerDownHandler);
            pointer.pointerPress = pressed;
            ExecuteEvents.ExecuteHierarchy(hit, pointer, ExecuteEvents.pointerUpHandler);
            ExecuteEvents.ExecuteHierarchy(hit, pointer, ExecuteEvents.pointerClickHandler);
        }

        private void BeginPauseDrag(float x, float y)
        {
            CancelPausePointer();
            if (!TryPausePointer(x, y, out _pausePointer,
                                 out GameObject hit)) return;

            _pausePointer.pressPosition = _pausePointer.position;
            _pausePointer.rawPointerPress = hit;
            _pausePressTarget = ExecuteEvents.ExecuteHierarchy(
                hit, _pausePointer, ExecuteEvents.pointerDownHandler);
            _pausePointer.pointerPress = _pausePressTarget;
            _pauseDragTarget = ExecuteEvents.GetEventHandler<IDragHandler>(hit);
            if (_pauseDragTarget == null) return;

            _pausePointer.pointerDrag = _pauseDragTarget;
            ExecuteEvents.Execute(_pauseDragTarget, _pausePointer,
                                  ExecuteEvents.initializePotentialDrag);
            ExecuteEvents.Execute(_pauseDragTarget, _pausePointer,
                                  ExecuteEvents.beginDragHandler);
            _pausePointer.dragging = true;
        }

        private void ContinuePauseDrag(float dx, float dy)
        {
            if (_pausePointer == null || _pauseDragTarget == null
                || !Finite(dx) || !Finite(dy)) return;
            Vector2 delta = new Vector2(dx * PauseDisplayWidth(),
                                        -dy * PauseDisplayHeight());
            _pausePointer.delta = delta;
            _pausePointer.position += delta;
            ExecuteEvents.Execute(_pauseDragTarget, _pausePointer,
                                  ExecuteEvents.dragHandler);
        }

        private void EndPauseDrag(float x, float y)
        {
            if (_pausePointer == null) return;
            if (TryPauseScreenPoint(x, y, out Vector2 point))
            {
                _pausePointer.delta = point - _pausePointer.position;
                _pausePointer.position = point;
            }
            if (_pauseDragTarget != null)
            {
                ExecuteEvents.Execute(_pauseDragTarget, _pausePointer,
                                      ExecuteEvents.dragHandler);
                ExecuteEvents.Execute(_pauseDragTarget, _pausePointer,
                                      ExecuteEvents.endDragHandler);
            }
            if (_pausePressTarget != null)
                ExecuteEvents.Execute(_pausePressTarget, _pausePointer,
                                      ExecuteEvents.pointerUpHandler);
            ClearPausePointer();
        }

        private void CancelPausePointer()
        {
            if (_pausePointer != null && _pauseDragTarget != null)
                ExecuteEvents.Execute(_pauseDragTarget, _pausePointer,
                                      ExecuteEvents.endDragHandler);
            if (_pausePointer != null && _pausePressTarget != null)
                ExecuteEvents.Execute(_pausePressTarget, _pausePointer,
                                      ExecuteEvents.pointerUpHandler);
            ClearPausePointer();
        }

        private void ClearPausePointer()
        {
            _pausePointer = null;
            _pausePressTarget = null;
            _pauseDragTarget = null;
        }

        private bool TryPausePointer(float x, float y,
                                     out PointerEventData pointer,
                                     out GameObject hit)
        {
            pointer = null;
            hit = null;
            EventSystem events = EventSystem.current;
            if (events == null || !TryPauseScreenPoint(x, y, out Vector2 screen)
                || !TryPauseWorldPoint(x, y, out Vector3 world)) return false;

            Graphic best = null;
            int bestOrder = int.MinValue;
            int bestDepth = int.MinValue;
            foreach (Graphic graphic in _pauseSettingsCanvas
                         .GetComponentsInChildren<Graphic>(false))
            {
                if (graphic == null || !graphic.raycastTarget
                    || graphic.canvasRenderer == null || graphic.canvasRenderer.cull
                    || !ContainsWorld(graphic.rectTransform, world)
                    || !AllowsPauseRaycast(graphic.transform, world)) continue;
                Canvas canvas = graphic.canvas;
                int order = canvas != null ? canvas.sortingOrder : 0;
                int depth = graphic.depth;
                if (order < bestOrder || (order == bestOrder && depth < bestDepth))
                    continue;
                best = graphic;
                bestOrder = order;
                bestDepth = depth;
            }
            if (best == null) return false;

            pointer = new PointerEventData(events)
            {
                pointerId = -1001,
                button = PointerEventData.InputButton.Left,
                position = screen,
                pressPosition = screen,
                clickCount = 1,
                clickTime = Time.unscaledTime,
                eligibleForClick = true
            };
            hit = best.gameObject;
            return true;
        }

        private bool TryPauseWorldPoint(float x, float y, out Vector3 world)
        {
            world = Vector3.zero;
            if (_pauseSettingsCanvas == null || !Finite(x) || !Finite(y)
                || x < 0f || x > 1f || y < 0f || y > 1f) return false;
            var rect = _pauseSettingsCanvas.transform as RectTransform;
            if (rect == null || rect.rect.width <= 0f || rect.rect.height <= 0f)
                return false;
            Vector2 local = new Vector2(
                Mathf.Lerp(rect.rect.xMin, rect.rect.xMax, x),
                Mathf.Lerp(rect.rect.yMax, rect.rect.yMin, y));
            world = rect.TransformPoint(local);
            return true;
        }

        private static bool TryPauseScreenPoint(float x, float y, out Vector2 point)
        {
            point = Vector2.zero;
            if (!Finite(x) || !Finite(y) || x < 0f || x > 1f || y < 0f || y > 1f)
                return false;
            point = new Vector2(x * PauseDisplayWidth(),
                                (1f - y) * PauseDisplayHeight());
            return true;
        }

        private bool AllowsPauseRaycast(Transform item, Vector3 world)
        {
            Transform current = item;
            Transform root = _pauseSettingsCanvas != null
                ? _pauseSettingsCanvas.transform : null;
            while (current != null)
            {
                foreach (CanvasGroup group in current.GetComponents<CanvasGroup>())
                    if (group != null && !group.blocksRaycasts) return false;

                var rectMask = current.GetComponent<RectMask2D>();
                if (rectMask != null && rectMask.enabled
                    && !ContainsWorld(rectMask.transform as RectTransform, world))
                    return false;
                var mask = current.GetComponent<Mask>();
                if (mask != null && mask.enabled
                    && !ContainsWorld(mask.transform as RectTransform, world))
                    return false;
                if (current == root) break;
                current = current.parent;
            }
            return true;
        }

        private static bool ContainsWorld(RectTransform rect, Vector3 world)
        {
            if (rect == null) return false;
            Vector3 local = rect.InverseTransformPoint(world);
            return rect.rect.Contains(new Vector2(local.x, local.y));
        }

        private static int PauseDisplayWidth()
        {
            return Display.displays.Length > 1 && Display.displays[1].systemWidth > 0
                ? Display.displays[1].systemWidth
                : Mathf.RoundToInt(UI.SecondScreenUI.ReferenceWidth);
        }

        private static int PauseDisplayHeight()
        {
            return Display.displays.Length > 1 && Display.displays[1].systemHeight > 0
                ? Display.displays[1].systemHeight
                : Mathf.RoundToInt(UI.SecondScreenUI.ReferenceHeight);
        }

        private static bool Finite(float value) =>
            !float.IsNaN(value) && !float.IsInfinity(value);

        private void HidePauseSettings()
        {
            CancelPausePointer();
            foreach (CanvasTargetSnapshot snapshot in _routedSettingsCanvases)
                snapshot.Restore();
            _routedSettingsCanvases.Clear();
            if (_routedSettings != null)
            {
                _routedSettings.SettingsClosed -= HidePauseSettings;
                _routedSettingsSnapshot?.Restore();
            }
            _routedSettings = null;
            _routedSettingsSnapshot = null;
            if (_routedMenu != null && _routedMenu.m_menuDialog != null
                && _routedMenu.m_root != null
                && _routedMenu.m_root.gameObject.activeSelf)
                _routedMenu.m_menuDialog.gameObject.SetActive(
                    _pauseMenuDialogWasActive);
            _routedMenu = null;
            _pauseMenuDialogWasActive = false;
            _pauseSettingsStage = null;
            if (_pauseSettingsCanvas != null) Destroy(_pauseSettingsCanvas);
            _pauseSettingsCanvas = null;
        }

        private const KeyCode CycleKey = KeyCode.F5;
        private const KeyCode BackKey = KeyCode.F6;

        /// <summary>
        /// Asks Unity what displays exist and tries to bring the second one up.
        /// </summary>
        /// <remarks>
        /// Runs once. <c>Display.displays</c> is fixed at startup on every platform
        /// Unity supports, so retrying would only repeat the same answer, and
        /// Activate on an already-active display is not free.
        /// </remarks>
        private void Probe()
        {
            _probed = true;

            int count = Display.displays.Length;
            Plugin.Log.LogInfo(
                "[SecondScreen] Display probe: Unity reports " + count + " display(s); " +
                "main=" + Screen.width + "x" + Screen.height);

            if (count < 2)
            {
                Plugin.Log.LogInfo(
                    "[SecondScreen] No second Unity display — the host is not offering one. " +
                    "Staying on the socket path.");
                return;
            }

            var second = Display.displays[1];
            try
            {
                // The requested size is a hint; Unity clamps it to what the host
                // actually provides, and `active` afterwards is the only answer
                // worth trusting.
                second.Activate();
            }
            catch (System.Exception e)
            {
                Plugin.Log.LogWarning("[SecondScreen] Display 1 Activate threw: " + e.Message);
                return;
            }

            SecondDisplayLive = second.active;
            if (SecondDisplayLive) ScaleRenderTarget(second);

            Plugin.Log.LogInfo(
                "[SecondScreen] Display 1 active=" + SecondDisplayLive +
                " system=" + second.systemWidth + "x" + second.systemHeight +
                " render=" + second.renderingWidth + "x" + second.renderingHeight);
        }

        /// <summary>
        /// Renders the panel below the panel's own resolution.
        /// </summary>
        /// <remarks>
        /// <para>
        /// The bottom screen activates at 1240x1080, and the game is running at
        /// 1280x720. That is 1.34 million pixels against 0.92 million: the
        /// second screen is <em>forty-five percent more expensive than Valheim
        /// itself</em>, every frame, to draw a map and a row of item icons.
        /// Nothing about that ratio was intended and it went unnoticed because
        /// the panel looks correct — it is only the cost that is absurd.
        /// </para>
        /// <para>
        /// <c>SetRenderingResolution</c> separates the two: the render target
        /// shrinks, the panel keeps its own resolution, and the display scales
        /// between them. Three quarters is the default rather than a half
        /// because this surface is mostly text and thin icon outlines, which is
        /// the content that shows softening first — and it still removes about
        /// four in every ten pixels drawn here.
        /// </para>
        /// <para>
        /// Applied once at probe time. The render target survives for the life
        /// of the display, and re-setting it per frame would reallocate it.
        /// </para>
        /// </remarks>
        internal static void ApplyConfiguredRenderScale()
        {
            if (Instance == null || Display.displays.Length < 2) return;
            Display second = Display.displays[1];
            if (second != null && second.active) Instance.ScaleRenderTarget(second);
        }

        private void ScaleRenderTarget(Display second)
        {
            float scale = UI.SecondScreenPreferences.RenderScale;

            int width = Mathf.Max(160, Mathf.RoundToInt(second.systemWidth * scale));
            int height = Mathf.Max(160, Mathf.RoundToInt(second.systemHeight * scale));
            if (width == _renderWidth && height == _renderHeight) return;

            try
            {
                second.SetRenderingResolution(width, height);
                _renderWidth = width;
                _renderHeight = height;
                Plugin.Log.LogInfo(
                    "[SecondScreen] Panel render target " + width + "x" + height +
                    " for a " + second.systemWidth + "x" + second.systemHeight + " panel (" +
                    Mathf.RoundToInt((1f - scale * scale) * 100f) + "% fewer pixels).");
            }
            catch (System.Exception e)
            {
                Plugin.Log.LogWarning(
                    "[SecondScreen] Could not scale the panel render target: " + e.Message);
            }
        }

        /// <summary>
        /// Reparents the HUD onto the second display's canvas.
        /// </summary>
        /// <remarks>
        /// A fresh Canvas rather than retargeting the game's own: Valheim hangs
        /// everything off one root canvas, so setting <c>targetDisplay</c> on it
        /// would move the entire interface — menus, chat, death screen — not the
        /// pieces wanted here. A new canvas takes only what is given to it.
        ///
        /// <c>worldPositionStays: false</c> on every move, so each element keeps
        /// its anchored layout instead of being flung to wherever its old world
        /// position lands under the new parent.
        ///
        /// The list itself is <see cref="HudBandMover.CollectLowerHudRoots"/>, the
        /// same one the row arrangement uses. Reaching the second screen a
        /// different way is no reason to disagree about what the HUD is, and the
        /// four elements this used to move were four of fifteen — everything else
        /// stayed on the game screen with no bars beside it.
        ///
        /// Ancestors first: where one root already contains another, moving the
        /// outer one carries the inner with it, and moving the inner one
        /// afterwards would tear it back out of its own prefab.
        /// </remarks>
        private void TryMove()
        {
            if (Minimap.instance == null || Hud.instance == null) return;

            var hud = Hud.instance;
            var root = hud.m_rootObject != null
                ? hud.m_rootObject.transform as RectTransform
                : null;
            if (root == null) return;   // HUD not built yet; try again next frame

            var minimap = _fSmallRoot?.GetValue(Minimap.instance) as GameObject;
            if (minimap == null) return;

            var roots = HudBandMover.CollectLowerHudRoots(hud, root)
                .Where(candidate => candidate && candidate != root)
                .Distinct()
                .ToList();
            roots = roots.Where(candidate =>
                    !roots.Any(other => other != candidate && candidate.IsChildOf(other)))
                .ToList();

            if (roots.Count < 5)
            {
                // The same guard the row arrangement uses. A handful of roots
                // means this build of Valheim is not the one these field names
                // describe, and moving a partial HUD is worse than moving none.
                Plugin.Log.LogError(
                    "[SecondScreen] Valheim HUD topology mismatch (only " + roots.Count +
                    " movable roots); refusing the display move.");
                _nextMoveAttempt = Time.unscaledTime + 5f;
                return;
            }

            _ui = new UI.SecondScreenUI();
            bool built;
            try
            {
                built = _ui.Build();
            }
            catch (System.Exception error)
            {
                Plugin.Log.LogError("[SecondScreen] Panel build failed: " + error);
                _ui.Destroy();
                _ui = null;
                _nextMoveAttempt = Time.unscaledTime + 5f;
                return;
            }
            if (!built)
            {
                Plugin.Log.LogError("[SecondScreen] Could not build the panel; InventoryGui absent.");
                _ui.Destroy();
                _ui = null;
                _nextMoveAttempt = Time.unscaledTime + 2f;
                return;
            }
            _canvas = _ui.Canvas;

            // m_foodBarRoot can be nested under another collected HUD root and
            // therefore removed by the ancestor filter above. Snapshot it on
            // its own before layout hides it, so reset restores the exact stock
            // hierarchy and CanvasGroup state.
            // The same applies to the health/stamina ornaments and the food
            // icon overlays that some Hud prefabs keep outside their bar root.
            // Snapshot every visual before SecondScreenLayout adds its hiding
            // CanvasGroup, so unloading/reloading the mod restores it exactly.
            foreach (var visual in SecondScreenLayout.CollectStockVitalVisuals(hud))
            {
                if (visual != null && !_movedRoots.ContainsKey(visual))
                    _movedRoots.Add(visual, new TransformSnapshot(visual));
            }

            int moved = 0;
            foreach (var item in roots) moved += Move(item);

            SecondScreenLayout.Apply(_ui, hud, roots);
            _suppressedVitalGroups.Clear();
            foreach (var visual in SecondScreenLayout.CollectStockVitalVisuals(hud))
            {
                var group = visual != null ? visual.GetComponent<CanvasGroup>() : null;
                if (group != null && !_suppressedVitalGroups.Contains(group))
                    _suppressedVitalGroups.Add(group);
            }

            // The real map stays on display 0. Display 1 gets its own view of
            // the live material, player marker and pins. It never changes the
            // stock map mode, so controller input and the B binding remain
            // entirely owned by normal gameplay/top-map code.
            if (!UI.LargeMap.Attach(_ui.PageMap, _ui.MapTools))
            {
                Plugin.Log.LogError("[SecondScreen] Could not build the plain map image.");
            }

            _moved = true;
            _nextMoveAttempt = 0f;
            _movedHud = hud;
            _movedMinimap = Minimap.instance;
            Plugin.Log.LogInfo("[SecondScreen] Moved " + moved + " HUD roots to display 1.");
        }

        /// <remarks>
        /// Moves and nothing else. This used to call <c>SetActive(true)</c> as
        /// well, on the reasoning that a root arriving on a fresh canvas ought to
        /// be shown — but these are not ours to show. Valheim switches the mount
        /// panel on when you are riding, the ship's controls when you are at a
        /// helm, the boss bar during a raid and the save icon while it writes;
        /// forcing them all on put a horse's health bar and a ship's rudder on
        /// the panel of somebody standing in a meadow on foot.
        /// </remarks>
        private int Move(Transform t)
        {
            if (t == null || _canvas == null) return 0;
            if (!_movedRoots.ContainsKey(t))
                _movedRoots.Add(t, new TransformSnapshot(t));
            t.SetParent(_ui.Root, worldPositionStays: false);
            return 1;
        }

        private void ResetPanel()
        {
            HidePauseSettings();
            UI.LargeMap.Reset();

            foreach (var pair in _movedRoots.ToArray())
                if (pair.Key) pair.Value.Restore();
            _movedRoots.Clear();
            _suppressedVitalGroups.Clear();

            _ui?.Destroy();
            _ui = null;
            _canvas = null;
            _movedHud = null;
            _movedMinimap = null;
            _moved = false;
        }

        private void OnDestroy()
        {
            HidePauseSettings();
            ResetPanel();
            if (_loading != null)
            {
                _loading.Destroy();
                _loading = null;
            }
            SecondDisplayLive = false;
            if (Instance == this) Instance = null;
        }

        /// <summary>Everything reparenting and layout changed on one HUD root.</summary>
        private sealed class TransformSnapshot
        {
            private readonly Transform _transform;
            private readonly Transform _parent;
            private readonly int _siblingIndex;
            private readonly Vector3 _localPosition;
            private readonly Quaternion _localRotation;
            private readonly Vector3 _localScale;
            private readonly bool _active;
            private readonly bool _isRect;
            private readonly Vector2 _anchorMin;
            private readonly Vector2 _anchorMax;
            private readonly Vector2 _pivot;
            private readonly Vector2 _sizeDelta;
            private readonly Vector3 _anchoredPosition;
            private readonly CanvasGroup _canvasGroup;
            private readonly float _groupAlpha;
            private readonly bool _groupInteractable;
            private readonly bool _groupBlocksRaycasts;

            public TransformSnapshot(Transform value)
            {
                _transform = value;
                _parent = value.parent;
                _siblingIndex = value.GetSiblingIndex();
                _localPosition = value.localPosition;
                _localRotation = value.localRotation;
                _localScale = value.localScale;
                _active = value.gameObject.activeSelf;

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

                _canvasGroup = value.GetComponent<CanvasGroup>();
                if (_canvasGroup != null)
                {
                    _groupAlpha = _canvasGroup.alpha;
                    _groupInteractable = _canvasGroup.interactable;
                    _groupBlocksRaycasts = _canvasGroup.blocksRaycasts;
                }
            }

            public void Restore()
            {
                if (!_transform) return;
                if (_parent) _transform.SetParent(_parent, false);
                _transform.localPosition = _localPosition;
                _transform.localRotation = _localRotation;
                _transform.localScale = _localScale;
                _transform.gameObject.SetActive(_active);

                if (_isRect)
                {
                    var rect = (RectTransform)_transform;
                    rect.anchorMin = _anchorMin;
                    rect.anchorMax = _anchorMax;
                    rect.pivot = _pivot;
                    rect.sizeDelta = _sizeDelta;
                    rect.anchoredPosition3D = _anchoredPosition;
                }

                if (_parent)
                    _transform.SetSiblingIndex(Mathf.Min(_siblingIndex, _parent.childCount - 1));

                var currentGroup = _transform.GetComponent<CanvasGroup>();
                if (_canvasGroup != null && currentGroup != null)
                {
                    currentGroup.alpha = _groupAlpha;
                    currentGroup.interactable = _groupInteractable;
                    currentGroup.blocksRaycasts = _groupBlocksRaycasts;
                }
                else if (_canvasGroup == null && currentGroup != null)
                {
                    currentGroup.alpha = 1f;
                    currentGroup.interactable = true;
                    currentGroup.blocksRaycasts = true;
                    Object.Destroy(currentGroup);
                }
            }
        }

        private sealed class CanvasTargetSnapshot
        {
            private readonly Canvas _canvas;
            private readonly int _targetDisplay;

            public CanvasTargetSnapshot(Canvas canvas)
            {
                _canvas = canvas;
                _targetDisplay = canvas.targetDisplay;
            }

            public bool Matches(Canvas canvas) => _canvas == canvas;

            public void Restore()
            {
                if (_canvas != null) _canvas.targetDisplay = _targetDisplay;
            }
        }
    }
}
