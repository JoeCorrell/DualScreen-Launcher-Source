using System;
using System.Collections.Generic;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using HarmonyLib;
using UnityEngine;

namespace ValheimPerformance
{
    /// <summary>
    /// Removes repeated native animation, audio, and shader writes whose inputs
    /// are demonstrably unchanged while retaining vanilla update cadence.
    /// </summary>
    /// <remarks>
    /// Damped animator parameters are deliberately untouched: their setter is
    /// the integration step, so forwarding the same target every fixed tick is
    /// required. Cached writes are periodically replayed and are invalidated on
    /// ownership, component, enablement, and playback lifecycle transitions so
    /// an external mod's state cannot remain authoritative indefinitely.
    /// </remarks>
    internal static class AnimationAudioState
    {
        private const int VerificationFrames = 120;

        private static readonly ConditionalWeakTable<ZSyncAnimation, AnimationState>
            AnimationStates = new ConditionalWeakTable<ZSyncAnimation, AnimationState>();

        private static readonly ConditionalWeakTable<ZSFX, AudioState>
            AudioStates = new ConditionalWeakTable<ZSFX, AudioState>();

        private static bool _animationWrappersInstalled;
        private static bool _audioWrappersInstalled;

        private sealed class AnimationState
        {
            public bool Enabled;
            public Animator? Animator;
            public int Invocation;
            public int LastRemoteInvocation = int.MinValue;
            public int NextVerificationFrame;
            public Dictionary<int, bool>? Bools;
            public Dictionary<int, float>? Floats;
            public Dictionary<int, int>? Integers;
            public bool HasSpeed;
            public float Speed;

            public void ClearValues()
            {
                Bools?.Clear();
                Floats?.Clear();
                Integers?.Clear();
                HasSpeed = false;
            }
        }

        private sealed class AudioState
        {
            public bool Enabled;
            public AudioSource? Source;
            public int Invocation;
            public int LastWriteInvocation = int.MinValue;
            public int NextVerificationFrame;
            public bool HasVolume;
            public float Volume;
            public bool HasPitch;
            public float Pitch;

            public void ClearValues()
            {
                HasVolume = false;
                HasPitch = false;
            }
        }

        private static bool AnimationOptimizationEnabled =>
            Plugin.OptimizeAnimationSync != null
            && Plugin.OptimizeAnimationSync.Value;

        private static bool AudioOptimizationEnabled =>
            Plugin.OptimizeAudioUpdates != null
            && Plugin.OptimizeAudioUpdates.Value;

        private static bool EnvironmentOptimizationEnabled =>
            Plugin.RemoveDuplicateEnvShaderWrite != null
            && Plugin.RemoveDuplicateEnvShaderWrite.Value;

        private static int NextVerification(UnityEngine.Object owner)
        {
            return Time.frameCount + VerificationFrames
                + (owner.GetInstanceID() & 31);
        }

        private static bool VerificationDue(int frame, int deadline)
        {
            return unchecked(frame - deadline) >= 0;
        }

        private static void BeginAnimationSync(ZSyncAnimation instance,
            Animator animator, bool init)
        {
            if (!_animationWrappersInstalled) return;

            if (!AnimationOptimizationEnabled)
            {
                if (AnimationStates.TryGetValue(instance, out AnimationState state))
                    state.Enabled = false;
                return;
            }

            // Do not allocate cache state for locally-owned or currently
            // ownerless animations. The first actual remote setter lazily
            // creates it; subsequent prefixes then track ownership gaps.
            if (!AnimationStates.TryGetValue(instance,
                    out AnimationState current))
            {
                return;
            }
            current.Invocation = unchecked(current.Invocation + 1);

            int frame = Time.frameCount;
            if (!current.Enabled || init
                || !ReferenceEquals(current.Animator, animator)
                || VerificationDue(frame, current.NextVerificationFrame))
            {
                current.ClearValues();
                current.Animator = animator;
                current.NextVerificationFrame = NextVerification(instance);
            }
            current.Enabled = true;
        }

