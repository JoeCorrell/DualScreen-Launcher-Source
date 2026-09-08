


package com.dualscreen.mods.botw

import com.dualscreen.companion.api.CompanionHostV2
import com.dualscreen.companion.api.CompanionControllerTarget
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.Choreographer
import android.view.MotionEvent
import com.google.android.filament.Camera
import com.google.android.filament.Engine
import com.google.android.filament.EntityManager
import com.google.android.filament.Filament
import com.google.android.filament.IndirectLight
import com.google.android.filament.LightManager
import com.google.android.filament.RenderTarget
import com.google.android.filament.Renderer
import com.google.android.filament.Scene
import com.google.android.filament.Skybox
import com.google.android.filament.SwapChain
import com.google.android.filament.SwapChainFlags
import com.google.android.filament.Texture
import com.google.android.filament.View
import com.google.android.filament.Viewport
import com.google.android.filament.gltfio.AssetLoader
import com.google.android.filament.gltfio.FilamentAsset
import com.google.android.filament.gltfio.Gltfio
import com.google.android.filament.gltfio.ResourceLoader
import com.google.android.filament.gltfio.UbershaderProvider
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import kotlin.math.atan
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sqrt
import kotlin.math.sin
import kotlin.math.tan
import org.json.JSONObject


internal data class BotwPlayerAppearance(
    val head: String?,
    val chest: String?,
    val legs: String?,
    val weapon: String?,
    val bow: String?,
    val shield: String?
)

internal enum class PlayerRenderProfile {
    Botw,
    Sshd
}







