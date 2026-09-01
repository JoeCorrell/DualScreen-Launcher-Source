using System;
using System.Collections.Generic;
using System.Reflection;
using System.Reflection.Emit;
using HarmonyLib;
using UnityEngine;

namespace ValheimPerformance
{
    /// <summary>
    /// Reuses buffers for the all-hit casts made by every active projectile on
    /// every owned physics tick.
    /// </summary>
    /// <remarks>
    /// A non-allocating Unity query reports at most the supplied buffer length,
    /// and does not say whether a full buffer was truncated. Full buffers are
    /// therefore grown and queried again. Saturating the bounded maximum falls
    /// back to Valheim's original allocating query rather than losing a hit.
    ///
    /// Projectile.FixedUpdate sorts and consumes an exact-length array. Each
    /// completed non-allocating query is copied into an exact-size retained
    /// result for the current re-entrancy depth, preserving that contract. The
    /// depth scope prevents an OnHit callback which synchronously updates
    /// another projectile from overwriting the outer projectile's hit array.
    /// </remarks>
    internal static class ProjectileQueries
    {
        private const int InitialQueryCapacity = 16;
        private const int MaximumQueryCapacity = 1024;
        private const int MaximumCachedDepth = 8;
        private const int MaximumExactBuffersPerDepth = 16;
        private const int MaximumExactElementsPerDepth = 2048;

        [ThreadStatic]
        private static int _fixedUpdateDepth;

        [ThreadStatic]
        private static ScopeBuffers[]? _scopes;

        private sealed class ScopeBuffers
        {
            internal RaycastHit[] Query = new RaycastHit[InitialQueryCapacity];
            internal readonly Dictionary<int, RaycastHit[]> Exact =
                new Dictionary<int, RaycastHit[]>(4);
            internal int ExactElements;

            internal RaycastHit[] GetExact(int count)
            {
                if (count == 0) return Array.Empty<RaycastHit>();
                if (Exact.TryGetValue(count, out RaycastHit[] result))
                    return result;

                result = new RaycastHit[count];
                if (Exact.Count < MaximumExactBuffersPerDepth
                    && ExactElements + count <= MaximumExactElementsPerDepth)
                {
                    Exact.Add(count, result);
                    ExactElements += count;
                }
                return result;
            }
        }

        private static RaycastHit[] RaycastAll(Vector3 origin, Vector3 direction,
            float maxDistance, int layerMask)
        {
            if (!Plugin.OptimizeProjectileQueries.Value)
                return Physics.RaycastAll(origin, direction, maxDistance, layerMask);

            int scopeIndex = _fixedUpdateDepth - 1;
            ScopeBuffers? scope = GetScope(scopeIndex);
            if (scope == null)
                return Physics.RaycastAll(origin, direction, maxDistance, layerMask);

            while (true)
            {
                RaycastHit[] query = scope.Query;
                int count = Physics.RaycastNonAlloc(origin, direction, query,
                    maxDistance, layerMask, QueryTriggerInteraction.UseGlobal);
                if (count < query.Length)
                    return CopyExact(scope, query, count);

                if (!Grow(scope))
                    return Physics.RaycastAll(origin, direction, maxDistance, layerMask);
            }
        }

        private static RaycastHit[] SphereCastAll(Vector3 origin, float radius,
            Vector3 direction, float maxDistance, int layerMask)
        {
            if (!Plugin.OptimizeProjectileQueries.Value)
            {
                return Physics.SphereCastAll(origin, radius, direction,
                    maxDistance, layerMask);
            }

            int scopeIndex = _fixedUpdateDepth - 1;
            ScopeBuffers? scope = GetScope(scopeIndex);
            if (scope == null)
            {
                return Physics.SphereCastAll(origin, radius, direction,
                    maxDistance, layerMask);
            }

            while (true)
            {
                RaycastHit[] query = scope.Query;
                int count = Physics.SphereCastNonAlloc(origin, radius, direction,
                    query, maxDistance, layerMask,
                    QueryTriggerInteraction.UseGlobal);
                if (count < query.Length)
                    return CopyExact(scope, query, count);

                if (!Grow(scope))
                {
                    return Physics.SphereCastAll(origin, radius, direction,
                        maxDistance, layerMask);
                }
            }
        }

