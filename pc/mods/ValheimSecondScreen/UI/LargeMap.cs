using System.Collections.Generic;
using System.Reflection;
using HarmonyLib;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

namespace ValheimSecondScreen.UI
{
    /// <summary>A self-contained, live map view for display 1.</summary>
    /// <remarks>
    /// It reuses Valheim's always-live small-map material, full-resolution map
    /// textures, pin prefab and player marker, but never activates or resizes
    /// the stock large-map hierarchy. The large material is not updated by the
    /// game until the real map has opened once; the small material is ready from
    /// the first gameplay frame and uses the same generated textures.
    /// </remarks>
    internal static class LargeMap
    {
        private static readonly FieldInfo PinsField =
            AccessTools.Field(typeof(Minimap), "m_pins");
        private static readonly FieldInfo VisibleIconTypesField =
            AccessTools.Field(typeof(Minimap), "m_visibleIconTypes");
        private static readonly FieldInfo SharedMapFadeField =
            AccessTools.Field(typeof(Minimap), "m_sharedMapDataFade");
        private static readonly FieldInfo MapTextureField =
            AccessTools.Field(typeof(Minimap), "m_mapTexture");
        private static readonly FieldInfo ForestMaskTextureField =
            AccessTools.Field(typeof(Minimap), "m_forestMaskTexture");
        private static readonly FieldInfo HeightTextureField =
            AccessTools.Field(typeof(Minimap), "m_heightTexture");
        private static readonly FieldInfo FogTextureField =
            AccessTools.Field(typeof(Minimap), "m_fogTexture");

        private static GameObject _root;
        private static RawImage _image;
        private static RectTransform _overlay;
        private static RawImage _source;
        private static Material _sourceMaterial;
        private static Material _material;
        private static RectTransform _playerMarker;
        private static RectTransform _shipMarker;
        private static Vector3 _offset;
        private static float _zoom;
        private static bool _zoomInitialised;
        private static float _mapAspect = 1f;
        private static Texture _boundFogTexture;
        private static float _lastZoom = float.NaN;
        private static Vector3 _lastCentre;
        private static bool _sourceStateCopied;
        // A NaN sentinel cannot work here: every comparison against NaN is
        // false, so the "has it moved" test never fired and the centre was
        // never written at all. A plain flag says it properly.
        private static bool _haveCentre;

        /// <summary>
        /// Pointer-only fallback for Wemu builds that route display-one touch
        /// directly into Unity. It never reads controller axes or changes the
        /// stock Minimap mode, so normal game input remains untouched. The host
        /// socket path still supplies multi-touch pinch on Android.
        /// </summary>
        private sealed class PointerInput : MonoBehaviour,
            IPointerDownHandler, IPointerUpHandler, IPointerClickHandler,
            IBeginDragHandler, IDragHandler, IEndDragHandler, IScrollHandler
        {
            private RectTransform? _rect;
            private MapToolbar? _toolbar;
            private Vector2 _last;
            private bool _dragging;
            private bool _moved;
            private int _pointerId = int.MinValue;
            private int _releasedPointerId = int.MinValue;
            private bool _releasedWasTap;

            private void Awake() => _rect = transform as RectTransform;

            public void Bind(MapToolbar toolbar) => _toolbar = toolbar;

            public void OnPointerDown(PointerEventData eventData)
            {
                if (eventData.button != PointerEventData.InputButton.Left
                    || _pointerId != int.MinValue) return;
                _pointerId = eventData.pointerId;
                _moved = false;
                _releasedPointerId = int.MinValue;
                _releasedWasTap = false;
            }

            public void OnPointerUp(PointerEventData eventData)
            {
                if (eventData.button != PointerEventData.InputButton.Left
                    || eventData.pointerId != _pointerId) return;
                _releasedPointerId = _pointerId;
                _releasedWasTap = !_moved;
                _pointerId = int.MinValue;
                _dragging = false;
            }

            public void OnPointerClick(PointerEventData eventData)
            {
                if (eventData.button != PointerEventData.InputButton.Left
                    || eventData.pointerId != _releasedPointerId
                    || !_releasedWasTap || _toolbar == null
                    || !TryLocal(eventData, out Vector2 local)) return;
                _releasedWasTap = false;
                _toolbar.Tap(_rect, local);
            }

            public void OnBeginDrag(PointerEventData eventData)
            {
                _dragging = eventData.button == PointerEventData.InputButton.Left
                            && eventData.pointerId == _pointerId
                            && TryLocal(eventData, out _last);
            }

            public void OnDrag(PointerEventData eventData)
            {
                if (!_dragging || eventData.pointerId != _pointerId
                    || !TryLocal(eventData, out Vector2 current)
                    || _rect == null || _rect.rect.width <= 0f
                    || _rect.rect.height <= 0f) return;
                Vector2 delta = current - _last;
                _last = current;
                if (delta.sqrMagnitude > 0.25f) _moved = true;
                Pan(delta.x / _rect.rect.width,
                    -delta.y / _rect.rect.height);
            }

            public void OnEndDrag(PointerEventData eventData)
            {
                if (eventData.pointerId == _pointerId) _dragging = false;
            }

            public void OnScroll(PointerEventData eventData)
            {
                if (Mathf.Abs(eventData.scrollDelta.y) < 0.01f) return;
                Scale(eventData.scrollDelta.y > 0f ? 1f / 1.20f : 1.20f);
            }

            private bool TryLocal(PointerEventData eventData, out Vector2 point)
            {
                point = Vector2.zero;
                return _rect != null && RectTransformUtility.ScreenPointToLocalPointInRectangle(
                    _rect, eventData.position, eventData.pressEventCamera, out point);
            }
        }