internal class BotwPlayerRenderView(
    context: Context,
    private val host: CompanionHostV2,
    private val profile: PlayerRenderProfile = PlayerRenderProfile.Botw
) : android.view.View(context),
    Choreographer.FrameCallback {

    private data class LoadedSlot(val actor: String, val asset: FilamentAsset)

    private data class AssetBytes(
        val actor: String,
        val bytes: ByteArray,
        val sourceActor: String = actor
    )

    private data class AttachmentSpec(
        val bone: String,
        val translation: FloatArray,
        val rotationDegrees: FloatArray
    )

    private data class IndexedAsset(val file: String, val attachment: AttachmentSpec?)

    private data class PreviewAnimationFrame(
        val timeMs: Long,
        val bones: Map<String, FloatArray>
    )

    private data class PreviewAnimationClip(
        val source: String,
        val durationMs: Long,
        val holdLastFrame: Boolean,
        val frames: List<PreviewAnimationFrame>
    )

    private data class Bounds3(
        var minimumX: Float = Float.POSITIVE_INFINITY,
        var minimumY: Float = Float.POSITIVE_INFINITY,
        var minimumZ: Float = Float.POSITIVE_INFINITY,
        var maximumX: Float = Float.NEGATIVE_INFINITY,
        var maximumY: Float = Float.NEGATIVE_INFINITY,
        var maximumZ: Float = Float.NEGATIVE_INFINITY
    ) {
        val valid: Boolean
            get() = minimumX.isFinite() && minimumY.isFinite() && minimumZ.isFinite() &&
                maximumX.isFinite() && maximumY.isFinite() && maximumZ.isFinite()

        fun include(x: Float, y: Float, z: Float) {
            minimumX = min(minimumX, x)
            minimumY = min(minimumY, y)
            minimumZ = min(minimumZ, z)
            maximumX = max(maximumX, x)
            maximumY = max(maximumY, y)
            maximumZ = max(maximumZ, z)
        }

        fun include(other: Bounds3) {
            if (!other.valid) return
            include(other.minimumX, other.minimumY, other.minimumZ)
            include(other.maximumX, other.maximumY, other.maximumZ)
        }
    }

    private companion object {
        private const val BOTW_ASSET_INDEX = "botw/player/index.json"
        private const val SSHD_ASSET_INDEX = "sshd/player/index.json"
        private const val ANIMATION_INDEX = "botw/player/animations.json"
        private const val BOTW_PLAYER_CACHE_DIRECTORY = "dualscreen/botw/player"
        private const val SSHD_PLAYER_CACHE_DIRECTORY = "dualscreen/sshd/player"
        private const val MAX_PLAYER_ASSET_BYTES = 16L * 1024L * 1024L
        private const val GLB_MAGIC = 0x46546C67
        private const val GLB_JSON_CHUNK_TYPE = 0x4E4F534A
        private const val SLOT_BASE = "Base"
        private const val SLOT_HEAD = "Head"
        private const val SLOT_HAND_LEFT = "HandLeft"
        private const val SLOT_HAND_RIGHT = "HandRight"
        private const val SLOT_CHEST = "Chest"
        private const val SLOT_LEGS = "Legs"
        private const val SLOT_WEAPON = "Weapon"
        private const val SLOT_BOW = "Bow"
        private const val SLOT_SHIELD = "Shield"
        private const val SSHD_OUTFIT_OPENING = 0
        private const val SSHD_OUTFIT_HERO = 1
        private const val SSHD_ACTOR_OPENING = "LinkOpening"
        private const val SSHD_ACTOR_HERO = "LinkHero"
        private const val PORTRAIT_FIT_MARGIN = 1.015f
        private const val HELD_ITEM_PREVIEW_SCALE = 0.26f



        private const val SHIELD_BACK_DEPTH_OFFSET = -0.10f
        private const val MAX_FRAME_RETRIES = 6
        private const val READBACK_TIMEOUT_MS = 2_000L


        private const val DIAGNOSTIC_INTERVAL_MS = 10_000L
        private val bodySlots = setOf(SLOT_BASE, SLOT_HEAD, SLOT_CHEST, SLOT_LEGS)
        private val equipmentSlots = setOf(SLOT_WEAPON, SLOT_BOW, SLOT_SHIELD)
        private val defaultActors = mapOf(
            SLOT_HEAD to "Armor_Default_Head",
            SLOT_CHEST to "Armor_Default_Upper",
            SLOT_LEGS to "Armor_Default_Lower"
        )

        @Volatile
        private var librariesInitialized = false

        private fun initializeLibraries() {
            if (librariesInitialized) return
            synchronized(BotwPlayerRenderView::class.java) {
                if (librariesInitialized) return
                Filament.init()
                Gltfio.init()
                librariesInitialized = true
            }
        }
    }

    private val assetIndex = if (profile == PlayerRenderProfile.Sshd) {
        SSHD_ASSET_INDEX
    } else {
        BOTW_ASSET_INDEX
    }
    private val playerCacheDirectory = if (profile == PlayerRenderProfile.Sshd) {
        SSHD_PLAYER_CACHE_DIRECTORY
    } else {
        BOTW_PLAYER_CACHE_DIRECTORY
    }
    private val playerPipelineMode = if (profile == PlayerRenderProfile.Sshd) {
        3
    } else {
        3
    }
    private val requiredBodySlots = if (profile == PlayerRenderProfile.Sshd) {
        setOf(SLOT_BASE)
    } else {
        bodySlots
    }
    private val logName = if (profile == PlayerRenderProfile.Sshd) "SSHD" else "BOTW"

    private val indexedAssets: Map<String, IndexedAsset> by lazy {
        try {
            val index = host.openAsset(assetIndex).bufferedReader().use { reader ->
                val actors = JSONObject(reader.readText()).getJSONObject("actors")
                buildMap(actors.length()) {
                    actors.keys().forEach { actor ->
                        try {
                            val value = actors.get(actor)
                            val entry = if (value is JSONObject) {
                                val attachment = value.optJSONObject("attachment")?.let { objectValue ->
                                    fun vector(name: String): FloatArray {
                                        val array = objectValue.getJSONArray(name)
                                        require(array.length() == 3) {
                                            "$actor $name must have 3 values"
                                        }
                                        return FloatArray(3) { coordinate ->
                                            array.getDouble(coordinate).toFloat().also { component ->
                                                require(component.isFinite()) {
                                                    "$actor $name contains a non-finite value"
                                                }
                                            }
                                        }
                                    }
                                    val bone = objectValue.getString("bone")
                                    require(bone.isNotBlank()) { "$actor attachment bone is blank" }
                                    AttachmentSpec(
                                        bone = bone,
                                        translation = vector("translation"),
                                        rotationDegrees = vector("rotationDegrees")
                                    )
                                }
                                IndexedAsset(value.getString("file"), attachment)
                            } else {
                                IndexedAsset(value as String, null)
                            }
                            this[actor] = entry
                        } catch (exception: Exception) {


                            ModuleLog(host).warning(
                                "[BOTW Companion] Ignoring player asset $actor: " +
                                    exception.message
                            )
                        }
                    }
                }
            }
            val required = if (profile == PlayerRenderProfile.Sshd) {
                setOf(SSHD_ACTOR_OPENING, SSHD_ACTOR_HERO)
            } else {
                setOf("Link", "Armor_Default_Head", "Armor_Default_Upper", "Armor_Default_Lower")
            }
            val missing = required - index.keys
            if (missing.isNotEmpty()) {
                ModuleLog(host).error("[BOTW Companion] Player model index is missing: ${missing.joinToString()}")
                emptyMap()
            } else {
                index
            }
        } catch (exception: Exception) {
            ModuleLog(host).error("[BOTW Companion] Player model index failed: ${exception.message}")
            emptyMap()
        }
    }
    private val previewAnimations: Map<String, PreviewAnimationClip> by lazy {
        try {
            val root = host.openAsset(ANIMATION_INDEX).bufferedReader().use { reader ->
                JSONObject(reader.readText()).getJSONObject("clips")
            }
            buildMap {
                root.keys().forEach { clipName ->
                    val value = root.getJSONObject(clipName)
                    val source = value.getString("source")
                    val framesJson = value.getJSONArray("frames")
                    val frames = List(framesJson.length()) { frameIndex ->
                        val frameJson = framesJson.getJSONObject(frameIndex)
                        val bonesJson = frameJson.getJSONObject("bones")
                        val bones = buildMap {
                            bonesJson.keys().forEach { boneName ->
                                val matrixJson = bonesJson.getJSONArray(boneName)
                                require(matrixJson.length() == 16) {
                                    "$source bone $boneName must have 16 matrix values"
                                }
                                put(
                                    boneName,
                                    FloatArray(16) { component ->
                                        matrixJson.getDouble(component).toFloat()
                                    }
                                )
                            }
                        }
                        PreviewAnimationFrame(frameJson.getLong("timeMs"), bones)
                    }
                    if (frames.isNotEmpty()) {
                        put(
                            clipName,
                            PreviewAnimationClip(
                                source = source,
                                durationMs = value.getLong("durationMs"),
                                holdLastFrame = value.getBoolean("holdLastFrame"),
                                frames = frames
                            )
                        )
                    }
                }
            }
        } catch (exception: Exception) {
            ModuleLog(host).error("[BOTW Companion] Player animation data failed: ${exception.message}")
            emptyMap()
        }

    }

    private fun hasValidatedBotwBodyPose(bytes: ByteArray): Boolean {
        return try {
            if (bytes.size < 20) return false
            val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            if (
                buffer.getInt(0) != GLB_MAGIC ||
                buffer.getInt(4) != 2 ||
                buffer.getInt(8) != bytes.size
            ) {
                return false
            }
            val jsonLength = buffer.getInt(12)
            if (
                buffer.getInt(16) != GLB_JSON_CHUNK_TYPE ||
                jsonLength !in 2..(bytes.size - 20)
            ) {
                return false
            }
            val document = JSONObject(
                String(bytes, 20, jsonLength, Charsets.UTF_8).trimEnd(' ', '\u0000')
            )
            val pose = document.optJSONObject("extras")?.optJSONObject("botwPose")
                ?: return false
            (document.optJSONArray("meshes")?.length() ?: 0) > 0 &&
                (document.optJSONArray("skins")?.length() ?: 0) > 0 &&
                pose.optString("animation") == "Nml_Wait" &&
                pose.optInt("changedAnimatedNodes", 0) > 0
        } catch (_: Exception) {
            false
        }
    }

    private val missingActors = mutableSetOf<String>()
    private val desiredActors = mutableMapOf<String, String?>()
    private val requestedActors = mutableMapOf<String, String?>()
    private val requestGenerations = mutableMapOf<String, Int>()
    private val pendingSlots = mutableSetOf<String>()
    private val loadedSlots = mutableMapOf<String, LoadedSlot>()
    private var appearance: BotwPlayerAppearance? = null
    private var cachedAppearance: BotwPlayerAppearance? = null
    private var sshdOutfitActor: String? = null
    private var shouldBeVisible = false
    private val previewBoneEntities = mutableMapOf<String, MutableMap<String, Int>>()
    private val restBoneTransforms = mutableMapOf<Int, FloatArray>()
    private var activePreviewClip: PreviewAnimationClip? = null
    private var activePreviewFrameIndex = -1
    private var activePreviewStartedAtNanos = 0L
    private var previewAnimationGeneration = 0
    private var displayedPreviewFrame: PreviewAnimationFrame? = null
    private var heldItemActor: String? = null
    private var heldItemBitmap: Bitmap? = null
    private var heldItemEating = false
    private var itemSounds: BotwItemSounds? = null
    private var holdStateGraceUntil = 0L
    private var lastReportedHeldItemCount = -1
    private var yawRadians = 0f
    private var lastTouchX = 0f
    private var portraitDragged = false
    private var framePending = false
    private var readbackPending = false
    private var renderDirty = false
    private var frameRetryBudget = 0
    private var targetGeneration = 0
    private var bodyReady = false
    private var viewportWidth = 0
    private var viewportHeight = 0
    private var releasing = false
    private var released = false
    private val mainHandler = Handler(Looper.getMainLooper())
    private val previewAnimationFrameCallback = Choreographer.FrameCallback { frameTimeNanos ->
        advancePreviewAnimation(frameTimeNanos)
    }
    private val interpolatedPreviewMatrix = FloatArray(16)
    private val bitmapPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { isFilterBitmap = false }
    private var frameBitmap: Bitmap? = null
    private var readbackBuffer: ByteBuffer? = null
    private var renderRequests = 0
    private var frameAttempts = 0
    private var renderedFrames = 0
    private var beginFrameFailures = 0
    private var readbacksStarted = 0
    private var readbacksCompleted = 0
    private var nextReadbackId = 0
    private var activeReadbackId = 0
    private var readbackPumpFrames = 0
    private var lastRenderReason = "none"
    private var lastDiagnosticAt = 0L
    private var readbackStartedAt = 0L

    private var ioExecutor: ExecutorService? = null
    private var nativeHeldPreview: BotwHeldPreview? = null
    private var heldPreviewGeneration = 0
    private var heldPreviewPending = false
    private var lastValidHeldPreviewAt = 0L
    private val heldSlots = (0 until 5).map { "Held$it" }
    private val heldPreviewPoll = Runnable { pollHeldPreview() }

    private fun wakeHeldPreview() {
        if (profile != PlayerRenderProfile.Botw || !shouldBeVisible || released || releasing) return
        if (engine == null) {
            val current = appearance ?: return
            cachedAppearance = null
            beginAppearanceRender(current)
        }
        mainHandler.removeCallbacks(heldPreviewPoll)
        pollHeldPreview()
    }

    private fun pollHeldPreview() {
        if (profile != PlayerRenderProfile.Botw || !shouldBeVisible || released || releasing ||
            heldPreviewPending || engine == null) return
        val executor = ioExecutor ?: return
        val generation = heldPreviewGeneration
        heldPreviewPending = true
        executor.execute {
            val sample = runCatching { BotwHeldPreview.parse(host.query(2, "")) }
                .getOrNull()
            mainHandler.post {
                if (generation != heldPreviewGeneration) return@post
                heldPreviewPending = false
                if (!shouldBeVisible || released || releasing) return@post
                if (sample != null) {
                    lastValidHeldPreviewAt = SystemClock.uptimeMillis()
                    applyHeldPreview(sample)
                } else if (nativeHeldPreview != null &&
                    SystemClock.uptimeMillis() - lastValidHeldPreviewAt > 500L) {
                    ModuleLog(host).warning("[BOTW Companion] Native held pose unavailable for 500ms; clearing stale preview")
                    applyHeldPreview(BotwHeldPreview(0, emptyMap(), emptyList()))
                }
                // Only the visible held-character preview runs at animation cadence. The
                // inventory/map/stats poll remains unchanged; no unbounded frame queue.
                mainHandler.postDelayed(heldPreviewPoll,
                    if (nativeHeldPreview?.items?.isNotEmpty() == true) 16L else 250L)
            }
        }
    }

    private fun applyHeldPreview(live: BotwHeldPreview) {
        if (!bodyReady) return
        val previous = nativeHeldPreview
        val sample = live.stationaryAttachments(previous)
        if (previous != null && previous.items.size == sample.items.size) {
            sample.items.forEachIndexed { index, item ->
                val old = previous.items[index]
                if (old.id == item.id && old !== item) {
                    ModuleLog(host).info("[BOTW Preview] settled attachment actor=${item.actor} " +
                        "handY=${old.handLocal[13]}->${item.handLocal[13]}")
                }
            }
        }
        if (previous?.items?.map { it.id to it.actor }.orEmpty() !=
            sample.items.map { it.id to it.actor }) {
            ModuleLog(host).info("[BOTW Companion] Native held preview count=${sample.items.size} " +
                "bones=${sample.bones.size} actors=${sample.items.joinToString { it.actor }}")
        }
        nativeHeldPreview = sample.takeIf { it.items.isNotEmpty() }
        if (previous?.playerId != sample.playerId && previous != null) {
            heldSlots.forEach { slot -> requestSlot(slot, null) }
            if (!heldItemEating) cancelPreviewAction(clearHeldItem = false)
        }
        heldSlots.forEachIndexed { index, slot ->
            val item = sample.items.getOrNull(index)
            if (requestedActors[slot] != item?.actor ||
                (item != null && slot !in loadedSlots && slot !in pendingSlots &&
                    item.actor !in missingActors)) {
                // Never retain the last ingredient while a different model is loading.
                if (requestedActors[slot] != item?.actor) removeSlot(slot)
                requestSlot(slot, item?.actor)
            }
        }
        if (sample.items.isEmpty()) {
            if (previous != null) {
                if (!heldItemEating) cancelPreviewAction(clearHeldItem = false)
                cachedAppearance = null
                requestRenderFrame("native-hold-released")
            }
            return
        }
        // The portrait uses the native pouch pose, not the gameplay locomotion
        // skeleton. Native Weapon_R/item worlds supply each collection's initial
        // attachment; walking/running must not overwrite that portrait-local pose.
        if (!heldItemEating && (previous?.items?.size != sample.items.size ||
                displayedPreviewFrame == null && activePreviewClip == null)) {
            startNativePouchPreview(sample.items.size)
        }
        val attachmentsChanged = previous == null || previous.items.size != sample.items.size ||
            sample.items.indices.any { sample.items[it] !== previous.items[it] }
        if (attachmentsChanged) {
            heldSlots.forEachIndexed { index, slot ->
                val loaded = loadedSlots[slot] ?: return@forEachIndexed
                val item = sample.items.getOrNull(index) ?: return@forEachIndexed
                if (loaded.actor == item.actor) applyHeldSlotTransform(slot, loaded.asset)
            }
            requestRenderFrame("native-held-attachments")
        }
    }

    private fun startNativePouchPreview(count: Int) {
        val few = previewAnimations["Hold"] ?: return
        val many = previewAnimations["HoldMany"] ?: return
        // PauseMenuPlayer_Lift.bas uses the Few/Many blend's native 0..5 range.
        val weight = count.coerceIn(0, 5) / 5f
        val frames = few.frames.mapIndexed { i, frame ->
            val other = many.frames.getOrNull(i) ?: many.frames.last()
            PreviewAnimationFrame(frame.timeMs, frame.bones.mapValues { (name, matrix) ->
                BotwHeldTransforms.interpolate(matrix, other.bones[name] ?: matrix, weight)
            })
        }
        Choreographer.getInstance().removeFrameCallback(previewAnimationFrameCallback)
        previewAnimationGeneration++
        activePreviewClip = few.copy(source = "Lift_Lift_Pouch_Few/Many", frames = frames)
        activePreviewStartedAtNanos = 0L
        activePreviewFrameIndex = -1
        cachedAppearance = null
        startPreviewAnimation()
    }

    private fun applyHeldSlotTransform(slot: String, asset: FilamentAsset): Boolean {
        val index = heldSlots.indexOf(slot)
        val item = nativeHeldPreview?.items?.getOrNull(index) ?: return false
        val player = loadedSlots[SLOT_BASE]?.asset ?: return false
        val entity = player.getFirstEntityByName(BotwHeldPreview.HAND)
        val transforms = engine?.transformManager ?: return false
        if (entity == 0 || !transforms.hasComponent(entity) ||
            !transforms.hasComponent(asset.root)) return false
        val root = transforms.getInstance(asset.root)
        transforms.setParent(root, transforms.getInstance(entity))
        transforms.setTransform(root, item.handLocal)
        return true
    }
    private var engine: Engine? = null
    private var renderer: Renderer? = null
    private var scene: Scene? = null
    private var filamentView: View? = null
    private var camera: Camera? = null
    private var cameraEntity = 0
    private var lightEntities = intArrayOf()
    private var indirectLight: IndirectLight? = null
    private var transparentSkybox: Skybox? = null
    private var swapChain: SwapChain? = null
    private var colorTexture: Texture? = null
    private var depthTexture: Texture? = null
    private var renderTarget: RenderTarget? = null
    private var materialProvider: UbershaderProvider? = null
    private var assetLoader: AssetLoader? = null
    private var resourceLoader: ResourceLoader? = null

    init {
        alpha = 0f
        visibility = INVISIBLE



        setWillNotDraw(false)
        isClickable = true
    }


    fun setAppearance(next: BotwPlayerAppearance, visible: Boolean) {
        check(Looper.myLooper() == Looper.getMainLooper())


        val renderAppearance = next.copy(weapon = null)
        appearance = renderAppearance
        shouldBeVisible = visible
        if (visible && profile == PlayerRenderProfile.Botw && itemSounds == null) {
            itemSounds = BotwItemSounds(context, host)
        }
        if (!visible || indexedAssets.isEmpty()) {
            cancelPreviewAction(clearHeldItem = true)
            alpha = 0f
            visibility = INVISIBLE
            bodyReady = false


            if (engine != null) releaseRenderer(keepBitmap = true, terminal = false)
            return
        }
        if (playerPipelineMode == 0) {
            alpha = 0f
            visibility = INVISIBLE
            if (engine != null) releaseRenderer(keepBitmap = false, terminal = false)
            return
        }

        val cached = frameBitmap
        if (engine == null && cachedAppearance == renderAppearance && cached != null &&
            (profile != PlayerRenderProfile.Botw || lastReportedHeldItemCount <= 0) &&
            !cached.isRecycled &&
            cached.width == viewportWidth && cached.height == viewportHeight
        ) {
            alpha = 1f
            visibility = VISIBLE
            invalidate()
            return
        }

        cachedAppearance = null
        beginAppearanceRender(renderAppearance)
    }

    fun setVisible(visible: Boolean) {
        setAppearance(
            BotwPlayerAppearance(
                head = null,
                chest = null,
                legs = null,
                weapon = null,
                bow = null,
                shield = null
            ),
            visible
        )
    }

    fun setSshdOutfit(outfit: Int) {
        check(Looper.myLooper() == Looper.getMainLooper())
        if (profile != PlayerRenderProfile.Sshd) return
        // The companion ABI deliberately normalizes these values for the UI:
        // 0 is the opening pl_* set and 1 is the green al* set. Unknown values
        // have no visual fallback; showing the wrong outfit is worse than
        // withholding the preview until a valid live selector arrives.
        val actor = when (outfit) {
            SSHD_OUTFIT_OPENING -> SSHD_ACTOR_OPENING
            SSHD_OUTFIT_HERO -> SSHD_ACTOR_HERO
            else -> null
        }
        if (sshdOutfitActor == actor) return
        sshdOutfitActor = actor
        cachedAppearance = null
        cancelPreviewAction(clearHeldItem = true)
        alpha = 0f
        visibility = INVISIBLE
        bodyReady = false
        val current = appearance
        if (actor == null) {
            if (engine != null) releaseRenderer(keepBitmap = false, terminal = false)
        } else if (shouldBeVisible && current != null) {
            beginAppearanceRender(current)
        }
    }

    private val itemPresentation by lazy {
        runCatching {
            host.openAsset("botw/player/item-presentation.json").bufferedReader().use {
                val actors = JSONObject(it.readText()).getJSONObject("actors")
                buildMap { actors.keys().forEach { name -> put(name, actors.getString(name)) } }
            }
        }.getOrElse {
            ModuleLog(host).error("[BOTW Companion] Native item presentation metadata unavailable: ${it.message}")
            emptyMap<String, String>()
        }
    }

    fun playItemAction(actorName: String, itemIcon: Bitmap?, eating: Boolean) {
        check(Looper.myLooper() == Looper.getMainLooper())
        if (profile == PlayerRenderProfile.Botw && !eating) {
            // Holding is driven by the actual collection, not the last tapped icon/clip.
            itemIcon?.takeUnless(Bitmap::isRecycled)?.recycle()
            wakeHeldPreview()
            return
        }
        if (!shouldBeVisible) {
            itemIcon?.takeUnless(Bitmap::isRecycled)?.recycle()
            return
        }
        val presentation = if (eating) itemPresentation[actorName] else "Hold"
        // PauseMenuPlayer_UseItem.bas chooses a repeat-hand clip only when a
        // second raw-food request arrives during UseItem. Other types keep their
        // own native clip; a drink must never fall back to the dish animation.
        val clipKey = if (presentation == "EatFood" && heldItemEating && activePreviewClip != null) {
            if (kotlin.random.Random.nextBoolean()) "EatFoodRepeatL" else "EatFoodRepeatR"
        } else presentation
        itemSounds?.stop()
        Choreographer.getInstance().removeFrameCallback(previewAnimationFrameCallback)
        previewAnimationGeneration++
        resetPreviewPose()
        heldItemBitmap?.takeUnless(Bitmap::isRecycled)?.recycle()
        heldItemBitmap = itemIcon
        if (profile == PlayerRenderProfile.Botw) {
            // A fixed 2-D inventory icon is not a native eating prop and obscures
            // Link's hands. Presentation is supplied by the selected game clip.
            heldItemBitmap?.takeUnless(Bitmap::isRecycled)?.recycle()
            heldItemBitmap = null
        }
        heldItemActor = actorName
        heldItemEating = eating
        holdStateGraceUntil = if (eating) 0L else SystemClock.uptimeMillis() + 2_000L
        activePreviewClip = clipKey?.let { previewAnimations[it] }
        activePreviewFrameIndex = -1
        activePreviewStartedAtNanos = 0L
        displayedPreviewFrame = null
        cachedAppearance = null
        invalidate()
        val clip = activePreviewClip
        if (clip == null) {
            ModuleLog(host).error("[BOTW Companion] Missing ${if (eating) "Eat" else "Hold"} animation")
            clearHeldItem()
            wakeHeldPreview()
            return
        }
        ModuleLog(host).info(
            "[BOTW Companion] Preview ${if (eating) "Eat" else "Hold"} $actorName " +
                "using game animation ${clip.source}"
        )
        val current = appearance ?: return
        if (engine == null) {
            beginAppearanceRender(current)
        }
        if (bodyReady) {
            startPreviewAnimation()
        }
    }

    fun syncHeldItemCount(count: Int) {
        check(Looper.myLooper() == Looper.getMainLooper())
        if (count != lastReportedHeldItemCount) {
            lastReportedHeldItemCount = count
            ModuleLog(host).info("[BOTW Companion] Game reports $count held pouch items")
            if (profile == PlayerRenderProfile.Botw) {
                if (count > 0 || engine != null) wakeHeldPreview()
            }
        }
        if (profile == PlayerRenderProfile.Botw) return
        if (count != 0 || heldItemEating || heldItemActor == null ||
            SystemClock.uptimeMillis() < holdStateGraceUntil
        ) {
            return
        }
        Choreographer.getInstance().removeFrameCallback(previewAnimationFrameCallback)
        previewAnimationGeneration++
        activePreviewClip = null
        activePreviewStartedAtNanos = 0L
        resetPreviewPose()
        clearHeldItem()
        cachedAppearance = null
        val current = appearance
        if (engine == null && current != null && shouldBeVisible) {
            beginAppearanceRender(current)
        } else {
            requestRenderFrame("hold-released")
        }
        ModuleLog(host).info("[BOTW Companion] Preview hold cleared after game drop")
    }

    private fun startPreviewAnimation() {
        if (activePreviewClip == null || activePreviewStartedAtNanos != 0L || !bodyReady) return
        activePreviewStartedAtNanos = System.nanoTime()
        if (heldItemEating) itemSounds?.begin(activePreviewClip!!.source)
        Choreographer.getInstance().postFrameCallback(previewAnimationFrameCallback)
    }

    private fun advancePreviewAnimation(frameTimeNanos: Long) {
        val clip = activePreviewClip ?: return
        if (!bodyReady || engine == null) return
        if (activePreviewStartedAtNanos == 0L) activePreviewStartedAtNanos = frameTimeNanos
        val elapsed = ((frameTimeNanos - activePreviewStartedAtNanos) / 1_000_000L)
            .coerceAtLeast(0L)
        if (heldItemEating) itemSounds?.advance(elapsed)
        val nextIndex = clip.frames.indexOfLast { frame -> frame.timeMs <= elapsed }
            .coerceAtLeast(0)
        if (nextIndex + 1 < clip.frames.size) {
            activePreviewFrameIndex = nextIndex
            val current = clip.frames[nextIndex]
            val next = clip.frames[nextIndex + 1]
            val interval = (next.timeMs - current.timeMs).coerceAtLeast(1L)
            val progress = ((elapsed - current.timeMs).toFloat() / interval.toFloat())
                .coerceIn(0f, 1f)
            applyInterpolatedPreviewFrame(current, next, progress)
            Choreographer.getInstance().postFrameCallback(previewAnimationFrameCallback)
        } else if (clip.holdLastFrame) {
            if (activePreviewFrameIndex != nextIndex) {
                activePreviewFrameIndex = nextIndex
                applyPreviewFrame(clip.frames[nextIndex])
            }
            activePreviewClip = null
            activePreviewStartedAtNanos = 0L
            ModuleLog(host).info("[BOTW Companion] Preview hold pose retained for $heldItemActor")
        } else {
            if (activePreviewFrameIndex != nextIndex) {
                activePreviewFrameIndex = nextIndex
                applyPreviewFrame(clip.frames[nextIndex])
            }
            val generation = previewAnimationGeneration
            mainHandler.postDelayed(
                {
                    if (activePreviewClip === clip &&
                        previewAnimationGeneration == generation
                    ) {
                        resetPreviewPose()
                        clearHeldItem()
                        itemSounds?.stop()
                        activePreviewClip = null
                        activePreviewStartedAtNanos = 0L
                        cachedAppearance = null
                        requestRenderFrame("${clip.source}-complete")
                    }
                },
                250L
            )
        }
    }

    private fun applyInterpolatedPreviewFrame(
        current: PreviewAnimationFrame,
        next: PreviewAnimationFrame,
        progress: Float
    ) {
        val nextEngine = engine ?: return
        val transforms = nextEngine.transformManager
        loadedSlots.forEach { (slot, loaded) ->
            if (slot !in bodySlots) return@forEach
            val entities = previewBoneEntities.getOrPut(slot) { mutableMapOf() }
            current.bones.forEach { (boneName, currentMatrix) ->
                val entity = entities.getOrPut(boneName) {
                    loaded.asset.getFirstEntityByName(boneName)
                }
                if (entity != 0 && transforms.hasComponent(entity)) {
                    val instance = transforms.getInstance(entity)
                    restBoneTransforms.getOrPut(entity) {
                        transforms.getTransform(instance, FloatArray(16))
                    }
                    val nextMatrix = next.bones[boneName] ?: currentMatrix
                    if (profile == PlayerRenderProfile.Botw) {
                        transforms.setTransform(instance,
                            BotwHeldTransforms.interpolate(currentMatrix, nextMatrix, progress))
                    } else {
                        for (index in interpolatedPreviewMatrix.indices) {
                            interpolatedPreviewMatrix[index] = currentMatrix[index] +
                                (nextMatrix[index] - currentMatrix[index]) * progress
                        }
                        transforms.setTransform(instance, interpolatedPreviewMatrix)
                    }
                }
            }
            loaded.asset.instance.animator.updateBoneMatrices()
        }
        displayedPreviewFrame = if (progress < 0.5f) current else next
        cachedAppearance = null
        requestRenderFrame("preview-animation-interpolated")
    }

    private fun applyPreviewFrame(frame: PreviewAnimationFrame) {
        val nextEngine = engine ?: return
        val transforms = nextEngine.transformManager
        loadedSlots.forEach { (slot, loaded) ->
            if (slot !in bodySlots) return@forEach
            val entities = previewBoneEntities.getOrPut(slot) { mutableMapOf() }
            frame.bones.forEach { (boneName, matrix) ->
                val entity = entities.getOrPut(boneName) {
                    loaded.asset.getFirstEntityByName(boneName)
                }
                if (entity != 0 && transforms.hasComponent(entity)) {
                    val instance = transforms.getInstance(entity)
                    restBoneTransforms.getOrPut(entity) {
                        transforms.getTransform(instance, FloatArray(16))
                    }
                    transforms.setTransform(instance, matrix)
                }
            }
            loaded.asset.instance.animator.updateBoneMatrices()
        }
        displayedPreviewFrame = frame
        cachedAppearance = null
        requestRenderFrame("preview-animation")
    }

    private fun resetPreviewPose() {
        val nextEngine = engine
        if (nextEngine != null) {
            val transforms = nextEngine.transformManager
            restBoneTransforms.forEach { (entity, matrix) ->
                if (transforms.hasComponent(entity)) {
                    transforms.setTransform(transforms.getInstance(entity), matrix)
                }
            }
            loadedSlots.values.forEach { loaded ->
                loaded.asset.instance.animator.updateBoneMatrices()
            }
        }
        restBoneTransforms.clear()
        displayedPreviewFrame = null
        activePreviewFrameIndex = -1
    }

    private fun clearHeldItem() {
        heldItemBitmap?.takeUnless(Bitmap::isRecycled)?.recycle()
        heldItemBitmap = null
        heldItemActor = null
        heldItemEating = false
        holdStateGraceUntil = 0L
        invalidate()
    }

    private fun cancelPreviewAction(clearHeldItem: Boolean) {
        itemSounds?.stop()
        Choreographer.getInstance().removeFrameCallback(previewAnimationFrameCallback)
        previewAnimationGeneration++
        activePreviewClip = null
        activePreviewStartedAtNanos = 0L
        resetPreviewPose()
        if (clearHeldItem) clearHeldItem()
    }

    private fun beginAppearanceRender(next: BotwPlayerAppearance) {
        val baseActor = if (profile == PlayerRenderProfile.Sshd) {
            sshdOutfitActor ?: run {
                alpha = 0f
                visibility = INVISIBLE
                bodyReady = false
                return
            }
        } else {
            "Link"
        }
        if (!ensureRenderer()) return

        desiredActors[SLOT_BASE] = baseActor
        desiredActors[SLOT_HEAD] = if (profile == PlayerRenderProfile.Sshd) null
            else next.head ?: defaultActors.getValue(SLOT_HEAD)
        desiredActors[SLOT_HAND_LEFT] = null
        desiredActors[SLOT_HAND_RIGHT] = null
        desiredActors[SLOT_CHEST] = if (profile == PlayerRenderProfile.Sshd) null
            else next.chest ?: defaultActors.getValue(SLOT_CHEST)
        desiredActors[SLOT_LEGS] = if (profile == PlayerRenderProfile.Sshd) null
            else next.legs ?: defaultActors.getValue(SLOT_LEGS)
        desiredActors[SLOT_WEAPON] = null
        desiredActors[SLOT_BOW] = if (profile == PlayerRenderProfile.Sshd) null else next.bow
        desiredActors[SLOT_SHIELD] = if (profile == PlayerRenderProfile.Sshd) null else next.shield
        desiredActors.forEach { (slot, actor) -> requestSlot(slot, actor) }
        updateReadiness()
        if (profile == PlayerRenderProfile.Botw) {
            mainHandler.removeCallbacks(heldPreviewPoll)
            pollHeldPreview()
        }
    }

    private fun ensureRenderer(): Boolean {
        if (released || releasing) return false
        if (engine != null) return true
        return try {
            initializeLibraries()
            val nextEngine = Engine.create(Engine.Backend.OPENGL)



            engine = nextEngine
            val nextRenderer = nextEngine.createRenderer()
            renderer = nextRenderer
            val nextScene = nextEngine.createScene()
            scene = nextScene
            val nextView = nextEngine.createView()
            filamentView = nextView
            val entities = EntityManager.get()
            val nextCameraEntity = entities.create()
            val nextCamera = nextEngine.createCamera(nextCameraEntity)
            cameraEntity = nextCameraEntity
            camera = nextCamera

            nextView.scene = nextScene
            nextView.camera = nextCamera
            nextView.isPostProcessingEnabled = true
            nextView.setShadowingEnabled(true)
            nextView.antiAliasing = View.AntiAliasing.FXAA
            nextView.ambientOcclusion = View.AmbientOcclusion.SSAO
            nextView.dithering = View.Dithering.TEMPORAL
            nextView.sampleCount = 4


            nextView.blendMode = View.BlendMode.TRANSLUCENT



            nextCamera.setExposure(8.0f, 1f / 125f, 100f)


            nextCamera.lookAt(0.0, 0.92, 4.0, 0.0, 0.92, 0.0, 0.0, 1.0, 0.0)
            nextRenderer.clearOptions = Renderer.ClearOptions().apply {
                clear = true
                discard = true
                clearColor = doubleArrayOf(0.0, 0.0, 0.0, 0.0)
            }

            val key = entities.create()
            LightManager.Builder(LightManager.Type.DIRECTIONAL)
                .color(1.0f, 0.96f, 0.88f)
                .intensity(68_000f)
                .direction(-0.38f, -0.62f, -0.68f)
                .castShadows(true)
                .build(nextEngine, key)
            nextScene.addEntity(key)
            lightEntities = intArrayOf(key)
            val fill = entities.create()
            LightManager.Builder(LightManager.Type.DIRECTIONAL)
                .color(
                    if (profile == PlayerRenderProfile.Sshd) 1.0f else 0.74f,
                    if (profile == PlayerRenderProfile.Sshd) 0.86f else 0.85f,
                    if (profile == PlayerRenderProfile.Sshd) 0.66f else 1.0f
                )
                .intensity(if (profile == PlayerRenderProfile.Sshd) 15_000f else 13_000f)
                .direction(0.62f, -0.20f, -0.76f)
                .castShadows(false)
                .build(nextEngine, fill)
            nextScene.addEntity(fill)
            val rim = entities.create()
            LightManager.Builder(LightManager.Type.DIRECTIONAL)
                .color(
                    if (profile == PlayerRenderProfile.Sshd) 1.0f else 0.46f,
                    if (profile == PlayerRenderProfile.Sshd) 0.72f else 0.68f,
                    if (profile == PlayerRenderProfile.Sshd) 0.42f else 1.0f
                )
                .intensity(if (profile == PlayerRenderProfile.Sshd) 8_000f else 9_000f)
                .direction(-0.25f, -0.18f, 0.95f)
                .castShadows(false)
                .build(nextEngine, rim)
            nextScene.addEntity(rim)
            lightEntities = intArrayOf(key, fill, rim)




            val ambient = IndirectLight.Builder()
                .irradiance(
                    1,
                    if (profile == PlayerRenderProfile.Sshd) {
                        floatArrayOf(0.82f, 0.74f, 0.62f)
                    } else {
                        floatArrayOf(0.70f, 0.76f, 0.86f)
                    }
                )
                .intensity(if (profile == PlayerRenderProfile.Sshd) 14_000f else 12_000f)
                .build(nextEngine)
            nextScene.indirectLight = ambient
            indirectLight = ambient



            val skybox = Skybox.Builder()
                .color(0f, 0f, 0f, 0f)
                .build(nextEngine)
            nextScene.skybox = skybox
            transparentSkybox = skybox

            val nextProvider = UbershaderProvider(nextEngine)
            materialProvider = nextProvider
            assetLoader = AssetLoader(nextEngine, nextProvider, entities)
            resourceLoader = ResourceLoader(nextEngine)
            ioExecutor = Executors.newSingleThreadExecutor { runnable ->
                Thread(
                    {
                        try {
                            android.os.Process.setThreadPriority(
                                android.os.Process.THREAD_PRIORITY_BACKGROUND
                            )
                        } catch (_: SecurityException) {

                        }
                        runnable.run()
                    },
                    "$logName player assets"
                ).apply {
                    isDaemon = true
                    priority = Thread.MIN_PRIORITY
                }
            }
            if (playerPipelineMode >= 2 && viewportWidth > 0 && viewportHeight > 0 &&
                !recreateOffscreenTarget(viewportWidth, viewportHeight)
            ) {
                throw IllegalStateException("could not create offscreen player target")
            }
            logPipelineDiagnostics("engine-ready", force = true)
            true
        } catch (exception: Exception) {
            ModuleLog(host).error("[BOTW Companion] Player renderer init failed: ${exception.message}")



            releaseRenderer(keepBitmap = false, terminal = false)
            false
        }
    }





    private fun recreateOffscreenTarget(width: Int, height: Int): Boolean {
        val nextEngine = engine ?: return false
        if (width <= 0 || height <= 0 || released || releasing) return false

        targetGeneration++
        if (swapChain != null || renderTarget != null) {


            nextEngine.flushAndWait()
        }


        activeReadbackId = 0
        readbackPending = false
        destroyOffscreenTarget(nextEngine)

        var nextColor: Texture? = null
        var nextDepth: Texture? = null
        var nextTarget: RenderTarget? = null
        var nextSwapChain: SwapChain? = null
        return try {
            nextColor = Texture.Builder()
                .width(width)
                .height(height)
                .levels(1)
                .sampler(Texture.Sampler.SAMPLER_2D)
                .format(Texture.InternalFormat.RGBA8)
                .usage(Texture.Usage.COLOR_ATTACHMENT or Texture.Usage.BLIT_SRC)
                .build(nextEngine)
            val depthFormat = if (
                Texture.isTextureFormatSupported(nextEngine, Texture.InternalFormat.DEPTH24)
            ) {
                Texture.InternalFormat.DEPTH24
            } else {
                Texture.InternalFormat.DEPTH16
            }
            nextDepth = Texture.Builder()
                .width(width)
                .height(height)
                .levels(1)
                .sampler(Texture.Sampler.SAMPLER_2D)
                .format(depthFormat)
                .usage(Texture.Usage.DEPTH_ATTACHMENT)
                .build(nextEngine)
            nextTarget = RenderTarget.Builder()
                .texture(RenderTarget.AttachmentPoint.COLOR, nextColor)
                .texture(RenderTarget.AttachmentPoint.DEPTH, nextDepth)
                .build(nextEngine)


            nextSwapChain = nextEngine.createSwapChain(
                width, height, SwapChainFlags.CONFIG_DEFAULT
            )




            filamentView?.renderTarget = nextTarget
            filamentView?.viewport = Viewport(0, 0, width, height)
            frameBitmap?.let { bitmap -> if (!bitmap.isRecycled) bitmap.recycle() }
            frameBitmap = null
            colorTexture = nextColor
            depthTexture = nextDepth
            renderTarget = nextTarget
            swapChain = nextSwapChain
            renderDirty = true
            true
        } catch (exception: Exception) {
            destroySafely("partial target detachment") { filamentView?.renderTarget = null }
            nextTarget?.let { target ->
                destroySafely("partial render target") { nextEngine.destroyRenderTarget(target) }
            }
            nextDepth?.let { texture ->
                destroySafely("partial depth texture") { nextEngine.destroyTexture(texture) }
            }
            nextColor?.let { texture ->
                destroySafely("partial color texture") { nextEngine.destroyTexture(texture) }
            }
            nextSwapChain?.let { chain ->
                destroySafely("partial swap chain") { nextEngine.destroySwapChain(chain) }
            }
            ModuleLog(host).error("[BOTW Companion] Offscreen player target failed: ${exception.message}")
            false
        }
    }

    private inline fun destroySafely(resource: String, action: () -> Unit) {
        try {
            action()
        } catch (exception: Exception) {
            ModuleLog(host).error("[BOTW Companion] Could not release $resource: ${exception.message}")
        }
    }

    private fun destroyOffscreenTarget(nextEngine: Engine) {


        val oldTarget = renderTarget
        renderTarget = null
        val oldDepth = depthTexture
        depthTexture = null
        val oldColor = colorTexture
        colorTexture = null
        val oldSwapChain = swapChain
        swapChain = null
        destroySafely("render-target detachment") { filamentView?.renderTarget = null }
        oldTarget?.let { target ->
            destroySafely("render target") { nextEngine.destroyRenderTarget(target) }
        }
        oldDepth?.let { texture ->
            destroySafely("depth texture") { nextEngine.destroyTexture(texture) }
        }
        oldColor?.let { texture ->
            destroySafely("color texture") { nextEngine.destroyTexture(texture) }
        }
        oldSwapChain?.let { chain ->
            destroySafely("swap chain") { nextEngine.destroySwapChain(chain) }
        }
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        frameBitmap?.let { bitmap ->
            if (!bitmap.isRecycled) canvas.drawBitmap(bitmap, 0f, 0f, bitmapPaint)
        }
        heldItemBitmap?.takeIf { heldItemEating }?.let { bitmap ->
            if (!bitmap.isRecycled) {
                val size = min(width, height) * HELD_ITEM_PREVIEW_SCALE
                val centerX = width * 0.50f
                val centerY = height * 0.39f
                canvas.drawBitmap(
                    bitmap,
                    null,
                    RectF(
                        centerX - size * 0.5f,
                        centerY - size * 0.5f,
                        centerX + size * 0.5f,
                        centerY + size * 0.5f
                    ),
                    bitmapPaint
                )
            }
        }
    }

    private fun requestSlot(slot: String, requestedActor: String?) {
        val matchingRequestIsActive = requestedActors[slot] == requestedActor &&
            (pendingSlots.contains(slot) || loadedSlots[slot]?.actor == requestedActor)
        if (matchingRequestIsActive) return
        requestedActors[slot] = requestedActor
        val generation = (requestGenerations[slot] ?: 0) + 1
        requestGenerations[slot] = generation
        if (requestedActor == null) {
            pendingSlots.remove(slot)
            removeSlot(slot)
            updateReadiness()
            return
        }

        pendingSlots.add(slot)
        ioExecutor?.execute {
            // Embedded Eden stores its configured user directory below .../Switch/Eden.
            // Search that location first so both BOTW and SSHD generated player assets are
            // found, while retaining the external-files and internal-files fallbacks.
            val configuredDirectory = runCatching { host.userDataDirectory() }
                .getOrNull()
                ?.let(::File)
            val externalDirectory = context.getExternalFilesDir(null)
            fun loadActorCandidates(actor: String): List<AssetBytes> {
                val file = indexedAssets[actor]?.file ?: return emptyList()
                val externalSources = listOfNotNull(
                    configuredDirectory,
                    externalDirectory,
                    context.filesDir
                )
                    .distinctBy(File::getAbsolutePath)
                    .asSequence()
                    .map { root -> File(root, "$playerCacheDirectory/$file") }
                    .filter { candidate ->
                        candidate.isFile && candidate.length() in 1..MAX_PLAYER_ASSET_BYTES
                    }
                val requiresValidatedPose = profile == PlayerRenderProfile.Botw &&
                    (actor == "Link" || actor.startsWith("Armor_"))
                val loadedFromFile = externalSources.mapNotNull { source ->
                    try {
                        val bytes = source.readBytes()
                        if (requiresValidatedPose && !hasValidatedBotwBodyPose(bytes)) {
                            ModuleLog(host).warning(
                                "[BOTW Companion] Ignoring unposed external player model " +
                                    "$actor at ${source.absolutePath}; using a packaged fallback"
                            )
                            null
                        } else {
                            AssetBytes(actor, bytes)
                        }
                    } catch (exception: Exception) {
                        ModuleLog(host).warning(
                            "[$logName Companion] Player model $actor could not be read from " +
                                "${source.absolutePath}: ${exception.message}"
                        )
                        null
                    }
                }.firstOrNull()
                val loadedFromPackage = try {
                    val assetDirectory = assetIndex.substringBeforeLast('/')
                    val bytes = host.openAsset("$assetDirectory/$file").use { stream ->
                        stream.readBytes()
                    }
                    if (bytes.size.toLong() in 1..MAX_PLAYER_ASSET_BYTES) {
                        AssetBytes(actor, bytes)
                    } else {
                        null
                    }
                } catch (_: Exception) {
                    null
                }
                // SSHD's preview assets are versioned with the app. An older externally
                // generated GLB can restore its raw bind pose, so its package wins. BOTW keeps
                // user-generated equipment overrides authoritative.
                return if (profile == PlayerRenderProfile.Sshd) {
                    listOfNotNull(loadedFromPackage, loadedFromFile)
                } else {
                    listOfNotNull(loadedFromFile, loadedFromPackage)
                }
            }
            val fallbackActor = if (profile == PlayerRenderProfile.Botw) {
                when (slot) {
                    SLOT_HEAD, SLOT_CHEST, SLOT_LEGS -> defaultActors.getValue(slot)
                    else -> null
                }
            } else {
                null
            }
            val requestedModels = loadActorCandidates(requestedActor)
            val fallbackModels = fallbackActor
                ?.takeUnless { it == requestedActor }
                ?.let(::loadActorCandidates)
                .orEmpty()
                .map { fallback ->
                    AssetBytes(requestedActor, fallback.bytes, fallback.sourceActor)
                }
            val candidates = requestedModels + fallbackModels
            post {
                if (released || releasing || requestGenerations[slot] != generation) return@post
                pendingSlots.remove(slot)
                if (candidates.isEmpty()) {
                    if (missingActors.add(requestedActor)) {
                        ModuleLog(host).warning(
                            "[BOTW Companion] No player model for $requestedActor; " +
                                "keeping the current $slot model"
                        )
                    }
                    updateReadiness()
                } else {
                    installSlot(slot, candidates)
                }
            }
        }
    }

    private fun installSlot(slot: String, models: List<AssetBytes>) {
        val loader = assetLoader
        val resources = resourceLoader
        val targetScene = scene
        if (loader == null || resources == null || targetScene == null) {
            updateReadiness()
            return
        }
        if (models.any { model ->
                installSlotCandidate(slot, model, loader, resources, targetScene)
            }
        ) {
            updateReadiness()
            return
        }
        val actor = models.first().actor
        if (missingActors.add(actor)) {
            ModuleLog(host).warning(
                "[BOTW Companion] Every player model candidate for $actor failed; " +
                    "keeping the current $slot model"
            )
        }
        updateReadiness()
    }

    private fun installSlotCandidate(
        slot: String,
        model: AssetBytes,
        loader: AssetLoader,
        resources: ResourceLoader,
        targetScene: Scene
    ): Boolean {
        var createdAsset: FilamentAsset? = null
        var addedToScene = false
        try {
            val buffer = ByteBuffer.allocateDirect(model.bytes.size)
                .order(ByteOrder.nativeOrder())
                .put(model.bytes)
            buffer.flip()
            val asset = loader.createAsset(buffer)
            if (asset == null) {
                ModuleLog(host).warning(
                    "[BOTW Companion] Invalid player model candidate ${model.sourceActor}"
                )
                return false
            }
            createdAsset = asset
            resources.loadResources(asset)
            asset.releaseSourceData()
            val animator = asset.instance.animator
            ModuleLog(host).info(
                "[$logName Companion] ${model.sourceActor} animation count=${animator.animationCount}"
            )
            if (animator.animationCount > 0) {
                val idle = (0 until animator.animationCount).firstOrNull { index ->
                    val name = animator.getAnimationName(index)
                    if (profile == PlayerRenderProfile.Sshd) {
                        name.equals("waitS", ignoreCase = true) ||
                            name.contains("waitS", ignoreCase = true)
                    } else {
                        name.contains("Nml_Wait", ignoreCase = true)
                    }
                } ?: 0
                // Use a sampled frame from the game's actual idle clip. This is
                // intentionally not the bind frame: a skinned SSHD asset must
                // never be presented in its raw T-pose while the preview is idle.
                val poseTime = animator.getAnimationDuration(idle) * 0.37f
                animator.applyAnimation(idle, poseTime)
                ModuleLog(host).info(
                    "[$logName Companion] Applied ${animator.getAnimationName(idle)} " +
                        "idle pose at ${"%.3f".format(poseTime)}s to ${model.sourceActor}"
                )
            }


            animator.updateBoneMatrices()
            if (!applySlotTransform(slot, model.sourceActor, asset)) {
                ModuleLog(host).warning(
                    "[BOTW Companion] Player equipment ${model.sourceActor} has no valid attachment"
                )
                loader.destroyAsset(asset)
                createdAsset = null
                return false
            }
            targetScene.addEntities(asset.entities)
            addedToScene = true
            removeSlot(slot)
            loadedSlots[slot] = LoadedSlot(model.actor, asset)
            missingActors.remove(model.actor)
            createdAsset = null
            if (model.sourceActor != model.actor) {
                ModuleLog(host).warning(
                    "[BOTW Companion] Player model ${model.actor} is unavailable; " +
                        "using ${model.sourceActor}"
                )
            }
            return true
        } catch (exception: Exception) {
            createdAsset?.let { asset ->
                if (addedToScene) targetScene.removeEntities(asset.entities)
                loader.destroyAsset(asset)
            }
            ModuleLog(host).error(
                "[BOTW Companion] Player model candidate ${model.sourceActor} failed: " +
                    exception.message
            )
            return false
        }
    }

    private fun removeSlot(slot: String) {
        if (slot == SLOT_BASE) {
            // A held asset is parented to this skeleton, so release it before its parent.
            heldSlots.forEach { heldSlot ->
                requestGenerations[heldSlot] = (requestGenerations[heldSlot] ?: 0) + 1
                pendingSlots.remove(heldSlot)
                removeSlot(heldSlot)
            }
        }
        val loaded = loadedSlots.remove(slot) ?: return
        previewBoneEntities.remove(slot)
        loaded.asset.entities.forEach(restBoneTransforms::remove)
        scene?.removeEntities(loaded.asset.entities)
        assetLoader?.destroyAsset(loaded.asset)
    }

    private fun updateReadiness() {
        val ready = shouldBeVisible && requiredBodySlots.all(loadedSlots::containsKey)
        val wasReady = bodyReady
        bodyReady = ready
        alpha = if (ready) 1f else 0f
        visibility = if (ready) VISIBLE else INVISIBLE



        if (ready && pendingSlots.isEmpty()) {
            displayedPreviewFrame?.let(::applyPreviewFrame)
            fitCameraToLoadedAssets()
            requestRenderFrame("readiness")
            startPreviewAnimation()
        }
        if (ready && !wasReady) {
            val actors = requiredBodySlots.joinToString { slot ->
                "$slot=${loadedSlots[slot]?.actor ?: "missing"}"
            }
            ModuleLog(host).info("[BOTW Companion] Player model ready: $actors")
        }
        if (!ready && shouldBeVisible && desiredActors.isNotEmpty() && pendingSlots.isEmpty()) {


            abandonRenderer("required player model did not load")
        }
    }

    private fun hasCurrentCachedPortrait(): Boolean {
        val bitmap = frameBitmap ?: return false
        return cachedAppearance == appearance && !bitmap.isRecycled &&
            bitmap.width == viewportWidth && bitmap.height == viewportHeight
    }

    private fun abandonRenderer(reason: String) {
        if (released || releasing || engine == null) return
        val keepBitmap = hasCurrentCachedPortrait()
        if (!keepBitmap) {
            alpha = 0f
            visibility = INVISIBLE
        }
        releaseRenderer(keepBitmap = keepBitmap, terminal = false)
        ModuleLog(host).warning("[BOTW Companion] Player renderer retired: $reason")
    }


    private fun fitCameraToLoadedAssets() {
        val nextCamera = camera ?: return
        if (viewportWidth <= 0 || viewportHeight <= 0 || loadedSlots.isEmpty()) return

        val completeBounds = Bounds3()
        val bodyBounds = Bounds3()
        val baseBounds = Bounds3()
        loadedSlots.forEach { (slot, loaded) ->
            val bounds = transformedBounds(loaded.asset)
            completeBounds.include(bounds)
            if (slot == SLOT_BASE) {
                baseBounds.include(bounds)
            }
            if (slot in bodySlots) {
                bodyBounds.include(bounds)
            }
        }
        if (!completeBounds.valid) return




        val framingBounds = baseBounds.takeIf { it.valid }
            ?: bodyBounds.takeIf { it.valid }
            ?: completeBounds
        val centerX = (framingBounds.minimumX + framingBounds.maximumX) * 0.5f
        val centerY = (framingBounds.minimumY + framingBounds.maximumY) * 0.5f
        val centerZ = (framingBounds.minimumZ + framingBounds.maximumZ) * 0.5f
        val halfY = max(
            0.05f,
            (framingBounds.maximumY - framingBounds.minimumY) * 0.5f
        )
        val bodyHalfX = (framingBounds.maximumX - framingBounds.minimumX) * 0.5f
        val bodyHalfZ = (framingBounds.maximumZ - framingBounds.minimumZ) * 0.5f
        val bodyRotationRadius = max(
            0.05f,
            sqrt(bodyHalfX * bodyHalfX + bodyHalfZ * bodyHalfZ)
        )



        val cappedBodyWidth = min(bodyRotationRadius, halfY * 0.52f)
        val framingHalfWidth = cappedBodyWidth

        val verticalFovDegrees = 34.0
        val verticalHalfAngle = Math.toRadians(verticalFovDegrees * 0.5)
        val aspect = viewportWidth.toDouble() / viewportHeight.toDouble()
        val horizontalHalfAngle = atan(tan(verticalHalfAngle) * aspect)
        val verticalDistance = halfY / tan(verticalHalfAngle).toFloat()
        val horizontalDistance = framingHalfWidth / tan(horizontalHalfAngle).toFloat()
        val distance = max(verticalDistance, horizontalDistance) * PORTRAIT_FIT_MARGIN

        nextCamera.setProjection(
            verticalFovDegrees,
            aspect,
            0.05,
            max(12.0, distance.toDouble() + 5.0),
            Camera.Fov.VERTICAL
        )
        nextCamera.lookAt(
            centerX.toDouble(), centerY.toDouble(), (centerZ + distance).toDouble(),
            centerX.toDouble(), centerY.toDouble(), centerZ.toDouble(),
            0.0, 1.0, 0.0
        )
    }


    private fun transformedBounds(asset: FilamentAsset): Bounds3 {
        val result = Bounds3()
        val box = asset.boundingBox
        val center = box.center
        val half = box.halfExtent
        val transforms = engine?.transformManager
        val matrix = if (transforms != null && transforms.hasComponent(asset.root)) {
            transforms.getWorldTransform(transforms.getInstance(asset.root), FloatArray(16))
        } else {
            floatArrayOf(
                1f, 0f, 0f, 0f,
                0f, 1f, 0f, 0f,
                0f, 0f, 1f, 0f,
                0f, 0f, 0f, 1f
            )
        }
        for (x in floatArrayOf(center[0] - half[0], center[0] + half[0])) {
            for (y in floatArrayOf(center[1] - half[1], center[1] + half[1])) {
                for (z in floatArrayOf(center[2] - half[2], center[2] + half[2])) {
                    result.include(
                        matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12],
                        matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13],
                        matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14]
                    )
                }
            }
        }
        return result
    }

    private fun applySlotTransform(slot: String, actor: String, asset: FilamentAsset): Boolean {
        if (slot in heldSlots) return applyHeldSlotTransform(slot, asset)
        if (profile == PlayerRenderProfile.Sshd && slot != SLOT_BASE) {
            val spec = indexedAssets[actor]?.attachment ?: return false
            val player = loadedSlots[SLOT_BASE]?.asset ?: return false
            val parentEntity = player.getFirstEntityByName(spec.bone)
            val nextEngine = engine ?: return false
            val transforms = nextEngine.transformManager
            if (parentEntity == 0 || !transforms.hasComponent(parentEntity) ||
                !transforms.hasComponent(asset.root)
            ) {
                return false
            }
            val rootInstance = transforms.getInstance(asset.root)
            transforms.setParent(rootInstance, transforms.getInstance(parentEntity))
            transforms.setTransform(rootInstance, attachmentTransform(spec))
            ModuleLog(host).info("[SSHD Companion] Attached $actor to ${spec.bone}")
            return true
        }
        if (slot in bodySlots) {
            applyYaw(asset)
            return true
        }
        if (slot !in equipmentSlots) return false

        val spec = indexedAssets[actor]?.attachment ?: return false
        val player = loadedSlots[SLOT_BASE]?.asset ?: return false
        val parentEntity = player.getFirstEntityByName(spec.bone)
        val nextEngine = engine ?: return false
        val transforms = nextEngine.transformManager
        if (parentEntity == 0 || !transforms.hasComponent(parentEntity) ||
            !transforms.hasComponent(asset.root)
        ) {
            return false
        }

        val rootInstance = transforms.getInstance(asset.root)
        transforms.setParent(rootInstance, transforms.getInstance(parentEntity))
        transforms.setTransform(
            rootInstance,
            attachmentTransform(
                spec,
                localYAdjustment = if (slot == SLOT_SHIELD) SHIELD_BACK_DEPTH_OFFSET else 0f
            )
        )
        ModuleLog(host).info("[BOTW Companion] Attached $actor to ${spec.bone}")
        return true
    }


    private fun attachmentTransform(
        spec: AttachmentSpec,
        localYAdjustment: Float = 0f
    ): FloatArray {
        val x = Math.toRadians(spec.rotationDegrees[0].toDouble()).toFloat()
        val y = Math.toRadians(spec.rotationDegrees[1].toDouble()).toFloat()
        val z = Math.toRadians(spec.rotationDegrees[2].toDouble()).toFloat()
        val cx = cos(x)
        val sx = sin(x)
        val cy = cos(y)
        val sy = sin(y)
        val cz = cos(z)
        val sz = sin(z)



        return floatArrayOf(
            cz * cy,
            sz * cy,
            -sy,
            0f,
            cz * sy * sx - sz * cx,
            sz * sy * sx + cz * cx,
            cy * sx,
            0f,
            cz * sy * cx + sz * sx,
            sz * sy * cx - cz * sx,
            cy * cx,
            0f,
            spec.translation[0],
            spec.translation[1] + localYAdjustment,
            spec.translation[2],
            1f
        )
    }

    private fun applyYaw(asset: FilamentAsset) {
        val nextEngine = engine ?: return
        val transforms = nextEngine.transformManager
        if (!transforms.hasComponent(asset.root)) return
        val c = cos(yawRadians)
        val s = sin(yawRadians)
        val unitScale = if (profile == PlayerRenderProfile.Sshd) 0.01f else 1f
        transforms.setTransform(
            transforms.getInstance(asset.root),
            floatArrayOf(
                c * unitScale, 0f, -s * unitScale, 0f,
                0f, unitScale, 0f, 0f,
                s * unitScale, 0f, c * unitScale, 0f,
                0f, 0f, 0f, 1f
            )
        )
    }

    private fun applyYawToAll() {
        loadedSlots.forEach { (slot, loaded) ->


            if ((profile == PlayerRenderProfile.Sshd && slot == SLOT_BASE) ||
                (profile == PlayerRenderProfile.Botw && slot in bodySlots)
            ) {
                applyYaw(loaded.asset)
            }
        }
    }

    private fun logPipelineDiagnostics(event: String, force: Boolean = false) {
        val now = SystemClock.uptimeMillis()
        if (!force && now - lastDiagnosticAt < DIAGNOSTIC_INTERVAL_MS) return
        lastDiagnosticAt = now
        val readbackMs = if (readbackStartedAt == 0L) 0.0 else
            (SystemClock.elapsedRealtimeNanos() - readbackStartedAt) / 1_000_000.0
        ModuleLog(host).info(
            "[$logName Companion] Player pipeline mode=$playerPipelineMode event=$event " +
                "engine=${engine != null}, target=${renderTarget != null}, " +
                "requests=$renderRequests, attempts=$frameAttempts, rendered=$renderedFrames, " +
                "beginFails=$beginFrameFailures, readbacks=$readbacksCompleted/$readbacksStarted, " +
                "pumps=$readbackPumpFrames, pendingSlots=${pendingSlots.size}, " +
                "dirty=$renderDirty, readPending=$readbackPending, last=$lastRenderReason, " +
                "readbackMs=${"%.2f".format(readbackMs)}"
        )
    }

    private fun requestRenderFrame(reason: String) {
        if (released || releasing) return
        renderRequests++
        lastRenderReason = reason
        renderDirty = true
        if (playerPipelineMode < 2) {
            logPipelineDiagnostics("request-suppressed")
            return
        }
        if (swapChain == null || renderTarget == null || alpha == 0f) return
        if (!readbackPending) frameRetryBudget = MAX_FRAME_RETRIES
        scheduleRenderFrame()
    }

    private fun scheduleRenderFrame() {
        if (released || releasing || framePending || swapChain == null || renderTarget == null ||
            alpha == 0f || (!renderDirty && !readbackPending) || frameRetryBudget <= 0
        ) return
        frameRetryBudget--
        framePending = true
        Choreographer.getInstance().postFrameCallback(this)
    }

    private fun finishReadback(
        buffer: ByteBuffer,
        width: Int,
        height: Int,
        generation: Int,
        renderedAppearance: BotwPlayerAppearance?,
        renderedYaw: Float,
        readbackId: Int
    ) {


        if (activeReadbackId != readbackId) return
        activeReadbackId = 0
        readbackPending = false
        readbacksCompleted++
        if (!released && !releasing && generation == targetGeneration &&
            width == viewportWidth && height == viewportHeight
        ) {
            try {
                val bitmap = frameBitmap?.takeIf {
                    it.width == width && it.height == height && !it.isRecycled
                } ?: Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
                buffer.rewind()
                bitmap.copyPixelsFromBuffer(buffer)
                frameBitmap = bitmap
                if (!renderDirty && pendingSlots.isEmpty() && renderedAppearance != null &&
                    renderedAppearance == appearance && renderedYaw == yawRadians
                ) {
                    cachedAppearance = renderedAppearance
                }
                invalidate()
            } catch (exception: Exception) {
                ModuleLog(host).error("[BOTW Companion] Player readback failed: ${exception.message}")
            }
        }

        if (framePending) {
            Choreographer.getInstance().removeFrameCallback(this)
            framePending = false
        }
        if (renderDirty && !released && !releasing) {
            frameRetryBudget = MAX_FRAME_RETRIES
            scheduleRenderFrame()
        } else {
            frameRetryBudget = 0
        }
        logPipelineDiagnostics("readback-complete", force = true)
        readbackStartedAt = 0L
    }

    private fun scheduleReadbackTimeout(generation: Int, readbackId: Int) {
        mainHandler.postDelayed(
            {
                if (!released && !releasing && engine != null && readbackPending &&
                    activeReadbackId == readbackId && targetGeneration == generation
                ) {
                    logPipelineDiagnostics("readback-timeout", force = true)
                    abandonRenderer("readback did not complete within ${READBACK_TIMEOUT_MS}ms")
                }
            },
            READBACK_TIMEOUT_MS
        )
    }
    override fun doFrame(frameTimeNanos: Long) {
        framePending = false
        frameAttempts++
        val nextRenderer = renderer ?: return
        val nextSwapChain = swapChain ?: return
        val nextView = filamentView ?: return
        val nextTarget = renderTarget ?: return

        if (readbackPending) {
            readbackPumpFrames++


            if (nextRenderer.beginFrame(nextSwapChain, frameTimeNanos)) {
                nextRenderer.endFrame()
            }
            if (frameRetryBudget > 0) scheduleRenderFrame() else engine?.flush()
            return
        }
        if (!renderDirty) return

        if (nextRenderer.beginFrame(nextSwapChain, frameTimeNanos)) {
            if (playerPipelineMode == 2) {
                renderDirty = false
                try {
                    nextRenderer.render(nextView)
                    renderedFrames++
                } catch (exception: Exception) {
                    renderDirty = true
                    ModuleLog(host).error("[BOTW Companion] Player render-only frame failed: ${exception.message}")
                } finally {
                    nextRenderer.endFrame()
                }
                frameRetryBudget = 0
                logPipelineDiagnostics("render-only", force = true)
                return
            }
            val width = viewportWidth
            val height = viewportHeight
            val generation = targetGeneration
            val renderedAppearance = appearance
            val renderedYaw = yawRadians
            nextReadbackId += 1
            val readbackId = nextReadbackId
            val byteCount = width * height * 4
            val buffer = readbackBuffer?.takeIf { it.capacity() >= byteCount }
                ?: ByteBuffer.allocateDirect(byteCount)
                    .order(ByteOrder.nativeOrder())
                    .also { readbackBuffer = it }
            buffer.clear()
            buffer.limit(byteCount)
            val descriptor = Texture.PixelBufferDescriptor(
                buffer,
                Texture.Format.RGBA,
                Texture.Type.UBYTE,
                1,
                0,
                0,
                width,
                mainHandler,
                Runnable {
                    finishReadback(
                        buffer, width, height, generation, renderedAppearance, renderedYaw,
                        readbackId
                    )
                }
            )
            renderDirty = false
            try {
                nextRenderer.render(nextView)
                renderedFrames++
                readbackPending = true
                activeReadbackId = readbackId
                readbacksStarted++
                readbackStartedAt = SystemClock.elapsedRealtimeNanos()
                nextRenderer.readPixels(nextTarget, 0, 0, width, height, descriptor)
                scheduleReadbackTimeout(generation, readbackId)
            } catch (exception: Exception) {
                if (activeReadbackId == readbackId) {
                    activeReadbackId = 0
                    readbackPending = false
                }
                renderDirty = true
                ModuleLog(host).error("[BOTW Companion] Player frame failed: ${exception.message}")
            } finally {
                nextRenderer.endFrame()
            }
            if (!readbackPending) {
                frameRetryBudget = 0
                mainHandler.post {
                    if (!released && !releasing && engine != null && renderDirty &&
                        !readbackPending
                    ) {
                        abandonRenderer("snapshot render/readback failed")
                    }
                }
                return
            }
            frameRetryBudget = MAX_FRAME_RETRIES
            scheduleRenderFrame()
        } else {
            beginFrameFailures++


            if (frameRetryBudget > 0) {
                scheduleRenderFrame()
            } else {
                mainHandler.post {
                    if (!released && !releasing && engine != null && renderDirty &&
                        !readbackPending && frameRetryBudget <= 0
                    ) {
                        abandonRenderer("beginFrame retry budget exhausted")
                    }
                }
            }
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (alpha == 0f) return false
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                lastTouchX = event.x
                portraitDragged = false
            }
            MotionEvent.ACTION_MOVE -> {
                val delta = event.x - lastTouchX
                lastTouchX = event.x
                if (kotlin.math.abs(delta) > 0.5f) {
                    portraitDragged = true
                    yawRadians += delta * 0.009f



                    if (engine != null) applyYawToAll()
                }
            }
            MotionEvent.ACTION_UP -> {
                if (portraitDragged) {
                    cachedAppearance = null
                    val current = appearance
                    if (engine == null && shouldBeVisible && current != null) {
                        beginAppearanceRender(current)
                    } else if (engine != null) {
                        applyYawToAll()
                        if (pendingSlots.isEmpty()) {
                            fitCameraToLoadedAssets()
                            requestRenderFrame("touch")
                        }
                    }
                }
                portraitDragged = false
            }
            MotionEvent.ACTION_CANCEL -> {
                portraitDragged = false
            }
        }
        return true
    }

    override fun onSizeChanged(width: Int, height: Int, oldWidth: Int, oldHeight: Int) {
        super.onSizeChanged(width, height, oldWidth, oldHeight)
        if (width <= 0 || height <= 0) return
        viewportWidth = width
        viewportHeight = height
        if (engine != null) {
            if (playerPipelineMode >= 2 && !recreateOffscreenTarget(width, height)) return
            if (pendingSlots.isEmpty()) {
                fitCameraToLoadedAssets()
                requestRenderFrame("resize")
            }
        } else {
            val bitmap = frameBitmap
            if (bitmap == null || bitmap.isRecycled || bitmap.width != width ||
                bitmap.height != height
            ) {
                cachedAppearance = null
                appearance?.takeIf { shouldBeVisible }?.let(::beginAppearanceRender)
            }
        }
    }

    override fun onDetachedFromWindow() {
        itemSounds?.release()
        itemSounds = null
        cancelPreviewAction(clearHeldItem = true)
        releaseRenderer(keepBitmap = false, terminal = true)
        super.onDetachedFromWindow()
    }

    private fun releaseRenderer(keepBitmap: Boolean = false, terminal: Boolean = true) {
        itemSounds?.stop()
        if (released || releasing) return
        logPipelineDiagnostics(
            if (keepBitmap) "retire-cached" else "release", force = true
        )
        releasing = true
        heldPreviewGeneration++
        heldPreviewPending = false
        mainHandler.removeCallbacks(heldPreviewPoll)
        nativeHeldPreview = null
        try {
            targetGeneration++
            Choreographer.getInstance().removeFrameCallback(this)
            framePending = false
            readbackPending = false
            activeReadbackId = 0
            renderDirty = false
            frameRetryBudget = 0
            requestGenerations.keys.toList().forEach { slot ->
                requestGenerations[slot] = (requestGenerations[slot] ?: 0) + 1
            }
            pendingSlots.clear()
            previewBoneEntities.clear()
            restBoneTransforms.clear()
            ioExecutor?.shutdownNow()
            ioExecutor = null
            if (!keepBitmap) {
                frameBitmap?.takeUnless(Bitmap::isRecycled)?.let { bitmap ->
                    destroySafely("portrait bitmap") { bitmap.recycle() }
                }
                frameBitmap = null
                cachedAppearance = null
            }
            readbackBuffer = null



            val nextEngine = engine
            engine = null
            if (nextEngine != null) {



                destroySafely("queued GPU work") { nextEngine.flushAndWait() }
                destroyOffscreenTarget(nextEngine)



                val assets = loadedSlots.entries
                    .sortedBy { (slot, _) -> if (slot in equipmentSlots) 0 else 1 }
                    .map { it.value.asset }
                loadedSlots.clear()
                assets.forEach { asset ->
                    destroySafely("player asset scene membership") {
                        scene?.removeEntities(asset.entities)
                    }
                    destroySafely("player asset") { assetLoader?.destroyAsset(asset) }
                }

                val oldLightEntities = lightEntities
                lightEntities = intArrayOf()
                oldLightEntities.forEach { entity ->
                    destroySafely("portrait light scene membership") {
                        scene?.removeEntity(entity)
                    }
                    destroySafely("portrait light") { nextEngine.destroyEntity(entity) }
                    destroySafely("portrait light entity") { EntityManager.get().destroy(entity) }
                }

                val oldIndirectLight = indirectLight
                indirectLight = null
                oldIndirectLight?.let { light ->
                    destroySafely("indirect-light scene membership") {
                        scene?.indirectLight = null
                    }
                    destroySafely("indirect light") { nextEngine.destroyIndirectLight(light) }
                }

                val oldSkybox = transparentSkybox
                transparentSkybox = null
                oldSkybox?.let { skybox ->
                    destroySafely("skybox scene membership") { scene?.skybox = null }
                    destroySafely("transparent skybox") { nextEngine.destroySkybox(skybox) }
                }

                val oldResourceLoader = resourceLoader
                resourceLoader = null
                oldResourceLoader?.let { loader ->
                    destroySafely("resource loader") { loader.destroy() }
                }
                val oldAssetLoader = assetLoader
                assetLoader = null
                oldAssetLoader?.let { loader ->
                    destroySafely("asset loader") { loader.destroy() }
                }
                val oldMaterialProvider = materialProvider
                materialProvider = null
                oldMaterialProvider?.let { provider ->
                    destroySafely("player materials") { provider.destroyMaterials() }
                    destroySafely("material provider") { provider.destroy() }
                }

                val oldCameraEntity = cameraEntity
                cameraEntity = 0
                if (oldCameraEntity != 0) {
                    destroySafely("portrait camera") {
                        nextEngine.destroyCameraComponent(oldCameraEntity)
                    }
                    destroySafely("portrait camera entity") {
                        EntityManager.get().destroy(oldCameraEntity)
                    }
                }

                val oldView = filamentView
                filamentView = null
                oldView?.let { view ->
                    destroySafely("Filament view") { nextEngine.destroyView(view) }
                }
                val oldRenderer = renderer
                renderer = null
                oldRenderer?.let { nextRenderer ->
                    destroySafely("Filament renderer") {
                        nextEngine.destroyRenderer(nextRenderer)
                    }
                }
                val oldScene = scene
                scene = null
                oldScene?.let { nextScene ->
                    destroySafely("Filament scene") { nextEngine.destroyScene(nextScene) }
                }
                destroySafely("final GPU work") { nextEngine.flushAndWait() }
                destroySafely("Filament engine") { nextEngine.destroy() }
            }
        } catch (exception: Exception) {



            ModuleLog(host).error("[BOTW Companion] Player renderer release failed: ${exception.message}")
        } finally {
            requestedActors.clear()
            desiredActors.clear()
            pendingSlots.clear()
            loadedSlots.clear()
            lightEntities = intArrayOf()
            indirectLight = null
            transparentSkybox = null
            resourceLoader = null
            assetLoader = null
            materialProvider = null
            renderTarget = null
            depthTexture = null
            colorTexture = null
            swapChain = null
            engine = null
            renderer = null
            scene = null
            filamentView = null
            camera = null
            cameraEntity = 0
            bodyReady = false
            released = terminal
            releasing = false
        }
    }
}
