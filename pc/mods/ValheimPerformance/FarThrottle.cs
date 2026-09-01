using System.Runtime.CompilerServices;
using HarmonyLib;
using UnityEngine;

namespace ValheimPerformance
{
    /// <summary>
    /// Lets things far from the camera update less often than the frame rate.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Valheim drives a great deal of its world from lists of
    /// <c>IMonoUpdater</c>, ticked in full on the main thread every frame. That
    /// is a good design — it beats thousands of Unity <c>Update</c> callbacks —
    /// but it is unconditional, and most of what it ticks is too far away to
    /// see the result of.
    /// </para>
    /// <para>
    /// This replaces an earlier version that ran from the plugin's two-second
    /// reapply timer. That version could not have worked: it had no way to stop
    /// the game's own update, so all it did was tick distant smoke an extra time
    /// every two seconds, ageing it slightly early. Skipping an update has to
    /// happen at the update, which means a prefix.
    /// </para>
    /// <para>
    /// Two rules keep this free rather than paid. Work is skipped only past a
    /// distance where a frame of staleness is smaller than a pixel of movement,
    /// and the frame a given object updates on is derived from its instance id,
    /// so the skipped work is spread evenly across the stride instead of every
    /// distant object landing on the same frame — which would replace a steady
    /// cost with a periodic spike, the opposite of the point.
    /// </para>
    /// <para>
    /// Where the object's own timers depend on it, elapsed delta is accumulated
    /// across skipped frames and handed to the next update. Lifetimes and fades
    /// therefore follow the real frame times rather than an average stride.
    /// </para>
    /// </remarks>
    internal static class FarThrottle
    {
        private static Vector3 _eye;
        private static int _eyeFrame = -1;
        private static bool _haveEye;

        private sealed class TimingState
        {
            public float PendingDelta;
        }

        private static readonly ConditionalWeakTable<Component, TimingState> Timing =
            new ConditionalWeakTable<Component, TimingState>();

        /// <summary>
        /// The camera position, looked up once per frame however many callers ask.
        /// </summary>
        /// <remarks>
        /// These prefixes run hundreds of times a frame, so the lookup itself has
        /// to be cheap or it becomes the cost it was added to avoid.
        /// </remarks>
        private static bool Eye(out Vector3 eye)
        {
            int frame = Time.frameCount;
            if (_eyeFrame != frame)
            {
                _eyeFrame = frame;
                var camera = Utils.GetMainCamera();
                _haveEye = camera != null;
                if (_haveEye) _eye = camera!.transform.position;
            }

            eye = _eye;
            return _haveEye;
        }

        /// <summary>
        /// Whether this instance should run its update on this frame.
        /// </summary>
        /// <returns>
        /// True to let the original method run. False to skip it entirely.
        /// </returns>
        private static bool Due(Transform transform, float distance, int stride, int id)
        {
            if (stride <= 1 || transform == null) return true;
            if (!Eye(out Vector3 eye)) return true;

            float far = distance * distance;
            if ((transform.position - eye).sqrMagnitude < far) return true;

            return (Time.frameCount % stride) == ((id & int.MaxValue) % stride);
        }

        /// <summary>
        /// As <see cref="Due"/>, and pays back the skipped frames in the delta.
        /// </summary>
        private static bool DueScaled(Component component, float distance, int stride,
                                      int id, ref float deltaTime)
        {
            if (component == null) return true;
            if (stride <= 1) return Flush(component, ref deltaTime);
            Transform transform = component.transform;
            if (transform == null || !Eye(out Vector3 eye))
                return Flush(component, ref deltaTime);

            float far = distance * distance;
            if ((transform.position - eye).sqrMagnitude < far)
                return Flush(component, ref deltaTime);

            TimingState timing = Timing.GetOrCreateValue(component);
            timing.PendingDelta += deltaTime;

            if ((Time.frameCount % stride) != ((id & int.MaxValue) % stride)) return false;

            deltaTime = timing.PendingDelta;
            timing.PendingDelta = 0f;
            return true;
        }

        private static bool Flush(Component? component, ref float deltaTime)
        {
            if (component != null && Timing.TryGetValue(component, out TimingState? timing)
                && timing != null
                && timing.PendingDelta > 0f)
            {
                deltaTime += timing.PendingDelta;
                timing.PendingDelta = 0f;
            }
            return true;
        }