        private static AnimationState GetRemoteAnimationState(
            ZSyncAnimation instance, Animator animator)
        {
            AnimationState state = AnimationStates.GetOrCreateValue(instance);
            if (!state.Enabled || !ReferenceEquals(state.Animator, animator))
            {
                state.ClearValues();
                state.Enabled = true;
                state.Animator = animator;
                state.NextVerificationFrame = NextVerification(instance);
            }

            if (state.LastRemoteInvocation != state.Invocation)
            {
                // No wrapper calls in the preceding SyncParameters invocation
                // means it took the owner/no-owner branch. Force the first
                // remote update after that transition through to Animator.
                if (state.LastRemoteInvocation
                    != unchecked(state.Invocation - 1))
                {
                    state.ClearValues();
                }
                state.LastRemoteInvocation = state.Invocation;
            }
            return state;
        }

        private static void SetBoolIfChanged(Animator animator, int hash,
            bool value, ZSyncAnimation instance)
        {
            if (!AnimationOptimizationEnabled)
            {
                animator.SetBool(hash, value);
                return;
            }

            AnimationState state = GetRemoteAnimationState(instance, animator);
            Dictionary<int, bool>? values = state.Bools;
            if (values != null && values.TryGetValue(hash, out bool previous)
                && previous == value)
            {
                return;
            }

            animator.SetBool(hash, value);
            values ??= state.Bools = new Dictionary<int, bool>(4);
            values[hash] = value;
        }

        private static void SetFloatIfChanged(Animator animator, int hash,
            float value, ZSyncAnimation instance)
        {
            if (!AnimationOptimizationEnabled)
            {
                animator.SetFloat(hash, value);
                return;
            }

            AnimationState state = GetRemoteAnimationState(instance, animator);
            Dictionary<int, float>? values = state.Floats;
            if (values != null && values.TryGetValue(hash, out float previous)
                && previous.Equals(value))
            {
                return;
            }

            animator.SetFloat(hash, value);
            values ??= state.Floats = new Dictionary<int, float>(4);
            values[hash] = value;
        }

        private static void SetIntegerIfChanged(Animator animator, int hash,
            int value, ZSyncAnimation instance)
        {
            if (!AnimationOptimizationEnabled)
            {
                animator.SetInteger(hash, value);
                return;
            }

            AnimationState state = GetRemoteAnimationState(instance, animator);
            Dictionary<int, int>? values = state.Integers;
            if (values != null && values.TryGetValue(hash, out int previous)
                && previous == value)
            {
                return;
            }

            animator.SetInteger(hash, value);
            values ??= state.Integers = new Dictionary<int, int>(4);
            values[hash] = value;
        }

        private static void SetAnimationSpeedIfChanged(Animator animator,
            float value, ZSyncAnimation instance)
        {
            if (!AnimationOptimizationEnabled)
            {
                animator.speed = value;
                return;
            }

            AnimationState state = GetRemoteAnimationState(instance, animator);
            if (state.HasSpeed && state.Speed.Equals(value)) return;

            animator.speed = value;
            state.Speed = value;
            state.HasSpeed = true;
        }

        private static void BeginAudioUpdate(ZSFX instance,
            AudioSource source)
        {
            if (!_audioWrappersInstalled) return;

            if (!AudioOptimizationEnabled)
            {
                if (AudioStates.TryGetValue(instance, out AudioState state))
                    state.Enabled = false;
                return;
            }

            // Silent and never-played ZSFX objects need no cache allocation.
            // The first actual volume/pitch write creates state lazily.
            if (!AudioStates.TryGetValue(instance, out AudioState current))
                return;
            current.Invocation = unchecked(current.Invocation + 1);
            int frame = Time.frameCount;
            if (!current.Enabled || !ReferenceEquals(current.Source, source)
                || VerificationDue(frame, current.NextVerificationFrame))
            {
                current.ClearValues();
                current.Source = source;
                current.NextVerificationFrame = NextVerification(instance);
            }
            current.Enabled = true;
        }

