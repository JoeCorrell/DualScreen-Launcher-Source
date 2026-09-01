using System.Collections.Generic;
using System.Reflection;
using System.Reflection.Emit;
using HarmonyLib;
using UnityEngine;

namespace ValheimPerformance
{
    /// <summary>
    /// Reuses the four exact-size arrays built by every active melee trail on
    /// every physics tick.
    /// </summary>
    /// <remarks>
    /// Valheim resizes four retained lists, immediately copies each list with
    /// <c>ToArray</c>, then overwrites every element of all four arrays before
    /// assigning them to the Mesh. The list contents copied by ToArray are
    /// therefore dead work. Arrays are retained by exact element count so the
    /// Mesh still receives precisely the same lengths and values as vanilla.
    /// Mesh property assignment copies the data synchronously into native mesh
    /// storage, making a buffer safe to reuse for the next trail invocation.
    /// </remarks>
    internal static class WeaponTrailBuffers
    {
        private const int MaximumArraysPerType = 128;
        private const int MaximumElementsPerType = 131072;

        private static readonly Dictionary<int, Vector3[]> Vertices =
            new Dictionary<int, Vector3[]>();
        private static readonly Dictionary<int, Vector2[]> Uvs =
            new Dictionary<int, Vector2[]>();
        private static readonly Dictionary<int, Color[]> Colors =
            new Dictionary<int, Color[]>();
        private static readonly Dictionary<int, int[]> Triangles =
            new Dictionary<int, int[]>();
        private static int _vertexElements;
        private static int _uvElements;
        private static int _colorElements;
        private static int _triangleElements;

        private static Vector3[] GetVertices(List<Vector3> source)
        {
            PerformanceDiagnostics.CountWeaponTrailFrame();
            return Plugin.OptimizeWeaponTrails.Value
                ? GetOrCreate(Vertices, source.Count, ref _vertexElements)
                : source.ToArray();
        }

        private static Vector2[] GetUvs(List<Vector2> source)
        {
            return Plugin.OptimizeWeaponTrails.Value
                ? GetOrCreate(Uvs, source.Count, ref _uvElements)
                : source.ToArray();
        }

        private static Color[] GetColors(List<Color> source)
        {
            return Plugin.OptimizeWeaponTrails.Value
                ? GetOrCreate(Colors, source.Count, ref _colorElements)
                : source.ToArray();
        }

        private static int[] GetTriangles(List<int> source)
        {
            return Plugin.OptimizeWeaponTrails.Value
                ? GetOrCreate(Triangles, source.Count, ref _triangleElements)
                : source.ToArray();
        }

        private static T[] GetOrCreate<T>(Dictionary<int, T[]> cache, int count,
                                          ref int retainedElements)
        {
            if (count <= 0) return System.Array.Empty<T>();
            if (cache.TryGetValue(count, out T[] array)) return array;
            array = new T[count];
            if (cache.Count < MaximumArraysPerType
                && retainedElements + count <= MaximumElementsPerType)
            {
                cache.Add(count, array);
                retainedElements += count;
            }
            PerformanceDiagnostics.CountWeaponTrailBufferGrowth();
            return array;
        }

        [HarmonyPatch(typeof(MeleeWeaponTrail), nameof(MeleeWeaponTrail.CustomFixedUpdate))]
        internal static class Patch
        {
            private static IEnumerable<CodeInstruction> Transpiler(
                IEnumerable<CodeInstruction> instructions, MethodBase __originalMethod)
            {
                var code = new List<CodeInstruction>(instructions);
                MethodInfo vector3ToArray = AccessTools.Method(
                    typeof(List<Vector3>), nameof(List<Vector3>.ToArray));
                MethodInfo vector2ToArray = AccessTools.Method(
                    typeof(List<Vector2>), nameof(List<Vector2>.ToArray));
                MethodInfo colorToArray = AccessTools.Method(
                    typeof(List<Color>), nameof(List<Color>.ToArray));
                MethodInfo intToArray = AccessTools.Method(
                    typeof(List<int>), nameof(List<int>.ToArray));
                MethodInfo getVertices = AccessTools.Method(
                    typeof(WeaponTrailBuffers), nameof(GetVertices));
                MethodInfo getUvs = AccessTools.Method(
                    typeof(WeaponTrailBuffers), nameof(GetUvs));
                MethodInfo getColors = AccessTools.Method(
                    typeof(WeaponTrailBuffers), nameof(GetColors));
                MethodInfo getTriangles = AccessTools.Method(
                    typeof(WeaponTrailBuffers), nameof(GetTriangles));

                if (vector3ToArray == null || vector2ToArray == null
                    || colorToArray == null || intToArray == null
                    || getVertices == null || getUvs == null
                    || getColors == null || getTriangles == null
                    || Count(code, vector3ToArray) != 1
                    || Count(code, vector2ToArray) != 1
                    || Count(code, colorToArray) != 1
                    || Count(code, intToArray) != 1)
                {
                    Plugin.Log.LogWarning("[Perf] MeleeWeaponTrail array layout "
                        + "changed; weapon-trail optimization was left disabled.");
                    return code;
                }

                Replace(code, vector3ToArray, getVertices);
                Replace(code, vector2ToArray, getUvs);
                Replace(code, colorToArray, getColors);
                Replace(code, intToArray, getTriangles);
                return code;
            }

            private static int Count(List<CodeInstruction> code, MethodInfo method)
            {
                int count = 0;
                foreach (CodeInstruction instruction in code)
                    if (instruction.Calls(method)) count++;
                return count;
            }

            private static void Replace(List<CodeInstruction> code,
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
