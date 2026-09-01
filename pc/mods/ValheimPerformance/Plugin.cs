using BepInEx;
using BepInEx.Configuration;
using HarmonyLib;
using UnityEngine;

namespace ValheimPerformance
{
    /// <summary>Targeted, visual-neutral hot-path fixes for Valheim.</summary>
    /// <remarks>
    /// <para>
    /// The default profile contains only changes tied to concrete Valheim hot
    /// paths and whose final game state is equivalent to vanilla. Experimental
    /// Unity pipeline trades and behavior-changing throttles remain available,
    /// but are deliberately disabled until a controlled A/B run proves them on
    /// the current renderer and workload.
    /// </para>
    /// <para>
    /// The second half buys frames with quality, and every one of those is off
    /// by default and says on its own line what it costs. That split is the
    /// point of the file: a performance mod that quietly dims the game is
    /// indistinguishable from a broken one, and the person who installed it has
    /// no way to tell which they have.
    /// </para>
    /// <para>
    /// Scene-bound quality objects are checked on a slow timer because a world
    /// transition replaces them. Pipeline settings are applied at startup and
    /// from Valheim's graphics-settings event; they are not polled every frame.
    /// </para>
    /// </remarks>
    [BepInPlugin(Guid, "Valheim Performance", PluginVersion)]
    public sealed class Plugin : BaseUnityPlugin
    {
        public const string Guid = "com.profmags.valheimperformance";
        public const string PluginVersion = "1.5.0";

        public static Plugin Instance { get; private set; } = null!;
        public static new BepInEx.Logging.ManualLogSource Log { get; private set; } = null!;

        // ---- runtime features (safe defaults plus opt-in experiments) ----
        public static ConfigEntry<bool> WarmShaders = null!;
        public static ConfigEntry<bool> EnableInstancing = null!;
        public static ConfigEntry<bool> ThrottleDistantUpdates = null!;
        public static ConfigEntry<bool> SmokeCollisions = null!;
        public static ConfigEntry<bool> CacheCharacterColors = null!;
        public static ConfigEntry<bool> CacheDryWaterChecks = null!;
        public static ConfigEntry<bool> ReuseCollisionCallbacks = null!;
        public static ConfigEntry<bool> ThreadedTextureCreation = null!;
        public static ConfigEntry<bool> OptimizeAutoPickup = null!;
        public static ConfigEntry<bool> OptimizeEquipmentModifiers = null!;
        public static ConfigEntry<bool> CacheBodyFriction = null!;
        public static ConfigEntry<bool> OptimizeZPackage = null!;
        public static ConfigEntry<bool> AvoidMaterialClones = null!;
        public static ConfigEntry<bool> OptimizeWeaponTrails = null!;
        public static ConfigEntry<bool> OptimizeProjectileQueries = null!;
        public static ConfigEntry<bool> OptimizeAnimationSync = null!;
        public static ConfigEntry<bool> OptimizeAudioUpdates = null!;
        public static ConfigEntry<bool> RemoveDuplicateEnvShaderWrite = null!;
        public static ConfigEntry<bool> OptimizeMeshUploads = null!;
        public static ConfigEntry<bool> OptimizePlacementGhost = null!;

        // ---- diagnostics: off unless collecting a controlled comparison ----
        public static ConfigEntry<bool> DiagnosticsEnabled = null!;
        public static ConfigEntry<int> DiagnosticsWindowSeconds = null!;

        // ---- opt-in render-pipeline trades ----
        public static ConfigEntry<int> QueuedFrames = null!;
        public static ConfigEntry<bool> WidenUploadBudget = null!;
        public static ConfigEntry<int> UploadTimeSlice = null!;
        public static ConfigEntry<int> UploadBufferSize = null!;
        public static ConfigEntry<bool> SpreadGarbageCollection = null!;
        public static ConfigEntry<int> GcSliceMicroseconds = null!;
        public static ConfigEntry<int> ShaderVariantsPerFrame = null!;

        // ---- opt-in distance throttles ----
        public static ConfigEntry<float> SmokeDistance = null!;
        public static ConfigEntry<int> SmokeStride = null!;
        public static ConfigEntry<float> LightDistance = null!;
        public static ConfigEntry<int> LightStride = null!;
        public static ConfigEntry<float> VisualsDistance = null!;
        public static ConfigEntry<int> VisualsStride = null!;

