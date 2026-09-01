using System.Collections;
using UnityEngine;

namespace ValheimPerformance
{
    /// <summary>Prepares loaded shader variants in bounded loading-frame slices.</summary>
    internal static class Warmup
    {
        private static bool _running;
        private static bool _done;

        public static IEnumerator Run()
        {
            if (_running || _done || !Plugin.WarmShaders.Value) yield break;
            _running = true;
            var wait = new WaitForSecondsRealtime(0.20f);

            // BepInEx starts before the world prefab databases. Waiting here is
            // what makes the collection search useful instead of a one-shot scan
            // of the main menu's small material set.
            while (ObjectDB.instance == null || ZNetScene.instance == null)
            {
                if (!Plugin.WarmShaders.Value)
                {
                    _running = false;
                    yield break;
                }
                yield return wait;
            }

            int budget = Mathf.Clamp(Plugin.ShaderVariantsPerFrame.Value, 0, 128);
            if (budget <= 0)
            {
                _running = false;
                yield break;
            }

            // A scene can publish its databases a few frames before its asset
            // bundles finish registering their shader collections. Give that
            // hand-off a bounded window instead of permanently missing warmup.
            ShaderVariantCollection[]? collections = null;
            for (int attempt = 0; attempt < 20; attempt++)
            {
                yield return attempt == 0 ? null : wait;
                collections = Resources.FindObjectsOfTypeAll<ShaderVariantCollection>();
                if (collections != null && collections.Length > 0) break;
            }
            if (collections == null || collections.Length == 0)
            {
                Plugin.Log.LogInfo("[Perf] No loaded shader variant collection to warm.");
                _running = false;
                yield break;
            }

            int warmedCollections = 0;
            int warmedVariants = 0;
            foreach (var collection in collections)
            {
                if (collection == null || collection.isWarmedUp) continue;
                int before = collection.warmedUpVariantCount;
                bool complete = false;
                bool failed = false;
                while (!complete && !failed && collection != null
                       && Plugin.WarmShaders.Value)
                {
                    try
                    {
                        complete = collection.WarmUpProgressively(budget);
                    }
                    catch (System.Exception error)
                    {
                        failed = true;
                        Plugin.Log.LogWarning("[Perf] Could not warm " + collection.name
                                              + ": " + error.Message);
                    }
                    if (!complete && !failed) yield return null;
                }

                if (!failed && collection != null)
                {
                    warmedVariants += Mathf.Max(0,
                        collection.warmedUpVariantCount - before);
                    if (collection.isWarmedUp) warmedCollections++;
                }

                yield return null;
            }

            _done = Plugin.WarmShaders.Value;
            _running = false;
            Plugin.Log.LogInfo("[Perf] Progressively warmed " + warmedVariants
                               + " variants in " + warmedCollections
                               + " collection(s).");
        }
    }
}
