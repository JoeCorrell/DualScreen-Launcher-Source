using System.Collections.Generic;
using System.Text;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace ValheimSecondScreen.UI
{
    /// <summary>Live status, effects, weight, food and world-time cards.</summary>
    internal sealed class Dashboard
    {
        private static readonly Color GoldText = UiTheme.Gold;
        private static readonly Color BodyText = UiTheme.Text;
        private static readonly Color BarLabel = UiTheme.GoldBright;
        private static readonly Color BarValue = new Color(1f, 0.97f, 0.90f, 1f);
        private static readonly Color Weak = UiTheme.Muted;
        private static readonly Color Track = new Color(0.018f, 0.014f, 0.010f, 0.95f);

        private readonly HorizontalBar _health;
        private readonly HorizontalBar _stamina;
        private readonly HorizontalBar _eitr;
        private readonly HorizontalBar _adrenaline;
        private readonly List<FoodSlot> _foodSlots = new List<FoodSlot>();
        private readonly List<EffectRow> _effects = new List<EffectRow>();
        private readonly List<StatusEffect> _liveEffects = new List<StatusEffect>();
        private readonly TMP_Text _day;
        private readonly TMP_Text _clock;
        private readonly TMP_Text _weather;
        private readonly TMP_Text _biome;
        private readonly TMP_Text _wind;
        private readonly TMP_Text _moreEffects;
        private readonly GameObject _emptyEffects;
        private readonly TMP_Text _emptyEffectsTitle;
        private readonly TMP_Text _emptyEffectsHint;
        private readonly TMP_Text _dayPhase;
        private readonly Image _weatherIcon;
        private float _nextRefresh;

        private sealed class EffectRow
        {
            public GameObject Root;
            public Image Icon;
            public TMP_Text Name;
            public TMP_Text Time;
        }

        private sealed class FoodSlot
        {
            public Image Background;
            public Image Overlay;
            public Image Icon;
            public TMP_Text Time;
            public TMP_Text Empty;
        }

        public Dashboard(RectTransform status, RectTransform buffs,
                         RectTransform time,
                         TMP_FontAsset font)
        {
            Title(status, "STATUS");
            Title(buffs, "BUFFS");
            Title(time, "TIME & WEATHER");

            _health = new HorizontalBar(status, "Health", 76f,
                new Color(0.50f, 0.035f, 0.035f, 1f));
            _stamina = new HorizontalBar(status, "Stamina", 142f,
                new Color(0.57f, 0.29f, 0.012f, 1f));
            _eitr = new HorizontalBar(status, "Eitr", 208f,
                new Color(0.48f, 0.08f, 0.69f, 1f), true);
            _adrenaline = new HorizontalBar(status, "Adrenaline", 274f,
                new Color(0.82f, 0.32f, 0.08f, 1f), true);

            const float slotMargin = 14f;
            const float slotGap = 8f;
            float slotWidth = (status.rect.width - slotMargin * 2f - slotGap * 2f) / 3f;
            float slotHeight = Mathf.Clamp(slotWidth, 88f, 108f);
            var foodLabel = SecondScreenUI.MakeText(status, "FoodLabel", "FOOD", 19f,
                BodyText, TextAlignmentOptions.Left);
            AnchorBottom(foodLabel.transform as RectTransform, slotMargin,
                         slotMargin + slotHeight + 4f,
                         status.rect.width - slotMargin * 2f, 28f);
            for (int i = 0; i < 3; i++)
            {
                float x = slotMargin + i * (slotWidth + slotGap);
                _foodSlots.Add(CreateFoodSlot(status, x, slotMargin,
                                              slotWidth, slotHeight, i + 1));
            }

            for (int i = 0; i < 4; i++)
                _effects.Add(CreateEffectRow(buffs, 72f + i * 94f));

            _emptyEffects = CreateEmptyEffects(
                buffs, out _emptyEffectsTitle, out _emptyEffectsHint);

            _moreEffects = SecondScreenUI.MakeText(
                buffs, "MoreEffects", "", 16f, Weak,
                TextAlignmentOptions.Center);
            AnchorBottom(_moreEffects.transform as RectTransform, 14f, 8f,
                         buffs.rect.width - 28f, 26f);
            _moreEffects.fontStyle = FontStyles.Bold;
            _moreEffects.gameObject.SetActive(false);

            const float tileMargin = 14f;
            const float tileGap = 8f;
            const float tileTop = 70f;
            float tileHeight = Mathf.Max(142f, time.rect.height - tileTop - 14f);
            float availableWidth = time.rect.width - tileMargin * 2f - tileGap * 2f;
            float dayWidth = availableWidth * 0.26f;
            float clockWidth = availableWidth * 0.31f;
            float weatherWidth = availableWidth - dayWidth - clockWidth;
            var dayTile = CreateInfoTile(time, "DayTile", tileMargin, tileTop,
                                         dayWidth, tileHeight, "WORLD DAY");
            var clockTile = CreateInfoTile(time, "ClockTile",
                tileMargin + dayWidth + tileGap, tileTop,
                clockWidth, tileHeight, "WORLD TIME");
            var weatherTile = CreateInfoTile(time, "WeatherTile",
                tileMargin + dayWidth + clockWidth + tileGap * 2f, tileTop,
                weatherWidth, tileHeight, "WEATHER");

            _day = SecondScreenUI.MakeText(dayTile, "Day", "2", 44f,
                BarValue, TextAlignmentOptions.Center);
            PlaceTop(_day.rectTransform, 8f, 42f, dayWidth - 16f, 58f);
            _day.fontStyle = FontStyles.Bold;
            SecondScreenUI.FitText(_day, 24f, 44f);
            _dayPhase = SecondScreenUI.MakeText(dayTile, "Phase", "DAYLIGHT", 16f,
                Weak, TextAlignmentOptions.Center);
            PlaceTop(_dayPhase.rectTransform, 8f, 108f, dayWidth - 16f, 28f);
            _dayPhase.fontStyle = FontStyles.Bold;
            SecondScreenUI.FitText(_dayPhase, 10f, 16f);

            _clock = SecondScreenUI.MakeText(clockTile, "Clock", "00:00", 43f,
                BarValue, TextAlignmentOptions.Center);
            PlaceTop(_clock.rectTransform, 8f, 48f, clockWidth - 16f, 72f);
            _clock.fontStyle = FontStyles.Bold;
            SecondScreenUI.FitText(_clock, 23f, 43f);

            var sunObject = new GameObject("WeatherIcon", typeof(RectTransform), typeof(Image));
            var sunRect = (RectTransform)sunObject.transform;
            sunRect.SetParent(weatherTile, false);
            sunRect.anchorMin = new Vector2(0f, 0.5f);
            sunRect.anchorMax = new Vector2(0f, 0.5f);
            sunRect.pivot = new Vector2(0f, 0.5f);
            sunRect.anchoredPosition = new Vector2(16f, -7f);
            sunRect.sizeDelta = new Vector2(68f, 68f);
            _weatherIcon = sunObject.GetComponent<Image>();
            _weatherIcon.sprite = UiIcons.Get("sun");
            _weatherIcon.color = new Color(1f, 0.66f, 0.02f, 1f);
            _weatherIcon.raycastTarget = false;

            _weather = SecondScreenUI.MakeText(weatherTile, "Weather", "Clear", 29f,
                BodyText, TextAlignmentOptions.Left);
            PlaceTop(_weather.rectTransform, 96f, 44f,
                     Mathf.Max(84f, weatherWidth - 108f), 62f);
            _weather.fontStyle = FontStyles.Bold;
            SecondScreenUI.FitText(_weather, 14f, 29f, true);

            _biome = SecondScreenUI.MakeText(weatherTile, "Biome", "MEADOWS", 14f,
                Weak, TextAlignmentOptions.Left);
            PlaceTop(_biome.rectTransform, 96f, 112f,
                     Mathf.Max(84f, weatherWidth - 108f), 26f);
            _biome.fontStyle = FontStyles.Bold;
            SecondScreenUI.FitText(_biome, 10f, 14f);

            _wind = SecondScreenUI.MakeText(weatherTile, "Wind", "WIND  CALM", 13f,
                Weak, TextAlignmentOptions.Left);
            PlaceTop(_wind.rectTransform, 96f, 140f,
                     Mathf.Max(84f, weatherWidth - 108f), 24f);
            _wind.fontStyle = FontStyles.Bold;
            SecondScreenUI.FitText(_wind, 9f, 13f);
        }

        public void Refresh()
        {
            if (Time.unscaledTime < _nextRefresh) return;
            _nextRefresh = Time.unscaledTime + 0.1f;

            var player = Player.m_localPlayer;
            if (player == null) return;

            _health.Set(player.GetHealth(), player.GetMaxHealth());
            _stamina.Set(player.GetStamina(), player.GetMaxStamina());
            _eitr.Set(player.GetEitr(), player.GetMaxEitr());
            _adrenaline.Set(player.GetAdrenaline(), player.GetMaxAdrenaline());
            SetText(_biome, Friendly(player.GetCurrentBiome().ToString()).ToUpperInvariant());

            var foods = player.GetFoods();
            for (int i = 0; i < _foodSlots.Count; i++)
            {
                FoodSlot slot = _foodSlots[i];
                Sprite sprite = null;
                if (foods != null && i < foods.Count && foods[i].m_item != null)
                {
                    var icons = foods[i].m_item.m_shared != null
                        ? foods[i].m_item.m_shared.m_icons
                        : null;
                    if (icons != null && icons.Length > 0)
                        sprite = icons[Mathf.Clamp(foods[i].m_item.m_variant, 0, icons.Length - 1)];
                }
                bool hasFood = foods != null && i < foods.Count;
                SetSprite(slot.Icon, sprite);
                SetActive(slot.Empty.gameObject, !hasFood);
                SetActive(slot.Time.gameObject,
                    hasFood && SecondScreenPreferences.ShowFoodTimers);
                SetColour(slot.Background, hasFood
                    ? UiTheme.SlotEquipped : UiTheme.Slot);
                SetColour(slot.Overlay, hasFood
                    ? new Color(0.82f, 0.43f, 0.08f, 0.11f)
                    : new Color(0.18f, 0.13f, 0.08f, 0.08f));
                if (hasFood)
                {
                    float remaining = Mathf.Max(0f, foods[i].m_time);
                    SetText(slot.Time, remaining >= 60f
                        ? Mathf.CeilToInt(remaining / 60f) + "m"
                        : Mathf.FloorToInt(remaining) + "s");
                    bool canEatAgain = foods[i].m_item != null && foods[i].CanEatAgain();
                    SetTextColour(slot.Time, canEatAgain
                        ? new Color(1f, 0.72f, 0.48f,
                            0.65f + Mathf.Sin(Time.time * 10f) * 0.25f)
                        : BodyText);
                    if (canEatAgain)
                    {
                        float pulse = 0.12f + (Mathf.Sin(Time.time * 7f) + 1f) * 0.05f;
                        SetColour(slot.Overlay, new Color(1f, 0.39f, 0.04f, pulse));
                    }
                }
            }

            bool showEffects = SecondScreenPreferences.ShowStatusEffects;
            _liveEffects.Clear();
            if (showEffects)
                player.GetSEMan().GetHUDStatusEffects(_liveEffects);
            bool showEmptyEffects = !showEffects || _liveEffects.Count == 0;
            SetActive(_emptyEffects, showEmptyEffects);
            if (showEmptyEffects)
            {
                SetText(_emptyEffectsTitle, showEffects
                    ? "NO ACTIVE EFFECTS" : "STATUS EFFECTS HIDDEN");
                SetText(_emptyEffectsHint, showEffects
                    ? "BUFFS AND DEBUFFS WILL APPEAR HERE"
                    : "ENABLE THEM IN LOWER-SCREEN SETTINGS");
            }
            for (int i = 0; i < _effects.Count; i++)
            {
                var row = _effects[i];
                if (!showEffects || i >= _liveEffects.Count)
                {
                    SetActive(row.Root, false);
                    continue;
                }

                var effect = _liveEffects[i];
                if (effect == null)
                {
                    SetActive(row.Root, false);
                    continue;
                }
                SetActive(row.Root, true);
                SetSprite(row.Icon, effect.m_icon);
                SetText(row.Name, Localize(effect.m_name));
                string duration = effect.GetIconText();
                SetText(row.Time, string.IsNullOrEmpty(duration) ? "ACTIVE" : duration);
            }
            int hiddenEffects = showEffects
                ? Mathf.Max(0, _liveEffects.Count - _effects.Count) : 0;
            SetActive(_moreEffects.gameObject, hiddenEffects > 0);
            if (hiddenEffects > 0)
                SetText(_moreEffects, "+" + hiddenEffects + " MORE EFFECT"
                                     + (hiddenEffects == 1 ? "" : "S"));

            var environment = EnvMan.instance;
            if (environment != null)
            {
                float fraction = Mathf.Repeat(environment.GetDayFraction(), 1f);
                int minutes = Mathf.FloorToInt(fraction * 24f * 60f);
                int hour = minutes / 60;
                int minute = minutes % 60;
                SetText(_day, Mathf.Max(1, environment.GetDay()).ToString());
                SetText(_dayPhase, DayPhase(fraction));
                if (SecondScreenPreferences.Use24HourClock)
                    SetText(_clock, hour.ToString("00") + ":" + minute.ToString("00"));
                else
                {
                    int displayHour = hour % 12;
                    if (displayHour == 0) displayHour = 12;
                    SetText(_clock, displayHour + ":" + minute.ToString("00")
                                    + (hour < 12 ? " AM" : " PM"));
                }
                var setup = environment.GetCurrentEnvironment();
                bool showWeather = SecondScreenPreferences.ShowWeatherDetails;
                SetActive(_weatherIcon.gameObject, showWeather);
                SetActive(_wind.gameObject, showWeather);
                if (showWeather)
                {
                    string environmentName = setup != null ? setup.m_name : "Clear";
                    SetText(_weather, Friendly(Localize(environmentName)));
                    bool night = fraction < 0.20f || fraction > 0.80f;
                    string weatherIcon = WeatherIcon(setup, night);
                    SetSprite(_weatherIcon, UiIcons.Get(weatherIcon));
                    SetColour(_weatherIcon, WeatherColour(weatherIcon));
                    float wind = Mathf.Clamp01(environment.GetWindIntensity());
                    string direction = wind < 0.025f
                        ? "CALM" : WindDirection(environment.GetWindDir());
                    SetText(_wind, "WIND  " + direction + "  "
                                   + Mathf.RoundToInt(wind * 100f) + "%");
                }
                else SetText(_weather, "HIDDEN");
            }
        }

        private static void SetActive(GameObject target, bool active)
        {
            if (target != null && target.activeSelf != active) target.SetActive(active);
        }

        private static void SetText(TMP_Text target, string value)
        {
            if (target != null && target.text != value) target.text = value;
        }

        private static void SetTextColour(TMP_Text target, Color value)
        {
            if (target != null && target.color != value) target.color = value;
        }

        private static void SetSprite(Image target, Sprite value)
        {
            if (target == null) return;
            if (target.sprite != value) target.sprite = value;
            bool enabled = value != null;
            if (target.enabled != enabled) target.enabled = enabled;
        }

        private static void SetColour(Image target, Color value)
        {
            if (target != null && target.color != value) target.color = value;
        }

        private sealed class HorizontalBar
        {
            private readonly GameObject _root;
            private readonly RectTransform _fill;
            private readonly Image _fillImage;
            private readonly VitalGradient _gradient;
            private readonly TMP_Text _value;
            private readonly bool _hideWhenEmpty;
            private float _lastFraction = -1f;
            private int _lastCurrent = int.MinValue;
            private int _lastMaximum = int.MinValue;

            public HorizontalBar(RectTransform parent, string name, float top, Color colour,
                                 bool hideWhenEmpty = false)
            {
                _hideWhenEmpty = hideWhenEmpty;
                float parentWidth = Mathf.Max(260f, parent.rect.width);
                const float margin = 14f;
                const float height = 50f;
                float trackWidth = parentWidth - margin * 2f;
                var track = BarTrack(parent, name + "Track");
                _root = track.gameObject;
                track.anchorMin = new Vector2(0f, 1f);
                track.anchorMax = new Vector2(0f, 1f);
                track.pivot = new Vector2(0f, 1f);
                track.anchoredPosition = new Vector2(margin, -top);
                track.sizeDelta = new Vector2(trackWidth, height);
                _fill = Fill(track, "Fill", colour);
                _fillImage = _fill.GetComponent<Image>();
                _gradient = _fill.gameObject.AddComponent<VitalGradient>();
                ApplyGradient(colour);

                float labelSize = name.Length > 8 ? 18f : 21f;
                var label = SecondScreenUI.MakeText(track, name + "Label", name,
                    labelSize, BarLabel, TextAlignmentOptions.MidlineLeft);
                var labelRect = label.transform as RectTransform;
                labelRect.anchorMax = new Vector2(0.48f, 1f);
                labelRect.offsetMin = new Vector2(13f, 0f);
                labelRect.offsetMax = new Vector2(-5f, 0f);
                label.fontStyle = FontStyles.Bold;
                SecondScreenUI.TrySetOutline(
                    label, 0.10f, new Color32(8, 5, 3, 255));
                SecondScreenUI.FitText(label, 12f, labelSize);

                _value = SecondScreenUI.MakeText(track, name + "Value", "0 / 0", 19f,
                    BarValue, TextAlignmentOptions.MidlineRight);
                _value.rectTransform.anchorMin = new Vector2(0.48f, 0f);
                _value.rectTransform.offsetMin = new Vector2(5f, 0f);
                _value.rectTransform.offsetMax = new Vector2(-13f, 0f);
                _value.fontStyle = FontStyles.Bold;
                SecondScreenUI.TrySetOutline(
                    _value, 0.10f, new Color32(8, 5, 3, 255));
                SecondScreenUI.FitText(_value, 12f, 19f);
            }

            public void Set(float current, float maximum)
            {
                if (_hideWhenEmpty)
                {
                    bool visible = maximum > 0.01f;
                    if (_root.activeSelf != visible) _root.SetActive(visible);
                    if (!visible) return;
                }
                float fraction = maximum > 0f
                    ? Mathf.Clamp01(current / maximum) : 0f;
                int currentText = Mathf.CeilToInt(current);
                int maximumText = Mathf.CeilToInt(maximum);
                if (!Mathf.Approximately(_lastFraction, fraction))
                {
                    _lastFraction = fraction;
                    _fill.anchorMax = new Vector2(fraction, 1f);
                }
                if (_lastCurrent != currentText || _lastMaximum != maximumText)
                {
                    _lastCurrent = currentText;
                    _lastMaximum = maximumText;
                    _value.text = currentText + " / " + maximumText;
                }
            }

            private void ApplyGradient(Color colour)
            {
                if (_fillImage == null || _gradient == null) return;
                var bottom = new Color(colour.r * 0.44f, colour.g * 0.44f,
                                       colour.b * 0.44f, colour.a);
                var top = Color.Lerp(colour,
                    new Color(1f, 0.78f, 0.42f, 1f), 0.18f);
                top.a = colour.a;
                _fillImage.sprite = null;
                _fillImage.type = Image.Type.Simple;
                _fillImage.color = Color.white;
                _gradient.SetColors(bottom, top);
            }
        }

        private static EffectRow CreateEffectRow(RectTransform parent, float top)
        {
            var root = new GameObject("Effect", typeof(RectTransform), typeof(Image));
            var rect = (RectTransform)root.transform;
            rect.SetParent(parent, false);
            rect.anchorMin = new Vector2(0f, 1f);
            rect.anchorMax = new Vector2(1f, 1f);
            rect.pivot = new Vector2(0.5f, 1f);
            rect.sizeDelta = new Vector2(-28f, 86f);
            rect.anchoredPosition = new Vector2(0f, -top);
            var background = root.GetComponent<Image>();
            UiSkin.Plain(background, new Color(0.018f, 0.013f, 0.009f, 0.42f));
            background.raycastTarget = false;

            var accentObject = new GameObject("Accent", typeof(RectTransform), typeof(Image));
            var accentRect = (RectTransform)accentObject.transform;
            accentRect.SetParent(rect, false);
            accentRect.anchorMin = new Vector2(0f, 0.18f);
            accentRect.anchorMax = new Vector2(0f, 0.82f);
            accentRect.pivot = new Vector2(0f, 0.5f);
            accentRect.anchoredPosition = new Vector2(5f, 0f);
            accentRect.sizeDelta = new Vector2(4f, 0f);
            var accent = accentObject.GetComponent<Image>();
            accent.color = new Color(0.96f, 0.58f, 0.12f, 0.72f);
            accent.raycastTarget = false;

            var iconObject = new GameObject("Icon", typeof(RectTransform), typeof(Image));
            var iconRect = (RectTransform)iconObject.transform;
            iconRect.SetParent(rect, false);
            iconRect.anchorMin = new Vector2(0f, 0.5f);
            iconRect.anchorMax = new Vector2(0f, 0.5f);
            iconRect.pivot = new Vector2(0f, 0.5f);
            iconRect.anchoredPosition = new Vector2(17f, 0f);
            iconRect.sizeDelta = new Vector2(54f, 54f);
            var icon = iconObject.GetComponent<Image>();
            icon.preserveAspect = true;
            icon.raycastTarget = false;

            var label = SecondScreenUI.MakeText(rect, "Name", "", 21f, BodyText,
                TextAlignmentOptions.MidlineLeft);
            var labelRect = (RectTransform)label.transform;
            labelRect.offsetMin = new Vector2(82f, 0f);
            labelRect.offsetMax = new Vector2(-88f, 0f);
            label.fontStyle = FontStyles.Bold;
            SecondScreenUI.FitText(label, 12f, 21f);

            var timeBadge = new GameObject("TimeBadge", typeof(RectTransform), typeof(Image));
            var timeBadgeRect = (RectTransform)timeBadge.transform;
            timeBadgeRect.SetParent(rect, false);
            timeBadgeRect.anchorMin = new Vector2(1f, 0.5f);
            timeBadgeRect.anchorMax = new Vector2(1f, 0.5f);
            timeBadgeRect.pivot = new Vector2(1f, 0.5f);
            timeBadgeRect.anchoredPosition = new Vector2(-10f, 0f);
            timeBadgeRect.sizeDelta = new Vector2(72f, 32f);
            var badgeImage = timeBadge.GetComponent<Image>();
            UiSkin.Plain(badgeImage, new Color(0.28f, 0.15f, 0.035f, 0.62f));
            badgeImage.raycastTarget = false;
            var time = SecondScreenUI.MakeText(timeBadgeRect, "Time", "", 15f, GoldText,
                TextAlignmentOptions.Center);
            time.fontStyle = FontStyles.Bold;
            SecondScreenUI.FitText(time, 9f, 15f);

            return new EffectRow { Root = root, Icon = icon, Name = label, Time = time };
        }

        private static GameObject CreateEmptyEffects(RectTransform parent,
            out TMP_Text title, out TMP_Text hint)
        {
            var root = new GameObject("EmptyEffects", typeof(RectTransform), typeof(Image));
            var rect = (RectTransform)root.transform;
            rect.SetParent(parent, false);
            rect.anchorMin = new Vector2(0.5f, 0.5f);
            rect.anchorMax = new Vector2(0.5f, 0.5f);
            rect.pivot = new Vector2(0.5f, 0.5f);
            rect.anchoredPosition = new Vector2(0f, -10f);
            rect.sizeDelta = new Vector2(Mathf.Max(220f, parent.rect.width - 48f), 190f);
            var background = root.GetComponent<Image>();
            UiSkin.Plain(background, new Color(0.018f, 0.013f, 0.009f, 0.24f));
            background.raycastTarget = false;

            var iconObject = new GameObject("Icon", typeof(RectTransform), typeof(Image));
            var iconRect = (RectTransform)iconObject.transform;
            iconRect.SetParent(rect, false);
            iconRect.anchorMin = new Vector2(0.5f, 1f);
            iconRect.anchorMax = new Vector2(0.5f, 1f);
            iconRect.pivot = new Vector2(0.5f, 1f);
            iconRect.anchoredPosition = new Vector2(0f, -20f);
            iconRect.sizeDelta = new Vector2(68f, 68f);
            var icon = iconObject.GetComponent<Image>();
            icon.sprite = UiIcons.Get("buffs");
            icon.color = new Color(0.72f, 0.61f, 0.46f, 0.58f);
            icon.preserveAspect = true;
            icon.raycastTarget = false;

            title = SecondScreenUI.MakeText(rect, "Title", "NO ACTIVE EFFECTS", 20f,
                BodyText, TextAlignmentOptions.Center);
            PlaceTop(title.rectTransform, 12f, 94f, rect.rect.width - 24f, 30f);
            title.fontStyle = FontStyles.Bold;
            SecondScreenUI.FitText(title, 12f, 20f);

            hint = SecondScreenUI.MakeText(rect, "Hint",
                "BUFFS AND DEBUFFS WILL APPEAR HERE", 13f, Weak,
                TextAlignmentOptions.Center);
            PlaceTop(hint.rectTransform, 14f, 132f, rect.rect.width - 28f, 40f);
            SecondScreenUI.FitText(hint, 9f, 13f, true);
            return root;
        }

        private static RectTransform CreateInfoTile(RectTransform parent, string name,
            float left, float top, float width, float height, string heading)
        {
            var root = new GameObject(name, typeof(RectTransform), typeof(Image));
            var rect = (RectTransform)root.transform;
            rect.SetParent(parent, false);
            rect.anchorMin = new Vector2(0f, 1f);
            rect.anchorMax = new Vector2(0f, 1f);
            rect.pivot = new Vector2(0f, 1f);
            rect.anchoredPosition = new Vector2(left, -top);
            rect.sizeDelta = new Vector2(width, height);
            var background = root.GetComponent<Image>();
            UiSkin.Plain(background, new Color(0.018f, 0.013f, 0.009f, 0.34f));
            background.raycastTarget = false;

            var accentObject = new GameObject("Accent", typeof(RectTransform), typeof(Image));
            var accentRect = (RectTransform)accentObject.transform;
            accentRect.SetParent(rect, false);
            accentRect.anchorMin = new Vector2(0.10f, 1f);
            accentRect.anchorMax = new Vector2(0.90f, 1f);
            accentRect.pivot = new Vector2(0.5f, 1f);
            accentRect.anchoredPosition = new Vector2(0f, -4f);
            accentRect.sizeDelta = new Vector2(0f, 3f);
            var accent = accentObject.GetComponent<Image>();
            accent.color = new Color(0.96f, 0.58f, 0.12f, 0.42f);
            accent.raycastTarget = false;

            var label = SecondScreenUI.MakeText(rect, "Heading", heading, 14f, Weak,
                TextAlignmentOptions.Center);
            PlaceTop(label.rectTransform, 8f, 12f, width - 16f, 24f);
            label.fontStyle = FontStyles.Bold;
            SecondScreenUI.FitText(label, 9f, 14f);
            return rect;
        }

        private static FoodSlot CreateFoodSlot(RectTransform parent, float x, float bottom,
                                               float width, float height, int number)
        {
            var go = new GameObject("FoodSlot" + number, typeof(RectTransform), typeof(Image));
            var rect = (RectTransform)go.transform;
            rect.SetParent(parent, false);
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.zero;
            rect.pivot = Vector2.zero;
            rect.anchoredPosition = new Vector2(x, bottom);
            rect.sizeDelta = new Vector2(width, height);
            var background = go.GetComponent<Image>();
            UiSkin.Slot(background, UiTheme.Slot);
            background.raycastTarget = false;

            var overlayObject = new GameObject("Tint", typeof(RectTransform), typeof(Image));
            var overlayRect = (RectTransform)overlayObject.transform;
            overlayRect.SetParent(rect, false);
            overlayRect.anchorMin = Vector2.zero;
            overlayRect.anchorMax = Vector2.one;
            overlayRect.offsetMin = new Vector2(3f, 3f);
            overlayRect.offsetMax = new Vector2(-3f, -3f);
            var overlay = overlayObject.GetComponent<Image>();
            overlay.color = new Color(0.18f, 0.13f, 0.08f, 0.08f);
            overlay.raycastTarget = false;

            var iconObject = new GameObject("Icon", typeof(RectTransform), typeof(Image));
            var iconRect = (RectTransform)iconObject.transform;
            iconRect.SetParent(rect, false);
            iconRect.anchorMin = new Vector2(0.5f, 0.5f);
            iconRect.anchorMax = new Vector2(0.5f, 0.5f);
            iconRect.pivot = new Vector2(0.5f, 0.5f);
            iconRect.anchoredPosition = new Vector2(0f, 8f);
            float iconSize = Mathf.Min(width - 16f, height - 25f);
            iconRect.sizeDelta = new Vector2(iconSize, iconSize);
            var icon = iconObject.GetComponent<Image>();
            icon.preserveAspect = true;
            icon.raycastTarget = false;

            var empty = SecondScreenUI.MakeText(rect, "Empty", number.ToString(), 23f,
                new Color(0.45f, 0.38f, 0.29f, 0.72f), TextAlignmentOptions.Center);
            empty.fontStyle = FontStyles.Bold;
            empty.rectTransform.offsetMin = new Vector2(4f, 16f);
            empty.rectTransform.offsetMax = new Vector2(-4f, -4f);

            var time = SecondScreenUI.MakeText(rect, "Time", "", 18f,
                BodyText, TextAlignmentOptions.Center);
            time.rectTransform.anchorMin = Vector2.zero;
            time.rectTransform.anchorMax = new Vector2(1f, 0f);
            time.rectTransform.pivot = new Vector2(0.5f, 0f);
            time.rectTransform.anchoredPosition = new Vector2(0f, 4f);
            time.rectTransform.sizeDelta = new Vector2(-8f, 24f);
            time.gameObject.SetActive(false);
            return new FoodSlot
            {
                Background = background,
                Overlay = overlay,
                Icon = icon,
                Time = time,
                Empty = empty
            };
        }

        private static void Title(RectTransform parent, string text)
        {
            var title = SecondScreenUI.MakeText(parent, "Title", text, 31f, GoldText);
            title.fontStyle = FontStyles.Bold;
            var rect = (RectTransform)title.transform;
            rect.anchorMin = new Vector2(0f, 1f);
            rect.anchorMax = new Vector2(1f, 1f);
            rect.pivot = new Vector2(0.5f, 1f);
            rect.sizeDelta = new Vector2(0f, 56f);
            rect.anchoredPosition = Vector2.zero;
        }

        private static RectTransform BarTrack(RectTransform parent, string name)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Image));
            var rect = (RectTransform)go.transform;
            rect.SetParent(parent, false);
            var image = go.GetComponent<Image>();
            // A vital track is not a button. Use Valheim's own GuiBar artwork
            // when the live HUD exposes it, with a quiet dark tint behind the
            // existing gradient fill. A clean track remains the compatibility
            // fallback for prefabs without a bar sprite.
            Sprite native = Textures.Get("BarBackground");
            if (native != null)
            {
                image.sprite = native;
                image.type = native.border.sqrMagnitude > 0.01f
                    ? Image.Type.Sliced : Image.Type.Simple;
                image.fillCenter = true;
                image.color = new Color(0.30f, 0.25f, 0.19f, 0.86f);
            }
            else UiSkin.Plain(image, Track);
            image.raycastTarget = false;
            return rect;
        }

        private static RectTransform Fill(RectTransform parent, string name, Color colour)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Image));
            var rect = (RectTransform)go.transform;
            rect.SetParent(parent, false);
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.one;
            rect.pivot = new Vector2(0f, 0.5f);
            rect.offsetMin = new Vector2(3f, 3f);
            rect.offsetMax = new Vector2(-3f, -3f);
            var image = go.GetComponent<Image>();
            image.color = colour;
            image.raycastTarget = false;

            var shine = new GameObject("Shine", typeof(RectTransform), typeof(Image));
            var shineRect = (RectTransform)shine.transform;
            shineRect.SetParent(rect, false);
            shineRect.anchorMin = new Vector2(0f, 0.58f);
            shineRect.anchorMax = Vector2.one;
            shineRect.offsetMin = Vector2.zero;
            shineRect.offsetMax = Vector2.zero;
            shine.GetComponent<Image>().color = new Color(1f, 0.82f, 0.36f, 0.16f);
            shine.GetComponent<Image>().raycastTarget = false;
            return rect;
        }

        private static void AnchorTop(RectTransform rect, float x, float y,
                                      float width, float height)
        {
            rect.anchorMin = new Vector2(0f, 1f);
            rect.anchorMax = new Vector2(0f, 1f);
            rect.pivot = new Vector2(0f, 1f);
            rect.anchoredPosition = new Vector2(x, y);
            rect.sizeDelta = new Vector2(width, height);
        }

        private static void PlaceTop(RectTransform rect, float left, float top,
                                     float width, float height)
        {
            rect.anchorMin = new Vector2(0f, 1f);
            rect.anchorMax = new Vector2(0f, 1f);
            rect.pivot = new Vector2(0f, 1f);
            rect.anchoredPosition = new Vector2(left, -top);
            rect.sizeDelta = new Vector2(width, height);
        }

        private static void AnchorBottom(RectTransform rect, float x, float y,
                                         float width, float height)
        {
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.zero;
            rect.pivot = Vector2.zero;
            rect.anchoredPosition = new Vector2(x, y);
            rect.sizeDelta = new Vector2(width, height);
        }

        private static void AnchorBody(RectTransform rect, float min, float max)
        {
            rect.anchorMin = new Vector2(min, 0f);
            rect.anchorMax = new Vector2(max, 1f);
            rect.offsetMin = new Vector2(0f, 18f);
            rect.offsetMax = new Vector2(0f, -58f);
        }

        private static string Localize(string value)
        {
            return Localization.instance != null
                ? Localization.instance.Localize(value)
                : value;
        }

        private static string Friendly(string value)
        {
            if (string.IsNullOrEmpty(value)) return "Clear";
            var result = new StringBuilder(value.Length + 6);
            char previous = '\0';
            foreach (char c in value.Replace('_', ' '))
            {
                if (char.IsUpper(c) && previous != '\0' && previous != ' '
                    && !char.IsUpper(previous)) result.Append(' ');
                result.Append(c);
                previous = c;
            }
            return result.ToString();
        }

        private static string DayPhase(float fraction)
        {
            if (fraction < 0.20f || fraction >= 0.82f) return "NIGHT";
            if (fraction < 0.27f) return "DAWN";
            if (fraction < 0.48f) return "MORNING";
            if (fraction < 0.68f) return "AFTERNOON";
            return "DUSK";
        }

        private static string WindDirection(Vector3 direction)
        {
            if (direction.sqrMagnitude < 0.0001f) return "CALM";
            float heading = Mathf.Atan2(direction.x, direction.z) * Mathf.Rad2Deg;
            int sector = Mathf.RoundToInt(Mathf.Repeat(heading, 360f) / 45f) & 7;
            switch (sector)
            {
                case 0: return "N";
                case 1: return "NE";
                case 2: return "E";
                case 3: return "SE";
                case 4: return "S";
                case 5: return "SW";
                case 6: return "W";
                default: return "NW";
            }
        }

        private static string WeatherIcon(EnvSetup setup, bool night)
        {
            if (setup == null) return night ? "moon" : "sun";
            string name = (setup.m_name ?? "").ToLowerInvariant();
            if (name.Contains("snow") || name.Contains("blizzard"))
                return "weather_snow";
            if (name.Contains("mist") || name.Contains("fog"))
                return "weather_fog";
            if (setup.m_isWet || name.Contains("rain") || name.Contains("storm")
                || name.Contains("thunder"))
                return "weather_rain";
            return night ? "moon" : "sun";
        }

        private static Color WeatherColour(string icon)
        {
            switch (icon)
            {
                case "weather_rain": return new Color(0.64f, 0.72f, 0.82f, 1f);
                case "weather_snow": return new Color(0.86f, 0.91f, 0.96f, 1f);
                case "weather_fog": return new Color(0.72f, 0.72f, 0.68f, 1f);
                case "moon": return new Color(0.70f, 0.76f, 0.92f, 1f);
                default: return new Color(1f, 0.66f, 0.02f, 1f);
            }
        }
    }
}
