using UnityEngine;

namespace ValheimPerformance
{
    /// <summary>
    /// Stops smoke particles colliding with each other.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Every puff of smoke in Valheim is a real rigidbody — <c>Smoke.Awake</c>
    /// takes one, and <c>Smoke.CustomUpdate</c> writes its mass and adds a force
    /// to it on every frame. A base with several fires therefore hands PhysX
    /// hundreds of bodies, packed into a few cubic metres, and the broad phase
    /// pairs each of them against its neighbours. That is where a built-up base
    /// loses its frames, and none of it is on the GPU.
    /// </para>
    /// <para>
    /// The collisions that matter are smoke against the world: it is what makes
    /// smoke pool under a roof and pour out of a smoke hole, and it is left
    /// alone. Smoke against <em>smoke</em> produces nothing anyone can see —
    /// these are translucent billboards that overlap constantly and are meant
    /// to. Removing that one pair takes the quadratic part out of the crowd
    /// while the behaviour that reads as smoke is untouched.
    /// </para>
    /// <para>
    /// The layer is read from a live particle rather than named, because a layer
    /// index is a project setting that a game update is free to renumber, and
    /// disabling collisions on the wrong layer would be both invisible here and
    /// serious in the world.
    /// </para>
    /// </remarks>
    internal static class SmokePhysics
    {
        private static int _layer = -1;
        private static bool _originalIgnored;
        private static bool _captured;
        private static bool _warnedWrongLayer;

        public static void Apply(bool enabled)
        {
            if (!enabled)
            {
                Restore();
                return;
            }
            if (_captured) return;

            foreach (var updater in Smoke.Instances)
            {
                var smoke = updater as Smoke;
                if (smoke == null) continue;

                int liveLayer = smoke.gameObject.layer;
                int namedLayer = LayerMask.NameToLayer("smoke");
                // A modded smoke prefab on Default (or any shared gameplay
                // layer) must never cause a global self-collision override.
                // NameToLayer resolves the current project's numeric index, so
                // this remains safe if a game update renumbers the layer.
                if (namedLayer < 0 || liveLayer != namedLayer)
                {
                    if (!_warnedWrongLayer)
                    {
                        _warnedWrongLayer = true;
                        Plugin.Log.LogWarning("[Perf] Refused smoke collision tuning: "
                            + "the live prefab uses layer " + liveLayer + " ("
                            + LayerMask.LayerToName(liveLayer) + ") instead of the "
                            + "dedicated smoke layer " + namedLayer + ".");
                    }
                    continue;
                }

                _layer = liveLayer;
                _originalIgnored = Physics.GetIgnoreLayerCollision(_layer, _layer);
                _captured = true;
                Physics.IgnoreLayerCollision(_layer, _layer, true);
                Plugin.Log.LogInfo("[Perf] Smoke no longer collides with smoke (layer "
                                   + _layer + ", " + LayerMask.LayerToName(_layer) + ").");
                return;
            }
        }

        public static void Restore()
        {
            if (!_captured || _layer < 0) return;
            // Do not overwrite a later choice made by another mod. We only own
            // the matrix entry while it still contains the value we applied.
            if (Physics.GetIgnoreLayerCollision(_layer, _layer))
                Physics.IgnoreLayerCollision(_layer, _layer, _originalIgnored);
            _captured = false;
            _layer = -1;
        }
    }
}