        // ---- paid: visible ----
        public static ConfigEntry<float> LodBias = null!;
        public static ConfigEntry<float> ShadowDistance = null!;
        public static ConfigEntry<int> ShadowCascades = null!;
        public static ConfigEntry<float> ClutterAmount = null!;
        public static ConfigEntry<int> LightLimit = null!;
        public static ConfigEntry<int> ShadowLimit = null!;
        public static ConfigEntry<float> MistDensity = null!;
        public static ConfigEntry<bool> SoftParticles = null!;

        private static ConfigEntry<int> ConfigSchemaVersion = null!;

        private Harmony? _harmony;
        private float _nextApply;

        private void Awake()
        {
            Instance = this;
            Log = Logger;
            Bind();

            _harmony = new Harmony(Guid);
            InstallPatches();

            GraphicsSettingsManager.GraphicsSettingsChanged += OnGraphicsSettingsChanged;

            StartCoroutine(Instancing.Run());
            StartCoroutine(Warmup.Run());
            RuntimeTuning.Apply();
            Pipeline.Apply();
            Quality.Apply();

            bool runtimeTweaksEnabled = WarmShaders.Value
                || EnableInstancing.Value
                || ThrottleDistantUpdates.Value
                || SmokeCollisions.Value
                || CacheCharacterColors.Value
                || CacheDryWaterChecks.Value
                || ReuseCollisionCallbacks.Value
                || ThreadedTextureCreation.Value
                || OptimizeAutoPickup.Value
                || OptimizeEquipmentModifiers.Value
                || CacheBodyFriction.Value
                || OptimizeZPackage.Value
                || AvoidMaterialClones.Value
                || OptimizeWeaponTrails.Value
                || OptimizeProjectileQueries.Value
                || OptimizeAnimationSync.Value
                || OptimizeAudioUpdates.Value
                || RemoveDuplicateEnvShaderWrite.Value
                || OptimizeMeshUploads.Value
                || OptimizePlacementGhost.Value
                || QueuedFrames.Value > 0
                || WidenUploadBudget.Value
                || SpreadGarbageCollection.Value;
            Log.LogInfo("[Perf] Loaded. Runtime optimizations "
                        + (runtimeTweaksEnabled ? "on" : "off")
                        + "; quality tweaks "
                        + (Quality.AnyPaidTweakEnabled ? "ON — the picture will change" : "off"));
        }

        private void OnDestroy()
        {
            GraphicsSettingsManager.GraphicsSettingsChanged -= OnGraphicsSettingsChanged;
            Instancing.Restore();
            SmokePhysics.Restore();
            RuntimeTuning.Restore();
            Pipeline.Restore();
            Quality.Restore();
            _harmony?.UnpatchSelf();
        }

        /// <summary>
        /// Applies Harmony features independently so one method renamed by a
        /// game update cannot prevent the pipeline and every unrelated patch
        /// from loading.
        /// </summary>
        private void InstallPatches()
        {
            if (_harmony == null) return;

            var patchTypes = new[]
            {
                typeof(FarThrottle.SmokePatch),
                typeof(FarThrottle.LightFlickerPatch),
                typeof(FarThrottle.VisEquipmentPatch),
                typeof(VisualCache.UpdateColorsPatch),
                typeof(CollisionContacts.GameCollisionPatch),
                typeof(DryWaterTriggerCache.WaterTriggerPatch),
                typeof(ValheimHotPaths.AutoPickupPatch),
                typeof(ValheimHotPaths.EquipmentModifiersPatch),
                typeof(ValheimHotPaths.BodyFrictionPatch),
                typeof(ValheimHotPaths.ZPackageWritePatch),
                typeof(ValheimHotPaths.ZPackageReadPatch),
                typeof(MaterialCloneFixes.MaterialVariationPatch),
                typeof(MaterialCloneFixes.MaterialFaderPatch),
                typeof(WeaponTrailBuffers.Patch),
                typeof(PlacementGhostBuffers.Patch),
                typeof(ProjectileQueries.Patch),
                typeof(MeshAllocationFixes.HeightmapIndicesPatch),
                typeof(MeshAllocationFixes.LiquidPostBuildIndicesPatch),
                typeof(MeshAllocationFixes.LiquidRebuildIndicesPatch),
                typeof(MeshAllocationFixes.HeightmapModifierBuffersPatch),
                typeof(TrailSmoothingBuffers.Patch),
                typeof(AnimationAudioState.AnimationPatch),
                typeof(AnimationAudioState.ZsfxPatch),
                typeof(AnimationAudioState.ZsfxResetPatch),
                typeof(AnimationAudioState.ShieldHumPatch),
                typeof(AnimationAudioState.EnvShaderPatch)
            };

            foreach (System.Type patchType in patchTypes)
            {
                try
                {
                    _harmony.CreateClassProcessor(patchType).Patch();
                }
                catch (System.Exception error)
                {
                    Log.LogWarning("[Perf] Skipped incompatible patch "
                        + patchType.Name + ": " + error.Message);
                }
            }
        }

