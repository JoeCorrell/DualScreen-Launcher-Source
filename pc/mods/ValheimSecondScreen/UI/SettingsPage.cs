using System;
using System.Collections.Generic;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace ValheimSecondScreen.UI
{
    /// <summary>Settings owned by the second-screen UI only.</summary>
    internal sealed class SettingsPage
    {
        // Nine rows fit the 1240x1080 lower display without shrinking touch
        // targets; additional controls use the page arrows below.
        private const int VisibleRows = 9;
        private const int CategoryColumns = 4;
        private const float BodyTop = 160f;
        private const float BodyBottom = 16f;
        private const float RowGap = 7f;
        private static readonly Color Idle = UiTheme.Control;
        private static readonly Color Selected = UiTheme.ControlSelected;
        private static readonly Color Gold = UiTheme.Gold;
        private static readonly Color Text = UiTheme.Text;
        private static readonly Color Weak = UiTheme.Muted;

        private readonly SecondScreenUI _owner;
        private readonly RectTransform _page;
        private readonly TMP_FontAsset _font;
        private readonly Vector2 _size;
        private readonly List<Entry> _entries = new List<Entry>();
        private readonly List<Row> _rows = new List<Row>();
        private readonly List<Button> _buttons = new List<Button>();
        private readonly List<Image> _categoryFrames = new List<Image>();
        private readonly List<Image> _categoryAccents = new List<Image>();

        private TMP_Text _message;
        private TMP_Text _pageIndicator;
        private Button _previousPage;
        private Button _nextPage;
        private int _category;
        private int _entryPage;
        private float _nextPaint;
        private string _entryGroup = "";

        private static readonly string[] CategoryNames =
        {
            "INTERFACE", "MAP", "TOUCH", "PERFORMANCE"
        };
        private static readonly string[] CategoryDescriptions =
        {
            "LOWER-DISPLAY APPEARANCE, INVENTORY AND WORLD INFORMATION",
            "LOWER MAP SCALE, PANNING AND MARKERS",
            "TOUCH TIMING, CONFIRMATIONS AND SHORTCUTS",
            "VALHEIM PERFORMANCE, STREAMING, DISTANCE AND QUALITY"
        };

        private static readonly string[] PipelineFields =
        {
            "QueuedFrames", "WidenUploadBudget", "UploadTimeSlice",
            "UploadBufferSize", "SpreadGarbageCollection",
            "GcSliceMicroseconds", "ShaderVariantsPerFrame",
            "DiagnosticsEnabled", "DiagnosticsWindowSeconds"
        };

        private static readonly string[] DistanceFields =
        {
            "SmokeDistance", "SmokeStride", "LightDistance", "LightStride",
            "VisualsDistance", "VisualsStride"
        };

        private sealed class Entry
        {
            public string Name;
            public string Detail;
            public Func<string> Value;
            public Action<int> Change;
            public bool IsAction;
            public string Group;
        }

        private sealed class Row
        {
            public GameObject Root;
            public TMP_Text Name;
            public TMP_Text Detail;
            public TMP_Text Value;
            public Button Less;
            public Button ValueButton;
            public Button More;
        }

        public SettingsPage(SecondScreenUI owner, RectTransform page,
                            TMP_FontAsset font, Vector2 size)
        {
            _owner = owner;
            _page = page;
            _font = font;
            _size = size;
            Build();
            SetCategory(0);
        }

        private void Build()
        {
            var title = Label(_page, "Title", "SETTINGS", 34f, Gold,
                              TextAlignmentOptions.Center);
            Place(title.rectTransform, 0f, 5f, _size.x, 44f);
            title.fontStyle = FontStyles.Bold;
            UiSkin.PageHeading(_page, title, 76f);

            _message = Label(_page, "Scope", "These settings affect the lower display only",
                             17f, Weak, TextAlignmentOptions.Center);
            Place(_message.rectTransform, 8f, 47f, _size.x - 264f, 26f);
            SecondScreenUI.FitText(_message, 10f, 17f);

            // Page navigation is touched much more often than an individual
            // value. Keep it at the theme's minimum touch size instead of the
            // old 34-pixel target, which was unnecessarily fiddly on the Thor.
            const float pageArrow = 52f;
            _previousPage = ActionButton(_page, "PreviousPage", "<",
                _size.x - 236f, 25f, pageArrow, pageArrow, () => ChangePage(-1));
            _pageIndicator = Label(_page, "PageIndicator", "", 15f, Weak,
                TextAlignmentOptions.Center);
            Place(_pageIndicator.rectTransform, _size.x - 176f, 38f, 104f, 26f);
            _nextPage = ActionButton(_page, "NextPage", ">",
                _size.x - 64f, 25f, pageArrow, pageArrow, () => ChangePage(1));

            const float left = 12f;
            const float gap = 8f;
            const float categoryTop = 80f;
            const float categoryHeight = 66f;
            float width = (_size.x - left * 2f
                           - gap * (CategoryColumns - 1)) / CategoryColumns;
            for (int i = 0; i < CategoryNames.Length; i++)
            {
                int category = i;
                int column = i % CategoryColumns;
                Button button = ActionButton(_page, "Category_" + CategoryNames[i],
                    CategoryNames[i], left + column * (width + gap),
                    categoryTop, width, categoryHeight,
                    () => SetCategory(category));
                var label = button.GetComponentInChildren<TMP_Text>(true);
                if (label != null)
                {
                    label.fontSize = 22f;
                    SecondScreenUI.FitText(label, 12f, 22f);
                }
                _categoryFrames.Add(button.GetComponent<Image>());
                _categoryAccents.Add(UiSkin.SelectionAccent(
                    (RectTransform)button.transform, true));
            }

            float rowHeight = (_size.y - BodyTop - BodyBottom
                               - RowGap * (VisibleRows - 1))
                              / VisibleRows;
            for (int i = 0; i < VisibleRows; i++)
            {
                int index = i;
                _rows.Add(CreateRow(BodyTop + i * (rowHeight + RowGap), rowHeight,
                                    () => ChangeVisible(index, -1),
                                    () => ChangeVisible(index, 1)));
            }
        }

        public void Tick()
        {
            if (!_page.gameObject.activeInHierarchy) return;
            if (Time.unscaledTime >= _nextPaint)
            {
                _nextPaint = Time.unscaledTime + 0.25f;
                Paint();
            }
        }

        public void Refresh(bool force = false)
        {
            if (force || Time.unscaledTime >= _nextPaint)
            {
                _nextPaint = Time.unscaledTime + 0.25f;
                Paint();
            }
        }

        public bool Tap(RectTransform sourceRoot, Vector2 sourcePoint)
        {
            if (!_page.gameObject.activeInHierarchy || sourceRoot == null) return false;
            Vector3 world = sourceRoot.TransformPoint(sourcePoint);
            foreach (Button button in _buttons)
            {
                if (button == null || !button.gameObject.activeInHierarchy || !button.interactable)
                    continue;
                var rect = button.transform as RectTransform;
                if (rect == null) continue;
                Vector3 local = rect.InverseTransformPoint(world);
                if (!rect.rect.Contains(new Vector2(local.x, local.y))) continue;
                button.onClick.Invoke();
                return true;
            }
            return false;
        }

        private void SetCategory(int category)
        {
            _category = Mathf.Clamp(category, 0, CategoryNames.Length - 1);
            _entryPage = 0;
            if (_message != null)
            {
                string prefix = _category == 3 && PerformanceSettingsBridge.Available
                    ? "VALHEIM PERFORMANCE v" + PerformanceSettingsBridge.Version + "  |  "
                    : "";
                _message.text = prefix + CategoryDescriptions[_category];
            }
            BuildEntries();
            Paint();
        }

        private void BuildEntries()
        {
            _entries.Clear();
            _entryGroup = "";
            if (_category == 0)
            {
                _entryGroup = "INTERFACE";
                BuildAppearance();
                BuildHud();
            }
            else if (_category == 1)
            {
                _entryGroup = "MAP";
                BuildMap();
            }
            else if (_category == 2)
            {
                _entryGroup = "TOUCH";
                BuildTouch();
            }
            else BuildPerformance();
        }

        private void BuildPerformance()
        {
            if (BuildPerformanceUnavailable()) return;
            BuildRuntime();
            BuildPipeline();
            BuildDistance();
            BuildQuality();
        }

        private void BuildAppearance()
        {
            AddFloat("Background brightness", () => SecondScreenPreferences.BackgroundBrightness,
                value => { SecondScreenPreferences.BackgroundBrightness = value; _owner.ApplyPreferences(); },
                0.55f, 1.35f, 0.05f, Percent,
                "Brightness of the lower display's panel texture.");
            AddFloat("Dark tint overlay", () => SecondScreenPreferences.Shade,
                value => { SecondScreenPreferences.Shade = value; _owner.ApplyPreferences(); },
                0f, 0.55f, 0.05f, Percent,
                "Adds contrast without changing the main game display.");
            AddFloat("Panel render scale", () => SecondScreenPreferences.RenderScale,
                value =>
                {
                    SecondScreenPreferences.RenderScale = value;
                    Hooks.SecondDisplayMover.ApplyConfiguredRenderScale();
                },
                0.50f, 1f, 0.05f, Percent,
                "Higher is sharper; lower reduces the GPU cost of the second display.");
            AddToggle("Button touch feedback", () => SecondScreenPreferences.ButtonFeedback,
                value => SecondScreenPreferences.ButtonFeedback = value,
                "Show the warm press tint and subtle compression on controls.");
            AddChoice("Default page", () => SecondScreenPreferences.DefaultPage,
                SecondScreenPreferences.DefaultPageCount,
                _ => SecondScreenPreferences.DefaultPageName,
                value => SecondScreenPreferences.DefaultPage = value,
                "Page shown when the lower display first becomes ready.");
            AddToggle("Remember last page", () => SecondScreenPreferences.RememberLastTab,
                value => SecondScreenPreferences.RememberLastTab = value,
                "Resume the last gameplay page instead of the default.");
            AddAction("Reset lower-screen settings", "RESET", () =>
            {
                SecondScreenPreferences.ResetAll();
                _owner.ApplyPreferences();
                Hooks.SecondDisplayMover.ApplyConfiguredRenderScale();
                LargeMap.SetConfiguredZoom();
                SetCategory(0);
            }, "Restore every option on this screen to its default.");
        }

        private void BuildHud()
        {
            AddToggle("Item detail badges", () => SecondScreenPreferences.ShowItemBadges,
                value =>
                {
                    SecondScreenPreferences.ShowItemBadges = value;
                    _owner.Gear?.Refresh();
                    _owner.Hotbar?.Refresh();
                    _owner.Containers?.Refresh();
                    _owner.Crafting?.Refresh();
                }, "Show quality stars, equipped state and condition labels.");
            AddToggle("Food timers", () => SecondScreenPreferences.ShowFoodTimers,
                value => SecondScreenPreferences.ShowFoodTimers = value,
                "Show the remaining duration directly on each food slot.");
            AddToggle("Carry capacity meter", () => SecondScreenPreferences.ShowCarryMeter,
                value =>
                {
                    SecondScreenPreferences.ShowCarryMeter = value;
                    _owner.Gear?.Refresh(true);
                }, "Show the weight bar beneath the inventory capacity.");
            AddToggle("Status effects", () => SecondScreenPreferences.ShowStatusEffects,
                value => SecondScreenPreferences.ShowStatusEffects = value,
                "Show active buffs and debuffs on the dashboard.");
            AddToggle("24-hour clock", () => SecondScreenPreferences.Use24HourClock,
                value => SecondScreenPreferences.Use24HourClock = value,
                "Switch between 24-hour and 12-hour world-time labels.");
            AddToggle("Weather details", () => SecondScreenPreferences.ShowWeatherDetails,
                value => SecondScreenPreferences.ShowWeatherDetails = value,
                "Show the current weather name and icon.");
            AddToggle("Interaction hints", () => SecondScreenPreferences.ShowInteractionHints,
                value =>
                {
                    SecondScreenPreferences.ShowInteractionHints = value;
                    _owner.Gear?.Refresh(true);
                    _owner.Containers?.Refresh(true);
                }, "Show inventory tap, move and hold guidance.");
        }

        private void BuildMap()
        {
            AddFloat("Default zoom", () => SecondScreenPreferences.MapZoom,
                value => { SecondScreenPreferences.MapZoom = value; LargeMap.SetConfiguredZoom(); },
                0f, 1f, 0.1f, Percent,
                "Starting zoom used each time the map is initialized.");
            AddFloat("Drag sensitivity", () => SecondScreenPreferences.MapPanSensitivity,
                value => SecondScreenPreferences.MapPanSensitivity = value,
                0.4f, 2f, 0.1f, Times,
                "How far the map moves with a one-finger drag.");
            AddFloat("Pinch sensitivity", () => SecondScreenPreferences.MapPinchSensitivity,
                value => SecondScreenPreferences.MapPinchSensitivity = value,
                0.4f, 2f, 0.1f, Times,
                "How quickly a two-finger pinch changes zoom.");
            AddFloat("Player marker size", () => SecondScreenPreferences.PlayerMarkerScale,
                value => SecondScreenPreferences.PlayerMarkerScale = value,
                0.65f, 1.75f, 0.1f, Times,
                "Scale of your live position and facing marker.");
            AddFloat("Map pin size", () => SecondScreenPreferences.PinScale,
                value => SecondScreenPreferences.PinScale = value,
                0.65f, 1.75f, 0.1f, Times,
                "Scale of saved pins, pings and player markers.");
            AddToggle("Follow player", () => SecondScreenPreferences.MapFollowPlayer,
                value => { SecondScreenPreferences.MapFollowPlayer = value; if (value) LargeMap.Centre(); },
                "Keep the map centered on the player while travelling.");
            AddAction("Centre map now", "CENTRE", LargeMap.Centre,
                "Return the current map view to the player.");
            AddAction("Reset map view", "RESET", () =>
            {
                SecondScreenPreferences.MapZoom = 0f;
                SecondScreenPreferences.MapFollowPlayer = false;
                LargeMap.Centre();
                LargeMap.SetConfiguredZoom();
            }, "Restore zoom, centering and follow behavior.");
        }

        private void BuildTouch()
        {
            AddFloat("Hold-to-use delay", () => SecondScreenPreferences.HoldSeconds,
                value => SecondScreenPreferences.HoldSeconds = value,
                0.30f, 1.20f, 0.05f, Seconds,
                "Time before a hold equips, eats or uses an item.");
            AddToggle("Hold progress", () => SecondScreenPreferences.ShowHoldProgress,
                value => SecondScreenPreferences.ShowHoldProgress = value,
                "Tint an item slot while its hold action is charging.");
            AddToggle("Confirm discard", () => SecondScreenPreferences.ConfirmDiscard,
                value => SecondScreenPreferences.ConfirmDiscard = value,
                "Require a second discard tap before dropping an item.");
            AddToggle("Keep map tool selected", () => SecondScreenPreferences.KeepMapToolSelected,
                value => SecondScreenPreferences.KeepMapToolSelected = value,
                "Keep placing, pinging or removing until the tool is tapped again.");
            AddAction("Cancel selected item", "CANCEL", () => _owner.Gear?.CancelDrag(),
                "Clear an inventory move that is currently waiting for a destination.");
            AddAction("Return to inventory", "OPEN", () => _owner.SetTab(1),
                "Open the main inventory dashboard immediately.");
        }

        private bool BuildPerformanceUnavailable()
        {
            if (PerformanceSettingsBridge.Available) return false;
            AddAction("Valheim Performance", "NOT LOADED", () => { },
                "Install and enable ValheimPerformance to control it here.");
            return true;
        }

        private void BuildRuntime()
        {
            if (BuildPerformanceUnavailable()) return;
            _entryGroup = "SAFE RUNTIME";
            AddPerformanceToggle("Auto-pickup query reuse", "OptimizeAutoPickup",
                "Remove the collider-array allocation from the player's 50 Hz pickup scan.");
            AddPerformanceToggle("Direct equipment modifiers", "OptimizeEquipmentModifiers",
                "Remove fixed-tick reflection and float boxing from equipped-item stats.");
            AddPerformanceToggle("Character friction cache", "CacheBodyFriction",
                "Skip identical PhysicMaterial writes while preserving state changes.");
            AddPerformanceToggle("Network package buffers", "OptimizeZPackage",
                "Avoid full-payload temporary arrays during ZDO synchronization.");
            AddPerformanceToggle("Avoid material clones", "AvoidMaterialClones",
                "Use shared getters at audited read-only Valheim material call sites.");
            AddPerformanceToggle("Weapon-trail buffers", "OptimizeWeaponTrails",
                "Reuse four exact-size mesh arrays during melee trail updates.");
            AddPerformanceToggle("Projectile query buffers", "OptimizeProjectileQueries",
                "Reuse complete, distance-sorted hit buffers instead of allocating every physics tick.");
            AddPerformanceToggle("Animation state cache", "OptimizeAnimationSync",
                "Skip identical remote Animator writes while keeping damped movement updates continuous.");
            AddPerformanceToggle("Audio state cache", "OptimizeAudioUpdates",
                "Skip exact duplicate volume, pitch and stopped-state writes.");
            AddPerformanceToggle("Duplicate sky update", "RemoveDuplicateEnvShaderWrite",
                "Remove Valheim's second consecutive write of the same sky-sun shader vector.");
            AddPerformanceToggle("Mesh upload buffers", "OptimizeMeshUploads",
                "Send existing terrain and liquid index lists to Unity without duplicate arrays.");
            AddPerformanceToggle("Build-preview buffers", "OptimizePlacementGhost",
                "Reuse current collider results without reducing placement checks or caching world state.");
            AddPerformanceToggle("Cache character colours", "CacheCharacterColors",
                "Avoid rebuilding unchanged skin and hair materials every frame.");
            AddPerformanceToggle("Reuse collision callbacks", "ReuseCollisionCallbacks",
                "Reuse Unity's callback wrappers and exact-size contact buffers to reduce garbage.");
            AddPerformanceToggle("Threaded texture creation", "ThreadedTextureCreation",
                "Create supported textures away from the render thread.");
            _entryGroup = "EXPERIMENTAL";
            AddPerformanceToggle("Experimental: shader warmup", "WarmShaders",
                "Valheim exposed no loaded shader collection in testing; disabled by default.");
            AddPerformanceToggle("Experimental: force instancing", "EnableInstancing",
                "Can replace dynamic batching or compile shader variants; measure before use.");
            AddPerformanceToggle("Experimental: distant throttles", "ThrottleDistantUpdates",
                "Changes smoke, light and remote-equipment update timing.");
            AddPerformanceToggle("Experimental: smoke collisions", "SmokeCollisions",
                "Changes smoke-body interaction and may alter plume routing.");
            AddPerformanceToggle("Experimental: dry water cache", "CacheDryWaterChecks",
                "May delay wave-triggered splash detection for stationary objects.");
            AddAction("Reset performance mod", "RESET ALL",
                PerformanceSettingsBridge.ResetAll,
                "Restore every runtime, pipeline, distance and quality default.");
        }

        private void BuildPipeline()
        {
            if (BuildPerformanceUnavailable()) return;
            _entryGroup = "PIPELINE";
            AddPerformanceInt("Queued frames", "QueuedFrames", 0, 4, 1,
                value => value <= 0 ? "GAME" : value.ToString(),
                "More queue depth can smooth DXVK stalls but adds input latency.");
            AddPerformanceToggle("Wider upload budget", "WidenUploadBudget",
                "Use the configured persistent upload ring and time-slice controls.");
            AddPerformanceInt("Upload time slice", "UploadTimeSlice", 1, 16, 1,
                value => value + " ms",
                "Maximum frame time available to asynchronous texture uploads.");
            AddPerformanceInt("Upload buffer", "UploadBufferSize", 4, 128, 4,
                value => value + " MB",
                "Persistent memory reserved for streamed texture and mesh data.");
            AddPerformanceToggle("Incremental garbage collection", "SpreadGarbageCollection",
                "Spread supported managed collection work across multiple frames.");
            AddPerformanceInt("GC frame slice", "GcSliceMicroseconds", 250, 8000, 250,
                value => value + " us",
                "Smaller slices reduce individual hitches but finish collection slower.");
            AddPerformanceInt("Shader variants per frame", "ShaderVariantsPerFrame",
                0, 128, 4, value => value <= 0 ? "OFF" : value.ToString(),
                "Limits each loading-frame warmup slice; already warmed shaders remain.");
            AddPerformanceToggle("A/B diagnostics", "DiagnosticsEnabled",
                "Log bounded frame-time, hitch and GC statistics for controlled comparisons.");
            AddPerformanceInt("Diagnostics window", "DiagnosticsWindowSeconds",
                15, 600, 15, value => value + " s",
                "Duration of each diagnostics sample; 120 seconds is recommended.");
            AddAction("Reset pipeline budgets", "RESET", () =>
                PerformanceSettingsBridge.Reset(PipelineFields),
                "Restore the performance mod's recommended pipeline defaults.");
        }

        private void BuildDistance()
        {
            if (BuildPerformanceUnavailable()) return;
            _entryGroup = "EXPERIMENTAL DISTANCE";
            AddPerformanceFloat("Smoke range", "SmokeDistance", 10f, 150f, 5f,
                Metres, "Distance before smoke uses its reduced update rate.");
            AddPerformanceInt("Smoke update stride", "SmokeStride", 1, 10, 1,
                Stride, "1 updates every frame; larger values update less often.");
            AddPerformanceFloat("Flickering-light range", "LightDistance", 10f, 120f,
                5f, Metres, "Distance before flame movement and flicker are throttled.");
            AddPerformanceInt("Light update stride", "LightStride", 1, 10, 1,
                Stride, "1 updates every frame; larger values update less often.");
            AddPerformanceFloat("Character visual range", "VisualsDistance", 10f,
                100f, 2f, Metres,
                "Distance before remote equipment visuals update less frequently.");
            AddPerformanceInt("Character update stride", "VisualsStride", 1, 10, 1,
                Stride, "1 updates every frame; larger values update less often.");
            AddAction("Reset distance throttles", "RESET", () =>
                PerformanceSettingsBridge.Reset(DistanceFields),
                "Restore the disabled experimental distance defaults.");
        }

        private void BuildQuality()
        {
            if (BuildPerformanceUnavailable()) return;
            _entryGroup = "VISUAL QUALITY";
            AddPerformanceFloat("LOD bias", "LodBias", 0f, 2f, 0.1f,
                GameOrTimes, "Lower values swap to simpler models sooner; 0 uses the game.");
            AddPerformanceFloat("Shadow distance", "ShadowDistance", 0f, 200f, 10f,
                GameOrMetres, "Shorter distances save GPU time but remove distant shadows.");
            AddPerformanceInt("Shadow cascades", "ShadowCascades", 0, 4, 1,
                value => value <= 0 ? "GAME" : value.ToString(),
                "Fewer cascades reduce shadow cost and distant shadow precision.");
            AddPerformanceFloat("Ground clutter", "ClutterAmount", 0f, 2f, 0.1f,
                GameOrPercent, "Density multiplier for grass and ground cover.");
            AddPerformanceInt("Dynamic light limit", "LightLimit", -1, 64, 1,
                GameOrCount, "Caps simultaneous dynamic lights; -1 uses the game.");
            AddPerformanceInt("Shadowed light limit", "ShadowLimit", -1, 64, 1,
                GameOrCount, "Caps lights that cast shadows; -1 uses the game.");
            AddPerformanceFloat("Mist density", "MistDensity", 0f, 1f, 0.1f,
                GameOrPercent, "Reduces Mistlands fog density; 0 uses the game.");
            Add("Soft particles", () =>
                    PerformanceSettingsBridge.Get("SoftParticles", true) ? "GAME" : "OFF",
                _ => PerformanceSettingsBridge.Set("SoftParticles",
                    !PerformanceSettingsBridge.Get("SoftParticles", true)),
                "OFF saves a depth read but creates hard particle intersections.");
        }

        private void AddPerformanceToggle(string name, string field, string detail)
        {
            AddToggle(name,
                () => PerformanceSettingsBridge.Get(field, false),
                value => PerformanceSettingsBridge.Set(field, value), detail);
        }

        private void AddPerformanceInt(string name, string field, int min, int max,
                                       int step, Func<int, string> format, string detail)
        {
            Add(name, () => format(PerformanceSettingsBridge.Get(field, min)), direction =>
            {
                int current = PerformanceSettingsBridge.Get(field, min);
                PerformanceSettingsBridge.Set(field,
                    Mathf.Clamp(current + direction * step, min, max));
            }, detail);
        }

        private void AddPerformanceFloat(string name, string field, float min, float max,
                                         float step, Func<float, string> format,
                                         string detail)
        {
            Add(name, () => format(PerformanceSettingsBridge.Get(field, min)), direction =>
            {
                float current = PerformanceSettingsBridge.Get(field, min);
                float value = Mathf.Round((current + direction * step) / step) * step;
                PerformanceSettingsBridge.Set(field, Mathf.Clamp(value, min, max));
            }, detail);
        }

        private void AddToggle(string name, Func<bool> get, Action<bool> set,
                               string detail = "")
        {
            Add(name, () => get() ? "ON" : "OFF", _ => set(!get()), detail);
        }

        private void AddFloat(string name, Func<float> get, Action<float> set,
                              float min, float max, float step,
                              Func<float, string> format, string detail = "")
        {
            Add(name, () => format(get()), direction =>
                set(Mathf.Clamp(get() + direction * step, min, max)), detail);
        }

        private void AddChoice(string name, Func<int> get, int count,
                               Func<int, string> display, Action<int> set,
                               string detail = "")
        {
            Add(name, () => display(get()), direction =>
            {
                int value = ((get() + direction) % count + count) % count;
                set(value);
            }, detail);
        }

        private void AddAction(string name, string value, Action action,
                               string detail = "")
        {
            _entries.Add(new Entry
            {
                Name = name,
                Detail = detail,
                Value = () => value,
                Change = _ => action(),
                IsAction = true,
                Group = _entryGroup
            });
        }

        private void Add(string name, Func<string> value, Action<int> change,
                         string detail = "")
        {
            _entries.Add(new Entry
            {
                Name = name,
                Detail = detail,
                Value = value,
                Change = change,
                Group = _entryGroup
            });
        }

        private void Change(int index, int direction)
        {
            if (index < 0 || index >= _entries.Count) return;
            _entries[index].Change(direction);
            Paint();
        }

        private int PageCount => Mathf.Max(1,
            Mathf.CeilToInt(_entries.Count / (float)VisibleRows));

        private void ChangePage(int direction)
        {
            int count = PageCount;
            _entryPage = ((_entryPage + direction) % count + count) % count;
            Paint();
        }

        private void ChangeVisible(int row, int direction)
        {
            Change(_entryPage * VisibleRows + row, direction);
        }

        private void Paint()
        {
            for (int i = 0; i < _categoryFrames.Count; i++)
            {
                _categoryFrames[i].color = i == _category ? Selected : Idle;
                if (i < _categoryAccents.Count)
                    UiSkin.SetSelected(_categoryAccents[i], i == _category);
            }

            int pageCount = PageCount;
            _entryPage = Mathf.Clamp(_entryPage, 0, pageCount - 1);
            bool paged = pageCount > 1;
            if (_previousPage != null) _previousPage.gameObject.SetActive(paged);
            if (_nextPage != null) _nextPage.gameObject.SetActive(paged);
            if (_pageIndicator != null)
            {
                _pageIndicator.gameObject.SetActive(paged);
                _pageIndicator.text = (_entryPage + 1) + " / " + pageCount;
            }

            int start = _entryPage * VisibleRows;
            int visibleCount = Mathf.Clamp(_entries.Count - start, 0, VisibleRows);
            UpdatePageContext(start, visibleCount);
            LayoutRows(visibleCount);
            for (int i = 0; i < _rows.Count; i++)
            {
                bool visible = i < visibleCount;
                Row row = _rows[i];
                row.Root.SetActive(visible);
                if (!visible) continue;
                Entry entry = _entries[start + i];
                row.Name.text = entry.Name;
                row.Detail.text = entry.Detail ?? "";
                row.Value.text = entry.Value();

                // Action rows are a single clear call-to-action. Showing minus
                // and plus beside RESET/CENTRE/CANCEL made three identical
                // buttons look like a numeric setting and invited accidental
                // activation. Value rows retain the compact decrement/value/
                // increment arrangement.
                row.Less.gameObject.SetActive(!entry.IsAction);
                row.More.gameObject.SetActive(!entry.IsAction);
                LayoutControls(row, entry.IsAction);
            }
        }

        private void LayoutRows(int visibleCount)
        {
            if (visibleCount <= 0) return;
            float available = _size.y - BodyTop - BodyBottom;
            float rowHeight = (available - RowGap * (visibleCount - 1)) / visibleCount;
            float textWidth = _size.x - 410f;
            for (int i = 0; i < visibleCount; i++)
            {
                Row row = _rows[i];
                Place((RectTransform)row.Root.transform, 12f,
                      BodyTop + i * (rowHeight + RowGap), _size.x - 24f, rowHeight);
                // Sparse pages intentionally expand their cards to use the
                // screen. Keep the text block aligned with the centred controls
                // instead of leaving it stranded along the card's top edge.
                float textBlockHeight = Mathf.Min(72f, Mathf.Max(58f, rowHeight - 16f));
                float textTop = Mathf.Max(8f, (rowHeight - textBlockHeight) * 0.5f);
                Place(row.Name.rectTransform, 18f, textTop, textWidth, 32f);
                Place(row.Detail.rectTransform, 18f, textTop + 33f, textWidth,
                      Mathf.Max(24f, textBlockHeight - 33f));
            }
        }

        private void UpdatePageContext(int start, int visibleCount)
        {
            if (_message == null) return;
            if (_category != 3)
            {
                _message.text = CategoryDescriptions[_category];
                return;
            }
            if (!PerformanceSettingsBridge.Available)
            {
                _message.text = "GAME-WIDE PERFORMANCE CONTROLS  |  MOD NOT LOADED";
                return;
            }

            string groups = "";
            for (int i = 0; i < visibleCount; i++)
            {
                string group = _entries[start + i].Group;
                if (string.IsNullOrEmpty(group) || groups.Contains(group)) continue;
                groups = groups.Length == 0 ? group : groups + " + " + group;
            }
            if (groups.Length == 0) groups = "PERFORMANCE";

            bool caution = groups.Contains("EXPERIMENTAL")
                           || groups.Contains("VISUAL");
            _message.text = "GAME-WIDE v" + PerformanceSettingsBridge.Version
                + "  |  " + groups + "  |  "
                + (caution ? "MEASURE BEFORE KEEPING" : "VISUALS UNCHANGED");
        }

        private void LayoutControls(Row row, bool action)
        {
            const float controlSpan = 338f;
            const float controlGap = 8f;
            float controlLeft = _size.x - 374f;
            float rowHeight = ((RectTransform)row.Root.transform).rect.height;
            float controlHeight = Mathf.Min(72f, Mathf.Max(42f, rowHeight - 14f));
            float controlTop = (rowHeight - controlHeight) * 0.5f;
            float valueLeft = action
                ? controlLeft
                : controlLeft + controlHeight + controlGap;
            float controlWidth = action
                ? controlSpan
                : Mathf.Max(72f,
                    controlSpan - controlHeight * 2f - controlGap * 2f);
            float rightLeft = controlLeft + controlSpan - controlHeight;
            Place((RectTransform)row.Less.transform, controlLeft, controlTop,
                  controlHeight, controlHeight);
            Place((RectTransform)row.ValueButton.transform, valueLeft, controlTop,
                  controlWidth, controlHeight);
            Place((RectTransform)row.More.transform, rightLeft, controlTop,
                  controlHeight, controlHeight);
        }

        private Row CreateRow(float top, float height, Action less, Action more)
        {
            var root = new GameObject("Setting", typeof(RectTransform), typeof(Image));
            root.transform.SetParent(_page, false);
            var rect = (RectTransform)root.transform;
            Place(rect, 12f, top, _size.x - 24f, height);
            var image = root.GetComponent<Image>();
            image.color = new Color(0.025f, 0.018f, 0.012f, 0.64f);
            image.raycastTarget = false;

            float textWidth = _size.x - 410f;
            var name = Label(rect, "Name", "", 22f, Text,
                             TextAlignmentOptions.TopLeft);
            Place(name.rectTransform, 18f, 8f, textWidth, 34f);
            SecondScreenUI.FitText(name, 11f, 22f);

            var detail = Label(rect, "Detail", "", 14f, Weak,
                               TextAlignmentOptions.TopLeft);
            Place(detail.rectTransform, 18f, 41f, textWidth,
                  Mathf.Max(24f, height - 47f));
            detail.lineSpacing = -5f;
            SecondScreenUI.FitText(detail, 9.5f, 14f, true);

            const float controlSpan = 338f;
            const float controlGap = 8f;
            float controlLeft = _size.x - 374f;
            float buttonSize = height - 14f;
            float valueLeft = controlLeft + buttonSize + controlGap;
            float valueWidth = Mathf.Max(72f,
                controlSpan - buttonSize * 2f - controlGap * 2f);
            float rightLeft = controlLeft + controlSpan - buttonSize;

            Button left = ActionButton(rect, "Less", "-", controlLeft, 7f,
                                       buttonSize, buttonSize, less);
            Button value = ActionButton(rect, "Value", "", valueLeft, 7f,
                                        valueWidth, buttonSize, more);
            Button right = ActionButton(rect, "More", "+", rightLeft, 7f,
                                        buttonSize, buttonSize, more);
            var valueText = value.GetComponentInChildren<TMP_Text>(true);
            valueText.fontSize = 20f;
            SecondScreenUI.FitText(valueText, 10f, 20f);
            return new Row { Root = root, Name = name, Detail = detail,
                             Value = valueText,
                             Less = left, ValueButton = value, More = right };
        }

        private Button ActionButton(Transform parent, string name, string label,
                                    float left, float top, float width, float height,
                                    Action action)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Image), typeof(Button));
            go.transform.SetParent(parent, false);
            Place((RectTransform)go.transform, left, top, width, height);
            var image = go.GetComponent<Image>();
            if (name == "Less" || name == "More"
                || name == "PreviousPage" || name == "NextPage")
                UiSkin.Square(image, Idle);
            else UiSkin.Control(image, (RectTransform)go.transform);
            image.color = Idle;
            image.raycastTarget = true;
            var button = go.GetComponent<Button>();
            button.targetGraphic = image;
            button.transition = Selectable.Transition.ColorTint;
            button.colors = new ColorBlock
            {
                normalColor = Color.white,
                highlightedColor = new Color(1f, 0.88f, 0.64f, 1f),
                pressedColor = new Color(0.72f, 0.45f, 0.17f, 1f),
                selectedColor = Color.white,
                disabledColor = new Color(0.45f, 0.45f, 0.45f, 0.7f),
                colorMultiplier = 1f,
                fadeDuration = 0.08f
            };
            button.navigation = new Navigation { mode = Navigation.Mode.None };
            button.onClick.AddListener(() => action());
            var text = Label(go.transform, "Label", label, 19f, Gold,
                             TextAlignmentOptions.Center);
            text.rectTransform.offsetMin = new Vector2(5f, 2f);
            text.rectTransform.offsetMax = new Vector2(-5f, -2f);
            SecondScreenUI.FitText(text, 10f, 19f);
            _buttons.Add(button);
            return button;
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
            text.textWrappingMode = TextWrappingModes.NoWrap;
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

        private static string Percent(float value) => Mathf.RoundToInt(value * 100f) + "%";
        private static string Times(float value) => value.ToString("0.0") + "x";
        private static string Seconds(float value) => value.ToString("0.00") + "s";
        private static string Metres(float value) => Mathf.RoundToInt(value) + " m";
        private static string Stride(int value) => value <= 1 ? "EVERY FRAME" : "1 / " + value;
        private static string GameOrTimes(float value) => value <= 0f ? "GAME" : Times(value);
        private static string GameOrMetres(float value) => value <= 0f ? "GAME" : Metres(value);
        private static string GameOrPercent(float value) => value <= 0f ? "GAME" : Percent(value);
        private static string GameOrCount(int value) => value < 0 ? "GAME" : value.ToString();
    }
}