        private sealed class PinVisual
        {
            public GameObject Root;
            public RectTransform Rect;
            public Image Icon;
            public GameObject Checked;
        }

        private static readonly Dictionary<Minimap.PinData, PinVisual> PinVisuals =
            new Dictionary<Minimap.PinData, PinVisual>();
        private static readonly HashSet<Minimap.PinData> LivePins =
            new HashSet<Minimap.PinData>();
        private static readonly List<Minimap.PinData> DeadPins =
            new List<Minimap.PinData>();

        public static bool Active => _root != null;

        public static bool Attach(RectTransform parent, MapToolbar toolbar)
        {
            Reset();
            var minimap = Minimap.instance;
            if (parent == null || minimap == null
                || (minimap.m_mapImageSmall == null
                    && minimap.m_mapImageLarge == null))
                return false;

            _root = new GameObject(
                "SecondScreen_MapImage",
                typeof(RectTransform),
                typeof(CanvasRenderer),
                typeof(RawImage));
            var rect = (RectTransform)_root.transform;
            Fill(rect, parent);
            rect.SetAsFirstSibling();

            _image = _root.GetComponent<RawImage>();
            _image.raycastTarget = true;
            _root.AddComponent<PointerInput>().Bind(toolbar);
            _source = LiveSource(minimap);

            var overlayObject = new GameObject(
                "SecondScreen_MapMarkers",
                typeof(RectTransform),
                typeof(CanvasGroup));
            _overlay = (RectTransform)overlayObject.transform;
            Fill(_overlay, rect);
            var group = overlayObject.GetComponent<CanvasGroup>();
            group.interactable = false;
            group.blocksRaycasts = false;

            BuildStockMarkers(minimap);
            Tick();
            return true;
        }

        /// <summary>Refreshes the full-resolution map, player and visible pins.</summary>
        /// <summary>
        /// How often the map is recomputed, in seconds.
        /// </summary>
        /// <remarks>
        /// The map moves at walking pace and its pins do not move at all, so a
        /// frame-by-frame rebuild bought nothing and cost a material update and
        /// a reposition of every visible pin, every frame, on the second
        /// display's canvas. Fifteen times a second is smooth for something
        /// being read rather than aimed with.
        /// </remarks>
        private const float TickInterval = 1f / 15f;

        private static float _nextTick;

