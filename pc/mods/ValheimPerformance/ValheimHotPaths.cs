using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using HarmonyLib;
using UnityEngine;

namespace ValheimPerformance
{
    /// <summary>
    /// Removes four high-frequency sources of native calls and managed garbage
    /// without changing simulation cadence or the values Valheim computes.
    /// </summary>
    /// <remarks>
    /// Every patch in this file is independently gated.  The corresponding
    /// <see cref="BepInEx.Configuration.ConfigEntry{T}"/> fields are bound by
    /// <see cref="Plugin"/> so a setting changed by the companion UI takes
    /// effect on the next call; no restart or repatch is required.
    /// </remarks>
    internal static class ValheimHotPaths
    {
        private const int InitialPickupBufferSize = 32;
        private const int MaximumPickupBufferSize = 4096;
        private const int MaximumExactPickupBuffers = 64;
        private const int MaximumCachedPickupResultSize = 512;
        private const int MaximumCachedPickupElements = 8192;
        private const int ModifierValidationFrames = 600;
        private const int FrictionVerificationFrames = 120;

        [ThreadStatic]
        private static Collider[]? _pickupScratch;

        [ThreadStatic]
        private static Dictionary<long, Collider[]>? _pickupExactBuffers;

        [ThreadStatic]
        private static int _pickupExactElements;

        [ThreadStatic]
        private static Stack<Collider[]>? _activePickupResults;

        private static readonly FieldInfo? EquipmentModifierSourcesField =
            AccessTools.Field(typeof(Player), "s_equipmentModifierSourceFields");

        private static readonly string[] EquipmentModifierNames =
        {
            "m_movementModifier",
            "m_homeItemsStaminaModifier",
            "m_heatResistanceModifier",
            "m_jumpStaminaModifier",
            "m_attackStaminaModifier",
            "m_blockStaminaModifier",
            "m_dodgeStaminaModifier",
            "m_swimStaminaModifier",
            "m_sneakStaminaModifier",
            "m_runStaminaModifier",
            "m_maxAdrenaline"
        };

        private static bool _modifierLayoutChecked;
        private static bool _modifierLayoutValid;
        private static int _nextModifierValidationFrame;

        private static readonly ConditionalWeakTable<Character, FrictionState> FrictionStates =
            new ConditionalWeakTable<Character, FrictionState>();

        private static readonly object PackageAccessLock = new object();
        private static bool _packageAccessInitialized;
        private static bool _packageAccessAvailable;
        private static AccessTools.FieldRef<ZPackage, MemoryStream>? _packageStream;
        private static AccessTools.FieldRef<ZPackage, BinaryWriter>? _packageWriter;
        private static AccessTools.FieldRef<ZPackage, BinaryReader>? _packageReader;

        private sealed class FrictionState
        {
            public bool Initialized;
            public UnityEngine.Object? Material;
            public float StaticFriction;
            public float DynamicFriction;
            public PhysicsMaterialCombine Combine;
            public int NextVerificationFrame;
        }

        private struct PendingFrictionState
        {
            public FrictionState? Cache;
            public UnityEngine.Object? Material;
            public float StaticFriction;
            public float DynamicFriction;
            public PhysicsMaterialCombine Combine;
            public int NextVerificationFrame;
        }

        private static bool AutoPickupEnabled =>
            Plugin.OptimizeAutoPickup != null && Plugin.OptimizeAutoPickup.Value;

        private static bool EquipmentModifiersEnabled =>
            Plugin.OptimizeEquipmentModifiers != null
            && Plugin.OptimizeEquipmentModifiers.Value;

        private static bool CharacterFrictionEnabled =>
            Plugin.CacheBodyFriction != null && Plugin.CacheBodyFriction.Value;

        private static bool NetworkPackagesEnabled =>
            Plugin.OptimizeZPackage != null && Plugin.OptimizeZPackage.Value;

