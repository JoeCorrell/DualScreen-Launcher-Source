using UnityEngine;

namespace ValheimPerformance
{
    /// <summary>Visual-neutral Unity runtime switches with reversible state.</summary>
    internal static class RuntimeTuning
    {
        private static bool _capturedCollisionCallbacks;
        private static bool _originalCollisionCallbacks;
        private static bool _capturedThreadedTextures;
        private static bool _originalThreadedTextures;
        private static bool _loggedCollisionCallbacks;
        private static bool _loggedThreadedTextures;
        private static bool _collisionCallbacksUnavailable;
        private static bool _threadedTexturesUnavailable;
        private static bool _lastCollisionValue;
        private static bool _lastThreadedTextureValue;

        /// <summary>
        /// True only when Unity is actually reusing callback wrappers. The
        /// contact-array patch uses this instead of trusting the requested
        /// config value on renderers where the property is unavailable.
        /// </summary>
        internal static bool ReusingCollisionCallbacks { get; private set; }

        public static void Apply()
        {
            ApplyCollisionCallbacks();
            ApplyThreadedTextures();
        }

        private static void ApplyCollisionCallbacks()
        {
            if (_collisionCallbacksUnavailable) return;
            try
            {
                if (!_capturedCollisionCallbacks)
                {
                    _originalCollisionCallbacks = Physics.reuseCollisionCallbacks;
                    _capturedCollisionCallbacks = true;
                }

                bool wanted = Plugin.ReuseCollisionCallbacks.Value
                    ? true : _originalCollisionCallbacks;
                if (Physics.reuseCollisionCallbacks != wanted)
                    Physics.reuseCollisionCallbacks = wanted;
                _lastCollisionValue = wanted;

                ReusingCollisionCallbacks = Physics.reuseCollisionCallbacks;

                if (Plugin.ReuseCollisionCallbacks.Value
                    && ReusingCollisionCallbacks
                    && !_loggedCollisionCallbacks)
                {
                    _loggedCollisionCallbacks = true;
                    Plugin.Log.LogInfo("[Perf] Reusing managed physics collision callbacks.");
                }
            }
            catch (System.Exception error)
            {
                Plugin.Log.LogWarning("[Perf] Collision callback tuning unavailable: "
                                      + error.Message);
                _capturedCollisionCallbacks = false;
                _collisionCallbacksUnavailable = true;
                ReusingCollisionCallbacks = false;
            }
        }

        private static void ApplyThreadedTextures()
        {
            if (_threadedTexturesUnavailable) return;
            try
            {
                if (!_capturedThreadedTextures)
                {
                    _originalThreadedTextures = Texture.allowThreadedTextureCreation;
                    _capturedThreadedTextures = true;
                }

                bool wanted = Plugin.ThreadedTextureCreation.Value
                    ? true : _originalThreadedTextures;
                bool changed = Texture.allowThreadedTextureCreation != wanted;
                if (changed)
                    Texture.allowThreadedTextureCreation = wanted;
                _lastThreadedTextureValue = wanted;

                // This Unity version already defaults to threaded creation on
                // supported renderers. Log only when the mod made a real change,
                // rather than advertising a no-op as an optimization.
                if (changed && wanted && !_loggedThreadedTextures)
                {
                    _loggedThreadedTextures = true;
                    Plugin.Log.LogInfo("[Perf] Threaded texture creation enabled.");
                }
            }
            catch (System.Exception error)
            {
                Plugin.Log.LogWarning("[Perf] Threaded texture creation unavailable: "
                                      + error.Message);
                _capturedThreadedTextures = false;
                _threadedTexturesUnavailable = true;
            }
        }

        public static void Restore()
        {
            try
            {
                if (_capturedCollisionCallbacks
                    && Physics.reuseCollisionCallbacks
                    == _lastCollisionValue)
                    Physics.reuseCollisionCallbacks = _originalCollisionCallbacks;
            }
            catch { }

            try
            {
                if (_capturedThreadedTextures
                    && Texture.allowThreadedTextureCreation
                    == _lastThreadedTextureValue)
                    Texture.allowThreadedTextureCreation = _originalThreadedTextures;
            }
            catch { }

            _capturedCollisionCallbacks = false;
            _capturedThreadedTextures = false;
            ReusingCollisionCallbacks = false;
        }
    }
}