        public static void Tick()
        {
            if (_image == null) return;
            if (Time.unscaledTime < _nextTick) return;
            _nextTick = Time.unscaledTime + TickInterval;

            var minimap = Minimap.instance;
            var player = Player.m_localPlayer;
            bool visible = minimap != null && player != null
                           && !Game.m_noMap && !player.IsDead();
            _image.enabled = visible;
            if (_overlay != null) _overlay.gameObject.SetActive(visible);
            if (!visible) return;

            var liveSource = LiveSource(minimap);
            if (liveSource != null && liveSource != _source)
            {
                _source = liveSource;
                _sourceMaterial = null;
            }
            if (_source == null) return;

            if (!SynchroniseMaterial(minimap)) return;

            if (!_zoomInitialised)
            {
                _zoom = ConfiguredZoom(minimap);
                _zoomInitialised = true;
            }

            if (SecondScreenPreferences.MapFollowPlayer) _offset = Vector3.zero;

            // The stock small-map image is parked below an inactive hierarchy.
            // Its Graphic tint is not part of the map data and can be faded by
            // HUD transitions, so never inherit that alpha on the live copy.
            _image.color = Color.white;

            var mapRect = _image.rectTransform.rect;
            if (mapRect.width <= 0f || mapRect.height <= 0f) return;

            Vector3 centre = player.transform.position + _offset;
            Vector2 mapCentre = WorldToMap(minimap, centre);
            float aspect = mapRect.width / mapRect.height;
            _mapAspect = aspect;
            var uv = new Rect(0f, 0f, _zoom * aspect, _zoom)
            {
                center = mapCentre,
            };
            _image.uvRect = uv;

            /*
             * Written only when they change.
             *
             * Every Set on a material marks it dirty and has the render thread
             * re-upload its constant buffer. Standing still, none of these three
             * move, so the upload was pure cost -- and it happened on the second
             * display's own draw, which is the one competing with the game.
             */
            if (_sourceStateCopied || !Mathf.Approximately(_zoom, _lastZoom))
            {
                _lastZoom = _zoom;
                if (_material.HasProperty("_zoom"))
                    _material.SetFloat("_zoom", _zoom);
                if (_material.HasProperty("_pixelSize"))
                    _material.SetFloat("_pixelSize", 200f / _zoom);
            }
            if (_sourceStateCopied || !_haveCentre
                || (centre - _lastCentre).sqrMagnitude > 0.01f)
            {
                _haveCentre = true;
                _lastCentre = centre;
                if (_material.HasProperty("_mapCenter"))
                    _material.SetVector("_mapCenter", centre);
            }
            if (_material.HasProperty("_SharedFade") && SharedMapFadeField != null)
                _material.SetFloat("_SharedFade",
                    (float)SharedMapFadeField.GetValue(minimap));
            _sourceStateCopied = false;

            UpdatePlayerMarker(minimap, player, uv, mapRect);
            UpdatePins(minimap, uv, mapRect);
        }

        /// <summary>Moves only the bottom view. The stock top map is untouched.</summary>
        public static void Pan(float dx, float dy)
        {
            var minimap = Minimap.instance;
            if (!_zoomInitialised || minimap == null
                || float.IsNaN(dx) || float.IsNaN(dy)
                || float.IsInfinity(dx) || float.IsInfinity(dy)) return;

            // Pointer-count changes can produce one anomalous transform frame.
            // Limit a packet to a small part of the view so that frame cannot
            // throw the map across the world.
            dx = Mathf.Clamp(dx, -0.035f, 0.035f);
            dy = Mathf.Clamp(dy, -0.035f, 0.035f);
            float sensitivity = SecondScreenPreferences.MapPanSensitivity;
            dx *= sensitivity;
            dy *= sensitivity;
            if (SecondScreenPreferences.MapFollowPlayer)
                SecondScreenPreferences.MapFollowPlayer = false;
            float worldSize = minimap.m_textureSize * minimap.m_pixelSize;
            _offset.x -= dx * _zoom * _mapAspect * worldSize;
            _offset.z += dy * _zoom * worldSize;
            ClampOffset(minimap);
        }