        /// <summary>
        /// Same signature and result shape as Physics.OverlapSphere.  The exact
        /// length matters because AutoPickup's foreach loop dereferences every
        /// entry and therefore cannot consume the spare capacity of a NonAlloc
        /// query directly.
        /// </summary>
        private static Collider[] GetAutoPickupOverlaps(
            Vector3 position, float radius, int layerMask)
        {
            if (!AutoPickupEnabled)
                return Physics.OverlapSphere(position, radius, layerMask);

            PerformanceDiagnostics.CountAutoPickupQuery();
            Collider[]? scratch = _pickupScratch;
            if (scratch == null)
            {
                scratch = new Collider[InitialPickupBufferSize];
                _pickupScratch = scratch;
                PerformanceDiagnostics.CountAutoPickupBufferGrowth();
            }
            int count;

            while (true)
            {
                count = Physics.OverlapSphereNonAlloc(position, radius, scratch,
                    layerMask, QueryTriggerInteraction.UseGlobal);

                // NonAlloc returns the buffer length both when it overflowed
                // and when the result happened to fit exactly.  Retry with a
                // larger buffer in either case so no pickup is ever omitted.
                if (count < scratch.Length) break;

                if (scratch.Length >= MaximumPickupBufferSize)
                {
                    // Pathological modded scenes retain the allocating vanilla
                    // query rather than silently dropping colliders.
                    Array.Clear(scratch, 0, count);
                    Collider[] fallback = Physics.OverlapSphere(
                        position, radius, layerMask);
                    TrackPickupResult(fallback);
                    return fallback;
                }

                int nextSize = Math.Min(scratch.Length * 2,
                    MaximumPickupBufferSize);
                Array.Clear(scratch, 0, count);
                scratch = new Collider[nextSize];
                _pickupScratch = scratch;
                PerformanceDiagnostics.CountAutoPickupBufferGrowth();
            }

            Collider[] result;
            if (count == 0)
            {
                result = Array.Empty<Collider>();
            }
            else
            {
                Stack<Collider[]> active = _activePickupResults
                    ?? (_activePickupResults = new Stack<Collider[]>(2));
                int nestingDepth = active.Count;
                long key = ((long)nestingDepth << 32) | (uint)count;
                Dictionary<long, Collider[]> buffers = _pickupExactBuffers
                    ?? (_pickupExactBuffers = new Dictionary<long, Collider[]>(8));
                if (!buffers.TryGetValue(key, out result!))
                {
                    result = new Collider[count];
                    // Auto-pickup normally sees only a handful of colliders.
                    // Bound retained exact-size variants so a pathological
                    // modded scene cannot turn every observed count into an
                    // indefinitely retained array.
                    if (count <= MaximumCachedPickupResultSize
                        && buffers.Count < MaximumExactPickupBuffers
                        && _pickupExactElements + count
                            <= MaximumCachedPickupElements)
                    {
                        buffers.Add(key, result);
                        _pickupExactElements += count;
                    }
                }
                Array.Copy(scratch, 0, result, 0, count);
            }

            // Do not keep native collider wrappers alive through the scratch
            // array.  The exact result remains valid until AutoPickup returns.
            if (count > 0) Array.Clear(scratch, 0, count);
            TrackPickupResult(result);
            return result;
        }

        private static void TrackPickupResult(Collider[] result)
        {
            Stack<Collider[]> active = _activePickupResults
                ?? (_activePickupResults = new Stack<Collider[]>(2));
            active.Push(result);
        }

        private static void ReleasePickupResults(int entryDepth)
        {
            Stack<Collider[]>? active = _activePickupResults;
            if (active == null) return;

            while (active.Count > entryDepth)
            {
                Collider[] result = active.Pop();
                if (result.Length > 0) Array.Clear(result, 0, result.Length);
            }
        }

        private static bool ValidateModifierLayout()
        {
            int frame = Time.frameCount;
            if (_modifierLayoutChecked && frame < _nextModifierValidationFrame)
                return _modifierLayoutValid;

            _modifierLayoutChecked = true;
            _modifierLayoutValid = false;
            _nextModifierValidationFrame = frame + ModifierValidationFrames;

            if (EquipmentModifierSourcesField == null
                || !(EquipmentModifierSourcesField.GetValue(null) is FieldInfo[] fields)
                || fields.Length != EquipmentModifierNames.Length)
                return false;

            Type sharedData = typeof(ItemDrop.ItemData.SharedData);
            for (int i = 0; i < fields.Length; ++i)
            {
                FieldInfo? field = fields[i];
                if (field == null
                    || field.DeclaringType != sharedData
                    || field.FieldType != typeof(float)
                    || field.Name != EquipmentModifierNames[i])
                    return false;
            }

            _modifierLayoutValid = true;
            return true;
        }

