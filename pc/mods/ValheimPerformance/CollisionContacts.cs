using System;
using System.Collections.Generic;
using System.Reflection;
using System.Reflection.Emit;
using HarmonyLib;
using UnityEngine;

namespace ValheimPerformance
{
    /// <summary>
    /// Reuses the contact arrays consumed by Valheim's collision callbacks.
    /// </summary>
    /// <remarks>
    /// Unity's reusable <see cref="Collision"/> wrapper deliberately clears its
    /// legacy contact array before every callback. Reading <c>Collision.contacts</c>
    /// then allocates a new array. Valheim does that from its character grounding
    /// callback on every physics step, so enabling wrapper reuse by itself merely
    /// moves the remaining allocation one property access deeper.
    ///
    /// The patched methods consume the array synchronously and never retain it.
    /// An exact-size buffer per observed contact count therefore has identical
    /// length, ordering and contents without steady-state garbage.
    /// </remarks>
    internal static class CollisionContacts
    {
        [ThreadStatic]
        private static Dictionary<int, ContactPoint[]>? _buffers;

        private static ContactPoint[] GetContacts(Collision collision)
        {
            // With wrapper reuse off Unity already created and populated its
            // legacy array in Collision's constructor. Avoid an unnecessary copy.
            if (!RuntimeTuning.ReusingCollisionCallbacks)
            {
                PerformanceDiagnostics.CountCollisionContactFallback();
                return collision.contacts;
            }

            int count = collision.contactCount;
            if (count <= 0)
            {
                PerformanceDiagnostics.CountCollisionContactHit();
                return Array.Empty<ContactPoint>();
            }

            Dictionary<int, ContactPoint[]> buffers = _buffers
                ?? (_buffers = new Dictionary<int, ContactPoint[]>(4));
            if (!buffers.TryGetValue(count, out ContactPoint[]? contacts))
            {
                contacts = new ContactPoint[count];
                buffers.Add(count, contacts);
                PerformanceDiagnostics.CountCollisionContactBufferAllocation();
            }

            int copied = collision.GetContacts(contacts);
            // Contact data should be stable for the duration of a callback. If
            // Unity ever violates that contract, retain the game's exact fallback.
            if (copied == count)
            {
                PerformanceDiagnostics.CountCollisionContactHit();
                return contacts;
            }
            PerformanceDiagnostics.CountCollisionContactFallback();
            return collision.contacts;
        }

        [HarmonyPatch]
        internal static class GameCollisionPatch
        {
            private static readonly MethodInfo? ContactsGetter =
                AccessTools.PropertyGetter(typeof(Collision), nameof(Collision.contacts));
            private static readonly MethodInfo? CachedGetter =
                AccessTools.Method(typeof(CollisionContacts), nameof(GetContacts));

            private static IEnumerable<MethodBase> TargetMethods()
            {
                MethodInfo? character = AccessTools.Method(typeof(Character), "OnCollisionStay");
                MethodInfo? terrain = AccessTools.Method(typeof(FloatingTerrain), "OnCollisionStay");
                MethodInfo? impact = AccessTools.Method(typeof(ImpactEffect), "OnCollisionEnter");
                if (character != null) yield return character;
                if (terrain != null) yield return terrain;
                if (impact != null) yield return impact;
            }

            private static IEnumerable<CodeInstruction> Transpiler(
                IEnumerable<CodeInstruction> instructions, MethodBase __originalMethod)
            {
                int replacements = 0;
                foreach (CodeInstruction instruction in instructions)
                {
                    if (ContactsGetter != null && CachedGetter != null
                        && instruction.Calls(ContactsGetter))
                    {
                        instruction.opcode = OpCodes.Call;
                        instruction.operand = CachedGetter;
                        replacements++;
                    }
                    yield return instruction;
                }

                if (replacements == 0)
                {
                    Plugin.Log.LogWarning("[Perf] Contact-array optimization no longer "
                        + "matches " + __originalMethod.DeclaringType?.Name + "."
                        + __originalMethod.Name + "; that method was left unchanged.");
                }
            }
        }
    }
}
