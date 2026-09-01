using System.Reflection;
using HarmonyLib;
using UnityEngine;

namespace ValheimPerformance
{
    /// <summary>
    /// The settings that buy frames with picture quality.
    /// </summary>
    /// <remarks>
    /// Every value here defaults to a sentinel meaning "leave the game's own
    /// alone", so the mod as shipped changes nothing you can see. That is
    /// deliberate: a performance mod that dims the game by default is
    /// indistinguishable from a broken install, and the person running it has no
    /// way to tell which they have.
    /// </remarks>
    internal static class Quality
    {
        private static readonly FieldInfo ClutterScale =
            AccessTools.Field(typeof(ClutterSystem), "m_amountScale");

        private static bool _captured;
        private static float _originalLodBias;
        private static float _originalShadowDistance;
        private static int _originalShadowCascades;
        private static bool _originalSoftParticles;
        private static int _originalLightLimit;
        private static int _originalShadowLimit;
        private static bool _lodApplied;
        private static bool _shadowDistanceApplied;
        private static bool _shadowCascadesApplied;
        private static bool _softParticlesApplied;
        private static bool _lightLimitApplied;
        private static bool _shadowLimitApplied;

        private static ClutterSystem? _clutterSystem;
        private static float _originalClutter;
        private static bool _clutterCaptured;
        private static bool _clutterApplied;

        private static ParticleMist? _mist;
        private static int _originalLocalEmission;
        private static int _originalLocalEmissionPerUnit;
        private static float _originalDistantRange;
        private static bool _mistCaptured;
        private static bool _mistApplied;

        /// <summary>Whether anything here will visibly alter the game.</summary>
        public static bool AnyPaidTweakEnabled =>
            Plugin.LodBias.Value > 0f
            || Plugin.ShadowDistance.Value > 0f
            || Plugin.ShadowCascades.Value > 0
            || Plugin.ClutterAmount.Value > 0f
            || Plugin.LightLimit.Value >= 0
            || Plugin.ShadowLimit.Value >= 0
            || Plugin.MistDensity.Value > 0f
            || !Plugin.SoftParticles.Value;

        public static void Apply()
        {
            CaptureGlobals();

            if (Plugin.LodBias.Value > 0f)
            {
                if (!_lodApplied) _originalLodBias = QualitySettings.lodBias;
                if (!Mathf.Approximately(QualitySettings.lodBias,
                        Plugin.LodBias.Value))
                    QualitySettings.lodBias = Plugin.LodBias.Value;
                _lodApplied = true;
            }
            else if (_lodApplied)
            {
                QualitySettings.lodBias = _originalLodBias;
                _lodApplied = false;
            }
            else _originalLodBias = QualitySettings.lodBias;

            if (Plugin.ShadowDistance.Value > 0f)
            {
                if (!_shadowDistanceApplied)
                    _originalShadowDistance = QualitySettings.shadowDistance;
                if (!Mathf.Approximately(QualitySettings.shadowDistance,
                        Plugin.ShadowDistance.Value))
                    QualitySettings.shadowDistance = Plugin.ShadowDistance.Value;
                _shadowDistanceApplied = true;
            }
            else if (_shadowDistanceApplied)
            {
                QualitySettings.shadowDistance = _originalShadowDistance;
                _shadowDistanceApplied = false;
            }
            else _originalShadowDistance = QualitySettings.shadowDistance;

            if (Plugin.ShadowCascades.Value > 0)
            {
                if (!_shadowCascadesApplied)
                    _originalShadowCascades = QualitySettings.shadowCascades;
                int wanted = Mathf.Clamp(Plugin.ShadowCascades.Value, 1, 4);
                if (QualitySettings.shadowCascades != wanted)
                    QualitySettings.shadowCascades = wanted;
                _shadowCascadesApplied = true;
            }
            else if (_shadowCascadesApplied)
            {
                QualitySettings.shadowCascades = _originalShadowCascades;
                _shadowCascadesApplied = false;
            }
            else _originalShadowCascades = QualitySettings.shadowCascades;

            if (!Plugin.SoftParticles.Value)
            {
                if (!_softParticlesApplied)
                    _originalSoftParticles = QualitySettings.softParticles;
                if (QualitySettings.softParticles)
                    QualitySettings.softParticles = false;
                _softParticlesApplied = true;
            }
            else if (_softParticlesApplied)
            {
                QualitySettings.softParticles = _originalSoftParticles;
                _softParticlesApplied = false;
            }
            else _originalSoftParticles = QualitySettings.softParticles;

            /*
             * The light caps are static on LightLod, which is what makes them
             * worth setting from here: one write covers every torch, fire and
             * lantern in the world rather than a component per object.
             */
            if (Plugin.LightLimit.Value >= 0)
            {
                if (!_lightLimitApplied) _originalLightLimit = LightLod.m_lightLimit;
                if (LightLod.m_lightLimit != Plugin.LightLimit.Value)
                    LightLod.m_lightLimit = Plugin.LightLimit.Value;
                _lightLimitApplied = true;
            }
            else if (_lightLimitApplied)
            {
                LightLod.m_lightLimit = _originalLightLimit;
                _lightLimitApplied = false;
            }
            else _originalLightLimit = LightLod.m_lightLimit;

            if (Plugin.ShadowLimit.Value >= 0)
            {
                if (!_shadowLimitApplied) _originalShadowLimit = LightLod.m_shadowLimit;
                if (LightLod.m_shadowLimit != Plugin.ShadowLimit.Value)
                    LightLod.m_shadowLimit = Plugin.ShadowLimit.Value;
                _shadowLimitApplied = true;
            }
            else if (_shadowLimitApplied)
            {
                LightLod.m_shadowLimit = _originalShadowLimit;
                _shadowLimitApplied = false;
            }
            else _originalShadowLimit = LightLod.m_shadowLimit;

            ApplyClutter();
            ApplyMist();
        }