        private static bool EnsurePackageAccess()
        {
            if (_packageAccessInitialized) return _packageAccessAvailable;

            lock (PackageAccessLock)
            {
                if (_packageAccessInitialized) return _packageAccessAvailable;

                try
                {
                    FieldInfo? stream = AccessTools.Field(typeof(ZPackage), "m_stream");
                    FieldInfo? writer = AccessTools.Field(typeof(ZPackage), "m_writer");
                    FieldInfo? reader = AccessTools.Field(typeof(ZPackage), "m_reader");
                    if (stream == null || stream.FieldType != typeof(MemoryStream)
                        || writer == null || writer.FieldType != typeof(BinaryWriter)
                        || reader == null || reader.FieldType != typeof(BinaryReader))
                    {
                        _packageAccessAvailable = false;
                    }
                    else
                    {
                        _packageStream =
                            AccessTools.FieldRefAccess<ZPackage, MemoryStream>(stream);
                        _packageWriter =
                            AccessTools.FieldRefAccess<ZPackage, BinaryWriter>(writer);
                        _packageReader =
                            AccessTools.FieldRefAccess<ZPackage, BinaryReader>(reader);
                        _packageAccessAvailable = true;
                    }
                }
                catch (Exception error)
                {
                    _packageAccessAvailable = false;
                    Warn("Could not prepare allocation-free ZPackage access: "
                        + error.Message);
                }
                finally
                {
                    _packageAccessInitialized = true;
                }
            }

            return _packageAccessAvailable;
        }

        private static MethodBase RequireMethod(Type type, string name, Type[] parameters)
        {
            MethodInfo? method = AccessTools.Method(type, name, parameters);
            if (method == null)
            {
                throw new MissingMethodException(type.FullName, name + "("
                    + string.Join(", ", Array.ConvertAll(parameters,
                        parameter => parameter.Name)) + ")");
            }
            return method;
        }

        private static void Warn(string message)
        {
            try
            {
                Plugin.Log?.LogWarning("[Perf] " + message);
            }
            catch
            {
                // Logging must never turn a compatibility fallback into a crash.
            }
        }

        [HarmonyPatch]
        internal static class AutoPickupPatch
        {
            private static readonly MethodInfo? AllocatingOverlap = AccessTools.Method(
                typeof(Physics), nameof(Physics.OverlapSphere),
                new[] { typeof(Vector3), typeof(float), typeof(int) });

            private static readonly MethodInfo? ReusableOverlap = AccessTools.Method(
                typeof(ValheimHotPaths), nameof(GetAutoPickupOverlaps));

            private static MethodBase TargetMethod()
            {
                return RequireMethod(typeof(Player), "AutoPickup",
                    new[] { typeof(float) });
            }

            private static bool Prepare()
            {
                bool valid = AllocatingOverlap != null && ReusableOverlap != null;
                if (!valid)
                {
                    Warn("AutoPickup overlap optimization is incompatible with this "
                        + "game/Unity build and was not installed.");
                }
                return valid;
            }

            private static void Prefix(out int __state)
            {
                __state = _activePickupResults?.Count ?? 0;
            }

            private static IEnumerable<CodeInstruction> Transpiler(
                IEnumerable<CodeInstruction> instructions)
            {
                var code = new List<CodeInstruction>(instructions);
                var matches = new List<int>(1);
                for (int i = 0; i < code.Count; ++i)
                {
                    if (AllocatingOverlap != null && code[i].Calls(AllocatingOverlap))
                        matches.Add(i);
                }

                if (matches.Count != 1 || ReusableOverlap == null)
                {
                    Warn("AutoPickup IL contained " + matches.Count
                        + " matching overlap calls (expected exactly one); its IL "
                        + "was left unchanged.");
                    return code;
                }

                CodeInstruction call = code[matches[0]];
                call.opcode = OpCodes.Call;
                call.operand = ReusableOverlap;
                return code;
            }

            // Finalizers run on both normal and exceptional exits.  The entry
            // depth makes nested/reflection-driven calls safe, including a
            // nested call that returns before reaching the overlap query.
            private static Exception? Finalizer(Exception? __exception, int __state)
            {
                ReleasePickupResults(__state);
                return __exception;
            }
        }

        [HarmonyPatch]
        internal static class EquipmentModifiersPatch
        {
            private static MethodBase TargetMethod()
            {
                return RequireMethod(typeof(Player), "UpdateModifiers", Type.EmptyTypes);
            }