        /// <summary>Scales only the bottom view within Valheim's zoom limits.</summary>
        public static string Scale(float factor)
        {
            var minimap = Minimap.instance;
            if (minimap == null) return "no minimap";
            if (factor <= 0f || float.IsNaN(factor) || float.IsInfinity(factor))
                return "bad factor";

            if (!_zoomInitialised)
            {
                _zoom = ConfiguredZoom(minimap);
                _zoomInitialised = true;
            }

            // Treat every message as a small gesture update. This also makes
            // old hosts safe if they emit an outlier when a second finger lands.
            factor = Mathf.Clamp(factor, 0.8f, 1.25f);
            factor = 1f + (factor - 1f) * SecondScreenPreferences.MapPinchSensitivity;
            float current = _zoom;
            _zoom = Mathf.Clamp(current * factor,
                                minimap.m_minZoom, minimap.m_maxZoom);
            ClampOffset(minimap);
            return current.ToString("0.000") + " -> " + _zoom.ToString("0.000");
        }

        public static void Centre() => _offset = Vector3.zero;

        public static bool TryWorldPoint(float normalizedX, float normalizedY,
                                         out Vector3 world)
        {
            world = Vector3.zero;
            var minimap = Minimap.instance;
            if (_image == null || minimap == null
                || float.IsNaN(normalizedX) || float.IsNaN(normalizedY)) return false;
            Rect uv = _image.uvRect;
            float mx = Mathf.Lerp(uv.xMin, uv.xMax, Mathf.Clamp01(normalizedX));
            float my = Mathf.Lerp(uv.yMin, uv.yMax, Mathf.Clamp01(normalizedY));
            float worldSize = minimap.m_textureSize * minimap.m_pixelSize;
            float y = Player.m_localPlayer != null
                ? Player.m_localPlayer.transform.position.y : 0f;
            world = new Vector3((mx - 0.5f) * worldSize, y,
                                (my - 0.5f) * worldSize);
            return true;
        }

        public static bool RemovePinAt(Vector3 world)
        {
            var minimap = Minimap.instance;
            if (minimap == null || PinsField == null) return false;

            var pins = PinsField.GetValue(minimap) as List<Minimap.PinData>;
            if (pins == null) return false;

            float zoom = _zoomInitialised ? _zoom : ConfiguredZoom(minimap);
            float radius = minimap.m_removeRadius * zoom * 2f;

            // Match the actual lower-screen marker footprint as well as the
            // stock map radius. At very close zoom, Valheim's world-space
            // radius can be smaller than the rendered pin under a fingertip.
            if (_image != null && _image.rectTransform.rect.width > 0f
                && _image.rectTransform.rect.height > 0f)
            {
                Rect uv = _image.uvRect;
                Rect rect = _image.rectTransform.rect;
                float worldSize = minimap.m_textureSize * minimap.m_pixelSize;
                float worldPerPixel = Mathf.Max(
                    uv.width * worldSize / rect.width,
                    uv.height * worldSize / rect.height);
                float markerRadius = minimap.m_pinSizeLarge
                                     * SecondScreenPreferences.PinScale * 0.65f;
                radius = Mathf.Max(radius, worldPerPixel * markerRadius);
            }

            Minimap.PinData closest = null;
            float closestDistance = float.MaxValue;
            foreach (Minimap.PinData pin in pins)
            {
                // Remove only persistent pins currently drawn on the bottom
                // map. Runtime player, ping, event, and off-screen markers must
                // never be selected by a nearby touch.
                if (pin == null || !pin.m_save || !LivePins.Contains(pin)) continue;
                float distance = Utils.DistanceXZ(world, pin.m_pos);
                if (distance >= radius || distance >= closestDistance) continue;
                closest = pin;
                closestDistance = distance;
            }

            if (closest == null) return false;
            minimap.RemovePin(closest);
            return true;
        }

        public static void SetConfiguredZoom()
        {
            var minimap = Minimap.instance;
            if (minimap == null)
            {
                _zoomInitialised = false;
                return;
            }
            _zoom = ConfiguredZoom(minimap);
            _zoomInitialised = true;
            ClampOffset(minimap);
        }