        private static void CaptureGlobals()
        {
            if (_captured) return;
            _originalLodBias = QualitySettings.lodBias;
            _originalShadowDistance = QualitySettings.shadowDistance;
            _originalShadowCascades = QualitySettings.shadowCascades;
            _originalSoftParticles = QualitySettings.softParticles;
            _originalLightLimit = LightLod.m_lightLimit;
            _originalShadowLimit = LightLod.m_shadowLimit;
            _captured = true;
        }

        /// <summary>
        /// Records values the game's graphics menu just applied while mod
        /// overrides are active. Disabling an override then returns to the
        /// player's current preset instead of a stale preset from startup.
        /// </summary>
        public static void CaptureGameSettings()
        {
            CaptureGlobals();
            if (_lodApplied) _originalLodBias = QualitySettings.lodBias;
            if (_shadowDistanceApplied)
                _originalShadowDistance = QualitySettings.shadowDistance;
            if (_shadowCascadesApplied)
                _originalShadowCascades = QualitySettings.shadowCascades;
            if (_softParticlesApplied)
                _originalSoftParticles = QualitySettings.softParticles;
            if (_lightLimitApplied) _originalLightLimit = LightLod.m_lightLimit;
            if (_shadowLimitApplied) _originalShadowLimit = LightLod.m_shadowLimit;
        }

        /// <summary>
        /// Scales ground cover.
        /// </summary>
        /// <remarks>
        /// Written through the live instance rather than the prefab, because the
        /// clutter system is rebuilt per scene and a prefab edit would be undone
        /// by the next zone load. It multiplies the per-patch amount, so the
        /// saving is in blades drawn rather than in patches placed — which is
        /// where the cost actually is.
        /// </remarks>
        private static void ApplyClutter()
        {
            var system = ClutterSystem.instance;
            if (system != _clutterSystem)
            {
                RestoreClutter();
                _clutterSystem = system;
                if (system != null && ClutterScale != null)
                {
                    _originalClutter = (float)ClutterScale.GetValue(system);
                    _clutterCaptured = true;
                }
            }
            if (system == null || ClutterScale == null || !_clutterCaptured) return;

            if (Plugin.ClutterAmount.Value <= 0f)
            {
                if (_clutterApplied) RestoreClutterValue();
                else _originalClutter = (float)ClutterScale.GetValue(system);
                return;
            }

            if (!_clutterApplied)
                _originalClutter = (float)ClutterScale.GetValue(system);
            float wanted = Mathf.Clamp(Plugin.ClutterAmount.Value, 0.05f, 2f);
            if (!Mathf.Approximately((float)ClutterScale.GetValue(system), wanted))
            {
                ClutterScale.SetValue(system, wanted);
                // m_amountScale is consumed only while a patch is generated;
                // existing grass otherwise keeps the old amount indefinitely
                // while the player stands still.
                system.ClearAll();
            }
            _clutterApplied = true;
        }

