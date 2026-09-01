using System.Runtime.CompilerServices;
using HarmonyLib;
using UnityEngine;

namespace ValheimPerformance
{
    /// <summary>
    /// Avoids rebuilding identical renderer/material arrays for every visible
    /// player on every frame.
    /// </summary>
    internal static class VisualCache
    {
        private const int VerificationFrames = 120;

        private sealed class State
        {
            public bool Initialized;
            public Vector3 Skin;
            public Vector3 Hair;
            public GameObject? Beard;
            public GameObject? HairObject;
            public SkinnedMeshRenderer? Body;
            public int NextVerification;
        }

        private static readonly ConditionalWeakTable<VisEquipment, State> States =
            new ConditionalWeakTable<VisEquipment, State>();

        [HarmonyPatch(typeof(VisEquipment), "UpdateColors")]
        internal static class UpdateColorsPatch
        {
            private static bool Prefix(VisEquipment __instance,
                Vector3 ___m_skinColor, Vector3 ___m_hairColor,
                ZNetView ___m_nview, GameObject ___m_beardItemInstance,
                GameObject ___m_hairItemInstance)
            {
                if (!Plugin.CacheCharacterColors.Value) return true;

                Vector3 skin = ___m_skinColor;
                Vector3 hair = ___m_hairColor;
                ZDO? zdo = ___m_nview != null ? ___m_nview.GetZDO() : null;
                if (zdo != null)
                {
                    skin = zdo.GetVec3(ZDOVars.s_skinColor, Vector3.one);
                    hair = zdo.GetVec3(ZDOVars.s_hairColor, Vector3.one);
                }

                State state = States.GetOrCreateValue(__instance);
                int frame = Time.frameCount;
                bool changed = !state.Initialized
                    || !state.Skin.Equals(skin)
                    || !state.Hair.Equals(hair)
                    || state.Beard != ___m_beardItemInstance
                    || state.HairObject != ___m_hairItemInstance
                    || state.Body != __instance.m_bodyModel;

                // A slow verification pass preserves compatibility with another
                // mod replacing a renderer's materials without changing the
                // VisEquipment fields. Instance-id staggering keeps those passes
                // from all landing on the same frame.
                if (!changed && frame < state.NextVerification)
                {
                    PerformanceDiagnostics.CountCharacterColorSkip();
                    return false;
                }

                state.Initialized = true;
                state.Skin = skin;
                state.Hair = hair;
                state.Beard = ___m_beardItemInstance;
                state.HairObject = ___m_hairItemInstance;
                state.Body = __instance.m_bodyModel;
                state.NextVerification = frame + VerificationFrames
                    + (__instance.GetInstanceID() & 31);
                PerformanceDiagnostics.CountCharacterColorUpdate();
                return true;
            }
        }
    }
}
