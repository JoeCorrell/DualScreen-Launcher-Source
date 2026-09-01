using System;
using System.Collections.Generic;
using System.Reflection;
using System.Reflection.Emit;
using HarmonyLib;
using UnityEngine;

namespace ValheimPerformance
{
    /// <summary>
    /// Removes managed staging copies from Valheim's terrain and liquid mesh
    /// uploads without changing any vertices, indices, topology, or bounds.
    /// </summary>
    internal static class MeshAllocationFixes
    {
        // Two float buffers at this limit retain at most 2 MiB per active
        // ApplyModifiers recursion depth. Normal Valheim heightmaps are tiny by
        // comparison. Pathological modded maps retain vanilla ToArray behavior.
        private const int MaximumRetainedHeights = 262144;
        private const int MaximumCachedHeightDepth = 4;

        private static readonly MethodInfo? IntListToArray = AccessTools.Method(
            typeof(List<int>), nameof(List<int>.ToArray), Type.EmptyTypes);
        private static readonly MethodInfo? ArraySetIndices = AccessTools.Method(
            typeof(Mesh), nameof(Mesh.SetIndices),
            new[] { typeof(int[]), typeof(MeshTopology), typeof(int) });
        private static readonly MethodInfo? UploadIndexListMethod = AccessTools.Method(
            typeof(MeshAllocationFixes), nameof(UploadIndexList));

        private static readonly MethodInfo? FloatListToArray = AccessTools.Method(
            typeof(List<float>), nameof(List<float>.ToArray), Type.EmptyTypes);
        private static readonly MethodInfo? CopyHeightsMethod = AccessTools.Method(
            typeof(MeshAllocationFixes), nameof(CopyHeights));

        [ThreadStatic]
        private static List<HeightBuffers>? _heightBuffersByDepth;

        [ThreadStatic]
        private static HeightBuffers? _activeHeightBuffers;

        [ThreadStatic]
        private static int _heightCopyOrdinal;

        [ThreadStatic]
        private static int _heightApplyDepth;

        private sealed class HeightBuffers
        {
            public float[] BaseHeights = Array.Empty<float>();
            public float[] LevelOnly = Array.Empty<float>();
        }

        private struct HeightBufferContext
        {
            public HeightBuffers? PreviousBuffers;
            public int PreviousOrdinal;
            public int PreviousDepth;
        }

        private static bool Enabled => Plugin.OptimizeMeshUploads != null
            && Plugin.OptimizeMeshUploads.Value;

        /// <summary>
        /// Mesh.SetIndices(List&lt;int&gt;, ...) reads the same Count elements as
        /// the array overload and copies them synchronously into native mesh
        /// storage. The disabled path deliberately retains Valheim's ToArray.
        /// </summary>
        private static void UploadIndexList(Mesh mesh, List<int> indices,
            MeshTopology topology, int submesh)
        {
            if (Enabled)
            {
                // These are the defaults used by the three-argument int[]
                // overload that Valheim originally calls.
                mesh.SetIndices(indices, topology, submesh,
                    calculateBounds: true, baseVertex: 0);
            }
            else
            {
                mesh.SetIndices(indices.ToArray(), topology, submesh);
            }
        }

        private static float[] CopyHeights(List<float> source)
        {
            if (!Enabled || _activeHeightBuffers == null
                || source.Count > MaximumRetainedHeights)
                return source.ToArray();

            int ordinal = _heightCopyOrdinal++;
            if (ordinal > 1) return source.ToArray();

            float[] destination = ordinal == 0
                ? _activeHeightBuffers.BaseHeights
                : _activeHeightBuffers.LevelOnly;
            if (destination.Length != source.Count)
            {
                destination = source.Count == 0
                    ? Array.Empty<float>()
                    : new float[source.Count];
                if (ordinal == 0)
                    _activeHeightBuffers.BaseHeights = destination;
                else
                    _activeHeightBuffers.LevelOnly = destination;
            }

            source.CopyTo(destination);
            return destination;
        }