            [HarmonyPriority(Priority.VeryLow)]
            private static bool Prefix(float[] ___m_equipmentModifierValues,
                ItemDrop.ItemData ___m_rightItem,
                ItemDrop.ItemData ___m_leftItem,
                ItemDrop.ItemData ___m_chestItem,
                ItemDrop.ItemData ___m_legItem,
                ItemDrop.ItemData ___m_helmetItem,
                ItemDrop.ItemData ___m_shoulderItem,
                ItemDrop.ItemData ___m_utilityItem,
                ItemDrop.ItemData ___m_trinketItem)
            {
                if (!EquipmentModifiersEnabled) return true;
                if (___m_equipmentModifierValues == null
                    || ___m_equipmentModifierValues.Length
                    != EquipmentModifierNames.Length
                    || !ValidateModifierLayout())
                    return true;

                // Keep the original right/left/chest/leg/helmet/shoulder/
                // utility/trinket addition order for every value.  Apart from
                // removing FieldInfo.GetValue and its boxed float, the numeric
                // operations are identical to Player.UpdateModifiers.
                float value = 0f;
                if (___m_rightItem != null) value += ___m_rightItem.m_shared.m_movementModifier;
                if (___m_leftItem != null) value += ___m_leftItem.m_shared.m_movementModifier;
                if (___m_chestItem != null) value += ___m_chestItem.m_shared.m_movementModifier;
                if (___m_legItem != null) value += ___m_legItem.m_shared.m_movementModifier;
                if (___m_helmetItem != null) value += ___m_helmetItem.m_shared.m_movementModifier;
                if (___m_shoulderItem != null) value += ___m_shoulderItem.m_shared.m_movementModifier;
                if (___m_utilityItem != null) value += ___m_utilityItem.m_shared.m_movementModifier;
                if (___m_trinketItem != null) value += ___m_trinketItem.m_shared.m_movementModifier;
                ___m_equipmentModifierValues[0] = value;

                value = 0f;
                if (___m_rightItem != null) value += ___m_rightItem.m_shared.m_homeItemsStaminaModifier;
                if (___m_leftItem != null) value += ___m_leftItem.m_shared.m_homeItemsStaminaModifier;
                if (___m_chestItem != null) value += ___m_chestItem.m_shared.m_homeItemsStaminaModifier;
                if (___m_legItem != null) value += ___m_legItem.m_shared.m_homeItemsStaminaModifier;
                if (___m_helmetItem != null) value += ___m_helmetItem.m_shared.m_homeItemsStaminaModifier;
                if (___m_shoulderItem != null) value += ___m_shoulderItem.m_shared.m_homeItemsStaminaModifier;
                if (___m_utilityItem != null) value += ___m_utilityItem.m_shared.m_homeItemsStaminaModifier;
                if (___m_trinketItem != null) value += ___m_trinketItem.m_shared.m_homeItemsStaminaModifier;
                ___m_equipmentModifierValues[1] = value;

                value = 0f;
                if (___m_rightItem != null) value += ___m_rightItem.m_shared.m_heatResistanceModifier;
                if (___m_leftItem != null) value += ___m_leftItem.m_shared.m_heatResistanceModifier;
                if (___m_chestItem != null) value += ___m_chestItem.m_shared.m_heatResistanceModifier;
                if (___m_legItem != null) value += ___m_legItem.m_shared.m_heatResistanceModifier;
                if (___m_helmetItem != null) value += ___m_helmetItem.m_shared.m_heatResistanceModifier;
                if (___m_shoulderItem != null) value += ___m_shoulderItem.m_shared.m_heatResistanceModifier;
                if (___m_utilityItem != null) value += ___m_utilityItem.m_shared.m_heatResistanceModifier;
                if (___m_trinketItem != null) value += ___m_trinketItem.m_shared.m_heatResistanceModifier;
                ___m_equipmentModifierValues[2] = value;

                value = 0f;
                if (___m_rightItem != null) value += ___m_rightItem.m_shared.m_jumpStaminaModifier;
                if (___m_leftItem != null) value += ___m_leftItem.m_shared.m_jumpStaminaModifier;
                if (___m_chestItem != null) value += ___m_chestItem.m_shared.m_jumpStaminaModifier;
                if (___m_legItem != null) value += ___m_legItem.m_shared.m_jumpStaminaModifier;
                if (___m_helmetItem != null) value += ___m_helmetItem.m_shared.m_jumpStaminaModifier;
                if (___m_shoulderItem != null) value += ___m_shoulderItem.m_shared.m_jumpStaminaModifier;
                if (___m_utilityItem != null) value += ___m_utilityItem.m_shared.m_jumpStaminaModifier;
                if (___m_trinketItem != null) value += ___m_trinketItem.m_shared.m_jumpStaminaModifier;
                ___m_equipmentModifierValues[3] = value;

                value = 0f;
                if (___m_rightItem != null) value += ___m_rightItem.m_shared.m_attackStaminaModifier;
                if (___m_leftItem != null) value += ___m_leftItem.m_shared.m_attackStaminaModifier;
                if (___m_chestItem != null) value += ___m_chestItem.m_shared.m_attackStaminaModifier;
                if (___m_legItem != null) value += ___m_legItem.m_shared.m_attackStaminaModifier;
                if (___m_helmetItem != null) value += ___m_helmetItem.m_shared.m_attackStaminaModifier;
                if (___m_shoulderItem != null) value += ___m_shoulderItem.m_shared.m_attackStaminaModifier;
                if (___m_utilityItem != null) value += ___m_utilityItem.m_shared.m_attackStaminaModifier;
                if (___m_trinketItem != null) value += ___m_trinketItem.m_shared.m_attackStaminaModifier;
                ___m_equipmentModifierValues[4] = value;

                value = 0f;
                if (___m_rightItem != null) value += ___m_rightItem.m_shared.m_blockStaminaModifier;
                if (___m_leftItem != null) value += ___m_leftItem.m_shared.m_blockStaminaModifier;
                if (___m_chestItem != null) value += ___m_chestItem.m_shared.m_blockStaminaModifier;
                if (___m_legItem != null) value += ___m_legItem.m_shared.m_blockStaminaModifier;
                if (___m_helmetItem != null) value += ___m_helmetItem.m_shared.m_blockStaminaModifier;
                if (___m_shoulderItem != null) value += ___m_shoulderItem.m_shared.m_blockStaminaModifier;
                if (___m_utilityItem != null) value += ___m_utilityItem.m_shared.m_blockStaminaModifier;
                if (___m_trinketItem != null) value += ___m_trinketItem.m_shared.m_blockStaminaModifier;
                ___m_equipmentModifierValues[5] = value;

                value = 0f;
                if (___m_rightItem != null) value += ___m_rightItem.m_shared.m_dodgeStaminaModifier;
                if (___m_leftItem != null) value += ___m_leftItem.m_shared.m_dodgeStaminaModifier;
                if (___m_chestItem != null) value += ___m_chestItem.m_shared.m_dodgeStaminaModifier;
                if (___m_legItem != null) value += ___m_legItem.m_shared.m_dodgeStaminaModifier;
                if (___m_helmetItem != null) value += ___m_helmetItem.m_shared.m_dodgeStaminaModifier;
                if (___m_shoulderItem != null) value += ___m_shoulderItem.m_shared.m_dodgeStaminaModifier;
                if (___m_utilityItem != null) value += ___m_utilityItem.m_shared.m_dodgeStaminaModifier;
                if (___m_trinketItem != null) value += ___m_trinketItem.m_shared.m_dodgeStaminaModifier;
                ___m_equipmentModifierValues[6] = value;

                value = 0f;
                if (___m_rightItem != null) value += ___m_rightItem.m_shared.m_swimStaminaModifier;
                if (___m_leftItem != null) value += ___m_leftItem.m_shared.m_swimStaminaModifier;
                if (___m_chestItem != null) value += ___m_chestItem.m_shared.m_swimStaminaModifier;
                if (___m_legItem != null) value += ___m_legItem.m_shared.m_swimStaminaModifier;
                if (___m_helmetItem != null) value += ___m_helmetItem.m_shared.m_swimStaminaModifier;
                if (___m_shoulderItem != null) value += ___m_shoulderItem.m_shared.m_swimStaminaModifier;
                if (___m_utilityItem != null) value += ___m_utilityItem.m_shared.m_swimStaminaModifier;
                if (___m_trinketItem != null) value += ___m_trinketItem.m_shared.m_swimStaminaModifier;
                ___m_equipmentModifierValues[7] = value;

                value = 0f;
                if (___m_rightItem != null) value += ___m_rightItem.m_shared.m_sneakStaminaModifier;
                if (___m_leftItem != null) value += ___m_leftItem.m_shared.m_sneakStaminaModifier;
                if (___m_chestItem != null) value += ___m_chestItem.m_shared.m_sneakStaminaModifier;
                if (___m_legItem != null) value += ___m_legItem.m_shared.m_sneakStaminaModifier;
                if (___m_helmetItem != null) value += ___m_helmetItem.m_shared.m_sneakStaminaModifier;
                if (___m_shoulderItem != null) value += ___m_shoulderItem.m_shared.m_sneakStaminaModifier;
                if (___m_utilityItem != null) value += ___m_utilityItem.m_shared.m_sneakStaminaModifier;
                if (___m_trinketItem != null) value += ___m_trinketItem.m_shared.m_sneakStaminaModifier;
                ___m_equipmentModifierValues[8] = value;

                value = 0f;
                if (___m_rightItem != null) value += ___m_rightItem.m_shared.m_runStaminaModifier;
                if (___m_leftItem != null) value += ___m_leftItem.m_shared.m_runStaminaModifier;
                if (___m_chestItem != null) value += ___m_chestItem.m_shared.m_runStaminaModifier;
                if (___m_legItem != null) value += ___m_legItem.m_shared.m_runStaminaModifier;
                if (___m_helmetItem != null) value += ___m_helmetItem.m_shared.m_runStaminaModifier;
                if (___m_shoulderItem != null) value += ___m_shoulderItem.m_shared.m_runStaminaModifier;
                if (___m_utilityItem != null) value += ___m_utilityItem.m_shared.m_runStaminaModifier;
                if (___m_trinketItem != null) value += ___m_trinketItem.m_shared.m_runStaminaModifier;
                ___m_equipmentModifierValues[9] = value;

                value = 0f;
                if (___m_rightItem != null) value += ___m_rightItem.m_shared.m_maxAdrenaline;
                if (___m_leftItem != null) value += ___m_leftItem.m_shared.m_maxAdrenaline;
                if (___m_chestItem != null) value += ___m_chestItem.m_shared.m_maxAdrenaline;
                if (___m_legItem != null) value += ___m_legItem.m_shared.m_maxAdrenaline;
                if (___m_helmetItem != null) value += ___m_helmetItem.m_shared.m_maxAdrenaline;
                if (___m_shoulderItem != null) value += ___m_shoulderItem.m_shared.m_maxAdrenaline;
                if (___m_utilityItem != null) value += ___m_utilityItem.m_shared.m_maxAdrenaline;
                if (___m_trinketItem != null) value += ___m_trinketItem.m_shared.m_maxAdrenaline;
                ___m_equipmentModifierValues[10] = value;

                PerformanceDiagnostics.CountModifierFastPath();
                return false;
            }
        }