        private static void OnGraphicsSettingsChanged()
        {
            // The game has just written its selected values. Save those as the
            // restore target before reasserting any explicit mod override.
            Quality.CaptureGameSettings();
            Pipeline.CaptureGameQueueDepth();
            Pipeline.Apply();
            Quality.Apply();
        }

        /// <summary>
        /// Applies values changed by an in-game settings surface immediately.
        /// Public so optional companion mods can invoke it without taking a
        /// compile-time dependency on this assembly.
        /// </summary>
        public void ApplySettingsNow()
        {
            Pipeline.Apply();
            Quality.Apply();
            RuntimeTuning.Apply();
            SmokePhysics.Apply(SmokeCollisions.Value);
            if (WarmShaders.Value && ShaderVariantsPerFrame.Value > 0)
                StartCoroutine(Warmup.Run());
        }

        /// <summary>
        /// Binds the settings, and survives not being able to.
        /// </summary>
        /// <remarks>
        /// <para>
        /// The config file lives on Android's shared storage, which the guest
        /// reaches through Wine's view of a FUSE mount. Opening it read-write
        /// there can fail with a sharing violation for reasons that have nothing
        /// to do with this mod and cannot be predicted from here — and it did,
        /// on the launch after a version added new keys and so turned a read
        /// into a rewrite.
        /// </para>
        /// <para>
        /// That threw out of <c>Awake</c>, which meant BepInEx dropped the whole
        /// plugin: no queue depth, no throttles, nothing, and the only sign was
        /// one line in a log nobody reads unless something already looks wrong.
        /// A performance mod that silently does not run is the worst failure
        /// available to it, because the game still works and the frame rate is
        /// merely disappointing.
        /// </para>
        /// <para>
        /// So a config that cannot be opened costs the settings, not the mod.
        /// The fallback binds against a path that is never written and never
        /// read, which yields every default with no file access at all.
        /// </para>
        /// </remarks>
        private void Bind()
        {
            try
            {
                BindTo(Config);
                return;
            }
            catch (System.Exception error)
            {
                Log.LogWarning("[Perf] Could not open the config file (" + error.Message
                               + "). Running on defaults; edits there will be ignored "
                               + "until it can be opened again.");
            }

            var defaults = new ConfigFile(Config.ConfigFilePath + ".defaults", false);
            defaults.SaveOnConfigSet = false;
            BindTo(defaults);
        }

