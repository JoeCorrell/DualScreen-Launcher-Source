using UnityEngine;

namespace ValheimSecondScreen.UI
{
    /// <summary>
    /// Persisted preferences owned exclusively by the lower-screen UI.  The
    /// prefix keeps these values separate from Valheim's normal settings.
    /// </summary>
    internal static class SecondScreenPreferences
    {
        private const string Prefix = "ValheimSecondScreen.";

        private static readonly int[] PageTabs = { 1, 3, 0, 2, 4, 6 };

        public static int DefaultPageCount => PageTabs.Length;

        public static float BackgroundBrightness
        {
            get => GetFloat("BackgroundBrightness", 1f, 0.55f, 1.35f);
            set => SetFloat("BackgroundBrightness", value, 0.55f, 1.35f);
        }

        public static float Shade
        {
            get => GetFloat("Shade", 0f, 0f, 0.55f);
            set => SetFloat("Shade", value, 0f, 0.55f);
        }

        /// <summary>
        /// Fraction of the physical lower-panel resolution rendered by Unity.
        /// The display compositor performs the final upscale, so layout and
        /// touch coordinates continue to use the panel's native dimensions.
        /// </summary>
        public static float RenderScale
        {
            get => GetFloat("RenderScale", 0.75f, 0.50f, 1f);
            set => SetFloat("RenderScale", value, 0.50f, 1f);
        }

        public static float HoldSeconds
        {
            get => GetFloat("HoldSeconds", 0.55f, 0.30f, 1.20f);
            set => SetFloat("HoldSeconds", value, 0.30f, 1.20f);
        }

        public static float MapZoom
        {
            get => GetFloat("MapZoom", 0f, 0f, 1f);
            set => SetFloat("MapZoom", value, 0f, 1f);
        }

        public static float MapPanSensitivity
        {
            get => GetFloat("MapPanSensitivity", 1f, 0.4f, 2f);
            set => SetFloat("MapPanSensitivity", value, 0.4f, 2f);
        }

        public static float MapPinchSensitivity
        {
            get => GetFloat("MapPinchSensitivity", 1f, 0.4f, 2f);
            set => SetFloat("MapPinchSensitivity", value, 0.4f, 2f);
        }

        public static float PlayerMarkerScale
        {
            get => GetFloat("PlayerMarkerScale", 1f, 0.65f, 1.75f);
            set => SetFloat("PlayerMarkerScale", value, 0.65f, 1.75f);
        }

        public static float PinScale
        {
            get => GetFloat("PinScale", 1f, 0.65f, 1.75f);
            set => SetFloat("PinScale", value, 0.65f, 1.75f);
        }

        public static bool MapFollowPlayer
        {
            get => GetBool("MapFollowPlayer", false);
            set => SetBool("MapFollowPlayer", value);
        }

        public static bool ShowItemBadges
        {
            get => GetBool("ShowItemBadges", true);
            set => SetBool("ShowItemBadges", value);
        }

        public static bool ShowFoodTimers
        {
            get => GetBool("ShowFoodTimers", true);
            set => SetBool("ShowFoodTimers", value);
        }

        public static bool ButtonFeedback
        {
            get => GetBool("ButtonFeedback", true);
            set => SetBool("ButtonFeedback", value);
        }

        public static bool ShowCarryMeter
        {
            get => GetBool("ShowCarryMeter", true);
            set => SetBool("ShowCarryMeter", value);
        }

        public static bool ShowStatusEffects
        {
            get => GetBool("ShowStatusEffects", true);
            set => SetBool("ShowStatusEffects", value);
        }

        public static bool Use24HourClock
        {
            get => GetBool("Use24HourClock", true);
            set => SetBool("Use24HourClock", value);
        }

        public static bool ShowWeatherDetails
        {
            get => GetBool("ShowWeatherDetails", true);
            set => SetBool("ShowWeatherDetails", value);
        }

        public static bool ShowInteractionHints
        {
            get => GetBool("ShowInteractionHints", true);
            set => SetBool("ShowInteractionHints", value);
        }

        public static bool ConfirmDiscard
        {
            get => GetBool("ConfirmDiscard", true);
            set => SetBool("ConfirmDiscard", value);
        }

        public static bool KeepMapToolSelected
        {
            get => GetBool("KeepMapToolSelected", false);
            set => SetBool("KeepMapToolSelected", value);
        }

        public static bool ShowHoldProgress
        {
            get => GetBool("ShowHoldProgress", true);
            set => SetBool("ShowHoldProgress", value);
        }

        public static bool RememberLastTab
        {
            get => GetBool("RememberLastTab", false);
            set => SetBool("RememberLastTab", value);
        }

        public static int DefaultPage
        {
            get => Mathf.Clamp(PlatformPrefs.GetInt(Key("DefaultPage"), 0), 0, PageTabs.Length - 1);
            set => SetInt("DefaultPage", Mathf.Clamp(value, 0, PageTabs.Length - 1));
        }

        public static int LastTab
        {
            get => Mathf.Clamp(PlatformPrefs.GetInt(Key("LastTab"), 1), 0, 6);
            set => SetInt("LastTab", Mathf.Clamp(value, 0, 6));
        }

        public static int InitialTab => RememberLastTab ? LastTab : PageTabs[DefaultPage];

        public static string DefaultPageName
        {
            get
            {
                string[] names =
                    { "INVENTORY", "CRAFTING", "MAP", "SKILLS", "TROPHIES", "CHAT" };
                return names[DefaultPage];
            }
        }

        public static void ResetAll()
        {
            string[] names =
            {
                "BackgroundBrightness", "Shade", "RenderScale", "HoldSeconds", "MapZoom",
                "MapPanSensitivity", "MapPinchSensitivity", "PlayerMarkerScale",
                "PinScale", "MapFollowPlayer", "ShowItemBadges", "ShowFoodTimers",
                "ButtonFeedback", "ShowCarryMeter", "ShowStatusEffects",
                "Use24HourClock", "ShowWeatherDetails", "ShowInteractionHints",
                "ConfirmDiscard", "KeepMapToolSelected", "ShowHoldProgress",
                "RememberLastTab", "DefaultPage", "LastTab"
            };
            foreach (string name in names) PlatformPrefs.DeleteKey(Key(name));
            Save();
        }

        private static float GetFloat(string name, float fallback, float min, float max)
        {
            return Mathf.Clamp(PlatformPrefs.GetFloat(Key(name), fallback), min, max);
        }

        private static bool GetBool(string name, bool fallback)
        {
            return PlatformPrefs.GetInt(Key(name), fallback ? 1 : 0) == 1;
        }

        private static void SetFloat(string name, float value, float min, float max)
        {
            PlatformPrefs.SetFloat(Key(name), Mathf.Clamp(value, min, max));
            Save();
        }

        private static void SetBool(string name, bool value)
        {
            SetInt(name, value ? 1 : 0);
        }

        private static void SetInt(string name, int value)
        {
            PlatformPrefs.SetInt(Key(name), value);
            Save();
        }

        private static string Key(string name) => Prefix + name;

        private static void Save()
        {
            try { PlatformPrefs.Save(); }
            catch { }
        }
    }
}