        [HarmonyPatch]
        internal static class BodyFrictionPatch
        {
            private static MethodBase TargetMethod()
            {
                return RequireMethod(typeof(Character), "UpdateBodyFriction",
                    Type.EmptyTypes);
            }

            [HarmonyPriority(Priority.Low)]
            private static bool Prefix(Character __instance,
                CapsuleCollider ___m_collider, float ___m_slippage,
                out PendingFrictionState __state)
            {
                __state = default;
                if (!CharacterFrictionEnabled)
                {
                    FrictionStates.Remove(__instance);
                    return true;
                }
                if (___m_collider == null) return true;

                float staticFriction;
                float dynamicFriction;
                PhysicsMaterialCombine combine = PhysicsMaterialCombine.Multiply;

                if (__instance.IsDead())
                {
                    staticFriction = 1f;
                    dynamicFriction = 1f;
                    combine = PhysicsMaterialCombine.Maximum;
                }
                else if (__instance.IsSwimming())
                {
                    staticFriction = 0.2f;
                    dynamicFriction = 0.2f;
                }
                else if (!__instance.IsOnGround())
                {
                    staticFriction = 0f;
                    dynamicFriction = 0f;
                }
                else if (__instance.IsFlying())
                {
                    staticFriction = 0f;
                    dynamicFriction = 0f;
                }
                else if (__instance.GetMoveDir().magnitude < 0.1f)
                {
                    staticFriction = 0.8f * (1f - ___m_slippage);
                    dynamicFriction = staticFriction;
                    combine = PhysicsMaterialCombine.Maximum;
                }
                else
                {
                    staticFriction = 0.4f * (1f - ___m_slippage);
                    dynamicFriction = staticFriction;
                }

                // One material lookup also catches a mod replacing the
                // character collider's material.  Vanilla performs this native
                // lookup three or four times and writes every property each tick.
                UnityEngine.Object material = ___m_collider.material;
                FrictionState cache = FrictionStates.GetOrCreateValue(__instance);
                int frame = Time.frameCount;
                bool unchanged = cache.Initialized
                    && ReferenceEquals(cache.Material, material)
                    && cache.StaticFriction.Equals(staticFriction)
                    && cache.DynamicFriction.Equals(dynamicFriction)
                    && cache.Combine == combine;
                if (unchanged && frame < cache.NextVerificationFrame)
                {
                    PerformanceDiagnostics.CountFrictionSkip();
                    return false;
                }

                // Let Valheim itself perform changed/verification writes.  This
                // preserves its precise side effects and composes with prefixes
                // that alter state before this low-priority prefix runs.
                __state = new PendingFrictionState
                {
                    Cache = cache,
                    Material = material,
                    StaticFriction = staticFriction,
                    DynamicFriction = dynamicFriction,
                    Combine = combine,
                    NextVerificationFrame = frame + FrictionVerificationFrames
                        + (__instance.GetInstanceID() & 31)
                };
                return true;
            }