        private static AudioState GetAudioWriteState(ZSFX instance,
            AudioSource source)
        {
            AudioState state = AudioStates.GetOrCreateValue(instance);
            if (!state.Enabled || !ReferenceEquals(state.Source, source))
            {
                state.ClearValues();
                state.Enabled = true;
                state.Source = source;
                state.NextVerificationFrame = NextVerification(instance);
            }

            if (state.LastWriteInvocation != state.Invocation)
            {
                // A stopped/non-playing update performs no setters. Force the
                // first update after such a gap so Play or another component
                // cannot leave this cache authoritative over the AudioSource.
                if (state.LastWriteInvocation
                    != unchecked(state.Invocation - 1))
                {
                    state.ClearValues();
                }
                state.LastWriteInvocation = state.Invocation;
            }
            return state;
        }

        private static void SetVolumeIfChanged(AudioSource source, float value,
            ZSFX instance)
        {
            if (!AudioOptimizationEnabled)
            {
                source.volume = value;
                return;
            }

            AudioState state = GetAudioWriteState(instance, source);
            if (state.HasVolume && state.Volume.Equals(value)) return;

            source.volume = value;
            state.Volume = value;
            state.HasVolume = true;
        }

        private static void SetPitchIfChanged(AudioSource source, float value,
            ZSFX instance)
        {
            if (!AudioOptimizationEnabled)
            {
                source.pitch = value;
                return;
            }

            AudioState state = GetAudioWriteState(instance, source);
            if (state.HasPitch && state.Pitch.Equals(value)) return;

            source.pitch = value;
            state.Pitch = value;
            state.HasPitch = true;
        }

        private static void InvalidateAudioState(ZSFX instance)
        {
            if (!AudioStates.TryGetValue(instance, out AudioState state)) return;
            state.ClearValues();
            state.LastWriteInvocation = int.MinValue;
        }

        private static void StopShieldHumIfPlaying(AudioSource source)
        {
            if (!AudioOptimizationEnabled || source.isPlaying)
                source.Stop();
        }

        private static void SetDuplicateSkyVector(int property, Vector4 value)
        {
            if (!EnvironmentOptimizationEnabled)
                Shader.SetGlobalVector(property, value);
        }

        private static MethodBase RequireMethod(Type type, string name,
            Type[] parameters)
        {
            MethodInfo? method = AccessTools.Method(type, name, parameters);
            if (method == null)
                throw new MissingMethodException(type.FullName, name);
            return method;
        }

        private static bool PrepareOwnerCallReplacement(
            List<CodeInstruction> code,
            MethodInfo? original,
            MethodInfo? replacement,
            int expected,
            List<int> matches)
        {
            if (original == null || replacement == null) return false;
            for (int i = 0; i < code.Count; ++i)
            {
                if (code[i].Calls(original)) matches.Add(i);
            }
            if (matches.Count != expected) return false;
            foreach (int index in matches)
            {
                // The current target methods contain no exception regions at
                // these calls. Refuse a future layout where moving block
                // boundaries around the injected owner argument is ambiguous.
                if (code[index].blocks.Count != 0) return false;
            }
            return true;
        }

        private static void ReplaceCallsWithOwner(List<CodeInstruction> code,
            List<int> matches, MethodInfo replacement)
        {
            for (int i = matches.Count - 1; i >= 0; --i)
            {
                int index = matches[i];
                CodeInstruction call = code[index];
                var loadOwner = new CodeInstruction(OpCodes.Ldarg_0);
                loadOwner.labels.AddRange(call.labels);
                call.labels.Clear();
                call.opcode = OpCodes.Call;
                call.operand = replacement;
                code.Insert(index, loadOwner);
            }
        }

        private static void Warn(string message)
        {
            try
            {
                Plugin.Log?.LogWarning("[Perf] " + message);
            }
            catch
            {
                // A compatibility fallback must not become a startup failure.
            }
        }