        private static float ConfiguredZoom(Minimap minimap)
        {
            return Mathf.Lerp(minimap.m_minZoom, minimap.m_maxZoom,
                              SecondScreenPreferences.MapZoom);
        }

        private static void ClampOffset(Minimap minimap)
        {
            var player = Player.m_localPlayer;
            if (minimap == null || player == null) return;

            float worldSize = minimap.m_textureSize * minimap.m_pixelSize;
            float halfX = Mathf.Min(0.5f,
                _zoom * Mathf.Max(0.01f, _mapAspect) * 0.5f);
            float halfY = Mathf.Min(0.5f, _zoom * 0.5f);
            Vector3 centre = player.transform.position + _offset;
            Vector2 point = WorldToMap(minimap, centre);
            point.x = Mathf.Clamp(point.x, halfX, 1f - halfX);
            point.y = Mathf.Clamp(point.y, halfY, 1f - halfY);

            _offset.x = (point.x - 0.5f) * worldSize - player.transform.position.x;
            _offset.z = (point.y - 0.5f) * worldSize - player.transform.position.z;
        }

        public static void Reset()
        {
            if (_root != null) Object.Destroy(_root);
            if (_material != null) Object.Destroy(_material);
            _root = null;
            _image = null;
            _overlay = null;
            _source = null;
            _sourceMaterial = null;
            _material = null;
            _playerMarker = null;
            _shipMarker = null;
            _offset = Vector3.zero;
            _zoom = 0f;
            _zoomInitialised = false;
            _mapAspect = 1f;
            _boundFogTexture = null;
            _lastZoom = float.NaN;
            _lastCentre = Vector3.zero;
            _haveCentre = false;
            _sourceStateCopied = false;
            PinVisuals.Clear();
            LivePins.Clear();
            DeadPins.Clear();
        }

        /// <summary>
        /// Keeps the independent material wired to Valheim's live procedural
        /// textures without sharing its zoom and centre with the stock map.
        /// </summary>
        private static bool SynchroniseMaterial(Minimap minimap)
        {
            var current = _source != null ? _source.material : null;
            if (current == null || _image == null) return false;

            if (current != _sourceMaterial || _material == null)
            {
                if (_material != null) Object.Destroy(_material);
                _sourceMaterial = current;
                _material = new Material(current)
                {
                    name = "SecondScreen_MapMaterial",
                };
                _image.material = _material;
                // A new material has no lower-screen view state yet. Force the
                // next Tick to write both values even if this is a reattach at
                // exactly the same position and zoom as the previous canvas.
                _lastZoom = float.NaN;
                _lastCentre = Vector3.zero;
                _haveCentre = false;
                Plugin.Log.LogInfo(
                    "[SecondScreen] Bottom map material initialised independently of the stock map root.");
            }

            // Valheim updates more than just the four procedural textures on
            // this material. Copy the complete live shader state so terrain and
            // fog cannot become blank, then make Tick rewrite our independent
            // zoom and centre before the canvas renders. Copying without that
            // immediate rewrite was the old pan/pinch snap-back bug.
            _material.CopyPropertiesFromMaterial(current);
            _sourceStateCopied = true;

            var map = MapTextureField?.GetValue(minimap) as Texture
                      ?? current.GetTexture("_MainTex");
            var forest = ForestMaskTextureField?.GetValue(minimap) as Texture
                         ?? current.GetTexture("_MaskTex");
            var height = HeightTextureField?.GetValue(minimap) as Texture
                         ?? current.GetTexture("_HeightTex");
            var fog = FogTextureField?.GetValue(minimap) as Texture
                      ?? current.GetTexture("_FogTex");
            if (map == null || fog == null) return false;

            // RawImage.mainTexture can override a material's _MainTex at the
            // CanvasRenderer. Bind both paths to the generated terrain texture.
            _image.texture = map;
            _material.SetTexture("_MainTex", map);
            if (forest != null) _material.SetTexture("_MaskTex", forest);
            if (height != null) _material.SetTexture("_HeightTex", height);
            _material.SetTexture("_FogTex", fog);

            if (fog != _boundFogTexture)
            {
                _boundFogTexture = fog;
                Plugin.Log.LogInfo(
                    "[SecondScreen] Bound live minimap textures: map="
                    + map.width + "x" + map.height + ", fog="
                    + fog.width + "x" + fog.height + ".");
            }
            return true;
        }