        private void BindTo(ConfigFile config)
        {
            bool saveOnSet = config.SaveOnConfigSet;
            config.SaveOnConfigSet = false;
            try
            {
            WarmShaders = config.Bind("Experimental", "WarmShaders", false,
                "Attempts to warm a loaded ShaderVariantCollection. Valheim did not "
                + "have one loaded in device traces, so this is disabled by default.");

            EnableInstancing = config.Bind("Experimental", "EnableInstancing", false,
                "Forces instancing on broad material classes. Disabled because it "
                + "can replace dynamic batching and compile missing shader variants; "
                + "enable only for an instrumented draw-call A/B test.");

            ThrottleDistantUpdates = config.Bind("Experimental", "ThrottleDistantUpdates", false,
                "Reduces update frequency for distant smoke, lights and equipment. "
                + "This changes timing and can delay visible state, so it is off.");

            SmokeCollisions = config.Bind("Experimental", "DisableSmokeSelfCollision", false,
                "Every puff of smoke is a physics body, and the game keeps up to a "
                + "hundred alive near fires. Disabling smoke-to-smoke collision may "
                + "alter plume routing and smoke exposure, so it is opt-in only.");

            CacheCharacterColors = config.Bind("Free", "CacheCharacterColors", true,
                "Stops Valheim rebuilding player skin and hair material arrays every "
                + "frame when neither colour nor model has changed. Equipment and "
                + "colour changes still appear immediately. The picture is unchanged.");

            CacheDryWaterChecks = config.Bind("Experimental", "CacheDryWaterChecks", false,
                "Avoids repeating the same native water-volume overlap every frame "
                + "for stationary dry triggers, but can delay a wave splash. Off by "
                + "default to preserve frame-accurate water behavior.");

            ReuseCollisionCallbacks = config.Bind("Free", "ReuseCollisionCallbacks", true,
                "Lets Unity reuse managed Collision wrappers and exact-size contact "
                + "arrays. Valheim consumes both synchronously, so this removes "
                + "steady physics garbage without changing collision results.");

            ThreadedTextureCreation = config.Bind("Free", "ThreadedTextureCreation", true,
                "Allows supported graphics backends to create textures away from the "
                + "render thread. Unity already enables this on many renderers; the "
                + "mod logs only when it makes an actual change.");

            OptimizeAutoPickup = config.Bind("Valheim hot paths", "OptimizeAutoPickup", true,
                "Reuses a growing non-allocating physics query for the local player's "
                + "50 Hz auto-pickup scan without truncating or changing its results.");

            OptimizeEquipmentModifiers = config.Bind("Valheim hot paths", "OptimizeEquipmentModifiers", true,
                "Replaces per-fixed-tick reflection and float boxing for equipped item "
                + "modifiers with direct reads in the same item and modifier order.");

            CacheBodyFriction = config.Bind("Valheim hot paths", "CacheBodyFriction", true,
                "Skips identical PhysicMaterial writes made for every owned character "
                + "on every physics tick, with periodic compatibility verification.");

            OptimizeZPackage = config.Bind("Valheim hot paths", "OptimizeZPackage", true,
                "Avoids temporary full-payload byte arrays when Valheim nests ZPackage "
                + "data, including the per-ZDO multiplayer synchronization path.");

            AvoidMaterialClones = config.Bind("Valheim hot paths", "AvoidMaterialClones", true,
                "Uses shared-material getters where Valheim only reads defaults or "
                + "selects an existing material, avoiding needless renderer clones.");

            OptimizeWeaponTrails = config.Bind("Valheim hot paths", "OptimizeWeaponTrails", true,
                "Reuses the four arrays Valheim recreates on every active melee-trail "
                + "physics tick; array lengths and final mesh data remain identical.");

            OptimizeProjectileQueries = config.Bind("Valheim hot paths", "OptimizeProjectileQueries", true,
                "Reuses growable physics-query buffers for active projectiles instead "
                + "of allocating RaycastHit arrays every fixed tick. The complete hit "
                + "set is still distance-sorted exactly as in vanilla.");

            OptimizeAnimationSync = config.Bind("Valheim hot paths", "OptimizeAnimationSync", true,
                "Skips identical native Animator writes for remote synchronized "
                + "parameters. Damped movement parameters still update every physics "
                + "tick, and cached state is periodically verified.");

            OptimizeAudioUpdates = config.Bind("Valheim hot paths", "OptimizeAudioUpdates", true,
                "Skips exact duplicate AudioSource volume, pitch and stopped-state "
                + "writes while retaining every actual audio transition.");

            RemoveDuplicateEnvShaderWrite = config.Bind("Valheim hot paths", "RemoveDuplicateEnvShaderWrite", true,
                "Removes Valheim's second consecutive write of the same sky-sun "
                + "shader vector in EnvMan.SetEnv; the resulting shader state is identical.");

            OptimizeMeshUploads = config.Bind("Valheim hot paths", "OptimizeMeshUploads", true,
                "Passes existing terrain and liquid index lists directly to Unity "
                + "instead of allocating full duplicate index arrays during mesh rebuilds.");

            OptimizePlacementGhost = config.Bind("Valheim hot paths", "OptimizePlacementGhost", true,
                "Reuses exact collider results for Valheim's per-frame build preview "
                + "without reducing placement checks or caching world state.");

            DiagnosticsEnabled = config.Bind("Diagnostics", "Enabled", false,
                "Collects bounded frame-time and GC statistics for controlled A/B "
                + "tests. Keep off during normal play to avoid measurement overhead.");

            DiagnosticsWindowSeconds = BindRange(config, "Diagnostics", "WindowSeconds", 120,
                "Length of each diagnostics reporting window.", 15, 600);

            QueuedFrames = BindRange(config, "Pipeline", "MaxQueuedFrames", 0,
                "How many frames the CPU may run ahead of the GPU. The game asks for "
                + "2. On a translated D3D-to-Vulkan stack, 3 can improve overlap "
                + "when the render thread otherwise waits for retirement, but the "
                + "result is workload-dependent and costs up to one frame of input "
                + "latency. 0 leaves the game's value alone; 4 is Unity's maximum.", 0, 4);

            WidenUploadBudget = config.Bind("Pipeline", "WidenUploadBudget", false,
                "Uses a larger persistent ring for streamed texture and mesh uploads "
                + "to avoid buffer growth and synchronization as zones load. The "
                + "time slice below controls the separate load-time/frame-time "
                + "tradeoff. The picture is unchanged.");

            UploadTimeSlice = BindRange(config, "Pipeline", "UploadTimeSliceMs", 2,
                "Milliseconds per frame available for streamed uploads. Unity's "
                + "default is 2. Raising this shortens streaming time but reserves "
                + "more of each frame for uploads; it is not a free FPS increase.", 1, 33);

            UploadBufferSize = BindRange(config, "Pipeline", "UploadBufferSizeMb", 32,
                "Megabytes held permanently for the upload ring. Unity's default is "
                + "4. This is real memory, not a budget.", 2, 512);

            SpreadGarbageCollection = config.Bind("Pipeline", "SpreadGarbageCollection", false,
                "Where the runtime supports incremental collection, caps how long a "
                + "single frame may spend collecting so the work lands across many "
                + "frames instead of one. Does not reduce the total work — trades a "
                + "periodic dropped frame for a slightly lower steady rate.");

            GcSliceMicroseconds = BindRange(config, "Pipeline", "GcSliceMicroseconds", 1500,
                "The cap above, in microseconds. Smaller spreads collection further "
                + "at the cost of collecting more slowly overall.", 100, 8000);

            ShaderVariantsPerFrame = BindRange(config, "Pipeline", "ShaderVariantsPerFrame", 0,
                "How many shader variants are prepared per loading-screen frame. "
                + "Smaller values reduce each warmup slice; 0 disables warmup.", 0, 128);

            SmokeDistance = BindRange(config, "Distance", "SmokeDistance", 50f,
                "Metres past which smoke updates at the reduced rate. Its lifetime "
                + "still advances at wall-clock speed, so it expires on schedule.", 0f, 500f);

            SmokeStride = BindRange(config, "Distance", "SmokeStride", 3,
                "One frame in this many, for distant smoke. 1 disables.", 1, 60);

            LightDistance = BindRange(config, "Distance", "LightDistance", 30f,
                "Metres past which a flickering light stops being re-jittered. The "
                + "movement it is skipping is a tenth of a metre, which at this "
                + "range is well under a pixel.", 0f, 500f);

            LightStride = BindRange(config, "Distance", "LightStride", 4,
                "One frame in this many, for distant lights. 1 disables.", 1, 60);

            VisualsDistance = BindRange(config, "Distance", "CharacterVisualsDistance", 24f,
                "Metres past which a character's appearance is refreshed at the "
                + "reduced rate. Kept short on purpose: this governs what someone "
                + "is wearing, and a weapon swap at the edge of view should not "
                + "visibly happen late.", 0f, 500f);

            VisualsStride = BindRange(config, "Distance", "CharacterVisualsStride", 4,
                "One frame in this many, for distant characters. 1 disables.", 1, 60);

            LodBias = BindRange(config, "Quality", "LodBias", 0f,
                "Above 1 keeps detailed models further away; below 1 swaps to "
                + "simpler ones sooner and shows visible pop-in. 0 leaves the "
                + "game's own value alone.", 0f, 5f);

            ShadowDistance = BindRange(config, "Quality", "ShadowDistance", 0f,
                "How far shadows are drawn, in metres. Distant shadows are the "
                + "single most expensive thing on screen in a forest. 0 leaves the "
                + "game's value alone; 80 is a large saving and visible on hillsides.", 0f, 1000f);

            ShadowCascades = BindRange(config, "Quality", "ShadowCascades", 0,
                "Shadow map splits: 4 is the game's, 2 costs less and coarsens "
                + "shadow edges at distance. 0 leaves it alone.", 0, 4);

            ClutterAmount = BindRange(config, "Quality", "ClutterAmount", 0f,
                "Grass and ground cover density, as a multiplier. 0.5 halves it "
                + "and is plainly visible on open ground. 0 leaves it alone.", 0f, 2f);

            LightLimit = BindRange(config, "Quality", "LightLimit", -1,
                "How many dynamic lights may be lit at once. A base with many "
                + "torches is where this pays. -1 is the game's unlimited.", -1, 128);

            ShadowLimit = BindRange(config, "Quality", "ShadowLimit", -1,
                "How many of those lights may cast shadows. This is the large one "
                + "indoors, and it is obvious: torches stop casting. -1 is unlimited.", -1, 128);

            MistDensity = BindRange(config, "Quality", "MistDensity", 0f,
                "Mistlands fog density as a multiplier. Cheap frames and a changed "
                + "biome — the mist is the point of the place. 0 leaves it alone.", 0f, 1f);

            SoftParticles = config.Bind("Quality", "SoftParticles", true,
                "Fades particles where they meet geometry. Turning it off removes "
                + "a depth read per particle and leaves hard edges where smoke "
                + "meets the ground.");

            ConfigSchemaVersion = config.Bind("Internal", "ConfigSchemaVersion", 0,
                "Internal migration marker. Do not edit.");
            bool migrated = MigrateUnsafeLegacyDefaults();

            // Android shared storage is exposed through Wine/FUSE. Rewriting
            // the whole file for every Bind/Value assignment caused sharing
            // violations in prior releases. Persist at most once, and only if
            // a schema migration actually changed persisted behavior. Missing
            // default-valued keys need not be written until the user edits them.
            if (saveOnSet && migrated)
            {
                try
                {
                    config.Save();
                }
                catch (System.Exception error)
                {
                    // The entries are already valid in memory. A failed FUSE
                    // rewrite must not discard the user's loaded choices and
                    // rebound everything to defaults for this session.
                    Log.LogWarning("[Perf] Could not persist the migrated config: "
                        + error.Message + ". Using the migrated values in memory.");
                }
            }
            }
            finally
            {
                config.SaveOnConfigSet = saveOnSet;
            }
        }