        private static ScopeBuffers? GetScope(int scopeIndex)
        {
            if ((uint)scopeIndex >= MaximumCachedDepth) return null;

            ScopeBuffers[] scopes = _scopes
                ?? (_scopes = new ScopeBuffers[MaximumCachedDepth]);
            return scopes[scopeIndex] ?? (scopes[scopeIndex] = new ScopeBuffers());
        }

        private static bool Grow(ScopeBuffers scope)
        {
            int current = scope.Query.Length;
            if (current >= MaximumQueryCapacity) return false;
            int next = Math.Min(current * 2, MaximumQueryCapacity);
            scope.Query = new RaycastHit[next];
            return true;
        }

        private static RaycastHit[] CopyExact(ScopeBuffers scope,
            RaycastHit[] query, int count)
        {
            RaycastHit[] result = scope.GetExact(count);
            if (count != 0) Array.Copy(query, 0, result, 0, count);
            return result;
        }

        [HarmonyPatch(typeof(Projectile), "FixedUpdate")]
        internal static class Patch
        {
            private static readonly Type[] RaycastParameters =
            {
                typeof(Vector3), typeof(Vector3), typeof(float), typeof(int)
            };

            private static readonly Type[] SphereCastParameters =
            {
                typeof(Vector3), typeof(float), typeof(Vector3), typeof(float),
                typeof(int)
            };

            private static void Prefix(out int __state)
            {
                __state = _fixedUpdateDepth;
                _fixedUpdateDepth = __state + 1;
            }

            private static Exception? Finalizer(Exception? __exception, int __state)
            {
                _fixedUpdateDepth = __state;
                return __exception;
            }

            private static IEnumerable<CodeInstruction> Transpiler(
                IEnumerable<CodeInstruction> instructions,
                MethodBase __originalMethod)
            {
                var code = new List<CodeInstruction>(instructions);
                MethodInfo? originalRaycast = AccessTools.Method(typeof(Physics),
                    nameof(Physics.RaycastAll), RaycastParameters);
                MethodInfo? originalSphereCast = AccessTools.Method(typeof(Physics),
                    nameof(Physics.SphereCastAll), SphereCastParameters);
                MethodInfo? replacementRaycast = AccessTools.Method(
                    typeof(ProjectileQueries), nameof(RaycastAll), RaycastParameters);
                MethodInfo? replacementSphereCast = AccessTools.Method(
                    typeof(ProjectileQueries), nameof(SphereCastAll),
                    SphereCastParameters);

                int raycastCalls = CountCalls(code, originalRaycast);
                int sphereCastCalls = CountCalls(code, originalSphereCast);
                if (originalRaycast == null || originalSphereCast == null
                    || replacementRaycast == null || replacementSphereCast == null
                    || raycastCalls != 1 || sphereCastCalls != 1)
                {
                    Plugin.Log.LogWarning("[Perf] Projectile.FixedUpdate physics-query "
                        + "layout changed (RaycastAll=" + raycastCalls
                        + ", SphereCastAll=" + sphereCastCalls
                        + "); projectile-query optimization was left disabled.");
                    return code;
                }

                ReplaceCall(code, originalRaycast, replacementRaycast);
                ReplaceCall(code, originalSphereCast, replacementSphereCast);
                return code;
            }

            private static int CountCalls(List<CodeInstruction> code,
                MethodInfo? method)
            {
                if (method == null) return 0;
                int count = 0;
                foreach (CodeInstruction instruction in code)
                    if (instruction.Calls(method)) count++;
                return count;
            }

            private static void ReplaceCall(List<CodeInstruction> code,
                MethodInfo original, MethodInfo replacement)
            {
                foreach (CodeInstruction instruction in code)
                {
                    if (!instruction.Calls(original)) continue;
                    instruction.opcode = OpCodes.Call;
                    instruction.operand = replacement;
                }
            }
        }
    }
}
