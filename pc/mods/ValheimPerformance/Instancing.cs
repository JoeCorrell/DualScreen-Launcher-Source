using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace ValheimPerformance
{
    /// <summary>Enables instancing on compatible opaque world materials.</summary>
    internal static class Instancing
    {
        private static readonly List<Material> Changed = new List<Material>();
        private static bool _applied;
        private static bool _unsupported;
        private static ObjectDB? _database;
        private static ZNetScene? _scene;

        /// <summary>
        /// Waits for Valheim's prefab databases, then applies once to the world
        /// material set. It also makes a live config disable reversible.
        /// </summary>
        public static IEnumerator Run()
        {
            var wait = new WaitForSecondsRealtime(0.50f);
            while (true)
            {
                bool enabled = Plugin.EnableInstancing.Value;
                bool worldReady = ObjectDB.instance != null && ZNetScene.instance != null;
                bool worldChanged = worldReady
                    && (!ReferenceEquals(_database, ObjectDB.instance)
                        || !ReferenceEquals(_scene, ZNetScene.instance));
                if (enabled && worldReady && (!_applied || worldChanged) && !_unsupported)
                {
                    yield return null;
                    _database = ObjectDB.instance;
                    _scene = ZNetScene.instance;
                    Apply();
                }
                else if (!enabled && _applied)
                {
                    Restore();
                }
                yield return wait;
            }
        }

        private static void Apply()
        {
            if (_unsupported) return;
            if (!SystemInfo.supportsInstancing)
            {
                _unsupported = true;
                Plugin.Log.LogInfo("[Perf] GPU instancing is unavailable on this renderer.");
                return;
            }
            _applied = true;
            int enabled = 0;
            int considered = 0;

            foreach (var material in Resources.FindObjectsOfTypeAll<Material>())
            {
                if (material == null || material.shader == null) continue;
                considered++;
                if (material.enableInstancing || !Eligible(material)) continue;

                try
                {
                    material.enableInstancing = true;
                    Changed.Add(material);
                    enabled++;
                }
                catch (Exception error)
                {
                    Plugin.Log.LogDebug("[Perf] Instancing skipped for "
                                        + material.name + ": " + error.Message);
                }
            }

            Plugin.Log.LogInfo("[Perf] Instancing enabled on " + enabled
                               + " of " + considered
                               + " loaded materials after world initialization.");
        }

        private static bool Eligible(Material material)
        {
            Shader shader = material.shader;
            if (shader == null || !shader.isSupported) return false;

            // UI, particles and transparent effects do not form useful world
            // batches and are the most likely to rely on non-instanced variants.
            if (material.renderQueue >= 3000) return false;
            string name = shader.name ?? "";
            if (name.StartsWith("UI/", StringComparison.OrdinalIgnoreCase)
                || name.StartsWith("Hidden/", StringComparison.OrdinalIgnoreCase)
                || name.IndexOf("TextMeshPro", StringComparison.OrdinalIgnoreCase) >= 0
                || name.IndexOf("Sprite", StringComparison.OrdinalIgnoreCase) >= 0
                || name.IndexOf("Particle", StringComparison.OrdinalIgnoreCase) >= 0)
                return false;

            string disabled = material.GetTag("DisableBatching", true, "False");
            return !string.Equals(disabled, "True", StringComparison.OrdinalIgnoreCase);
        }

        public static void Restore()
        {
            foreach (Material material in Changed)
            {
                try
                {
                    if (material != null) material.enableInstancing = false;
                }
                catch { }
            }
            Changed.Clear();
            _applied = false;
            _database = null;
            _scene = null;
        }
    }
}