            private static void Postfix(bool __runOriginal,
                PendingFrictionState __state)
            {
                // Do not claim a value was applied if another prefix skipped the
                // original or if this invocation was a cached skip.
                if (!__runOriginal || __state.Cache == null) return;

                PerformanceDiagnostics.CountFrictionRun();

                FrictionState cache = __state.Cache;
                cache.Initialized = true;
                cache.Material = __state.Material;
                cache.StaticFriction = __state.StaticFriction;
                cache.DynamicFriction = __state.DynamicFriction;
                cache.Combine = __state.Combine;
                cache.NextVerificationFrame = __state.NextVerificationFrame;
            }
        }

        [HarmonyPatch]
        internal static class ZPackageWritePatch
        {
            private static MethodBase TargetMethod()
            {
                return RequireMethod(typeof(ZPackage), nameof(ZPackage.Write),
                    new[] { typeof(ZPackage) });
            }

            private static bool Prepare()
            {
                bool valid = EnsurePackageAccess();
                if (!valid)
                {
                    Warn("ZPackage.Write optimization could not validate the "
                        + "package backing fields and was not installed.");
                }
                return valid;
            }

            [HarmonyPriority(Priority.VeryLow)]
            private static bool Prefix(ZPackage __instance, ZPackage pkg)
            {
                if (!NetworkPackagesEnabled) return true;
                if (pkg == null || ReferenceEquals(__instance, pkg)
                    || _packageStream == null || _packageWriter == null)
                {
                    PerformanceDiagnostics.CountPackageFallback();
                    return true;
                }

                MemoryStream sourceStream;
                MemoryStream destinationStream;
                BinaryWriter sourceWriter;
                BinaryWriter destinationWriter;
                ArraySegment<byte> segment;
                int length;

                // Complete every operation that can choose the vanilla fallback
                // before writing the destination length.  Falling back after a
                // partial write would corrupt the wire packet.
                try
                {
                    sourceStream = _packageStream(pkg);
                    destinationStream = _packageStream(__instance);
                    sourceWriter = _packageWriter(pkg);
                    destinationWriter = _packageWriter(__instance);
                    if (sourceStream == null || destinationStream == null
                        || sourceWriter == null
                        || destinationWriter == null)
                    {
                        PerformanceDiagnostics.CountPackageFallback();
                        return true;
                    }
                    if (!ReferenceEquals(sourceWriter.BaseStream, sourceStream)
                        || !ReferenceEquals(destinationWriter.BaseStream,
                            destinationStream)
                        || ReferenceEquals(destinationStream, sourceStream))
                    {
                        // SetReader/SetWriter can create arbitrary or aliased
                        // stream layouts. Scope the zero-copy path to Valheim's
                        // normal owned streams and preserve vanilla otherwise.
                        PerformanceDiagnostics.CountPackageFallback();
                        return true;
                    }

                    sourceWriter.Flush();
                    sourceStream.Flush();
                    if (sourceStream.Length > int.MaxValue
                        || !sourceStream.TryGetBuffer(out segment)
                        || segment.Array == null)
                    {
                        PerformanceDiagnostics.CountPackageFallback();
                        return true;
                    }

                    length = (int)sourceStream.Length;
                    if (segment.Array.Length - segment.Offset < length)
                    {
                        PerformanceDiagnostics.CountPackageFallback();
                        return true;
                    }
                }
                catch
                {
                    PerformanceDiagnostics.CountPackageFallback();
                    return true;
                }

                // GetArray/MemoryStream.ToArray allocated and copied here.  The
                // backing segment contains the same logical bytes. Aliased
                // source/destination streams were rejected above.
                destinationWriter.Write(length);
                destinationWriter.Write(segment.Array!, segment.Offset, length);
                PerformanceDiagnostics.CountPackageWrite();
                return false;
            }
        }