        private static RawImage LiveSource(Minimap minimap)
        {
            // Small-map mode is Valheim's normal gameplay state, so this
            // material receives CenterMap and exploration state immediately.
            // Both map images are backed by the same full-size procedural
            // textures; choosing it does not lower the bottom map's resolution.
            if (minimap == null) return null;
            return minimap.m_mapImageSmall != null
                ? minimap.m_mapImageSmall
                : minimap.m_mapImageLarge;
        }

        private static void BuildStockMarkers(Minimap minimap)
        {
            if (_overlay == null) return;
            _playerMarker = CloneMarker(minimap.m_largeMarker, "Player");
            _shipMarker = CloneMarker(minimap.m_largeShipMarker, "Ship");
        }

        private static RectTransform CloneMarker(RectTransform source, string name)
        {
            if (source == null || _overlay == null) return null;
            var clone = Object.Instantiate(source.gameObject, _overlay, false);
            clone.name = "SecondScreen_" + name + "Marker";
            clone.SetActive(false);
            var rect = clone.transform as RectTransform;
            rect.anchorMin = rect.anchorMax = new Vector2(0.5f, 0.5f);
            rect.pivot = new Vector2(0.5f, 0.5f);
            rect.localScale = Vector3.one;
            DisableRaycasts(clone);
            return rect;
        }

        private static void UpdatePlayerMarker(Minimap minimap, Player player,
                                               Rect uv, Rect mapRect)
        {
            var camera = Utils.GetMainCamera();
            float playerAngle = camera != null
                ? camera.transform.rotation.eulerAngles.y
                : player.transform.rotation.eulerAngles.y;
            SetMarker(_playerMarker, minimap, player.transform.position,
                      0f - playerAngle, uv, mapRect);

            var ship = player.GetControlledShip();
            if (ship != null)
                SetMarker(_shipMarker, minimap, ship.transform.position,
                          0f - ship.transform.rotation.eulerAngles.y, uv, mapRect);
            else if (_shipMarker != null)
                _shipMarker.gameObject.SetActive(false);
        }

        private static void SetMarker(RectTransform marker, Minimap minimap,
                                      Vector3 world, float angle,
                                      Rect uv, Rect mapRect)
        {
            if (marker == null) return;
            if (!TryMapPosition(minimap, world, uv, mapRect, out var position))
            {
                marker.gameObject.SetActive(false);
                return;
            }
            marker.gameObject.SetActive(true);
            marker.anchoredPosition = position;
            marker.localRotation = Quaternion.Euler(0f, 0f, angle);
            float scale = SecondScreenPreferences.PlayerMarkerScale;
            marker.localScale = new Vector3(scale, scale, 1f);
        }