        [HarmonyPatch]
        internal static class AnimationPatch
        {
            private static readonly MethodInfo? SetBool = AccessTools.Method(
                typeof(Animator), nameof(Animator.SetBool),
                new[] { typeof(int), typeof(bool) });
            private static readonly MethodInfo? SetFloat = AccessTools.Method(
                typeof(Animator), nameof(Animator.SetFloat),
                new[] { typeof(int), typeof(float) });
            private static readonly MethodInfo? SetInteger = AccessTools.Method(
                typeof(Animator), nameof(Animator.SetInteger),
                new[] { typeof(int), typeof(int) });
            private static readonly MethodInfo? SetSpeed = AccessTools.PropertySetter(
                typeof(Animator), nameof(Animator.speed));
            private static readonly MethodInfo? BoolWrapper = AccessTools.Method(
                typeof(AnimationAudioState), nameof(SetBoolIfChanged));
            private static readonly MethodInfo? FloatWrapper = AccessTools.Method(
                typeof(AnimationAudioState), nameof(SetFloatIfChanged));
            private static readonly MethodInfo? IntegerWrapper = AccessTools.Method(
                typeof(AnimationAudioState), nameof(SetIntegerIfChanged));
            private static readonly MethodInfo? SpeedWrapper = AccessTools.Method(
                typeof(AnimationAudioState), nameof(SetAnimationSpeedIfChanged));

            private static MethodBase TargetMethod()
            {
                return RequireMethod(typeof(ZSyncAnimation), "SyncParameters",
                    new[] { typeof(float), typeof(bool) });
            }

            private static void Prefix(ZSyncAnimation __instance,
                Animator ___m_animator, bool init)
            {
                BeginAnimationSync(__instance, ___m_animator, init);
            }

            [HarmonyPriority(Priority.VeryLow)]
            private static IEnumerable<CodeInstruction> Transpiler(
                IEnumerable<CodeInstruction> instructions)
            {
                _animationWrappersInstalled = false;
                var code = new List<CodeInstruction>(instructions);
                var boolCalls = new List<int>(1);
                var floatCalls = new List<int>(1);
                var integerCalls = new List<int>(1);
                var speedCalls = new List<int>(1);

                bool valid = PrepareOwnerCallReplacement(code, SetBool,
                                 BoolWrapper, 1, boolCalls)
                    && PrepareOwnerCallReplacement(code, SetFloat,
                                 FloatWrapper, 1, floatCalls)
                    && PrepareOwnerCallReplacement(code, SetInteger,
                                 IntegerWrapper, 1, integerCalls)
                    && PrepareOwnerCallReplacement(code, SetSpeed,
                                 SpeedWrapper, 1, speedCalls);
                if (!valid || BoolWrapper == null || FloatWrapper == null
                    || IntegerWrapper == null || SpeedWrapper == null)
                {
                    Warn("ZSyncAnimation.SyncParameters no longer matches the "
                        + "validated setter layout; animation writes were left unchanged.");
                    return code;
                }

                // Replace from the highest call site to the lowest so inserting
                // owner arguments cannot invalidate a later recorded index.
                var all = new List<(int Index, MethodInfo Wrapper)>(4)
                {
                    (boolCalls[0], BoolWrapper),
                    (floatCalls[0], FloatWrapper),
                    (integerCalls[0], IntegerWrapper),
                    (speedCalls[0], SpeedWrapper)
                };
                all.Sort((left, right) => right.Index.CompareTo(left.Index));
                foreach ((int index, MethodInfo wrapper) in all)
                {
                    ReplaceCallsWithOwner(code, new List<int>(1) { index }, wrapper);
                }
                _animationWrappersInstalled = true;
                return code;
            }
        }

        [HarmonyPatch]
        internal static class ZsfxPatch
        {
            private static readonly MethodInfo? SetVolume =
                AccessTools.PropertySetter(typeof(AudioSource),
                    nameof(AudioSource.volume));
            private static readonly MethodInfo? SetPitch =
                AccessTools.PropertySetter(typeof(AudioSource),
                    nameof(AudioSource.pitch));
            private static readonly MethodInfo? VolumeWrapper = AccessTools.Method(
                typeof(AnimationAudioState), nameof(SetVolumeIfChanged));
            private static readonly MethodInfo? PitchWrapper = AccessTools.Method(
                typeof(AnimationAudioState), nameof(SetPitchIfChanged));