        [HarmonyPatch]
        internal static class ZPackageReadPatch
        {
            private static MethodBase TargetMethod()
            {
                return RequireMethod(typeof(ZPackage), nameof(ZPackage.ReadPackage),
                    new[] { typeof(ZPackage).MakeByRefType() });
            }

            private static bool Prepare()
            {
                bool valid = EnsurePackageAccess();
                if (!valid)
                {
                    Warn("ZPackage.ReadPackage optimization could not validate the "
                        + "package backing fields and was not installed.");
                }
                return valid;
            }

            [HarmonyPriority(Priority.VeryLow)]
            private static bool Prefix(ZPackage __instance, ref ZPackage pkg)
            {
                if (!NetworkPackagesEnabled) return true;
                if (pkg == null || ReferenceEquals(__instance, pkg)
                    || _packageStream == null || _packageWriter == null
                    || _packageReader == null)
                {
                    PerformanceDiagnostics.CountPackageFallback();
                    return true;
                }

                MemoryStream destinationStream;
                MemoryStream sourceStream;
                BinaryWriter destinationWriter;
                BinaryReader sourceReader;

                // Validate all access before consuming the source length.  Once
                // those four bytes have been read, calling the original is no
                // longer a valid fallback.
                try
                {
                    destinationStream = _packageStream(pkg);
                    sourceStream = _packageStream(__instance);
                    destinationWriter = _packageWriter(pkg);
                    sourceReader = _packageReader(__instance);
                    if (destinationStream == null || sourceStream == null
                        || destinationWriter == null || sourceReader == null
                        || !destinationStream.TryGetBuffer(out _))
                    {
                        PerformanceDiagnostics.CountPackageFallback();
                        return true;
                    }
                    if (!ReferenceEquals(sourceReader.BaseStream, sourceStream)
                        || !ReferenceEquals(destinationWriter.BaseStream,
                            destinationStream)
                        || ReferenceEquals(sourceStream, destinationStream))
                    {
                        PerformanceDiagnostics.CountPackageFallback();
                        return true;
                    }
                }
                catch
                {
                    PerformanceDiagnostics.CountPackageFallback();
                    return true;
                }

                int count = sourceReader.ReadInt32();
                if (count < 0)
                {
                    // Preserve BinaryReader.ReadBytes' malformed-packet failure.
                    sourceReader.ReadBytes(count);
                    return false;
                }

                // Vanilla allocates ReadBytes(count) before clearing pkg.  Grow
                // the reusable destination first for the same OOM guarantee:
                // a failed/malicious reservation leaves its old contents intact.
                if (destinationStream.Capacity < count)
                    destinationStream.Capacity = count;

                if (!destinationStream.TryGetBuffer(out ArraySegment<byte> segment)
                    || segment.Array == null
                    || segment.Array.Length - segment.Offset < count)
                {
                    // A custom MemoryStream implementation can revoke buffer
                    // exposure after resizing.  We cannot call the original now,
                    // so reproduce its read-before-clear order as the defensive
                    // rare fallback.
                    byte[] fallback = sourceReader.ReadBytes(count);
                    destinationWriter.Flush();
                    destinationStream.SetLength(0L);
                    destinationStream.Position = 0L;
                    destinationStream.Write(fallback, 0, fallback.Length);
                    destinationStream.Position = 0L;
                    PerformanceDiagnostics.CountPackageFallback();
                    return false;
                }

                destinationWriter.Flush();
                destinationStream.SetLength(0L);
                destinationStream.Position = 0L;
                // Establish the logical length before the direct read. Growing
                // a MemoryStream after writing behind its API can zero the newly
                // exposed range and erase the bytes we just received.
                destinationStream.SetLength(count);

                int read = 0;
                while (read < count)
                {
                    int amount = sourceReader.Read(segment.Array,
                        segment.Offset + read, count - read);
                    if (amount <= 0) break;
                    read += amount;
                }

                // BinaryReader.ReadBytes returns a shorter array at EOF.  Match
                // that behavior rather than leaving zero-filled announced bytes.
                destinationStream.SetLength(read);
                destinationStream.Position = 0L;
                PerformanceDiagnostics.CountPackageRead();
                return false;
            }
        }
    }
}