        private static IEnumerable<CodeInstruction> ReplaceSingleIndexUpload(
            IEnumerable<CodeInstruction> instructions, MethodBase original)
        {
            var code = new List<CodeInstruction>(instructions);
            var conversions = new List<int>(1);
            var uploads = new List<int>(1);

            for (int i = 0; i < code.Count; ++i)
            {
                if (IntListToArray != null && code[i].Calls(IntListToArray))
                    conversions.Add(i);
                if (ArraySetIndices != null && code[i].Calls(ArraySetIndices))
                    uploads.Add(i);
            }

            // Current release IL is exactly:
            // List<int>.ToArray, ldc.i4 topology, ldc.i4 submesh,
            // Mesh.SetIndices(int[], MeshTopology, int).
            bool valid = conversions.Count == 1 && uploads.Count == 1
                && uploads[0] - conversions[0] == 3
                && UploadIndexListMethod != null;
            if (!valid)
            {
                Warn("mesh index upload no longer has the validated allocation "
                    + "shape in " + Describe(original) + "; left unchanged");
                return code;
            }

            CodeInstruction conversion = code[conversions[0]];
            conversion.opcode = OpCodes.Nop;
            conversion.operand = null;

            CodeInstruction upload = code[uploads[0]];
            upload.opcode = OpCodes.Call;
            upload.operand = UploadIndexListMethod;
            return code;
        }

        private static void Warn(string message)
        {
            try { Plugin.Log?.LogWarning("[Perf] " + message); }
            catch { }
        }

        private static string Describe(MethodBase method)
        {
            return (method.DeclaringType?.Name ?? "<unknown>") + "." + method.Name;
        }

        [HarmonyPatch(typeof(Heightmap), "RebuildCollisionMesh")]
        internal static class HeightmapIndicesPatch
        {
            private static IEnumerable<CodeInstruction> Transpiler(
                IEnumerable<CodeInstruction> instructions,
                MethodBase __originalMethod)
            {
                return ReplaceSingleIndexUpload(instructions, __originalMethod);
            }
        }

        [HarmonyPatch(typeof(LiquidVolume), "PostBuildMesh")]
        internal static class LiquidPostBuildIndicesPatch
        {
            private static IEnumerable<CodeInstruction> Transpiler(
                IEnumerable<CodeInstruction> instructions,
                MethodBase __originalMethod)
            {
                return ReplaceSingleIndexUpload(instructions, __originalMethod);
            }
        }

        [HarmonyPatch(typeof(LiquidVolume), "RebuildMesh")]
        internal static class LiquidRebuildIndicesPatch
        {
            private static IEnumerable<CodeInstruction> Transpiler(
                IEnumerable<CodeInstruction> instructions,
                MethodBase __originalMethod)
            {
                return ReplaceSingleIndexUpload(instructions, __originalMethod);
            }
        }

        [HarmonyPatch(typeof(Heightmap), "ApplyModifiers")]
        internal static class HeightmapModifierBuffersPatch
        {
            private static void Prefix(out HeightBufferContext __state)
            {
                __state = new HeightBufferContext
                {
                    PreviousBuffers = _activeHeightBuffers,
                    PreviousOrdinal = _heightCopyOrdinal,
                    PreviousDepth = _heightApplyDepth
                };

                List<HeightBuffers> depthBuffers = _heightBuffersByDepth
                    ?? (_heightBuffersByDepth = new List<HeightBuffers>(2));
                if (_heightApplyDepth < MaximumCachedHeightDepth)
                {
                    if (depthBuffers.Count == _heightApplyDepth)
                        depthBuffers.Add(new HeightBuffers());
                    _activeHeightBuffers = depthBuffers[_heightApplyDepth];
                }
                else
                {
                    // Unexpected recursive mod calls fall back to ToArray
                    // rather than retaining another pair of large buffers.
                    _activeHeightBuffers = null;
                }
                _heightApplyDepth++;
                _heightCopyOrdinal = 0;
            }

            private static IEnumerable<CodeInstruction> Transpiler(
                IEnumerable<CodeInstruction> instructions,
                MethodBase __originalMethod)
            {
                var code = new List<CodeInstruction>(instructions);
                // The source has two copies in the player-modifier branch and
                // two copies in the TerrainComp fallback branch. Only one pair
                // can execute because both are guarded by array == null.
                var matches = new List<int>(4);
                for (int i = 0; i < code.Count; ++i)
                {
                    if (FloatListToArray != null && code[i].Calls(FloatListToArray))
                        matches.Add(i);
                }

                if (matches.Count != 4 || CopyHeightsMethod == null)
                {
                    Warn("Heightmap.ApplyModifiers contained " + matches.Count
                        + " height-list copy sites (expected exactly four across "
                        + "its two exclusive initialization branches); left unchanged");
                    return code;
                }

                foreach (int index in matches)
                {
                    code[index].opcode = OpCodes.Call;
                    code[index].operand = CopyHeightsMethod;
                }
                return code;
            }

            private static Exception? Finalizer(Exception? __exception,
                HeightBufferContext __state)
            {
                _activeHeightBuffers = __state.PreviousBuffers;
                _heightCopyOrdinal = __state.PreviousOrdinal;
                _heightApplyDepth = __state.PreviousDepth;
                return __exception;
            }
        }
    }
}
