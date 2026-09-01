using HarmonyLib;

namespace ValheimSecondScreen.Hooks
{
    /// <summary>Routes the live in-game settings prefab to the lower display.</summary>
    [HarmonyPatch(typeof(Menu), nameof(Menu.OnSettings))]
    internal static class PauseMenuHooks
    {
        [HarmonyPostfix]
        private static void OnSettingsPostfix()
        {
            if (Settings.instance != null)
                SecondDisplayMover.Instance?.RoutePauseSettings(Settings.instance);
        }
    }

    /// <summary>
    /// Routes the prefab during its first active frame. The Menu postfix remains
    /// as a safe fallback, but waiting for OnSettings to return can expose the
    /// stock canvas on display zero for a frame.
    /// </summary>
    [HarmonyPatch(typeof(Settings), "Awake")]
    internal static class PauseSettingsAwakeHook
    {
        [HarmonyPostfix]
        private static void AwakePostfix(Settings __instance)
        {
            SecondDisplayMover.Instance?.RoutePauseSettings(__instance);
        }
    }
}