        /// <summary>
        /// Thins the Mistlands fog.
        /// </summary>
        /// <remarks>
        /// The mist is drawn twice: a dense volume near the player and a cheaper
        /// billboard field beyond it. Both are scaled together so the join does
        /// not become visible, which is what happens when only the near one is
        /// reduced — a clear bubble with a wall of fog at its edge.
        /// </remarks>
        private static void ApplyMist()
        {
            var mist = ParticleMist.instance;
            if (mist != _mist)
            {
                RestoreMist();
                _mist = mist;
                if (mist != null)
                {
                    _originalLocalEmission = mist.m_localEmission;
                    _originalLocalEmissionPerUnit = mist.m_localEmissionPerUnit;
                    _originalDistantRange = mist.m_distantMaxRange;
                    _mistCaptured = true;
                }
            }
            if (mist == null || !_mistCaptured) return;

            if (Plugin.MistDensity.Value <= 0f)
            {
                if (_mistApplied) RestoreMistValues();
                else
                {
                    _originalLocalEmission = mist.m_localEmission;
                    _originalLocalEmissionPerUnit = mist.m_localEmissionPerUnit;
                    _originalDistantRange = mist.m_distantMaxRange;
                }
                return;
            }

            if (!_mistApplied)
            {
                _originalLocalEmission = mist.m_localEmission;
                _originalLocalEmissionPerUnit = mist.m_localEmissionPerUnit;
                _originalDistantRange = mist.m_distantMaxRange;
            }
            float scale = Mathf.Clamp(Plugin.MistDensity.Value, 0.05f, 1f);
            int localEmission = Mathf.Max(0,
                Mathf.RoundToInt(_originalLocalEmission * scale));
            int localEmissionPerUnit = Mathf.Max(0,
                Mathf.RoundToInt(_originalLocalEmissionPerUnit * scale));
            float distantRange = _originalDistantRange * scale;
            if (mist.m_localEmission != localEmission)
                mist.m_localEmission = localEmission;
            if (mist.m_localEmissionPerUnit != localEmissionPerUnit)
                mist.m_localEmissionPerUnit = localEmissionPerUnit;
            if (!Mathf.Approximately(mist.m_distantMaxRange, distantRange))
                mist.m_distantMaxRange = distantRange;
            _mistApplied = true;
        }

        private static void RestoreClutterValue()
        {
            if (!_clutterApplied || !_clutterCaptured || _clutterSystem == null
                || ClutterScale == null) return;
            if (!Mathf.Approximately(
                    (float)ClutterScale.GetValue(_clutterSystem), _originalClutter))
            {
                ClutterScale.SetValue(_clutterSystem, _originalClutter);
                _clutterSystem.ClearAll();
            }
            _clutterApplied = false;
        }

        private static void RestoreClutter()
        {
            RestoreClutterValue();
            _clutterSystem = null;
            _clutterCaptured = false;
        }

        private static void RestoreMistValues()
        {
            if (!_mistApplied || !_mistCaptured || _mist == null) return;
            _mist.m_localEmission = _originalLocalEmission;
            _mist.m_localEmissionPerUnit = _originalLocalEmissionPerUnit;
            _mist.m_distantMaxRange = _originalDistantRange;
            _mistApplied = false;
        }

        private static void RestoreMist()
        {
            RestoreMistValues();
            _mist = null;
            _mistCaptured = false;
        }

        public static void Restore()
        {
            if (_captured)
            {
                if (_lodApplied) QualitySettings.lodBias = _originalLodBias;
                if (_shadowDistanceApplied)
                    QualitySettings.shadowDistance = _originalShadowDistance;
                if (_shadowCascadesApplied)
                    QualitySettings.shadowCascades = _originalShadowCascades;
                if (_softParticlesApplied)
                    QualitySettings.softParticles = _originalSoftParticles;
                if (_lightLimitApplied) LightLod.m_lightLimit = _originalLightLimit;
                if (_shadowLimitApplied) LightLod.m_shadowLimit = _originalShadowLimit;
            }
            RestoreClutter();
            RestoreMist();
            _lodApplied = _shadowDistanceApplied = _shadowCascadesApplied = false;
            _softParticlesApplied = _lightLimitApplied = _shadowLimitApplied = false;
        }
    }
}