        private static void UpdatePins(Minimap minimap, Rect uv, Rect mapRect)
        {
            var pins = PinsField != null
                ? PinsField.GetValue(minimap) as List<Minimap.PinData>
                : null;
            if (pins == null || _overlay == null || minimap.m_pinPrefab == null) return;

            var visibleTypes = VisibleIconTypesField != null
                ? VisibleIconTypesField.GetValue(minimap) as bool[]
                : null;
            float sharedFade = SharedMapFadeField != null
                ? (float)SharedMapFadeField.GetValue(minimap)
                : 1f;

            LivePins.Clear();
            foreach (var pin in pins)
            {
                if (pin == null || pin.m_icon == null) continue;
                int type = (int)pin.m_type;
                if (visibleTypes != null
                    && (type < 0 || type >= visibleTypes.Length || !visibleTypes[type]))
                    continue;
                if (pin.m_ownerID != 0L && sharedFade <= 0f) continue;
                if (!TryMapPosition(minimap, pin.m_pos, uv, mapRect, out var position))
                    continue;

                LivePins.Add(pin);
                if (!PinVisuals.TryGetValue(pin, out var visual)
                    || visual.Root == null)
                {
                    visual = CreatePin(minimap);
                    if (visual == null) continue;
                    PinVisuals[pin] = visual;
                }

                visual.Root.SetActive(true);
                visual.Icon.sprite = pin.m_icon;
                visual.Icon.color = pin.m_ownerID != 0L
                    ? new Color(0.7f, 0.7f, 0.7f, 0.8f * sharedFade)
                    : Color.white;
                visual.Rect.anchoredPosition = position;

                float size = pin.m_doubleSize
                    ? minimap.m_pinSizeLarge * 2f
                    : minimap.m_pinSizeLarge;
                size *= SecondScreenPreferences.PinScale;
                if (pin.m_animate)
                    size *= 0.8f + Mathf.Sin(Time.time * 5f) * 0.2f;
                if (pin.m_worldSize > 0f)
                {
                    float mapFraction = pin.m_worldSize
                                        / minimap.m_pixelSize
                                        / minimap.m_textureSize;
                    visual.Rect.sizeDelta = new Vector2(
                        mapFraction / uv.width * mapRect.width,
                        mapFraction / uv.height * mapRect.height)
                        * SecondScreenPreferences.PinScale;
                }
                else
                {
                    visual.Rect.sizeDelta = new Vector2(size, size);
                }

                if (visual.Checked != null)
                    visual.Checked.SetActive(pin.m_checked);
            }

            DeadPins.Clear();
            foreach (var pair in PinVisuals)
                if (!LivePins.Contains(pair.Key)) DeadPins.Add(pair.Key);
            foreach (var pin in DeadPins)
            {
                var visual = PinVisuals[pin];
                if (visual.Root != null) Object.Destroy(visual.Root);
                PinVisuals.Remove(pin);
            }
        }

        private static PinVisual CreatePin(Minimap minimap)
        {
            if (_overlay == null || minimap.m_pinPrefab == null) return null;
            var root = Object.Instantiate(minimap.m_pinPrefab, _overlay, false);
            root.name = "SecondScreen_MapPin";
            var rect = root.transform as RectTransform;
            var icon = root.GetComponent<Image>();
            if (rect == null || icon == null)
            {
                Object.Destroy(root);
                return null;
            }
            rect.anchorMin = rect.anchorMax = new Vector2(0.5f, 0.5f);
            rect.pivot = new Vector2(0.5f, 0.5f);
            rect.localScale = Vector3.one;
            DisableRaycasts(root);
            return new PinVisual
            {
                Root = root,
                Rect = rect,
                Icon = icon,
                Checked = root.transform.Find("Checked")?.gameObject,
            };
        }

        private static bool TryMapPosition(Minimap minimap, Vector3 world,
                                           Rect uv, Rect mapRect,
                                           out Vector2 position)
        {
            Vector2 point = WorldToMap(minimap, world);
            float nx = (point.x - uv.xMin) / uv.width;
            float ny = (point.y - uv.yMin) / uv.height;
            if (nx <= 0f || nx >= 1f || ny <= 0f || ny >= 1f)
            {
                position = Vector2.zero;
                return false;
            }
            position = new Vector2(
                (nx - 0.5f) * mapRect.width,
                (ny - 0.5f) * mapRect.height);
            return true;
        }

        private static Vector2 WorldToMap(Minimap minimap, Vector3 world)
        {
            float half = minimap.m_textureSize * 0.5f;
            return new Vector2(
                (world.x / minimap.m_pixelSize + half) / minimap.m_textureSize,
                (world.z / minimap.m_pixelSize + half) / minimap.m_textureSize);
        }

        private static void Fill(RectTransform rect, Transform parent)
        {
            rect.SetParent(parent, false);
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.one;
            rect.offsetMin = Vector2.zero;
            rect.offsetMax = Vector2.zero;
            rect.localScale = Vector3.one;
        }

        private static void DisableRaycasts(GameObject root)
        {
            foreach (var graphic in root.GetComponentsInChildren<Graphic>(true))
                graphic.raycastTarget = false;
        }
    }
}