        private static ConfigEntry<T> BindRange<T>(ConfigFile config, string section,
            string key, T value, string description, T minimum, T maximum)
            where T : System.IComparable
        {
            return config.Bind(section, key, value,
                new ConfigDescription(description,
                    new AcceptableValueRange<T>(minimum, maximum)));
        }

        /// <summary>
        /// Version 1.4 shipped speculative pipeline/physics defaults and some
        /// installed configs retained visible quality reductions. The current
        /// contract is visual-neutral, so a one-time migration explicitly
        /// returns every such switch to vanilla behavior.
        /// </summary>
        private static bool MigrateUnsafeLegacyDefaults()
        {
            const int currentSchema = 2;
            if (ConfigSchemaVersion.Value >= currentSchema) return false;

            WarmShaders.Value = false;
            EnableInstancing.Value = false;
            ThrottleDistantUpdates.Value = false;
            SmokeCollisions.Value = false;
            CacheDryWaterChecks.Value = false;
            QueuedFrames.Value = 0;
            WidenUploadBudget.Value = false;
            SpreadGarbageCollection.Value = false;
            ShaderVariantsPerFrame.Value = 0;
            UploadTimeSlice.Value = 2;

            LodBias.Value = 0f;
            ShadowDistance.Value = 0f;
            ShadowCascades.Value = 0;
            ClutterAmount.Value = 0f;
            LightLimit.Value = -1;
            ShadowLimit.Value = -1;
            MistDensity.Value = 0f;
            SoftParticles.Value = true;

            ConfigSchemaVersion.Value = currentSchema;
            Log.LogInfo("[Perf] Migrated legacy settings to the visual-neutral profile.");
            return true;
        }

        private void Update()
        {
            PerformanceDiagnostics.Tick();

            /*
             * Only scene-bound optional features are checked here. A portal or
             * world transition replaces ClutterSystem, ParticleMist and the live
             * smoke layer, so a slow identity check is required. Global pipeline
             * settings are instead driven by startup, explicit settings changes,
             * and Valheim's graphics-settings event; they are not polled.
             */
            if (Time.unscaledTime < _nextApply) return;
            _nextApply = Time.unscaledTime + 2f;

            Quality.Apply();
            SmokePhysics.Apply(SmokeCollisions.Value);
        }
    }
}
