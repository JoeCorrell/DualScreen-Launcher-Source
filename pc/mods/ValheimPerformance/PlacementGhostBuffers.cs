using System;
using System.Collections.Generic;
using System.Reflection;
using System.Reflection.Emit;
using HarmonyLib;
using UnityEngine;

namespace ValheimPerformance
{
    /// <summary>
    /// Removes the three recurring collider-array allocations from Valheim's
    /// build-placement preview without reducing how often placement is tested.
    /// </summary>
    internal static class PlacementGhostBuffers
    {
        private const int InitialOverlapCapacity = 32;
        private const int MaximumOverlapCapacity = 4096;
        private const int MaximumCachedArrays = 64;
        private const int MaximumCachedArrayLength = 512;
        private const int MaximumCachedElements = 8192;

        [ThreadStatic] private static Collider[]? _overlapScratch;
        [ThreadStatic] private static List<Collider>? _componentScratch;
        [ThreadStatic] private static Dictionary<long, Collider[]>? _exactBuffers;
        [ThreadStatic] private static Stack<Collider[]>? _activeResults;
        [ThreadStatic] private static int _cachedElements;

        private static bool Enabled => Plugin.OptimizePlacementGhost != null
            && Plugin.OptimizePlacementGhost.Value;

        private static Collider[] OverlapWithMask(Vector3 position, float radius,
            int layerMask)
        {
            if (!Enabled)
                return Physics.OverlapSphere(position, radius, layerMask);

            return QueryOverlap(position, radius, layerMask,
                QueryTriggerInteraction.UseGlobal, 1);
        }

        private static Collider[] OverlapAll(Vector3 position, float radius)
        {
            if (!Enabled)
                return Physics.OverlapSphere(position, radius);

            return QueryOverlap(position, radius, Physics.AllLayers,
                QueryTriggerInteraction.UseGlobal, 2);
        }

        private static Collider[] QueryOverlap(Vector3 position, float radius,
            int layerMask, QueryTriggerInteraction triggerInteraction, int kind)
        {
            Collider[] scratch = _overlapScratch
                ?? (_overlapScratch = new Collider[InitialOverlapCapacity]);
            int count;

            while (true)
            {
                count = Physics.OverlapSphereNonAlloc(position, radius, scratch,
                    layerMask, triggerInteraction);
                if (count < scratch.Length) break;

                if (scratch.Length >= MaximumOverlapCapacity)
                {
                    Array.Clear(scratch, 0, count);
                    Collider[] fallback = Physics.OverlapSphere(position, radius,
                        layerMask, triggerInteraction);
                    Track(fallback);
                    return fallback;
                }

                Array.Clear(scratch, 0, count);
                scratch = new Collider[Math.Min(scratch.Length * 2,
                    MaximumOverlapCapacity)];
                _overlapScratch = scratch;
            }

            Collider[] result = ExactBuffer(kind, count);
            if (count > 0)
            {
                Array.Copy(scratch, 0, result, 0, count);
                Array.Clear(scratch, 0, count);
            }
            Track(result);
            return result;
        }

        private static Collider[] PlacementColliders(GameObject ghost)
        {
            if (!Enabled)
                return ghost.GetComponentsInChildren<Collider>();

            List<Collider> scratch = _componentScratch
                ?? (_componentScratch = new List<Collider>(16));
            scratch.Clear();
            ghost.GetComponentsInChildren(includeInactive: false, scratch);

            Collider[] result = ExactBuffer(3, scratch.Count);
            if (scratch.Count > 0) scratch.CopyTo(result, 0);
            scratch.Clear();
            Track(result);
            return result;
        }

        private static Collider[] ExactBuffer(int kind, int count)
        {
            if (count == 0) return Array.Empty<Collider>();

            int depth = _activeResults?.Count ?? 0;
            long key = ((long)kind << 56) | ((long)depth << 32) | (uint)count;
            Dictionary<long, Collider[]> buffers = _exactBuffers
                ?? (_exactBuffers = new Dictionary<long, Collider[]>(12));
            if (buffers.TryGetValue(key, out Collider[] result)) return result;

            result = new Collider[count];
            if (count <= MaximumCachedArrayLength
                && buffers.Count < MaximumCachedArrays
                && _cachedElements + count <= MaximumCachedElements)
            {
                buffers.Add(key, result);
                _cachedElements += count;
            }
            return result;
        }

        private static void Track(Collider[] result)
        {
            Stack<Collider[]> active = _activeResults
                ?? (_activeResults = new Stack<Collider[]>(3));
            active.Push(result);
        }

