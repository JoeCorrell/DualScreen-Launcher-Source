using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using HarmonyLib;
using UnityEngine;

namespace ValheimPerformance
{
    /// <summary>
    /// Reuses MeleeWeaponTrail's existing Catmull-Rom result lists instead of
    /// replacing both fields with newly allocated Enumerable.ToList results.
    /// </summary>
    internal static class TrailSmoothingBuffers
    {
        private static readonly MethodInfo? VectorToList = FindVectorToList();
        private static readonly MethodInfo? FillListMethod = AccessTools.Method(
            typeof(TrailSmoothingBuffers), nameof(FillList));

        [ThreadStatic]
        private static List<Vector3>? _tipList;

        [ThreadStatic]
        private static List<Vector3>? _baseList;

        [ThreadStatic]
        private static int _listOrdinal;

        [ThreadStatic]
        private static bool _active;

        private struct SmoothListContext
        {
            public List<Vector3>? PreviousTip;
            public List<Vector3>? PreviousBase;
            public int PreviousOrdinal;
            public bool PreviousActive;
        }

        private static MethodInfo? FindVectorToList()
        {
            foreach (MethodInfo method in typeof(Enumerable).GetMethods(
                BindingFlags.Public | BindingFlags.Static))
            {
                if (method.Name != nameof(Enumerable.ToList)
                    || !method.IsGenericMethodDefinition)
                    continue;
                ParameterInfo[] parameters = method.GetParameters();
                if (parameters.Length == 1)
                    return method.MakeGenericMethod(typeof(Vector3));
            }
            return null;
        }

        private static List<Vector3> FillList(IEnumerable<Vector3> source)
        {
            if (Plugin.OptimizeWeaponTrails == null
                || !Plugin.OptimizeWeaponTrails.Value || !_active)
                return source.ToList();

            int ordinal = _listOrdinal++;
            List<Vector3>? destination = ordinal == 0
                ? _tipList
                : ordinal == 1 ? _baseList : null;
            if (destination == null) return source.ToList();

            destination.Clear();
            destination.AddRange(source);
            return destination;
        }

        private static void Warn(string message)
        {
            try { Plugin.Log?.LogWarning("[Perf] " + message); }
            catch { }
        }

        [HarmonyPatch(typeof(MeleeWeaponTrail),
            nameof(MeleeWeaponTrail.CustomFixedUpdate))]
        internal static class Patch
        {
            private static void Prefix(List<Vector3> ___m_smoothTipList,
                List<Vector3> ___m_smoothBaseList,
                out SmoothListContext __state)
            {
                __state = new SmoothListContext
                {
                    PreviousTip = _tipList,
                    PreviousBase = _baseList,
                    PreviousOrdinal = _listOrdinal,
                    PreviousActive = _active
                };

                // A re-entrant call falls back to vanilla allocations so it
                // cannot clear lists still in use by the outer invocation.
                if (_active || ___m_smoothTipList == null
                    || ___m_smoothBaseList == null
                    || ReferenceEquals(___m_smoothTipList, ___m_smoothBaseList))
                {
                    _tipList = null;
                    _baseList = null;
                    _active = false;
                }
                else
                {
                    _tipList = ___m_smoothTipList;
                    _baseList = ___m_smoothBaseList;
                    _active = true;
                }
                _listOrdinal = 0;
            }

            private static IEnumerable<CodeInstruction> Transpiler(
                IEnumerable<CodeInstruction> instructions,
                MethodBase __originalMethod)
            {
                var code = new List<CodeInstruction>(instructions);
                var matches = new List<int>(2);
                for (int i = 0; i < code.Count; ++i)
                {
                    if (VectorToList != null && code[i].Calls(VectorToList))
                        matches.Add(i);
                }

                if (matches.Count != 2 || FillListMethod == null)
                {
                    Warn("MeleeWeaponTrail.CustomFixedUpdate contained "
                        + matches.Count + " Vector3 ToList calls (expected exactly "
                        + "two); smoothing-list reuse was left disabled");
                    return code;
                }

                foreach (int index in matches)
                {
                    code[index].opcode = OpCodes.Call;
                    code[index].operand = FillListMethod;
                }
                return code;
            }

            private static Exception? Finalizer(Exception? __exception,
                SmoothListContext __state)
            {
                _tipList = __state.PreviousTip;
                _baseList = __state.PreviousBase;
                _listOrdinal = __state.PreviousOrdinal;
                _active = __state.PreviousActive;
                return __exception;
            }
        }
    }
}