            private static MethodBase TargetMethod()
            {
                return RequireMethod(typeof(ZSFX), nameof(ZSFX.CustomUpdate),
                    new[] { typeof(float), typeof(float) });
            }

            private static void Prefix(ZSFX __instance,
                AudioSource ___m_audioSource)
            {
                BeginAudioUpdate(__instance, ___m_audioSource);
            }

            [HarmonyPriority(Priority.VeryLow)]
            private static IEnumerable<CodeInstruction> Transpiler(
                IEnumerable<CodeInstruction> instructions)
            {
                _audioWrappersInstalled = false;
                var code = new List<CodeInstruction>(instructions);
                var volumeCalls = new List<int>(2);
                var pitchCalls = new List<int>(1);
                bool valid = PrepareOwnerCallReplacement(code, SetVolume,
                                 VolumeWrapper, 2, volumeCalls)
                    && PrepareOwnerCallReplacement(code, SetPitch,
                                 PitchWrapper, 1, pitchCalls);
                if (!valid || VolumeWrapper == null || PitchWrapper == null)
                {
                    Warn("ZSFX.CustomUpdate no longer matches the validated "
                        + "volume/pitch layout; audio writes were left unchanged.");
                    return code;
                }

                var all = new List<(int Index, MethodInfo Wrapper)>(3)
                {
                    (volumeCalls[0], VolumeWrapper),
                    (volumeCalls[1], VolumeWrapper),
                    (pitchCalls[0], PitchWrapper)
                };
                all.Sort((left, right) => right.Index.CompareTo(left.Index));
                foreach ((int index, MethodInfo wrapper) in all)
                {
                    ReplaceCallsWithOwner(code, new List<int>(1) { index }, wrapper);
                }
                _audioWrappersInstalled = true;
                return code;
            }
        }

        [HarmonyPatch]
        internal static class ZsfxResetPatch
        {
            private static IEnumerable<MethodBase> TargetMethods()
            {
                foreach (string methodName in new[]
                {
                    nameof(ZSFX.Play), nameof(ZSFX.Stop), "OnEnable", "OnDisable"
                })
                {
                    MethodInfo? method = AccessTools.Method(typeof(ZSFX),
                        methodName, Type.EmptyTypes);
                    if (method != null) yield return method;
                    else Warn("Could not find ZSFX." + methodName
                        + "; that audio cache reset was not installed.");
                }
            }

            private static void Prefix(ZSFX __instance)
            {
                InvalidateAudioState(__instance);
            }
        }

        [HarmonyPatch]
        internal static class ShieldHumPatch
        {
            private static readonly MethodInfo? Stop = AccessTools.Method(
                typeof(AudioSource), nameof(AudioSource.Stop), Type.EmptyTypes);
            private static readonly FieldInfo? ShieldHumSource =
                AccessTools.Field(typeof(AudioMan), "m_shieldHumSource");
            private static readonly MethodInfo? StopWrapper = AccessTools.Method(
                typeof(AnimationAudioState), nameof(StopShieldHumIfPlaying));

            private static MethodBase TargetMethod()
            {
                return RequireMethod(typeof(AudioMan), "UpdateShieldHum",
                    Type.EmptyTypes);
            }