        /// <summary>
        /// Smoke: a billboard that drifts a few centimetres a second.
        /// </summary>
        /// <remarks>
        /// The distance is generous because smoke is the one thing here whose
        /// motion is the whole effect. Past fifty metres a plume is a smudge
        /// above a roofline and the frame it was last moved on is not
        /// recoverable information.
        /// </remarks>
        [HarmonyPatch(typeof(Smoke), nameof(Smoke.CustomUpdate))]
        internal static class SmokePatch
        {
            private static bool Prefix(Smoke __instance, ref float deltaTime)
            {
                if (!Plugin.ThrottleDistantUpdates.Value)
                    return Flush(__instance, ref deltaTime);
                bool run = DueScaled(__instance, Plugin.SmokeDistance.Value,
                                     Plugin.SmokeStride.Value, __instance.GetInstanceID(),
                                     ref deltaTime);
                if (run) PerformanceDiagnostics.CountSmokeUpdate();
                else PerformanceDiagnostics.CountSmokeSkip();
                return run;
            }
        }

        /// <summary>
        /// Torches, campfires and lanterns.
        /// </summary>
        /// <remarks>
        /// <para>
        /// The cheapest-looking and most numerous of the three. Every flame in
        /// the game carries one, a built base has dozens lit at once, and each
        /// one does eight trigonometric calls and then writes
        /// <c>transform.localPosition</c> — which dirties the transform and
        /// makes Unity re-evaluate the light's world position and its effect on
        /// everything it touches. The write is the expensive half, not the
        /// arithmetic.
        /// </para>
        /// <para>
        /// The movement it is writing is a tenth of a metre of jitter. At
        /// thirty metres that is well under a pixel, and the intensity flicker
        /// it also stops updating is a variation of ten percent on a light
        /// whose falloff has already taken it close to nothing.
        /// </para>
        /// </remarks>
        [HarmonyPatch(typeof(LightFlicker), nameof(LightFlicker.CustomUpdate))]
        internal static class LightFlickerPatch
        {
            private static bool Prefix(LightFlicker __instance, ref float deltaTime)
            {
                if (!Plugin.ThrottleDistantUpdates.Value)
                    return Flush(__instance, ref deltaTime);
                bool run = DueScaled(__instance, Plugin.LightDistance.Value,
                                     Plugin.LightStride.Value, __instance.GetInstanceID(),
                                     ref deltaTime);
                if (run) PerformanceDiagnostics.CountLightUpdate();
                else PerformanceDiagnostics.CountLightSkip();
                return run;
            }
        }

        /// <summary>
        /// Character appearance: hair, beard, skin tint and worn equipment.
        /// </summary>
        /// <remarks>
        /// <para>
        /// <c>VisEquipment.CustomUpdate</c> calls <c>UpdateVisuals</c>
        /// unconditionally, and that path reads <c>Renderer.materials</c> and
        /// calls <c>GetComponentsInChildren&lt;Renderer&gt;</c> on the hair and
        /// beard. Both allocate an array, every frame, for every character in
        /// the scene — and reading <c>.materials</c> also instantiates the
        /// material rather than sharing it. On a busy screen this is one of the
        /// largest sources of per-frame garbage in the game, which is why it
        /// pairs with the collector setting in <see cref="Pipeline"/>.
        /// </para>
        /// <para>
        /// The delta is not scaled here because the method ignores it. Nothing
        /// in this path is time-driven: it reads current state and writes the
        /// matching visuals, so a skipped frame costs at most a few frames of
        /// delay before a distant character's changed equipment appears.
        /// </para>
        /// <para>
        /// The distance is short compared to the others, deliberately. This one
        /// governs what a character is *wearing*, and a player who swaps a
        /// shield at the edge of view should not visibly do it late.
        /// </para>
        /// </remarks>
        [HarmonyPatch(typeof(VisEquipment), nameof(VisEquipment.CustomUpdate))]
        internal static class VisEquipmentPatch
        {
            private static bool Prefix(VisEquipment __instance)
            {
                if (!Plugin.ThrottleDistantUpdates.Value) return true;
                bool run = Due(__instance.transform, Plugin.VisualsDistance.Value,
                               Plugin.VisualsStride.Value, __instance.GetInstanceID());
                if (run) PerformanceDiagnostics.CountEquipmentUpdate();
                else PerformanceDiagnostics.CountEquipmentSkip();
                return run;
            }
        }
    }
}
