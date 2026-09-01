using System.Runtime.CompilerServices;
using HarmonyLib;
using UnityEngine;

namespace ValheimPerformance
{
    /// <summary>
    /// Avoids repeating an unchanged, failed water-volume overlap every frame.
    /// </summary>
    /// <remarks>
    /// Once <see cref="WaterTrigger"/>'s cooldown expires, the original method
    /// calls <c>Floating.IsUnderWater</c> every frame until it succeeds. For a
    /// stationary trigger on dry land that is a native physics overlap forever.
    /// Moving triggers still probe immediately, triggers already overlapping a
    /// water volume still evaluate waves every frame, and stationary misses are
    /// periodically rechecked for streamed or newly-created volumes.
    /// </remarks>
    internal static class DryWaterTriggerCache
    {
        private const float VerificationInterval = 0.5f;

        private sealed class State
        {
            public bool Initialized;
            public Vector3 Position;
            public float NextVerification;
        }

        private static readonly ConditionalWeakTable<WaterTrigger, State> States =
            new ConditionalWeakTable<WaterTrigger, State>();

        [HarmonyPatch(typeof(WaterTrigger), nameof(WaterTrigger.CustomUpdate))]
        internal static class WaterTriggerPatch
        {
            private static bool Prefix(WaterTrigger __instance, float deltaTime, float time,
                ref float ___m_cooldownTimer, WaterVolume ___m_previousAndOut)
            {
                if (!Plugin.CacheDryWaterChecks.Value) return true;

                float nextTimer = ___m_cooldownTimer + deltaTime;
                if (nextTimer <= __instance.m_cooldownDelay
                    || ___m_previousAndOut != null)
                    return true;

                Vector3 position = __instance.transform.position;
                State state = States.GetOrCreateValue(__instance);
                if (!state.Initialized || state.Position != position
                    || time >= state.NextVerification)
                {
                    state.Initialized = true;
                    state.Position = position;
                    state.NextVerification = time + VerificationInterval;
                    PerformanceDiagnostics.CountDryWaterQuery();
                    return true;
                }

                // Preserve the original timer exactly while skipping only the
                // unchanged failed physics query at the bottom of the method.
                ___m_cooldownTimer = nextTimer;
                PerformanceDiagnostics.CountDryWaterSkip();
                return false;
            }
        }
    }
}
