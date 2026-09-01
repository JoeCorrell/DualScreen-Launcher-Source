using System.Collections.Generic;
using System.Reflection;
using System.Reflection.Emit;
using HarmonyLib;
using UnityEngine;

namespace ValheimPerformance
{
    /// <summary>
    /// Avoids Unity's implicit per-renderer material clones at read-only call sites.
    /// </summary>
    /// <remarks>
    /// Accessing <see cref="Renderer.material"/> or
    /// <see cref="Renderer.materials"/> instantiates renderer-local material copies.
    /// These Valheim call sites only inspect or select the assigned material; the
    /// fader performs its changes through a MaterialPropertyBlock. Shared getters
    /// therefore preserve the rendered result without manufacturing unused copies.
    /// </remarks>
    internal static class MaterialCloneFixes
    {
        private static readonly MethodInfo? MaterialsGetter =
            AccessTools.PropertyGetter(typeof(Renderer), nameof(Renderer.materials));
        private static readonly MethodInfo? MaterialGetter =
            AccessTools.PropertyGetter(typeof(Renderer), nameof(Renderer.material));
        private static readonly MethodInfo? SharedMaterialsWrapper =
            AccessTools.Method(typeof(MaterialCloneFixes), nameof(GetMaterials));
        private static readonly MethodInfo? SharedMaterialWrapper =
            AccessTools.Method(typeof(MaterialCloneFixes), nameof(GetMaterial));

        private static Material[] GetMaterials(Renderer renderer)
        {
            if (!Plugin.AvoidMaterialClones.Value) return renderer.materials;
            PerformanceDiagnostics.CountSharedMaterialRead();
            return renderer.sharedMaterials;
        }

        private static Material GetMaterial(Renderer renderer)
        {
            if (!Plugin.AvoidMaterialClones.Value) return renderer.material;
            PerformanceDiagnostics.CountSharedMaterialRead();
            return renderer.sharedMaterial;
        }

        [HarmonyPatch]
        internal static class MaterialVariationPatch
        {
            private static IEnumerable<MethodBase> TargetMethods()
            {
                MethodInfo? checkMaterial =
                    AccessTools.Method(typeof(MaterialVariation), "CheckMaterial");
                MethodInfo? updateMaterial =
                    AccessTools.Method(typeof(MaterialVariation), "UpdateMaterial");

                if (checkMaterial != null)
                    yield return checkMaterial;
                else
                    WarnMissingTarget("MaterialVariation.CheckMaterial");

                if (updateMaterial != null)
                    yield return updateMaterial;
                else
                    WarnMissingTarget("MaterialVariation.UpdateMaterial");
            }

            private static IEnumerable<CodeInstruction> Transpiler(
                IEnumerable<CodeInstruction> instructions, MethodBase __originalMethod)
            {
                int expected;

                if (__originalMethod.Name == "CheckMaterial")
                {
                    expected = 1;
                }
                else if (__originalMethod.Name == "UpdateMaterial")
                {
                    expected = 2;
                }
                else
                {
                    WarnUnchanged(__originalMethod, "unexpected target method");
                    return new List<CodeInstruction>(instructions);
                }

                return ReplaceGetter(instructions, __originalMethod, MaterialsGetter,
                    SharedMaterialsWrapper, expected);
            }
        }

        [HarmonyPatch]
        internal static class MaterialFaderPatch
        {
            private static IEnumerable<MethodBase> TargetMethods()
            {
                MethodInfo? awake = AccessTools.Method(typeof(MaterialFader), "Awake");
                if (awake != null)
                    yield return awake;
                else
                    WarnMissingTarget("MaterialFader.Awake");
            }

            private static IEnumerable<CodeInstruction> Transpiler(
                IEnumerable<CodeInstruction> instructions, MethodBase __originalMethod)
            {
                return ReplaceGetter(instructions, __originalMethod, MaterialGetter,
                    SharedMaterialWrapper, 1);
            }
        }

        private static IEnumerable<CodeInstruction> ReplaceGetter(
            IEnumerable<CodeInstruction> instructions,
            MethodBase originalMethod,
            MethodInfo? originalGetter,
            MethodInfo? replacement,
            int expected)
        {
            var code = new List<CodeInstruction>(instructions);
            if (originalGetter == null || replacement == null)
            {
                WarnUnchanged(originalMethod, "material getter metadata was unavailable");
                return code;
            }

            int matches = 0;
            foreach (CodeInstruction instruction in code)
            {
                if (instruction.Calls(originalGetter))
                    matches++;
            }

            // Validate first and mutate second. A game update must never leave
            // only half of a method using shared materials.
            if (matches != expected)
            {
                WarnUnchanged(originalMethod, "expected " + expected
                    + " material getter call(s), found " + matches);
                return code;
            }

            foreach (CodeInstruction instruction in code)
            {
                if (!instruction.Calls(originalGetter))
                    continue;

                instruction.opcode = OpCodes.Call;
                instruction.operand = replacement;
            }

            Plugin.Log.LogDebug("[Perf] Material clone guard patched "
                + originalMethod.DeclaringType?.Name + "."
                + originalMethod.Name + " (" + matches + " getter call(s)).");
            return code;
        }

        private static void WarnMissingTarget(string target)
        {
            Plugin.Log.LogWarning("[Perf] Material clone guard could not find "
                + target + "; it was left unchanged.");
        }

        private static void WarnUnchanged(MethodBase method, string reason)
        {
            Plugin.Log.LogWarning("[Perf] Material clone guard no longer matches "
                + method.DeclaringType?.Name + "." + method.Name + " ("
                + reason + "); that method was left unchanged.");
        }
    }
}