            [HarmonyPriority(Priority.VeryLow)]
            private static IEnumerable<CodeInstruction> Transpiler(
                IEnumerable<CodeInstruction> instructions)
            {
                var code = new List<CodeInstruction>(instructions);
                var matches = new List<int>(1);
                if (Stop != null && ShieldHumSource != null)
                {
                    for (int i = 2; i < code.Count; ++i)
                    {
                        if (code[i].Calls(Stop)
                            && code[i - 1].LoadsField(ShieldHumSource)
                            && code[i - 2].opcode == OpCodes.Ldarg_0)
                        {
                            matches.Add(i);
                        }
                    }
                }

                if (matches.Count != 1 || StopWrapper == null)
                {
                    Warn("AudioMan.UpdateShieldHum contained " + matches.Count
                        + " validated shield-hum Stop calls (expected one); "
                        + "it was left unchanged.");
                    return code;
                }

                CodeInstruction stopCall = code[matches[0]];
                stopCall.opcode = OpCodes.Call;
                stopCall.operand = StopWrapper;
                return code;
            }
        }

        [HarmonyPatch]
        internal static class EnvShaderPatch
        {
            private static readonly FieldInfo? SkyboxSunDirection =
                AccessTools.Field(typeof(EnvMan), "s_skyboxSunDir");
            private static readonly MethodInfo? Vector3ToVector4 =
                AccessTools.Method(typeof(Vector4), "op_Implicit",
                    new[] { typeof(Vector3) });
            private static readonly MethodInfo? SetGlobalVector = AccessTools.Method(
                typeof(Shader), nameof(Shader.SetGlobalVector),
                new[] { typeof(int), typeof(Vector4) });
            private static readonly MethodInfo? DuplicateWrapper = AccessTools.Method(
                typeof(AnimationAudioState), nameof(SetDuplicateSkyVector));

            private static MethodBase TargetMethod()
            {
                return RequireMethod(typeof(EnvMan), "SetEnv", new[]
                {
                    typeof(EnvSetup), typeof(float), typeof(float), typeof(float),
                    typeof(float), typeof(float)
                });
            }

            [HarmonyPriority(Priority.VeryLow)]
            private static IEnumerable<CodeInstruction> Transpiler(
                IEnumerable<CodeInstruction> instructions)
            {
                var code = new List<CodeInstruction>(instructions);
                var matches = new List<int>(1);
                if (SkyboxSunDirection != null && Vector3ToVector4 != null
                    && SetGlobalVector != null && DuplicateWrapper != null)
                {
                    for (int i = 0; i <= code.Count - 8; ++i)
                    {
                        if (!code[i].LoadsField(SkyboxSunDirection)
                            || !SameInstruction(code[i + 1], code[i + 5])
                            || !code[i + 2].Calls(Vector3ToVector4)
                            || !code[i + 3].Calls(SetGlobalVector)
                            || !code[i + 4].LoadsField(SkyboxSunDirection)
                            || !code[i + 6].Calls(Vector3ToVector4)
                            || !code[i + 7].Calls(SetGlobalVector)
                            || HasUnsafeFlowBoundary(code, i, 8))
                        {
                            continue;
                        }
                        matches.Add(i);
                    }
                }

                if (matches.Count != 1 || DuplicateWrapper == null)
                {
                    Warn("EnvMan.SetEnv contained " + matches.Count
                        + " validated duplicate sky-vector sequences (expected one); "
                        + "it was left unchanged.");
                    return code;
                }

                CodeInstruction duplicateCall = code[matches[0] + 7];
                duplicateCall.opcode = OpCodes.Call;
                duplicateCall.operand = DuplicateWrapper;
                return code;
            }

            private static bool SameInstruction(CodeInstruction left,
                CodeInstruction right)
            {
                return left.opcode == right.opcode
                    && Equals(left.operand, right.operand);
            }

            private static bool HasUnsafeFlowBoundary(
                List<CodeInstruction> code, int start, int count)
            {
                for (int offset = 0; offset < count; ++offset)
                {
                    CodeInstruction instruction = code[start + offset];
                    if (instruction.blocks.Count != 0) return true;

                    // A label on the beginning of the first write is safe:
                    // both writes still execute. A label anywhere later could
                    // enter between them, so the second call may not actually
                    // be redundant on that control-flow path.
                    if (offset != 0 && instruction.labels.Count != 0)
                        return true;
                }
                return false;
            }
        }
    }
}