        private static void Release(int entryDepth)
        {
            Stack<Collider[]>? active = _activeResults;
            if (active == null) return;
            while (active.Count > entryDepth)
            {
                Collider[] result = active.Pop();
                if (result.Length > 0) Array.Clear(result, 0, result.Length);
            }
        }

        private static MethodInfo? FindComponentArrayGetter()
        {
            foreach (MethodInfo method in AccessTools.GetDeclaredMethods(typeof(GameObject)))
            {
                if (method.Name != nameof(GameObject.GetComponentsInChildren)
                    || !method.IsGenericMethodDefinition
                    || method.GetParameters().Length != 0)
                    continue;
                return method.MakeGenericMethod(typeof(Collider));
            }
            return null;
        }

        [HarmonyPatch]
        internal static class Patch
        {
            private static readonly MethodInfo? MaskedOverlap = AccessTools.Method(
                typeof(Physics), nameof(Physics.OverlapSphere),
                new[] { typeof(Vector3), typeof(float), typeof(int) });

            private static readonly MethodInfo? UnmaskedOverlap = AccessTools.Method(
                typeof(Physics), nameof(Physics.OverlapSphere),
                new[] { typeof(Vector3), typeof(float) });

            private static readonly MethodInfo? ComponentArrayGetter =
                FindComponentArrayGetter();

            private static readonly MethodInfo? MaskedReplacement = AccessTools.Method(
                typeof(PlacementGhostBuffers), nameof(OverlapWithMask));

            private static readonly MethodInfo? UnmaskedReplacement = AccessTools.Method(
                typeof(PlacementGhostBuffers), nameof(OverlapAll));

            private static readonly MethodInfo? ComponentReplacement = AccessTools.Method(
                typeof(PlacementGhostBuffers), nameof(PlacementColliders));

            private static MethodBase TargetMethod()
            {
                MethodInfo? method = AccessTools.Method(typeof(Player),
                    "UpdatePlacementGhost", new[] { typeof(bool) });
                if (method == null)
                    throw new MissingMethodException(typeof(Player).FullName,
                        "UpdatePlacementGhost(Boolean)");
                return method;
            }

            private static bool Prepare()
            {
                bool valid = MaskedOverlap != null && UnmaskedOverlap != null
                    && ComponentArrayGetter != null && MaskedReplacement != null
                    && UnmaskedReplacement != null && ComponentReplacement != null;
                if (!valid)
                    Plugin.Log?.LogWarning("[Perf] Placement-ghost buffer APIs "
                        + "could not be resolved; the patch was not installed.");
                return valid;
            }

            private static void Prefix(out int __state)
            {
                __state = _activeResults?.Count ?? 0;
            }

            private static IEnumerable<CodeInstruction> Transpiler(
                IEnumerable<CodeInstruction> instructions)
            {
                var code = new List<CodeInstruction>(instructions);
                int masked = 0;
                int unmasked = 0;
                int components = 0;

                foreach (CodeInstruction instruction in code)
                {
                    if (MaskedOverlap != null && instruction.Calls(MaskedOverlap))
                        ++masked;
                    else if (UnmaskedOverlap != null && instruction.Calls(UnmaskedOverlap))
                        ++unmasked;
                    else if (ComponentArrayGetter != null
                        && instruction.Calls(ComponentArrayGetter))
                        ++components;
                }

                if (masked != 1 || unmasked != 1 || components != 1
                    || MaskedReplacement == null || UnmaskedReplacement == null
                    || ComponentReplacement == null)
                {
                    Plugin.Log?.LogWarning("[Perf] UpdatePlacementGhost contained "
                        + $"{masked}/{unmasked}/{components} expected allocation "
                        + "sites; its IL was left unchanged.");
                    return code;
                }

                foreach (CodeInstruction instruction in code)
                {
                    if (instruction.Calls(MaskedOverlap))
                    {
                        instruction.opcode = OpCodes.Call;
                        instruction.operand = MaskedReplacement;
                    }
                    else if (instruction.Calls(UnmaskedOverlap))
                    {
                        instruction.opcode = OpCodes.Call;
                        instruction.operand = UnmaskedReplacement;
                    }
                    else if (instruction.Calls(ComponentArrayGetter))
                    {
                        instruction.opcode = OpCodes.Call;
                        instruction.operand = ComponentReplacement;
                    }
                }
                return code;
            }

            private static Exception? Finalizer(Exception? __exception, int __state)
            {
                Release(__state);
                return __exception;
            }
        }
    }
}
