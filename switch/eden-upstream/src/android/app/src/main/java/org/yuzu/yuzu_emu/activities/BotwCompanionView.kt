


package org.yuzu.yuzu_emu.activities

import android.content.Context
import android.animation.ValueAnimator
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.ColorMatrixColorFilter
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PorterDuff
import android.graphics.PorterDuffXfermode
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.os.Handler
import android.os.Build
import android.os.Looper
import android.os.SystemClock
import android.util.LruCache
import android.view.InputDevice
import android.view.HapticFeedbackConstants
import android.view.animation.DecelerateInterpolator
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.View
import android.view.VelocityTracker
import android.view.ViewConfiguration
import android.widget.OverScroller
import java.io.File
import java.io.FileOutputStream
import java.security.MessageDigest
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import org.json.JSONArray
import org.json.JSONObject
import org.yuzu.yuzu_emu.BuildConfig
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.utils.Log
import kotlin.math.ceil
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.log2
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin


internal class BotwCompanionView(
    context: Context,
    private val onPlayerAppearanceChanged: (BotwPlayerAppearance, Boolean) -> Unit = { _, _ -> },
    private val onPlayerItemAction: (String, Bitmap?, Boolean) -> Unit = { _, _, _ -> },
    private val onPlayerHeldItemCountChanged: (Int) -> Unit = {}
) : View(context), BotwControllerTarget {
    private enum class Page(val title: String) { Map("Map"), Inventory("Inventory"), Quests("Quests") }
    private enum class ControllerFocus {
        Pages, Categories, Inventory, Runes, Map, QuestFilters, QuestList, ShrinePopup, ItemMenu
    }
    private enum class ContextAction(val label: String) { Hold("Hold"), Eat("Eat") }

    private data class Stats(
        val health: Int?, val maxHealth: Int?, val stamina: Float?, val maxStamina: Float?,
        val rupees: Int?, val defense: Int?, val attack: Int?, val bowAttack: Int?,
        val shieldGuard: Int?
    )

    private data class Item(
        val id: Long, val itemType: Int, val itemUse: Int, val actorName: String,
        val iconActorName: String,
        val name: String, val category: String,
        val power: Int?, val defense: Int?,
        val durability: Int?, val count: Int?, val modifierValue: Int?, val modifierFlags: Int?,
        val equippable: Boolean, val holdable: Boolean, val edible: Boolean,
        val equipped: Boolean, val description: String
    )

    private data class Equipment(val slot: String, val item: Item)
    private data class PlayerMap(
        val x: Float, val y: Float, val z: Float, val heading: Float
    )
    private data class MapPoint(
        val category: String, val name: String, val x: Float, val z: Float,
        val minZoom: Int, val maxZoom: Int,
        val iconAsset: String? = null, val iconWidth: Float = 1f, val iconHeight: Float = 1f,
        val discoveryFlag: String = ""
    )
    private data class ShrinePoint(
        val index: Int, val id: String, val name: String, val trial: String,
        val x: Float, val y: Float, val z: Float
    )
    private data class Quest(
        val id: Long, val actorName: String, val name: String, val objective: String,
        val step: String, val location: String, val type: String, val complete: Boolean
    )
    private data class Effect(val name: String, val seconds: Int, val level: Float)
    private data class Rune(
        val id: String, val name: String, val type: Int, val available: Boolean,
        val upgraded: Boolean, val selected: Boolean
    )
    private data class ChampionPower(
        val id: String, val name: String, val available: Boolean, val enabled: Boolean,
        val uses: Int, val maxUses: Int, val cooldownSeconds: Int
    )
    private data class SheikahSensor(
        val unlocked: Boolean, val upgraded: Boolean, val enabled: Boolean,
        val searchMode: Int
    )
    private data class ActionDiagnostic(
        val sequence: Int = 0, val validationFlags: Long = 0,
        val itemType: Int = 0, val itemUse: Int = 0,
        val quantityBefore: Int = 0, val inInventoryBefore: Boolean = false,
        val tab: Int? = null, val slot: Int? = null, val nodeOffset: Int? = null,
        val tabItemCount: Int? = null, val heldBefore: Int? = null,
        val nativeResult: Int = 0, val target: String = "0x0",
        val pouchManager: String = "0x0", val player: String = "0x0",
        val controller: String = "0x0", val activeFlagPointer: String = "0x0",
        val controllerStateBefore: Int? = null, val controllerActionBefore: Int? = null,
        val controllerActiveBefore: Int? = null, val controllerTargetBefore: String = "0x0",
        val tabHead: String = "0x0", val guestQuantityAfter: Int? = null,
        val guestInInventoryAfter: Boolean? = null, val quantityAfter: Int? = null,
        val inInventoryAfter: Boolean? = null, val heldAfter: Int? = null
    )

    private data class Snapshot(
        val status: String, val saveLoaded: Boolean, val capabilities: Int, val stats: Stats?,
        val inventory: List<Item>,
        val equipment: List<Equipment>, val quests: List<Quest>, val effects: List<Effect>,
        val runes: List<Rune>, val championPowers: List<ChampionPower> = emptyList(),
        val map: PlayerMap? = null,
        val enteredShrines: Set<String> = emptySet(),
        val clearedShrines: Set<String> = emptySet(),
        val equipState: Int = 0, val equipAttempts: Int = 0,
        val actionRequest: String = "0x0", val actionCompleted: String = "0x0",
        val actionType: Long = 0, val actionStage: Int = 0,
        val sensor: SheikahSensor? = null,
        val heldItemCount: Int? = null,
        val actionDiagnostic: ActionDiagnostic = ActionDiagnostic()
    )

    private data class LiveSnapshot(
        val status: String, val stats: Stats?, val selectedRune: Int?, val map: PlayerMap?,
        val heldItemCount: Int?, val actionState: Int, val actionAttempts: Int,
        val actionRequest: String, val actionCompleted: String, val actionType: Long,
        val actionStage: Int, val actionDiagnostic: ActionDiagnostic
    )

    private data class PendingContextAction(
        val item: Item,
        val action: ContextAction,
        val initialQuantity: Int,
        val initialHeldCount: Int,
        val initialHealth: Int?,
        val initialStamina: Float?,
        val initialEffects: List<Effect>,
        val deadline: Long,
        var needsFullSnapshot: Boolean = false
    )

    private data class PollPayload(
        val generation: Int,
        val full: Boolean,
        val raw: String,
        val fullSnapshot: Snapshot?,
        val liveSnapshot: LiveSnapshot?,
        val nativeNanos: Long,
        val parseNanos: Long,
        val totalNanos: Long
    )

    companion object {
        private const val DESIGN_WIDTH = 1240f
        private const val DESIGN_HEIGHT = 1080f
        private const val MAX_RUNTIME_TEXTURE_BYTES = 8 * 1024 * 1024
        private const val MAX_MAP_TEXTURE_BYTES = 32 * 1024 * 1024
        private const val MAP_TEXTURE_ASSET = "botw/ui/hyrule_map.png"
        private const val CAP_STATS = 1 shl 0
        private const val CAP_INVENTORY = 1 shl 2
        private const val CAP_EQUIPMENT = 1 shl 3
        private const val CAP_QUESTS = 1 shl 4
        private const val CAP_MAP = 1 shl 5
        private const val CAP_ITEM_ACTIONS = 1 shl 6
        private const val CAP_RUNES = 1 shl 8
        private const val CAP_FAST_TRAVEL = 1 shl 9
        private const val CAP_SHEIKAH_SENSOR = 1 shl 10
        private const val CAP_HOLD_ACTIONS = 1 shl 11
        private const val CAP_EAT_ACTIONS = 1 shl 12
        private const val ACTION_EQUIP_ITEM = 1
        private const val ACTION_SELECT_RUNE = 2
        private const val ACTION_FAST_TRAVEL = 3
        private const val ACTION_EAT_ITEM = 4
        private const val ACTION_HOLD_ITEM = 5
        private const val ACTION_UNEQUIP_ITEM = 6
        private const val MAILBOX_TYPE_HOLD = 0xFFFF_FFFDL
        private const val MAILBOX_TYPE_EAT = 0xFFFF_FFFCL
        private const val MAILBOX_TYPE_UNEQUIP = 0xFFFF_FFFBL


        private val SNAPSHOT_POLL_MODE = BuildConfig.BOTW_SNAPSHOT_POLL_MODE
        private const val LIVE_STATS_POLL_INTERVAL_MS = 1_000L
        private const val FULL_SNAPSHOT_INTERVAL_MS = 5_000L
        private const val ACTION_REFRESH_DELAY_MS = 120L
        private const val EQUIP_REFRESH_RETRY_MS = 200L
        private const val RUNTIME_TEXTURE_RETRY_MS = 2_000L
        private const val RUNE_REFRESH_TIMEOUT_MS = 1_000L
        private const val EQUIP_REFRESH_TIMEOUT_MS = 8_500L
        private const val CONTEXT_ACTION_TIMEOUT_MS = 4_000L
        private const val MAX_HELD_ITEMS = 5
        private const val TRANSITION_GRACE_MS = 30_000L
        private const val DIAGNOSTIC_INTERVAL_MS = 10_000L
        private const val CATEGORY_VISIBLE_COUNT = 4
        private const val CATEGORY_LEFT = 18f
        private const val CATEGORY_TOP = 98f
        private const val CATEGORY_RIGHT = 669f
        private const val CATEGORY_BOTTOM = 174f
        private const val CATEGORY_CELL_WIDTH =
            (CATEGORY_RIGHT - CATEGORY_LEFT) / CATEGORY_VISIBLE_COUNT
        private const val GRID_COLUMNS = 5
        private const val GRID_ROWS_VISIBLE = 4
        private const val GRID_CELL_WIDTH = 122f
        private const val GRID_CELL_HEIGHT = GRID_CELL_WIDTH
        private const val GRID_GAP = 6f
        private const val GRID_START_X = 20f
        private const val GRID_START_Y = 184f
        private const val GRID_BOTTOM =
            GRID_START_Y + GRID_ROWS_VISIBLE * (GRID_CELL_HEIGHT + GRID_GAP) - GRID_GAP
        private const val ITEM_CARD_TOP = 702f
        private const val ITEM_CARD_BOTTOM = 872f
        private const val POWER_ROW_TOP = 884f
        private const val POWER_ROW_BOTTOM = 1048f
        private const val QUEST_LIST_LEFT = 30f
        private const val QUEST_LIST_RIGHT = 520f
        private const val QUEST_LIST_TOP = 214f
        private const val QUEST_LIST_BOTTOM = 1046f
        private const val QUEST_ROW_STRIDE = 104f
        private const val QUEST_ROW_HEIGHT = 96f
        private const val QUEST_VISIBLE_ROWS = 8


        private const val MAP_WORLD_MIN = -6000f
        private const val MAP_WORLD_MAX = 6000f
        private const val MAP_MAX_ZOOM = 12f
        private const val MAP_PIN_HALF_SIZE = 33f
        private val MAP_SCALE_DISTANCES = floatArrayOf(2_000f, 1_000f, 500f, 200f, 100f, 50f)
        private val MAP_MARKER_CATEGORIES = setOf(
            "Sheikah Tower", "Shrine", "Stable", "Village"
        )
        private val categories = listOf(
            "Weapons", "Bows", "Shields", "Armor", "Materials", "Food", "Key Items"
        )

        private val categoryIconAssets = listOf(
            "botw/ui/categories/Nt_PauseIconL_00_s.png",
            "botw/ui/categories/Nt_PauseIconL_01_s.png",
            "botw/ui/categories/Nt_PauseIconL_02_s.png",
            "botw/ui/categories/Nt_PauseIconL_03_s.png",
            "botw/ui/categories/Nt_PauseIconL_04_s.png",
            "botw/ui/categories/Nt_PauseIconL_05_s.png",
            "botw/ui/categories/Nt_PauseIconL_06_s.png"
        )
    }

    private val handler = Handler(Looper.getMainLooper())
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val path = Path()
    private val normalTypeface = Typeface.create("sans-serif", Typeface.NORMAL)
    private val backgroundGradient = LinearGradient(
        0f, 0f, DESIGN_WIDTH, DESIGN_HEIGHT,
        intArrayOf(Color.rgb(2, 15, 18), Color.rgb(3, 24, 27), Color.rgb(1, 10, 13)),
        null, Shader.TileMode.CLAMP
    )
    private var page = Page.Inventory
    private var categoryIndex = 0
    // Presentation only: never delays input, game requests, or snapshot publication.
    private inner class UiTween(initial: Float) {
        var value = initial
            private set
        private var destination = initial
        private var animator: ValueAnimator? = null
        fun animateTo(target: Float, durationMs: Long = 180L, from: Float = value) {
            stop()
            destination = target
            value = target
            if (durationMs <= 0L || !isAttachedToWindow || !ValueAnimator.areAnimatorsEnabled()) {
                postInvalidateOnAnimation()
                return
            }
            value = from
            animator = ValueAnimator.ofFloat(from, target).apply {
                duration = durationMs
                interpolator = DecelerateInterpolator()
                addUpdateListener { value = it.animatedValue as Float; postInvalidateOnAnimation() }
                start()
            }
        }
        fun stop() { animator?.cancel(); animator = null }
        fun finish() { stop(); value = destination }
    }
    private val pageReveal = UiTween(1f)
    private val menuReveal = UiTween(1f)
    private val selectionGlow = UiTween(0f)
    private val navigationPosition = UiTween(page.ordinal.toFloat())
    private val touchRipple = UiTween(1f)
    private var animatedPage = page
    private var animatedCategory = categoryIndex
    private var animatedQuestFilter = 0
    private var animatedSelection: Pair<Int, Long?>? = null
    private var animatedContextId: Long? = null
    private var rippleX = 0f
    private var rippleY = 0f
    private val feedbackPaint = Paint(Paint.ANTI_ALIAS_FLAG)

    private fun updateUiMotion() {
        if (animatedPage != page) {
            navigationPosition.animateTo(page.ordinal.toFloat(), 220L)
        }
        if (animatedPage != page || animatedCategory != categoryIndex || animatedQuestFilter != questFilter) {
            pageReveal.animateTo(1f, from = .86f)
        }
        animatedPage = page
        animatedCategory = categoryIndex
        animatedQuestFilter = questFilter
        val selection = selectedItem to selectedQuestId
        if (animatedSelection != selection) selectionGlow.animateTo(0f, 240L, 1f)
        animatedSelection = selection
        if (animatedContextId != contextItemId) {
            if (contextItemId != null) menuReveal.animateTo(1f, 160L, 0f)
            else menuReveal.stop()
            animatedContextId = contextItemId
        }
    }

    private fun stopUiMotion() {
        listOf(pageReveal,menuReveal,selectionGlow,navigationPosition,touchRipple).forEach { it.finish() }
    }

    private fun confirmActionFeedback() {
        if (!isAttachedToWindow || !isShown) return
        // No IGNORE_* flags: honor system and per-view haptic preferences. Android
        // routes this to an available device actuator; controller rumble is separate.
        performHapticFeedback(if (Build.VERSION.SDK_INT >= 30) HapticFeedbackConstants.CONFIRM
            else HapticFeedbackConstants.KEYBOARD_TAP)
        selectionGlow.animateTo(0f, 280L, 1f)
    }
    private var categoryScrollX = 0f
    private var selectedItem = 0
    private var inventoryScrollY = 0f
    private var rowOffset: Int
        get() = (inventoryScrollY / (GRID_CELL_HEIGHT + GRID_GAP)).toInt()
        set(value) { inventoryScrollY = value * (GRID_CELL_HEIGHT + GRID_GAP) }
    private val inventoryRowRemainder: Float
        get() = inventoryScrollY - rowOffset * (GRID_CELL_HEIGHT + GRID_GAP)
    private var controllerActive = false
    private var controllerFocus = ControllerFocus.Pages
    private var controllerRuneIndex = 0
    private var controllerPopupAction = 0
    private var controllerMotionAt = 0L
    private var contextItemId: Long? = null
    private var contextActionIndex = 0
    private var selectedQuestId: Long? = null
    private var questFilter = 0
    private var questScroll = 0
    private var questMoved = false
    private var draggingQuests = false
    private var downQuestScroll = 0
    private var downQuestId: Long? = null
    private var mapZoom = 1f
    private var mapCenterX = 0f
    private var mapCenterZ = 0f
    private var mapCentered = false
    private var draggingMap = false
    private var mapMoved = false
    private var downMapCenterX = 0f
    private var downMapCenterZ = 0f
    private var selectedShrine: ShrinePoint? = null
    private val mapScaleDetector = ScaleGestureDetector(
        context,
        object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
            override fun onScaleBegin(detector: ScaleGestureDetector): Boolean {
                if (page != Page.Map) return false
                val viewScale = min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT)
                if (viewScale <= 0f || !viewScale.isFinite()) return false
                val focusX = (detector.focusX - (width - DESIGN_WIDTH * viewScale) / 2f) / viewScale
                val focusY = (detector.focusY - (height - DESIGN_HEIGHT * viewScale) / 2f) / viewScale
                if (!mapViewportRect().contains(focusX, focusY)) return false
                multiTouchGesture = true
                mapMoved = true
                draggingMap = false
                return true
            }

            override fun onScale(detector: ScaleGestureDetector): Boolean {
                if (page != Page.Map || !detector.scaleFactor.isFinite()) return false
                val viewScale = min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT)
                if (viewScale <= 0f || !viewScale.isFinite()) return false
                val focusX = (detector.focusX - (width - DESIGN_WIDTH * viewScale) / 2f) / viewScale
                val focusY = (detector.focusY - (height - DESIGN_HEIGHT * viewScale) / 2f) / viewScale
                val viewport = mapViewportRect()
                if (!viewport.contains(focusX, focusY)) return false

                val worldRange = MAP_WORLD_MAX - MAP_WORLD_MIN
                val oldVisibleWorld = worldRange / mapZoom
                val focusOffsetX = (focusX - viewport.centerX()) / viewport.width()
                val focusOffsetZ = (focusY - viewport.centerY()) / viewport.height()
                val focusWorldX = mapCenterX + focusOffsetX * oldVisibleWorld
                val focusWorldZ = mapCenterZ + focusOffsetZ * oldVisibleWorld
                val nextZoom = (mapZoom * detector.scaleFactor).coerceIn(1f, MAP_MAX_ZOOM)
                if (nextZoom == mapZoom) return true
                val nextVisibleWorld = worldRange / nextZoom
                mapZoom = nextZoom
                mapCenterX = focusWorldX - focusOffsetX * nextVisibleWorld
                mapCenterZ = focusWorldZ - focusOffsetZ * nextVisibleWorld
                clampMapCenter()
                mapCentered = true
                invalidate()
                return true
            }
        }
    )
    private var snapshot = Snapshot(
        "not_running", false, 0, null, emptyList(), emptyList(), emptyList(), emptyList(),
        emptyList()
    )
    private var downX = 0f
    private var downY = 0f
    private var downInventoryScrollY = 0f
    private val inventoryScroller = OverScroller(context)
    private val inventoryTouchConfig = ViewConfiguration.get(context)
    private var inventoryVelocity: VelocityTracker? = null
    private var downCategoryScrollX = 0f
    private var downInventoryCell: Int? = null
    private var downInventoryItemId: Long? = null
    private var downCategoryIndex: Int? = null
    private var downRuneType: Int? = null
    private var draggingInventory = false
    private var draggingScrollbar = false
    private var draggingCategories = false
    private var inventoryMoved = false
    private var categoryMoved = false
    private var multiTouchGesture = false
    private var lastPlayerAppearance: BotwPlayerAppearance? = null
    private var lastPlayerAppearanceVisible = false
    private var filteredInventorySource: List<Item>? = null
    private var filteredInventoryCategory = -1
    private var filteredInventoryItems: List<Item> = emptyList()
    private var visibleInventorySource: List<Item>? = null
    private var visibleInventoryCategory = -1
    private var visibleInventoryRow = -1
    private var visibleInventorySelection = -1
    private var visibleInventoryCache: List<Item> = emptyList()
    private var inventoryCountSource: List<Item>? = null
    private var inventoryCategoryCounts = IntArray(categories.size)
    private var lastLoggedStatus = ""
    private var lastLoggedCapabilities = -1
    private var lastLoggedEquipState = -1
    private var lastLoggedEquipAttempts = -1
    private var lastLoggedActionRequest = ""
    private var lastLoggedActionCompleted = ""
    private var lastLoggedActionType = -1L
    private var lastLoggedActionStage = -1
    private var lastLoggedActionDiagnosticSequence = -1
    private var lastRawSnapshot = ""
    private var lastRawLiveSnapshot = ""
    private var lastFullSnapshotAt = 0L
    private var pendingRuneType: Int? = null
    private var pendingRuneRefreshDeadline = 0L
    private var pendingEquipItemId: Long? = null
    private var pendingEquipExpectedState: Boolean? = null
    private var pendingEquipActionName = "Equip"
    private var pendingEquipRefreshDeadline = 0L
    private var pendingEquipNeedsFullSnapshot = false
    private var pendingContextAction: PendingContextAction? = null
    private var transientStateSince = 0L
    private var lastTransientStatus = ""
    private var pollInFlight = false
    private var pollGeneration = 0
    private var pollExecutor: ExecutorService? = null
    private var textureExecutor: ExecutorService? = null
    private var textureGeneration = 0
    @Volatile private var textureRevision: String? = null
    private var consecutivePollFailures = 0
    private var pollSamples = 0L
    private var fullPollSamples = 0L
    private var livePollSamples = 0L
    private var changedPollSamples = 0L
    private var nativePollNanos = 0L
    private var parsePollNanos = 0L
    private var totalPollNanos = 0L
    private var maximumNativePollNanos = 0L
    private var maximumTotalPollNanos = 0L
    private var drawSamples = 0L
    private var drawNanos = 0L
    private var maximumDrawNanos = 0L
    private var lastDiagnosticAt = 0L
    private val runtimeTextureRequests = mutableSetOf<String>()
    private val runtimeTextureMissing = mutableSetOf<String>()
    private val stockIconAliases: Map<String, String> by lazy {
        try {
            context.assets.open("botw/stock_aliases.json").bufferedReader().use { reader ->
                val root = JSONObject(reader.readText())
                buildMap(root.length()) {
                    root.keys().forEach { actor -> put(actor, root.getString(actor)) }
                }
            }
        } catch (exception: Exception) {
            Log.error("[BOTW Companion] Stock icon aliases failed: ${exception.message}")
            emptyMap()
        }
    }
    private val bitmapCache = object : LruCache<String, Bitmap>(24 * 1024) {
        override fun sizeOf(key: String, value: Bitmap): Int = max(1, value.byteCount / 1024)

        // Canvas display lists can still reference an evicted bitmap on RenderThread.
        // Let Android release it after those references expire; eviction is not ownership.
    }
    private var mapBitmap: Bitmap? = null
    private val mapMarkers: List<MapPoint> by lazy {
        loadNativeMapPoints()
    }
    private var mapDiscovery: BotwMapDiscovery? = null
    private var mapDiscoveryPending = false
    private var mapDiscoveryScheduled = false
    private var mapDiscoveryEpoch = 0
    private fun clearMapDiscovery() {
        mapDiscoveryEpoch++
        handler.removeCallbacks(mapDiscoveryPoll)
        mapDiscoveryScheduled = false
        mapDiscoveryPending = false
        mapDiscovery = null
        selectedShrine = null
    }
    private val mapDiscoveryPoll = object : Runnable {
        override fun run() {
            mapDiscoveryScheduled = false
            if (!isAttachedToWindow || page != Page.Map || !isSaveLoaded() || mapDiscoveryPending) return
            val executor = pollExecutor ?: return
            val generation = pollGeneration
            val previous = mapDiscovery
            val epoch = mapDiscoveryEpoch
            mapDiscoveryPending = true
            executor.execute {
                val next = runCatching {
                    BotwMapDiscovery.parse(NativeLibrary.getBotwMapDiscovery(previous?.revision ?: ""), previous)
                }.getOrNull()
                handler.post {
                    if (generation != pollGeneration || epoch != mapDiscoveryEpoch) return@post
                    mapDiscoveryPending = false
                    if (mapDiscovery !== next) {
                        mapDiscovery = next
                        if (selectedShrine?.let { !isShrineDiscovered(it) } == true) selectedShrine = null
                        if (page == Page.Map) postInvalidateOnAnimation()
                    }
                    if (page == Page.Map && isAttachedToWindow && isSaveLoaded()) {
                        mapDiscoveryScheduled = true
                        handler.postDelayed(this, 250L)
                    }
                }
            }
        }
    }

    private fun isShrineDiscovered(shrine: ShrinePoint): Boolean =
        mapDiscovery != null && (mapDiscovery!!.visible.contains("Location_${shrine.id}") ||
            shrine.id in snapshot.enteredShrines || shrine.id in snapshot.clearedShrines)
    private val shrines: List<ShrinePoint> by lazy { loadShrines() }




    private val categoryIconTintFilters by lazy {
        arrayOf(categoryMaskTint(muted), categoryMaskTint(cyan))
    }
    private val heartIconTint by lazy { categoryMaskTint(Color.rgb(234, 56, 62)) }
    private val warmWhiteIconTint by lazy { categoryMaskTint(warmWhite) }
    private val cyanIconTint by lazy { categoryMaskTint(cyan) }
    private val mutedIconTint by lazy { categoryMaskTint(muted) }
    private val cyanMapIconTint by lazy { mapMaskTint(cyan) }
    private val townMapIconTint by lazy {
        mapMaterialTint(Color.rgb(111, 66, 16), Color.rgb(150, 120, 40))
    }
    private val shrineBlueMapIconTint by lazy { mapMaskTint(Color.rgb(0, 150, 255)) }
    private val shrineGlowMapIconTint by lazy { mapMaskTint(Color.rgb(0, 72, 255)) }
    private val shrineOrangeMapIconTint by lazy { mapMaskTint(Color.rgb(255, 150, 0)) }
    private val shrineShadowMapIconTint by lazy { mapMaskTint(Color.BLACK) }
    private val shrineFlashMapIconTint by lazy { mapMaskTint(Color.WHITE) }
    private val additiveMapBlend by lazy { PorterDuffXfermode(PorterDuff.Mode.ADD) }
    private val championIconTints by lazy {
        mapOf(
            "grace" to categoryMaskTint(Color.rgb(92, 172, 213)),
            "fury" to categoryMaskTint(Color.rgb(181, 146, 53)),
            "gale" to categoryMaskTint(Color.rgb(112, 202, 128)),
            "protection" to categoryMaskTint(Color.rgb(226, 61, 55))
        )
    }

    private val poll = object : Runnable {
        override fun run() {
            if (SNAPSHOT_POLL_MODE == 0 || !isAttachedToWindow || pollInFlight) return
            val now = SystemClock.uptimeMillis()
            val requestFull = !isSaveLoaded() || lastFullSnapshotAt == 0L ||
                now - lastFullSnapshotAt >= FULL_SNAPSHOT_INTERVAL_MS
            val generation = pollGeneration
            val executor = pollExecutor ?: return
            pollInFlight = true
            try {
                executor.execute {
                    val pollStartedAt = SystemClock.elapsedRealtimeNanos()
                    try {
                        val nativeStartedAt = SystemClock.elapsedRealtimeNanos()
                        val raw = if (requestFull) {
                            NativeLibrary.getBotwCompanionSnapshot()
                        } else {
                            NativeLibrary.getBotwCompanionStatsSnapshot()
                        }
                        val nativeNanos = SystemClock.elapsedRealtimeNanos() - nativeStartedAt
                        val parseStartedAt = SystemClock.elapsedRealtimeNanos()
                        val fullSnapshot = if (requestFull) parseSnapshot(raw) else null
                        val liveSnapshot = if (requestFull) null else parseLiveSnapshot(raw)
                        val parseNanos = SystemClock.elapsedRealtimeNanos() - parseStartedAt
                        val totalNanos = SystemClock.elapsedRealtimeNanos() - pollStartedAt
                        handler.post {
                            applyPollPayload(
                                PollPayload(
                                    generation, requestFull, raw, fullSnapshot, liveSnapshot,
                                    nativeNanos, parseNanos, totalNanos
                                )
                            )
                        }
                    } catch (exception: Exception) {
                        val totalNanos = SystemClock.elapsedRealtimeNanos() - pollStartedAt
                        handler.post { applyPollFailure(generation, exception, totalNanos) }
                    }
                }
            } catch (exception: Exception) {
                pollInFlight = false
                applyPollFailure(generation, exception, 0L)
            }
        }
    }

    private val actionRefresh = object : Runnable {
        override fun run() {
            if (!isAttachedToWindow) return
            if (pollInFlight) {
                handler.postDelayed(this, 40L)
                return
            }



            val contextNeedsFull = pendingContextAction?.needsFullSnapshot == true
            if ((pendingRuneType != null && pendingEquipItemId == null) ||
                (pendingContextAction != null && !contextNeedsFull && pendingEquipItemId == null) ||
                (pendingEquipItemId != null && !pendingEquipNeedsFullSnapshot)
            ) {
                lastRawLiveSnapshot = ""
            } else {
                lastRawSnapshot = ""
                lastFullSnapshotAt = 0L
            }
            handler.removeCallbacks(poll)
            handler.post(poll)
        }
    }

    private fun createPollExecutor(): ExecutorService =
        Executors.newSingleThreadExecutor { command ->
            Thread(
                {
                    try {
                        android.os.Process.setThreadPriority(
                            android.os.Process.THREAD_PRIORITY_BACKGROUND
                        )
                    } catch (_: SecurityException) {

                    }
                    command.run()
                },
                "BOTW companion snapshots"
            ).apply {
                isDaemon = true
                priority = Thread.MIN_PRIORITY
            }
        }

    private fun createTextureExecutor(): ExecutorService =
        Executors.newSingleThreadExecutor { command ->
            Thread(
                {
                    try {
                        android.os.Process.setThreadPriority(
                            android.os.Process.THREAD_PRIORITY_LOWEST
                        )
                    } catch (_: SecurityException) {

                    }
                    command.run()
                },
                "BOTW RomFS textures"
            ).apply {
                isDaemon = true
                priority = Thread.MIN_PRIORITY
            }
        }

    private fun applyPollPayload(payload: PollPayload) {
        if (payload.generation != pollGeneration) return
        pollInFlight = false
        if (!isAttachedToWindow) return
        var changed = false
        if (payload.full) {
            fullPollSamples++
            val next = payload.fullSnapshot ?: return scheduleNextPoll()
            if (!next.saveLoaded) clearMapDiscovery()
            if (preserveStableSnapshot(next.status, next.saveLoaded)) {
                lastFullSnapshotAt = 0L
                recordPollTiming(payload.nativeNanos, payload.parseNanos, payload.totalNanos, false)
                if (!schedulePendingActionRefresh(fullSnapshot = true)) scheduleNextPoll()
                return
            }
            clearTransientState(next.status)
            lastFullSnapshotAt = SystemClock.uptimeMillis()
            next.heldItemCount?.let(onPlayerHeldItemCountChanged)




            if (payload.raw != lastRawSnapshot || next != snapshot) {
                val selectedId = filteredItems().getOrNull(selectedItem)?.id
                lastRawSnapshot = payload.raw
                lastRawLiveSnapshot = ""
                snapshot = next
                if (!next.saveLoaded || selectedShrine?.let {
                        it.id !in next.enteredShrines && it.id !in next.clearedShrines
                    } == true
                ) {
                    selectedShrine = null
                }
                if (!next.saveLoaded) {
                    pendingRuneType = null
                    pendingRuneRefreshDeadline = 0L
                    pendingEquipItemId = null
                    pendingEquipExpectedState = null
                    pendingEquipRefreshDeadline = 0L
                    pendingEquipNeedsFullSnapshot = false
                    pendingContextAction = null
                }
                changed = true
                logActionSnapshot(
                    next.status, next.capabilities, next.inventory.size, next.equipment.size,
                    next.runes.count { it.available }, next.stats?.rupees, next.equipState,
                    next.equipAttempts, next.actionRequest, next.actionCompleted, next.actionType,
                    next.actionStage, next.actionDiagnostic
                )
                clampSelection(selectedId)
                clampQuestSelection()
                if (!mapCentered) recenterMap(next.map, redraw = false)
                notifyPlayerAppearance()
                invalidate()
            }
        } else {
            livePollSamples++
            val live = payload.liveSnapshot


            if (live != null) {
                lastRawLiveSnapshot = payload.raw
                if (live.status == "ready" && isSaveLoaded()) {
                    if (live.actionState != snapshot.equipState ||
                        live.actionAttempts != snapshot.equipAttempts ||
                        live.actionRequest != snapshot.actionRequest ||
                        live.actionCompleted != snapshot.actionCompleted ||
                        live.actionType != snapshot.actionType ||
                        live.actionStage != snapshot.actionStage ||
                        live.actionDiagnostic != snapshot.actionDiagnostic
                    ) {
                        snapshot = snapshot.copy(
                            equipState = live.actionState,
                            equipAttempts = live.actionAttempts,
                            actionRequest = live.actionRequest,
                            actionCompleted = live.actionCompleted,
                            actionType = live.actionType,
                            actionStage = live.actionStage,
                            actionDiagnostic = live.actionDiagnostic
                        )
                        changed = true
                    }
                    logActionSnapshot(
                        live.status, snapshot.capabilities, snapshot.inventory.size,
                        snapshot.equipment.size, snapshot.runes.count { it.available },
                        live.stats?.rupees ?: snapshot.stats?.rupees, live.actionState,
                        live.actionAttempts, live.actionRequest, live.actionCompleted,
                        live.actionType, live.actionStage, live.actionDiagnostic
                    )
                    live.heldItemCount?.let { count ->
                        onPlayerHeldItemCountChanged(count)
                        if (count != snapshot.heldItemCount) {
                            snapshot = snapshot.copy(heldItemCount = count)
                            changed = true
                        }
                    }
                    live.stats?.let {
                        val merged = mergeLiveStats(it)
                        if (merged != snapshot.stats) {
                            snapshot = snapshot.copy(stats = merged)
                            changed = true
                        }
                    }
                    if (mergeLiveRuneSelection(live.selectedRune)) {
                        changed = true
                    }
                    live.map?.let { position ->
                        if (position != snapshot.map) {
                            snapshot = snapshot.copy(map = position)
                            if (!mapCentered) recenterMap(position, redraw = false)
                            changed = true
                        }
                    }
                    if (changed) {
                        invalidate()
                    }
                } else if (live.status != "ready") {
                    clearMapDiscovery()
                    lastFullSnapshotAt = 0L
                    if (isSaveLoaded() && !preserveStableSnapshot(live.status, false)) {
                        snapshot = Snapshot(
                            live.status, false, 0, null, emptyList(), emptyList(), emptyList(),
                            emptyList(), emptyList()
                        )
                        selectedShrine = null
                        pendingRuneType = null
                        pendingRuneRefreshDeadline = 0L
                        pendingEquipItemId = null
                        pendingEquipExpectedState = null
                        pendingEquipRefreshDeadline = 0L
                        pendingEquipNeedsFullSnapshot = false
                        pendingContextAction = null
                        lastRawSnapshot = ""
                        clampSelection()
                        notifyPlayerAppearance()
                        changed = true
                        invalidate()
                    }
                }
            }
        }
        consecutivePollFailures = 0
        recordPollTiming(payload.nativeNanos, payload.parseNanos, payload.totalNanos, changed)
        if (!schedulePendingActionRefresh(payload.full)) scheduleNextPoll()
    }

    private fun applyPollFailure(generation: Int, exception: Exception, totalNanos: Long) {
        if (generation != pollGeneration) return
        pollInFlight = false
        if (!isAttachedToWindow) return
        consecutivePollFailures++
        if (consecutivePollFailures == 1 || consecutivePollFailures % 10 == 0) {
            Log.error(
                "[BOTW Companion] Snapshot failed ($consecutivePollFailures): " +
                    exception.message
            )
        }
        if (isSaveLoaded()) markTransientState("poll_error")
        lastFullSnapshotAt = 0L
        recordPollTiming(0L, 0L, totalNanos, changed = false)
        if (!schedulePendingActionRefresh(fullSnapshot = false)) scheduleNextPoll()
    }

    private fun preserveStableSnapshot(status: String, saveLoaded: Boolean): Boolean {
        if (!isSaveLoaded()) return false
        if (status == "not_running" || status == "wrong_title" ||
            status == "unsupported_build"
        ) return false
        if (status == "ready" && saveLoaded) return false
        markTransientState(status)
        return SystemClock.uptimeMillis() - transientStateSince < TRANSITION_GRACE_MS
    }

    private fun markTransientState(status: String) {
        if (transientStateSince == 0L) transientStateSince = SystemClock.uptimeMillis()
        if (lastTransientStatus != status) {
            lastTransientStatus = status
            Log.info("[BOTW Companion] Preserving stable UI during transient state=$status")
        }
    }

    private fun clearTransientState(status: String) {
        if (transientStateSince != 0L) {
            Log.info("[BOTW Companion] Stable game state restored as $status")
        }
        transientStateSince = 0L
        lastTransientStatus = ""
    }

    private fun recordPollTiming(
        nativeNanos: Long, parseNanos: Long, totalNanos: Long, changed: Boolean
    ) {
        pollSamples++
        if (changed) changedPollSamples++
        nativePollNanos += nativeNanos
        parsePollNanos += parseNanos
        totalPollNanos += totalNanos
        maximumNativePollNanos = maxOf(maximumNativePollNanos, nativeNanos)
        maximumTotalPollNanos = maxOf(maximumTotalPollNanos, totalNanos)
        val now = SystemClock.uptimeMillis()
        if (now - lastDiagnosticAt >= DIAGNOSTIC_INTERVAL_MS) {
            lastDiagnosticAt = now
            val sampleDivisor = pollSamples.coerceAtLeast(1).toDouble()
            val drawDivisor = drawSamples.coerceAtLeast(1).toDouble()
            Log.info(
                "[BOTW Companion] Poll diagnostics mode=$SNAPSHOT_POLL_MODE " +
                    "samples=$pollSamples, full=$fullPollSamples, live=$livePollSamples, " +
                    "changed=$changedPollSamples, " +
                    "nativeAvgMs=${"%.3f".format(nativePollNanos / sampleDivisor / 1e6)}, " +
                    "nativeMaxMs=${"%.3f".format(maximumNativePollNanos / 1e6)}, " +
                    "parseAvgMs=${"%.3f".format(parsePollNanos / sampleDivisor / 1e6)}, " +
                    "totalAvgMs=${"%.3f".format(totalPollNanos / sampleDivisor / 1e6)}, " +
                    "totalMaxMs=${"%.3f".format(maximumTotalPollNanos / 1e6)}, " +
                    "draws=$drawSamples, drawAvgMs=${"%.3f".format(drawNanos / drawDivisor / 1e6)}, " +
                    "drawMaxMs=${"%.3f".format(maximumDrawNanos / 1e6)}, " +
                    "jsonChars=${lastRawSnapshot.length}"
            )
        }
    }

    private fun scheduleNextPoll() {
        val freezeReady = SNAPSHOT_POLL_MODE == 1 && isSaveLoaded()
        if (isAttachedToWindow && !freezeReady) {
            handler.postDelayed(poll, LIVE_STATS_POLL_INTERVAL_MS)
        }
    }

    init {
        setBackgroundColor(Color.rgb(2, 13, 16))
        isFocusable = true
        isFocusableInTouchMode = true
        keepScreenOn = true
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        BotwControllerBridge.attach(this)
        handler.removeCallbacks(poll)
        handler.removeCallbacks(actionRefresh)
        pollGeneration++
        pollInFlight = false
        pollExecutor?.shutdownNow()
        pollExecutor = createPollExecutor()
        textureGeneration++
        textureExecutor?.shutdownNow()
        textureExecutor = createTextureExecutor()
        textureRevision = null
        runtimeTextureRequests.clear()
        runtimeTextureMissing.clear()
        lastRawSnapshot = ""
        lastRawLiveSnapshot = ""
        lastFullSnapshotAt = 0L
        pendingRuneType = null
        pendingRuneRefreshDeadline = 0L
        pendingEquipItemId = null
        pendingEquipExpectedState = null
        pendingEquipRefreshDeadline = 0L
        pendingEquipNeedsFullSnapshot = false
        pendingContextAction = null
        transientStateSince = 0L
        lastTransientStatus = ""
        mapCentered = false
        mapZoom = 1f
        selectedQuestId = null
        questScroll = 0
        consecutivePollFailures = 0
        handler.post(poll)
    }

    override fun onDetachedFromWindow() {
        stopUiMotion()
        clearMapDiscovery()
        stopInventoryMotion()
        BotwControllerBridge.detach(this)
        handler.removeCallbacks(poll)
        handler.removeCallbacks(actionRefresh)
        pollGeneration++
        pollInFlight = false
        pollExecutor?.shutdownNow()
        pollExecutor = null
        textureGeneration++
        textureExecutor?.shutdownNow()
        textureExecutor = null
        textureRevision = null
        runtimeTextureRequests.clear()
        runtimeTextureMissing.clear()
        pendingRuneType = null
        pendingRuneRefreshDeadline = 0L
        pendingEquipItemId = null
        pendingEquipExpectedState = null
        pendingEquipRefreshDeadline = 0L
        pendingEquipNeedsFullSnapshot = false
        pendingContextAction = null
        transientStateSince = 0L
        lastTransientStatus = ""
        lastPlayerAppearance?.let { onPlayerAppearanceChanged(it, false) }
        lastPlayerAppearanceVisible = false
        bitmapCache.evictAll()
        mapBitmap?.takeUnless(Bitmap::isRecycled)?.recycle()
        mapBitmap = null
        super.onDetachedFromWindow()
    }

    override fun onDraw(canvas: Canvas) {
        val drawStartedAt = SystemClock.elapsedRealtimeNanos()
        super.onDraw(canvas)
        val scale = min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT)
        canvas.save()
        canvas.translate((width - DESIGN_WIDTH * scale) / 2f, (height - DESIGN_HEIGHT * scale) / 2f)
        canvas.scale(scale, scale)
        if (!isSaveLoaded()) {
            drawWaitingForGame(canvas)
            canvas.restore()
            recordDrawTime(drawStartedAt)
            return
        }
        drawBackground(canvas)
        updateUiMotion()
        drawTopNavigation(canvas)
        val pageLayer = if (pageReveal.value < 1f) canvas.saveLayerAlpha(
            0f, 86f, DESIGN_WIDTH, DESIGN_HEIGHT, (pageReveal.value*255).roundToInt()) else canvas.save()
        when (page) {
            Page.Inventory -> drawInventoryPage(canvas)
            Page.Map -> drawMapPage(canvas)
            Page.Quests -> drawQuestPage(canvas)
        }
        canvas.restoreToCount(pageLayer)
        contextItem()?.let {
            val menuLayer = if (menuReveal.value < 1f) canvas.saveLayerAlpha(
                0f, 0f, DESIGN_WIDTH, DESIGN_HEIGHT, (menuReveal.value*255).roundToInt()) else canvas.save()
            drawItemContextMenu(canvas, it)
            canvas.restoreToCount(menuLayer)
        }
        drawControllerFocus(canvas)
        if (touchRipple.value < 1f) {
            feedbackPaint.color = Color.argb(((1f-touchRipple.value)*65).roundToInt(), 65, 225, 239)
            canvas.drawCircle(rippleX,rippleY,10f+touchRipple.value*30f,feedbackPaint)
        }
        canvas.restore()
        recordDrawTime(drawStartedAt)
    }

    private fun recordDrawTime(startedAt: Long) {
        val elapsed = SystemClock.elapsedRealtimeNanos() - startedAt
        drawSamples++
        drawNanos += elapsed
        maximumDrawNanos = maxOf(maximumDrawNanos, elapsed)
    }

    private fun isSaveLoaded(candidate: Snapshot = snapshot): Boolean =
        candidate.status == "ready" && candidate.saveLoaded

    private fun drawWaitingForGame(canvas: Canvas) {
        paint.shader = backgroundGradient
        canvas.drawRect(0f, 0f, DESIGN_WIDTH, DESIGN_HEIGHT, paint)
        paint.shader = null
        val card = RectF(258f, 314f, 982f, 766f)
        panel(canvas, card)
        drawSheikahMedallion(canvas, card.centerX(), card.top + 128f, 62f)
        val title = when (snapshot.status) {
            "unsupported_build" -> "Unsupported Breath of the Wild build"
            "wrong_title" -> "Waiting for Breath of the Wild"
            "not_running" -> "Waiting for Breath of the Wild"
            else -> "Waiting for the save to load"
        }
        val detail = when (snapshot.status) {
            "unsupported_build" -> "This companion currently supports the validated 1.9.0 build."
            "wrong_title" -> "Open Breath of the Wild to activate its companion interface."
            "not_running" -> "Start the game to activate the lower-screen interface."
            else -> "The interface will appear when Link and the pouch are ready."
        }
        text(
            canvas,
            title,
            card.centerX(),
            card.top + 258f,
            29f,
            warmWhite,
            Paint.Align.CENTER
        )
        text(canvas, detail, card.centerX(), card.top + 306f, 17f, muted, Paint.Align.CENTER)
        stroke.color = bronzeDark
        canvas.drawLine(card.left + 94f, card.top + 340f, card.right - 94f, card.top + 340f, stroke)
        text(
            canvas, "DUAL-SCREEN COMPANION", card.centerX(), card.top + 388f,
            13f, cyan, Paint.Align.CENTER
        )
        cornerMarks(canvas, card, if (snapshot.status == "unsupported_build") bronze else cyan)
    }

    private fun drawBackground(canvas: Canvas) {
        paint.shader = backgroundGradient
        canvas.drawRect(0f, 0f, DESIGN_WIDTH, DESIGN_HEIGHT, paint)
        paint.shader = null
        stroke.color = Color.argb(45, 37, 205, 219)
        stroke.strokeWidth = 1f
        for (x in 28..1220 step 82) canvas.drawLine(x.toFloat(), 90f, x.toFloat() - 150f, 1016f, stroke)
        paint.color = Color.argb(30, 44, 211, 224)
        for (x in 18..1220 step 105) for (y in 102..1000 step 94) canvas.drawCircle(x.toFloat(), y.toFloat(), 1.25f, paint)
    }

    private fun drawTopNavigation(canvas: Canvas) {
        val gap = 8f
        val margin = 10f
        val tabWidth = (DESIGN_WIDTH - margin * 2f - gap * 2f) / 3f
        Page.entries.forEachIndexed { index, value ->
            val rect = RectF(margin + index * (tabWidth + gap), 9f, margin + index * (tabWidth + gap) + tabWidth, 79f)
            panel(canvas, rect, page == value)
            drawPageIcon(canvas, value, rect.left + 55f, rect.centerY())
            text(canvas, value.title, rect.centerX() + 14f, rect.centerY() + 10f, 25f, if (page == value) cyan else warmWhite, Paint.Align.CENTER)
            if (page == value) cornerMarks(canvas, rect, cyan)
        }
        val left = margin + navigationPosition.value*(tabWidth+gap)
        feedbackPaint.color = cyan
        canvas.drawRoundRect(RectF(left+24f,76f,left+tabWidth-24f,79f),1.5f,1.5f,feedbackPaint)
    }

    private fun drawInventoryPage(canvas: Canvas) {
        val left = RectF(10f, 90f, 677f, 1070f)
        val rightStats = RectF(687f, 90f, 1230f, 340f)
        val rightPlayer = RectF(687f, 350f, 1230f, 1070f)
        panel(canvas, left)
        panel(canvas, rightStats)
        panel(canvas, rightPlayer)
        if (snapshot.capabilities and CAP_INVENTORY != 0) {
            // Texture readiness belongs to each icon, not the entire inventory. A newly
            // exposed uncached row must never replace already-visible slots with a loader.
            exactTexturesReady(categoryIconAssets)
            exactItemTexturesReady(visibleInventoryItems())
            drawCategoryBar(canvas)
            drawInventoryGrid(canvas)
            drawSelectedItem(canvas)
            drawChampionPowers(canvas)
        } else {
            drawCalibrating(canvas, RectF(19f, 164f, 668f, 998f), "Reading inventory")
        }
        drawStats(canvas, rightStats)
        drawPlayerPanel(canvas, rightPlayer)
    }

    private fun drawCategoryBar(canvas: Canvas) {
        val rect = RectF(CATEGORY_LEFT, CATEGORY_TOP, CATEGORY_RIGHT, CATEGORY_BOTTOM)
        paint.color = Color.argb(115, 1, 17, 20)
        canvas.drawRect(rect, paint)
        canvas.save()
        canvas.clipRect(rect)
        categories.forEachIndexed { index, name ->
            val x = rect.left + index * CATEGORY_CELL_WIDTH - categoryScrollX
            val tabRect = RectF(x + 3f, rect.top + 3f, x + CATEGORY_CELL_WIDTH - 3f, rect.bottom - 3f)
            if (tabRect.right < rect.left || tabRect.left > rect.right) return@forEachIndexed
            if (index == categoryIndex) {
                paint.color = Color.argb(45, 32, 220, 235)
                canvas.drawRect(tabRect, paint)
                paint.color = cyan
                canvas.drawRect(tabRect.left + 5f, rect.bottom - 4f, tabRect.right - 5f, rect.bottom, paint)
            }
            stroke.color = if (index == categoryIndex) Color.argb(115, 33, 219, 234) else bronzeDark
            canvas.drawRect(tabRect, stroke)
            drawCategoryIcon(canvas, index, x + 34f, rect.centerY(), index == categoryIndex)
            text(
                canvas, shortCategory(name), x + 59f, rect.top + 31f, 18f,
                if (index == categoryIndex) warmWhite else muted
            )
            val count = inventoryCategoryCount(index)
            text(
                canvas, "$count item${if (count == 1) "" else "s"}", x + 59f,
                rect.top + 55f, 12f, if (index == categoryIndex) cyan else bronze
            )
        }
        canvas.restore()
        drawCategoryScrollIndicators(canvas, rect)
    }

    private fun drawCategoryScrollIndicators(canvas: Canvas, rect: RectF) {
        stroke.color = cyan
        stroke.strokeWidth = 2.5f
        if (categoryScrollX > 1f) {
            paint.color = Color.argb(175, 1, 18, 22)
            canvas.drawRect(rect.left, rect.top, rect.left + 20f, rect.bottom, paint)
            canvas.drawLine(rect.left + 13f, rect.centerY() - 8f, rect.left + 7f, rect.centerY(), stroke)
            canvas.drawLine(rect.left + 7f, rect.centerY(), rect.left + 13f, rect.centerY() + 8f, stroke)
        }
        if (categoryScrollX < maximumCategoryScroll() - 1f) {
            paint.color = Color.argb(175, 1, 18, 22)
            canvas.drawRect(rect.right - 20f, rect.top, rect.right, rect.bottom, paint)
            canvas.drawLine(rect.right - 13f, rect.centerY() - 8f, rect.right - 7f, rect.centerY(), stroke)
            canvas.drawLine(rect.right - 7f, rect.centerY(), rect.right - 13f, rect.centerY() + 8f, stroke)
        }
        stroke.strokeWidth = 1f
    }

    private fun maximumCategoryScroll() =
        max(0f, (categories.size - CATEGORY_VISIBLE_COUNT) * CATEGORY_CELL_WIDTH)

    private fun setCategoryScroll(value: Float) {
        categoryScrollX = value.coerceIn(0f, maximumCategoryScroll())
        invalidate()
    }

    private fun snapCategoryScroll() {
        setCategoryScroll(
            (categoryScrollX / CATEGORY_CELL_WIDTH).roundToInt() * CATEGORY_CELL_WIDTH
        )
    }

    private fun ensureCategoryVisible(index: Int) {
        val itemLeft = index * CATEGORY_CELL_WIDTH
        val itemRight = itemLeft + CATEGORY_CELL_WIDTH
        val viewportRight = categoryScrollX + CATEGORY_VISIBLE_COUNT * CATEGORY_CELL_WIDTH
        when {
            itemLeft < categoryScrollX -> setCategoryScroll(itemLeft)
            itemRight > viewportRight ->
                setCategoryScroll(itemRight - CATEGORY_VISIBLE_COUNT * CATEGORY_CELL_WIDTH)
        }
    }

    private fun drawInventoryGrid(canvas: Canvas) {
        val items = filteredItems()
        if (items.isEmpty()) {
            val empty = RectF(GRID_START_X, GRID_START_Y, 651f, GRID_BOTTOM)
            drawSheikahMedallion(canvas, empty.centerX(), empty.centerY() - 36f, 42f)
            text(
                canvas, "No ${categories.getOrElse(categoryIndex) { "items" }.lowercase()} carried",
                empty.centerX(), empty.centerY() + 42f, 18f, muted, Paint.Align.CENTER
            )
            return
        }
        val first = rowOffset * GRID_COLUMNS
        val last = min(items.size, first + GRID_COLUMNS * (GRID_ROWS_VISIBLE + 1))
        val remainder = inventoryRowRemainder
        canvas.save()
        canvas.clipRect(GRID_START_X, GRID_START_Y, 651f, GRID_BOTTOM)
        for (index in first until last) {
            val local = index - first
            val column = local % GRID_COLUMNS
            val row = local / GRID_COLUMNS
            val rect = RectF(
                GRID_START_X + column * (GRID_CELL_WIDTH + GRID_GAP),
                GRID_START_Y + row * (GRID_CELL_HEIGHT + GRID_GAP) - remainder,
                GRID_START_X + column * (GRID_CELL_WIDTH + GRID_GAP) + GRID_CELL_WIDTH,
                GRID_START_Y + row * (GRID_CELL_HEIGHT + GRID_GAP) + GRID_CELL_HEIGHT - remainder
            )
            drawItemCell(canvas, rect, items[index], index == selectedItem)
            if (index == selectedItem && selectionGlow.value > 0f) {
                feedbackPaint.color = Color.argb((selectionGlow.value*42).roundToInt(),65,225,239)
                canvas.drawRoundRect(rect,3f,3f,feedbackPaint)
            }
        }
        canvas.restore()
        val rowCount = ceil(items.size / GRID_COLUMNS.toFloat()).toInt()
        if (rowCount > GRID_ROWS_VISIBLE) {
            val track = RectF(661f, GRID_START_Y, 671f, GRID_BOTTOM)
            paint.color = Color.argb(90, 40, 190, 202)
            canvas.drawRoundRect(track, 5f, 5f, paint)
            val thumbHeight = max(64f, track.height() * GRID_ROWS_VISIBLE / rowCount)
            val thumbY = track.top + (track.height() - thumbHeight) * inventoryScrollY /
                (max(1, rowCount - GRID_ROWS_VISIBLE) * (GRID_CELL_HEIGHT + GRID_GAP))
            paint.color = cyan
            canvas.drawRoundRect(
                RectF(track.left - 2f, thumbY, track.right + 2f, thumbY + thumbHeight),
                7f, 7f, paint
            )
        }
    }

    private fun drawItemCell(canvas: Canvas, rect: RectF, item: Item, selected: Boolean) {
        paint.color = if (selected) Color.argb(75, 19, 181, 198) else Color.argb(110, 3, 20, 23)
        canvas.drawRect(rect, paint)
        stroke.color = if (selected) cyan else bronzeDark
        stroke.strokeWidth = if (selected) 2.5f else 1f
        canvas.drawRect(rect, stroke)
        if (selected) glowRect(canvas, rect)
        if (pendingEquipItemId == item.id && !item.equipped) {
            stroke.color = bronze
            stroke.strokeWidth = 2f
            canvas.drawRect(
                RectF(rect.left + 4f, rect.top + 4f, rect.right - 4f, rect.bottom - 4f), stroke
            )
            stroke.strokeWidth = 1f
        }
        drawItemGlyph(canvas, item, rect.centerX(), rect.centerY() - 3f, 1.18f)
        (item.count ?: item.power ?: item.defense)?.let {
            text(canvas, if (item.count != null && it > 1) "×$it" else "$it", rect.right - 7f, rect.bottom - 7f, 15f, warmWhite, Paint.Align.RIGHT)
        }
        if (item.equipped) {
            paint.color = cyan
            canvas.drawCircle(rect.right - 13f, rect.top + 13f, 9f, paint)
            stroke.color = Color.rgb(2, 24, 27)
            stroke.strokeWidth = 2f
            canvas.drawLine(rect.right - 17f, rect.top + 13f, rect.right - 14f, rect.top + 16f, stroke)
            canvas.drawLine(rect.right - 14f, rect.top + 16f, rect.right - 9f, rect.top + 10f, stroke)
            stroke.strokeWidth = 1f
        }
    }

    private fun drawSelectedItem(canvas: Canvas) {
        val items = filteredItems()
        if (items.isEmpty()) return
        val item = items[selectedItem.coerceIn(items.indices)]
        val rect = RectF(18f, ITEM_CARD_TOP, 669f, ITEM_CARD_BOTTOM)
        paint.color = Color.argb(180, 2, 17, 20)
        canvas.drawRect(rect, paint)
        stroke.color = bronzeDark
        canvas.drawRect(rect, stroke)
        canvas.drawLine(129f, rect.top + 10f, 129f, rect.bottom - 10f, stroke)
        drawItemGlyph(canvas, item, 80f, 786f, 1.25f)
        val pending = pendingEquipItemId == item.id
        val titleWidth = if (item.equipped || pending) 382f else 500f
        text(canvas, ellipsizeToWidth(item.name, titleWidth, 23f), 145f, 736f, 23f, warmWhite)
        text(
            canvas, "${item.category}  •  ${selectedItem + 1} of ${items.size}",
            145f, 760f, 13f, muted
        )
        if (pending) {
            badge(canvas, RectF(526f, 716f, 651f, 750f), "SWITCHING", bronze)
        } else if (item.equipped) {
            badge(canvas, RectF(542f, 716f, 651f, 750f), "EQUIPPED", cyan)
        }
        var badgeX = 145f
        val addDetailBadge = { label: String, value: Int ->
            drawLabeledBadge(canvas, RectF(badgeX, 771f, badgeX + 105f, 811f), label, "$value")
            badgeX += 112f
        }
        item.power?.let { addDetailBadge("Attack", it) }
        item.defense?.let { addDetailBadge("Defense", it) }
        item.count?.let { addDetailBadge("Count", it) }
        item.durability?.let { addDetailBadge("Durability", it) }
        val modifier = modifierLabel(item)
        if (modifier.isNotEmpty() && badgeX <= 521f) {
            drawLabeledBadge(
                canvas, RectF(badgeX, 771f, min(651f, badgeX + 124f), 811f),
                "Modifier", modifier
            )
        }
        if (item.category.equals("Weapons", true) ||
            item.category.equals("Bows", true) ||
            item.category.equals("Shields", true) ||
            item.category.equals("Armor", true)
        ) {
            multilineText(canvas, item.description, 145f, 835f, 500f, 14.5f, muted, 2)
        }
        cornerMarks(canvas, rect, bronze)
    }

    private fun drawStats(canvas: Canvas, rect: RectF) {
        if (snapshot.capabilities and CAP_STATS == 0) {
            drawCalibrating(canvas, rect, "Reading player status")
            return
        }
        val stats = snapshot.stats ?: return
        val requiredTextures = buildList {
            if (stats.health != null && stats.maxHealth != null) add("botw/ui/heart.png")
            if (stats.rupees != null) add("botw/ui/rupee.png")
            add("botw/ui/defense.png")
            add("botw/ui/attack.png")
            add("botw/ui/bow.png")
            add("botw/ui/guard.png")
        }
        if (!exactTexturesReady(requiredTextures)) {
            drawCalibrating(canvas, rect, "Loading exact status textures")
            return
        }
        if (stats.health != null && stats.maxHealth != null) {
            drawHealth(
                canvas, rect.left + 42f, rect.top + 40f,
                stats.health, stats.maxHealth
            )
        }
        stats.rupees?.let {
            drawRupee(canvas, rect.right - 112f, rect.top + 32f)
            text(canvas, "%,d".format(it), rect.right - 27f, rect.top + 43f, 24f, warmWhite, Paint.Align.RIGHT)
        }
        if (stats.stamina != null && stats.maxStamina != null) {
            drawStamina(canvas, rect.left + 42f, rect.top + 110f, stats.stamina, stats.maxStamina)
        }
        val statWidth = rect.width() / 4f
        val statY = rect.bottom - 29f
        statLine(canvas, rect.left + 8f, statY, "Defense", stats.defense, "botw/ui/defense.png", statWidth)
        statLine(canvas, rect.left + statWidth + 8f, statY, "Attack", stats.attack, "botw/ui/attack.png", statWidth)
        statLine(canvas, rect.left + statWidth * 2f + 8f, statY, "Bow", stats.bowAttack, "botw/ui/bow.png", statWidth)
        statLine(canvas, rect.left + statWidth * 3f + 8f, statY, "Guard", stats.shieldGuard, "botw/ui/guard.png", statWidth)
        if (snapshot.effects.isNotEmpty()) {


            val effectsRect = RectF(
                rect.left + 331f, rect.top + 73f, rect.right - 10f, rect.top + 168f
            )
            paint.color = Color.argb(88, 3, 27, 31)
            canvas.drawRect(effectsRect, paint)
            stroke.color = bronzeDark
            canvas.drawRect(effectsRect, stroke)
            snapshot.effects.take(3).forEachIndexed { index, effect ->
                val y = effectsRect.top + 21f + index * 29f
                drawEffectIcon(canvas, effectsRect.left + 22f, y, index)
                text(
                    canvas,
                    ellipsizeToWidth(
                        "${effect.name}  Lv ${effect.level.roundToInt()}", 150f, 13f
                    ),
                    effectsRect.left + 43f, y + 5f, 13f, muted
                )
                text(
                    canvas, formatTime(effect.seconds), effectsRect.right - 9f, y + 5f,
                    13f, warmWhite, Paint.Align.RIGHT
                )
            }
        }
        stroke.color = bronzeDark
        canvas.drawLine(rect.left + 10f, rect.bottom - 77f, rect.right - 10f, rect.bottom - 77f, stroke)
    }

    private fun drawPlayerPanel(canvas: Canvas, rect: RectF) {
        val equipmentTexturesReady = exactItemTexturesReady(snapshot.equipment.map { it.item })
        if (!equipmentTexturesReady) {
            drawCalibrating(canvas, rect, "Loading exact equipment textures")
            cornerMarks(canvas, rect, bronze)
            return
        }
        drawSheikahMedallion(canvas, rect.centerX(), rect.centerY() + 20f, 122f)
        if (snapshot.capabilities and CAP_EQUIPMENT != 0) drawEquipmentSlots(canvas, rect)
        if (snapshot.capabilities and CAP_RUNES != 0) drawRunes(canvas, rect)
        cornerMarks(canvas, rect, bronze)
    }

    private fun equipmentSlotPositions(panel: RectF) = mapOf(
            "Head" to RectF(panel.left + 18f, panel.top + 58f, panel.left + 112f, panel.top + 153f),
            "Chest" to RectF(panel.left + 18f, panel.top + 218f, panel.left + 112f, panel.top + 313f),
            "Legs" to RectF(panel.left + 18f, panel.top + 378f, panel.left + 112f, panel.top + 473f),
            "Weapon" to RectF(panel.right - 112f, panel.top + 58f, panel.right - 18f, panel.top + 153f),
            "Bow" to RectF(panel.right - 112f, panel.top + 218f, panel.right - 18f, panel.top + 313f),
            "Shield" to RectF(panel.right - 112f, panel.top + 378f, panel.right - 18f, panel.top + 473f)
        )

    private fun drawEquipmentSlots(canvas: Canvas, panel: RectF) {
        equipmentSlotPositions(panel).forEach { (slot, rect) ->
            text(canvas, slot, rect.centerX(), rect.top - 9f, 16f, bronze, Paint.Align.CENTER)
            val item = snapshot.equipment.firstOrNull { it.slot == slot }?.item
            if (item != null) {
                drawItemCell(canvas, rect, item, false)
            } else {
                paint.color = Color.argb(80, 3, 20, 23)
                canvas.drawRect(rect, paint)
                stroke.color = bronzeDark
                canvas.drawRect(rect, stroke)
                text(canvas, "—", rect.centerX(), rect.centerY() + 8f, 25f, muted, Paint.Align.CENTER)
            }
        }
    }

    private fun drawMapPage(canvas: Canvas) {
        val outer = RectF(10f, 90f, 1230f, 1070f)
        panel(canvas, outer)
        if (snapshot.capabilities and CAP_MAP == 0 || snapshot.map == null) {
            drawCalibrating(canvas, outer, "Reading map and player position")
            return
        }
        val viewport = mapViewportRect()
        if (!mapDiscoveryPending && !mapDiscoveryScheduled) {
            mapDiscoveryScheduled = true
            handler.post(mapDiscoveryPoll)
        }
        val position = snapshot.map ?: return
        if (!mapCentered) recenterMap(position, redraw = false)
        val map = loadBitmap(MAP_TEXTURE_ASSET)
        val visibleWorld = (MAP_WORLD_MAX - MAP_WORLD_MIN) / mapZoom
        val half = visibleWorld / 2f
        val worldLeft = (mapCenterX - half).coerceIn(
            MAP_WORLD_MIN, MAP_WORLD_MAX - visibleWorld
        )
        val worldTop = (mapCenterZ - half).coerceIn(
            MAP_WORLD_MIN, MAP_WORLD_MAX - visibleWorld
        )
        mapCenterX = worldLeft + half
        mapCenterZ = worldTop + half
        if (map == null) {
            drawCalibrating(canvas, viewport, "Loading Hyrule map terrain from ROMFS")
            return
        }
        val source = android.graphics.Rect(
            (((worldLeft - MAP_WORLD_MIN) / (MAP_WORLD_MAX - MAP_WORLD_MIN)) * map.width)
                .roundToInt(),
            (((worldTop - MAP_WORLD_MIN) / (MAP_WORLD_MAX - MAP_WORLD_MIN)) * map.height)
                .roundToInt(),
            ((((worldLeft + visibleWorld) - MAP_WORLD_MIN) /
                (MAP_WORLD_MAX - MAP_WORLD_MIN)) * map.width).roundToInt(),
            ((((worldTop + visibleWorld) - MAP_WORLD_MIN) /
                (MAP_WORLD_MAX - MAP_WORLD_MIN)) * map.height).roundToInt()
        )
        canvas.drawBitmap(map, source, viewport, paint)
        // Discovery is separate from the terrain texture: an installed texture/cache
        // must never reveal a region that this save has not unlocked.
        canvas.save()
        canvas.clipRect(viewport)
        paint.color = Color.rgb(3, 19, 24)
        paint.alpha = 255
        val discovery = mapDiscovery
        if (discovery == null) {
            canvas.drawRect(viewport, paint)
        } else {
            val scale = viewport.width()/visibleWorld
            canvas.translate(viewport.left-worldLeft*scale, viewport.top-worldTop*scale)
            canvas.scale(scale, scale)
            canvas.drawPath(discovery.fog, paint)
        }
        canvas.restore()
        drawMapMarkers(canvas, viewport, worldLeft, worldTop, visibleWorld)
        drawTowerMarkers(canvas, viewport, worldLeft, worldTop, visibleWorld)
        drawShrineMarkers(canvas, viewport, worldLeft, worldTop, visibleWorld)
        drawSavedMapMarkers(canvas, viewport, worldLeft, worldTop, visibleWorld)
        drawPlayerMapMarker(canvas, viewport, position, worldLeft, worldTop, visibleWorld)
        drawMapGuides(canvas, viewport, visibleWorld)
        stroke.color = cyan
        stroke.strokeWidth = 1.5f
        canvas.drawRect(viewport, stroke)
        stroke.strokeWidth = 1f
        cornerMarks(canvas, viewport, cyan)

        val info = RectF(946f, 112f, 1214f, 1018f)
        paint.color = Color.argb(178, 2, 17, 20)
        canvas.drawRect(info, paint)
        stroke.color = bronzeDark
        canvas.drawRect(info, stroke)
        text(canvas, "LIVE POSITION", info.centerX(), 154f, 15f, cyan, Paint.Align.CENTER)
        drawSheikahMedallion(canvas, info.centerX(), 244f, 54f)
        text(canvas, "X", 976f, 340f, 13f, bronze)
        text(canvas, "%+.1f".format(position.x), 1190f, 340f, 22f, warmWhite, Paint.Align.RIGHT)
        text(canvas, "Y", 976f, 386f, 13f, bronze)
        text(canvas, "%+.1f".format(position.y), 1190f, 386f, 22f, warmWhite, Paint.Align.RIGHT)
        text(canvas, "Z", 976f, 432f, 13f, bronze)
        text(canvas, "%+.1f".format(position.z), 1190f, 432f, 22f, warmWhite, Paint.Align.RIGHT)
        stroke.color = bronzeDark
        canvas.drawLine(970f, 458f, 1190f, 458f, stroke)
        text(canvas, "Altitude", 976f, 495f, 14f, muted)
        text(canvas, "${position.y.roundToInt()} m", 1190f, 495f, 20f, warmWhite, Paint.Align.RIGHT)
        text(canvas, "Zoom", 976f, 548f, 14f, muted)
        text(canvas, "${(mapZoom * 100).roundToInt()}%", 1190f, 548f, 20f, cyan, Paint.Align.RIGHT)
        drawMapButton(canvas, mapZoomOutRect(), "-")
        drawMapButton(canvas, mapZoomInRect(), "+")
        drawMapButton(canvas, mapCenterRect(), "CENTER ON LINK")
        val clearedShrines = shrines.count { it.id in snapshot.clearedShrines }
        text(
            canvas, "$clearedShrines / ${shrines.size} SHRINES CLEARED",
            info.centerX(), 797f, 13f, cyan, Paint.Align.CENTER
        )
        text(canvas, "Towns, stables, towers & shrines", info.centerX(), 823f, 12.5f, muted, Paint.Align.CENTER)
        text(canvas, "Drag to pan  |  Pinch to zoom", info.centerX(), 850f, 13f, muted, Paint.Align.CENTER)
        text(canvas, "Position updates live", info.centerX(), 879f, 14f, green, Paint.Align.CENTER)
        drawSheikahSensorStatus(canvas, info)
        cornerMarks(canvas, info, bronze)
        selectedShrine?.let { drawShrinePopup(canvas, it) }
    }

    private fun drawSheikahSensorStatus(canvas: Canvas, info: RectF) {
        val sensor = snapshot.sensor ?: return
        if (snapshot.capabilities and CAP_SHEIKAH_SENSOR == 0) return

        canvas.drawLine(info.left + 24f, 898f, info.right - 24f, 898f, stroke.apply {
            color = bronzeDark
            strokeWidth = 1f
        })
        val iconX = info.left + 45f
        val iconY = 954f
        stroke.color = if (sensor.unlocked && sensor.enabled) cyan else muted
        stroke.strokeWidth = 2f
        canvas.drawCircle(iconX, iconY, 25f, stroke)
        canvas.drawCircle(iconX, iconY, 15f, stroke)
        canvas.drawLine(iconX, iconY, iconX + 18f, iconY - 18f, stroke)
        paint.color = if (sensor.unlocked && sensor.enabled) cyan else muted
        canvas.drawCircle(iconX, iconY, 5f, paint)
        stroke.strokeWidth = 1f

        val title = if (sensor.upgraded) "SHEIKAH SENSOR +" else "SHEIKAH SENSOR"
        text(canvas, title, info.left + 82f, 927f, 14f, cyan)
        val state = when {
            !sensor.unlocked -> "NOT ACQUIRED"
            sensor.enabled -> "ACTIVE"
            else -> "DISABLED"
        }
        text(
            canvas, state, info.left + 82f, 956f, 17f,
            if (sensor.unlocked && sensor.enabled) green else muted
        )
        val target = when {
            !sensor.unlocked -> "Sensor unavailable"
            sensor.searchMode == 0 -> "Shrine detection"
            else -> "Compendium target"
        }
        text(canvas, target, info.left + 82f, 980f, 13f, warmWhite)
    }

    private fun drawQuestPage(canvas: Canvas) {
        val outer = RectF(10f, 90f, 1230f, 1070f)
        panel(canvas, outer)
        if (snapshot.capabilities and CAP_QUESTS == 0) {
            drawCalibrating(canvas, outer, "Reading quest log")
            return
        }
        clampQuestSelection()
        val listPanel = RectF(22f, 108f, 532f, 1052f)
        val detailPanel = RectF(544f, 108f, 1218f, 1052f)
        paint.color = Color.argb(150, 2, 17, 20)
        canvas.drawRect(listPanel, paint)
        canvas.drawRect(detailPanel, paint)
        stroke.color = bronzeDark
        canvas.drawRect(listPanel, stroke)
        canvas.drawRect(detailPanel, stroke)

        val categoryCounts = QuestCategory.entries.associateWith { category ->
            snapshot.quests.count { !it.complete && questCategory(it) == category }
        }
        val activeCount = snapshot.quests.count { !it.complete }
        text(canvas, "QUEST LOG", 42f, 142f, 18f, warmWhite)
        text(
            canvas, "$activeCount active", 512f, 142f, 13f,
            muted, Paint.Align.RIGHT
        )
        questFilterRects().forEachIndexed { index, rect ->
            val selected = questFilter == index
            paint.color = if (selected) Color.argb(62, 28, 214, 229) else Color.argb(80, 3, 20, 23)
            canvas.drawRect(rect, paint)
            stroke.color = if (selected) cyan else bronzeDark
            canvas.drawRect(rect, stroke)
            val category = QuestCategory.entries[index]
            val filterLabel = "${category.label}  ${categoryCounts[category] ?: 0}"
            text(
                canvas, filterLabel, rect.centerX(),
                rect.centerY() + 6f, 13f, if (selected) cyan else muted, Paint.Align.CENTER
            )
        }

        val quests = filteredQuests()
        val visibleRows = QUEST_VISIBLE_ROWS
        questScroll = questScroll.coerceIn(0, max(0, quests.size - visibleRows))
        quests.drop(questScroll).take(visibleRows).forEachIndexed { local, quest ->
            val rect = questRowRect(local)
            val selected = selectedQuestId == quest.id
            paint.color = if (selected) Color.argb(72, 23, 191, 206) else Color.argb(82, 3, 20, 23)
            canvas.drawRect(rect, paint)
            stroke.color = if (selected) cyan else bronzeDark
            canvas.drawRect(rect, stroke)
            if (selected) {
                paint.color = cyan
                canvas.drawRect(rect.left, rect.top, rect.left + 4f, rect.bottom, paint)
            }
            paint.color = if (quest.complete) green else cyan
            canvas.drawCircle(rect.left + 19f, rect.top + 22f, 6f, paint)
            text(
                canvas, ellipsizeToWidth(quest.name, rect.width() - 53f, 24f),
                rect.left + 34f, rect.top + 31f, 24f,
                if (quest.complete) muted else warmWhite
            )
            val summary = questSummary(quest)
            text(
                canvas, ellipsizeToWidth(summary, rect.width() - 28f, 17f),
                rect.left + 14f, rect.top + 62f, 17f, muted
            )
            text(
                canvas, if (quest.complete) "COMPLETE" else "ACTIVE", rect.right - 12f,
                rect.bottom - 8f, 15f, if (quest.complete) green else cyan, Paint.Align.RIGHT
            )
        }
        drawQuestScrollbar(canvas, quests.size, visibleRows)

        val selected = selectedQuest()
        if (selected == null) {
            drawCalibrating(canvas, detailPanel, "No quests in this filter")
        } else {
            drawQuestDetails(canvas, detailPanel, selected)
        }
        cornerMarks(canvas, listPanel, bronze)
        cornerMarks(canvas, detailPanel, bronze)
    }

    private fun mapViewportRect() = RectF(24f, 112f, 930f, 1018f)
    private fun mapZoomOutRect() = RectF(974f, 590f, 1074f, 654f)
    private fun mapZoomInRect() = RectF(1088f, 590f, 1188f, 654f)
    private fun mapCenterRect() = RectF(974f, 680f, 1188f, 744f)

    private fun drawMapButton(canvas: Canvas, rect: RectF, label: String) {
        paint.color = Color.argb(145, 3, 29, 33)
        canvas.drawRect(rect, paint)
        stroke.color = cyan
        canvas.drawRect(rect, stroke)
        text(
            canvas, label, rect.centerX(), rect.centerY() + 7f,
            if (label.length == 1) 28f else 13f, warmWhite, Paint.Align.CENTER
        )
    }

    private fun recenterMap(position: PlayerMap? = snapshot.map, redraw: Boolean = true) {
        position ?: return
        mapCenterX = position.x.coerceIn(MAP_WORLD_MIN, MAP_WORLD_MAX)
        mapCenterZ = position.z.coerceIn(MAP_WORLD_MIN, MAP_WORLD_MAX)
        clampMapCenter()
        mapCentered = true
        if (redraw) invalidate()
    }

    private fun setMapZoom(value: Float) {
        mapZoom = value.coerceIn(1f, MAP_MAX_ZOOM)
        clampMapCenter()
        invalidate()
    }

    private fun clampMapCenter() {
        val halfVisibleWorld = (MAP_WORLD_MAX - MAP_WORLD_MIN) / mapZoom / 2f
        val minimumCenter = MAP_WORLD_MIN + halfVisibleWorld
        val maximumCenter = MAP_WORLD_MAX - halfVisibleWorld
        mapCenterX = mapCenterX.coerceIn(minimumCenter, maximumCenter)
        mapCenterZ = mapCenterZ.coerceIn(minimumCenter, maximumCenter)
    }

    private fun worldToMap(
        viewport: RectF, x: Float, z: Float, worldLeft: Float, worldTop: Float,
        visibleWorld: Float
    ): Pair<Float, Float> =
        viewport.left + (x - worldLeft) / visibleWorld * viewport.width() to
            viewport.top + (z - worldTop) / visibleWorld * viewport.height()

    private fun mapDataZoom(): Int =
        (ceil(log2(mapZoom.toDouble())).toInt() + 1).coerceIn(0, 6)

    private fun drawMapGuides(canvas: Canvas, viewport: RectF, visibleWorld: Float) {
        val compassX = viewport.right - 42f
        val compassY = viewport.top + 43f
        paint.color = Color.argb(165, 1, 15, 19)
        canvas.drawCircle(compassX, compassY, 27f, paint)
        stroke.color = bronze
        stroke.strokeWidth = 1.5f
        canvas.drawCircle(compassX, compassY, 27f, stroke)
        canvas.drawLine(compassX, compassY - 18f, compassX, compassY + 18f, stroke)
        canvas.drawLine(compassX - 5f, compassY + 9f, compassX, compassY + 18f, stroke)
        canvas.drawLine(compassX + 5f, compassY + 9f, compassX, compassY + 18f, stroke)
        text(canvas, "N", compassX, compassY - 34f, 13f, warmWhite, Paint.Align.CENTER)

        val targetDistance = visibleWorld * 0.18f
        val distance = MAP_SCALE_DISTANCES
            .firstOrNull { it <= targetDistance } ?: 50f
        val barWidth = distance / visibleWorld * viewport.width()
        val left = viewport.left + 24f
        val bottom = viewport.bottom - 24f
        paint.color = Color.argb(165, 1, 15, 19)
        canvas.drawRoundRect(
            RectF(left - 12f, bottom - 31f, left + barWidth + 12f, bottom + 13f),
            5f, 5f, paint
        )
        stroke.color = warmWhite
        stroke.strokeWidth = 2f
        canvas.drawLine(left, bottom, left + barWidth, bottom, stroke)
        canvas.drawLine(left, bottom - 6f, left, bottom + 6f, stroke)
        canvas.drawLine(left + barWidth, bottom - 6f, left + barWidth, bottom + 6f, stroke)
        text(
            canvas, "${distance.roundToInt()} m", left + barWidth / 2f, bottom - 10f,
            12f, warmWhite, Paint.Align.CENTER
        )
        stroke.strokeWidth = 1f
    }

    private fun shrineAt(screenX: Float, screenY: Float): ShrinePoint? {
        val viewport = mapViewportRect()
        if (!viewport.contains(screenX, screenY)) return null
        val visibleWorld = (MAP_WORLD_MAX - MAP_WORLD_MIN) / mapZoom
        val half = visibleWorld / 2f
        val worldLeft = (mapCenterX - half).coerceIn(MAP_WORLD_MIN, MAP_WORLD_MAX - visibleWorld)
        val worldTop = (mapCenterZ - half).coerceIn(MAP_WORLD_MIN, MAP_WORLD_MAX - visibleWorld)
        val touchRadius = MAP_PIN_HALF_SIZE + 10f
        return shrines.asSequence()
            .filter { isShrineDiscovered(it) }
            .filter { it.id in snapshot.enteredShrines || it.id in snapshot.clearedShrines }
            .map { shrine ->
                val (x, y) = worldToMap(
                    viewport, shrine.x, shrine.z, worldLeft, worldTop, visibleWorld
                )
                shrine to ((x - screenX) * (x - screenX) + (y - screenY) * (y - screenY))
            }
            .filter { (_, distanceSquared) -> distanceSquared <= touchRadius * touchRadius }
            .minByOrNull { (_, distanceSquared) -> distanceSquared }
            ?.first
    }

    private fun drawSavedMapMarkers(
        canvas: Canvas, viewport: RectF, worldLeft: Float, worldTop: Float, visibleWorld: Float
    ) {
        val markers = mapDiscovery?.userMarkers ?: return
        canvas.save()
        canvas.clipRect(viewport)
        for (marker in markers) {
            val (x,y) = worldToMap(viewport,marker.x,marker.z,worldLeft,worldTop,visibleWorld)
            if (x !in viewport.left..viewport.right || y !in viewport.top..viewport.bottom) continue
            val half = MAP_PIN_HALF_SIZE
            val target = RectF(x-half,y-half,x+half,y+half)
            paint.alpha = 255
            if (marker.icon in 27..35) {
                // Native 288x32 atlas: nine 32x32 stamp glyphs. Enum 36 is unused.
                val atlas = loadBitmap("botw/ui/map/stamps.png") ?: continue
                if (atlas.width != atlas.height*9) continue
                val index = marker.icon-27
                val source = android.graphics.Rect(index*atlas.height,0,(index+1)*atlas.height,atlas.height)
                paint.colorFilter = cyanMapIconTint
                canvas.drawBitmap(atlas,source,target,paint)
            } else {
                val asset = when (marker.icon) {
                    37 -> "pin_red"; 38 -> "pin_blue"; 39 -> "pin_yellow"
                    40 -> "pin_green"; 41 -> "pin_purple"; else -> continue
                }
                val icon = loadBitmap("botw/ui/map/$asset.png") ?: continue
                paint.colorFilter = cyanMapIconTint
                canvas.drawBitmap(icon,null,target,paint)
            }
            paint.colorFilter = null
        }
        paint.colorFilter = null
        paint.alpha = 255
        canvas.restore()
    }

    private fun drawMapMarkers(
        canvas: Canvas, viewport: RectF, worldLeft: Float, worldTop: Float, visibleWorld: Float
    ) {
        val detailZoom = mapDataZoom()
        canvas.save()
        canvas.clipRect(viewport)
        mapMarkers.forEach { marker ->
            if (marker.category !in MAP_MARKER_CATEGORIES) return@forEach
            if (mapDiscovery?.visible?.contains(marker.discoveryFlag) != true) return@forEach
            if (marker.category == "Shrine" || marker.category == "Sheikah Tower") {
                return@forEach
            }
            if (detailZoom !in marker.minZoom..marker.maxZoom) return@forEach
            val (x, y) = worldToMap(
                viewport, marker.x, marker.z, worldLeft, worldTop, visibleWorld
            )
            if (x !in viewport.left..viewport.right || y !in viewport.top..viewport.bottom) {
                return@forEach
            }
            val iconAsset = marker.iconAsset ?: return@forEach
            val dominant = max(marker.iconWidth, marker.iconHeight).coerceAtLeast(1f)
            val halfWidth = marker.iconWidth / dominant * MAP_PIN_HALF_SIZE
            val halfHeight = marker.iconHeight / dominant * MAP_PIN_HALF_SIZE
            val markerIcon = loadBitmap(iconAsset)
            markerIcon?.let { icon ->
                paint.alpha = 255
                paint.colorFilter = when (iconAsset) {
                    "botw/ui/map/village.png",
                    "botw/ui/map/settlement.png",
                    "botw/ui/map/castle.png",
                    "botw/ui/map/stable.png" -> townMapIconTint
                    else -> cyanMapIconTint
                }
                canvas.drawBitmap(
                    icon, null,
                    RectF(x - halfWidth, y - halfHeight, x + halfWidth, y + halfHeight), paint
                )
                paint.colorFilter = null
                paint.alpha = 255
            }
            val showName = marker.category != "Shrine" && mapZoom >= 2.25f || mapZoom >= 3.35f
            if (showName) {
                text(
                    canvas, ellipsize(marker.name, 28), x, y + halfHeight + 15f,
                    11f, warmWhite, Paint.Align.CENTER
                )
            }
        }
        paint.alpha = 255
        canvas.restore()
    }

    private fun drawTowerMarkers(
        canvas: Canvas, viewport: RectF, worldLeft: Float, worldTop: Float, visibleWorld: Float
    ) {
        val core = loadBitmap("botw/ui/map/tower.png") ?: return
        val shadowGlow = loadBitmap("botw/ui/map/tower_outline.png") ?: return
        val overlay = loadBitmap("botw/ui/map/tower_glow.png") ?: return
        val flash = loadBitmap("botw/ui/map/tower_flash.png") ?: return
        val detailZoom = mapDataZoom()
        canvas.save()
        canvas.clipRect(viewport)
        mapMarkers.forEach { marker ->
            if (marker.category != "Sheikah Tower" ||
                mapDiscovery?.visible?.contains(marker.discoveryFlag) != true ||
                detailZoom !in marker.minZoom..marker.maxZoom
            ) return@forEach
            val (x, y) = worldToMap(
                viewport, marker.x, marker.z, worldLeft, worldTop, visibleWorld
            )
            if (x !in viewport.left..viewport.right || y !in viewport.top..viewport.bottom) {
                return@forEach
            }
            drawMapMaskLayer(
                canvas, shadowGlow, x, y, MAP_PIN_HALF_SIZE,
                shrineShadowMapIconTint, 255, false
            )
            drawMapMaskLayer(
                canvas, shadowGlow, x, y, MAP_PIN_HALF_SIZE,
                shrineGlowMapIconTint, 100, true
            )
            drawMapMaskLayer(
                canvas, core, x, y, MAP_PIN_HALF_SIZE,
                shrineBlueMapIconTint, 200, false
            )
            drawMapMaskLayer(
                canvas, overlay, x, y, MAP_PIN_HALF_SIZE,
                shrineBlueMapIconTint, 200, false
            )
            drawMapMaskLayer(
                canvas, flash, x, y, MAP_PIN_HALF_SIZE,
                shrineFlashMapIconTint, 100, true
            )
            if (mapZoom >= 3.35f) {
                text(
                    canvas, ellipsize(marker.name, 28), x, y + MAP_PIN_HALF_SIZE + 15f,
                    11f, warmWhite, Paint.Align.CENTER
                )
            }
        }
        paint.alpha = 255
        canvas.restore()
    }

    private fun drawShrineMarkers(
        canvas: Canvas, viewport: RectF, worldLeft: Float, worldTop: Float, visibleWorld: Float
    ) {
        val shrineCore = loadBitmap("botw/ui/map/shrine.png")
        val shrineOutline = loadBitmap("botw/ui/map/shrine_outline.png")
        val shrineGlow = loadBitmap("botw/ui/map/shrine_glow.png")
        val shrineFlash = loadBitmap("botw/ui/map/shrine_flash.png")
        val dlcCore = loadBitmap("botw/ui/map/shrine_dlc.png")
        val dlcOutline = loadBitmap("botw/ui/map/shrine_dlc_outline.png")
        val dlcGlow = loadBitmap("botw/ui/map/shrine_dlc_glow.png")
        val dlcFlash = loadBitmap("botw/ui/map/shrine_dlc_flash.png")
        canvas.save()
        canvas.clipRect(viewport)
        shrines.forEach { shrine ->
            val cleared = shrine.id in snapshot.clearedShrines
            if (!isShrineDiscovered(shrine)) return@forEach
            val (x, y) = worldToMap(
                viewport, shrine.x, shrine.z, worldLeft, worldTop, visibleWorld
            )
            if (x !in viewport.left..viewport.right || y !in viewport.top..viewport.bottom) {
                return@forEach
            }
            val isDlc = shrine.id.removePrefix("Dungeon").toIntOrNull()?.let { it >= 120 } == true
            val core = if (isDlc) dlcCore else shrineCore
            val outline = if (isDlc) dlcOutline else shrineOutline
            val glow = if (isDlc) dlcGlow else shrineGlow
            val flash = if (isDlc) dlcFlash else shrineFlash
            if (core == null || outline == null || glow == null || flash == null) {
                return@forEach
            }
            drawMapMaskLayer(
                canvas, glow, x, y, MAP_PIN_HALF_SIZE,
                shrineShadowMapIconTint, 200, false
            )
            drawMapMaskLayer(
                canvas, glow, x, y, MAP_PIN_HALF_SIZE,
                shrineGlowMapIconTint, 50, true
            )
            drawMapMaskLayer(
                canvas, core, x, y, MAP_PIN_HALF_SIZE,
                if (cleared) shrineBlueMapIconTint else shrineOrangeMapIconTint, 150, false
            )
            drawMapMaskLayer(
                canvas, outline, x, y, MAP_PIN_HALF_SIZE,
                shrineBlueMapIconTint, 150, false
            )
            drawMapMaskLayer(
                canvas, flash, x, y, MAP_PIN_HALF_SIZE,
                shrineFlashMapIconTint, 32, true
            )
            if (mapZoom >= 3.35f) {
                text(
                    canvas, ellipsize(shrine.name, 28), x, y + MAP_PIN_HALF_SIZE + 16f, 11.5f,
                    if (cleared) warmWhite else muted, Paint.Align.CENTER
                )
            }
        }
        paint.alpha = 255
        canvas.restore()
    }

    private fun drawMapMaskLayer(
        canvas: Canvas, bitmap: Bitmap, x: Float, y: Float, halfSize: Float,
        tint: ColorMatrixColorFilter, alpha: Int, additive: Boolean
    ) {
        paint.alpha = alpha
        paint.colorFilter = tint
        paint.xfermode = if (additive) additiveMapBlend else null
        canvas.drawBitmap(
            bitmap, null,
            RectF(x - halfSize, y - halfSize, x + halfSize, y + halfSize), paint
        )
        paint.xfermode = null
        paint.colorFilter = null
        paint.alpha = 255
    }

    private fun shrinePopupRect() = RectF(245f, 294f, 995f, 792f)
    private fun shrineTravelRect() = RectF(300f, 684f, 590f, 756f)
    private fun shrineCancelRect() = RectF(650f, 684f, 940f, 756f)

    private fun drawShrinePopup(canvas: Canvas, shrine: ShrinePoint) {
        paint.color = Color.argb(170, 0, 7, 9)
        canvas.drawRect(RectF(10f, 90f, 1230f, 1070f), paint)
        val popup = shrinePopupRect()
        paint.color = Color.rgb(2, 22, 26)
        canvas.drawRect(popup, paint)
        stroke.color = cyan
        stroke.strokeWidth = 2f
        canvas.drawRect(popup, stroke)
        stroke.strokeWidth = 1f
        cornerMarks(canvas, popup, cyan)
        drawSheikahMedallion(canvas, popup.centerX(), popup.top + 94f, 48f)
        val complete = shrine.id in snapshot.clearedShrines
        text(
            canvas, if (complete) "SHRINE COMPLETE" else "SHRINE ACTIVATED",
            popup.centerX(), popup.top + 168f,
            16f, if (complete) green else cyan, Paint.Align.CENTER
        )
        text(
            canvas, shrine.name, popup.centerX(), popup.top + 220f,
            32f, warmWhite, Paint.Align.CENTER
        )
        if (shrine.trial.isNotBlank()) {
            text(
                canvas, shrine.trial, popup.centerX(), popup.top + 260f,
                19f, muted, Paint.Align.CENTER
            )
        }
        text(
            canvas, "Travel to this shrine?", popup.centerX(), popup.top + 330f,
            22f, warmWhite, Paint.Align.CENTER
        )
        drawPopupButton(
            canvas, shrineTravelRect(), "TRAVEL",
            snapshot.capabilities and CAP_FAST_TRAVEL != 0
        )
        drawPopupButton(canvas, shrineCancelRect(), "CANCEL", true)
    }

    private fun drawPopupButton(canvas: Canvas, rect: RectF, label: String, enabled: Boolean) {
        paint.color = if (enabled) Color.argb(185, 3, 39, 44) else Color.argb(120, 14, 25, 27)
        canvas.drawRect(rect, paint)
        stroke.color = if (enabled) cyan else muted
        canvas.drawRect(rect, stroke)
        text(
            canvas, label, rect.centerX(), rect.centerY() + 8f, 20f,
            if (enabled) warmWhite else muted, Paint.Align.CENTER
        )
    }

    private fun loadNativeMapPoints(): List<MapPoint> = try {
        context.assets.open("botw/ui/map/discovery.json").bufferedReader().use { reader ->
            val markers = JSONObject(reader.readText()).getJSONArray("markers")
            buildList {
                for (index in 0 until markers.length()) {
                    val marker = markers.getJSONObject(index)
                    val category = marker.getString("category")
                    if (category !in MAP_MARKER_CATEGORIES) continue
                    val icon = when (category) {
                        "Sheikah Tower" -> "tower.png"
                        "Shrine" -> "shrine.png"
                        "Stable" -> "stable.png"
                        "Village" -> "village.png"
                        else -> continue
                    }
                    add(MapPoint(category, marker.getString("name"),
                        marker.getDouble("x").toFloat(), marker.getDouble("z").toFloat(),
                        0, Int.MAX_VALUE, "botw/ui/map/$icon", 1f, 1f,
                        marker.getString("flag")))
                }
            }
        }
    } catch (exception: Exception) {
        Log.error("[BOTW Companion] Native map metadata unavailable: ${exception.message}")
        emptyList()
    }

    private fun loadMapPoints(path: String, includedCategories: Set<String>): List<MapPoint> =
        try {
            context.assets.open(path).bufferedReader().use { reader ->
                val root = JSONArray(reader.readText())
                buildList {
                    for (categoryIndex in 0 until root.length()) {
                        val category = root.optJSONObject(categoryIndex) ?: continue
                        val categoryName = category.optString("name")
                        if (categoryName !in includedCategories) continue
                        val layers = category.optJSONArray("layers") ?: continue
                        for (layerIndex in 0 until layers.length()) {
                            val layer = layers.optJSONObject(layerIndex) ?: continue
                            val minimumZoom = layer.optInt("minZoom", 0)
                            val maximumZoom = layer.optInt("maxZoom", Int.MAX_VALUE)
                            val icon = layer.optJSONObject("icon")
                            val iconName = icon?.optString("url")
                                ?.substringAfterLast('/')
                                ?.takeIf { it.isNotBlank() && !it.contains("..") }
                            val iconWidth = icon?.optDouble("width", 1.0)?.toFloat() ?: 1f
                            val iconHeight = icon?.optDouble("height", 1.0)?.toFloat() ?: 1f
                            val markers = layer.optJSONArray("markers") ?: continue
                            for (markerIndex in 0 until markers.length()) {
                                val marker = markers.optJSONObject(markerIndex) ?: continue
                                val coords = marker.optJSONArray("coords") ?: continue
                                if (coords.length() < 2 || coords.opt(0) !is Number ||
                                    coords.opt(1) !is Number
                                ) continue

                                val x = (coords.optDouble(1) / 2.0).toFloat()
                                val z = (-coords.optDouble(0) / 2.0).toFloat()
                                if (!x.isFinite() || !z.isFinite()) continue
                                add(
                                    MapPoint(
                                        categoryName, marker.optString("name", marker.optString("id")),
                                        x, z, minimumZoom, maximumZoom,
                                        iconName?.let { "botw/ui/map/$it" }, iconWidth, iconHeight
                                    )
                                )
                            }
                        }
                    }
                }
            }
        } catch (exception: Exception) {
            Log.error("[BOTW Companion] Map point data failed for $path: ${exception.message}")
            emptyList()
        }

    private fun loadShrines(): List<ShrinePoint> =
        try {
            context.assets.open("botw/ui/map/shrines.json").bufferedReader().use { reader ->
                val root = JSONArray(reader.readText())
                buildList(root.length()) {
                    for (index in 0 until root.length()) {
                        val shrine = root.getJSONObject(index)
                        add(
                            ShrinePoint(
                                shrine.getInt("index"), shrine.getString("id"),
                                shrine.getString("name"), shrine.optString("trial"),
                                shrine.getDouble("x").toFloat(),
                                shrine.getDouble("y").toFloat(),
                                shrine.getDouble("z").toFloat()
                            )
                        )
                    }
                }
            }
        } catch (exception: Exception) {
            Log.error("[BOTW Companion] Shrine data failed: ${exception.message}")
            emptyList()
        }

    private fun drawPlayerMapMarker(
        canvas: Canvas, viewport: RectF, position: PlayerMap, worldLeft: Float,
        worldTop: Float, visibleWorld: Float
    ) {
        val (x, y) = worldToMap(
            viewport, position.x, position.z, worldLeft, worldTop, visibleWorld
        )
        if (x !in viewport.left..viewport.right || y !in viewport.top..viewport.bottom) return
        val icon = loadBitmap("botw/ui/map/player.png") ?: return
        val maximumSize = 50f
        val aspect = icon.width.toFloat() / icon.height.toFloat().coerceAtLeast(1f)
        val iconWidth = if (aspect >= 1f) maximumSize else maximumSize * aspect
        val iconHeight = if (aspect >= 1f) maximumSize / aspect else maximumSize
        canvas.save()
        canvas.rotate(position.heading, x, y)
        paint.alpha = 255
        paint.colorFilter = cyanMapIconTint
        canvas.drawBitmap(
            icon,
            null,
            RectF(
                x - iconWidth * 0.5f,
                y - iconHeight * 0.5f,
                x + iconWidth * 0.5f,
                y + iconHeight * 0.5f
            ),
            paint
        )
        paint.colorFilter = null
        canvas.restore()
    }

    private fun questFilterRects() = listOf(
        RectF(30f, 158f, 185f, 202f),
        RectF(192f, 158f, 347f, 202f),
        RectF(354f, 158f, 524f, 202f)
    )

    private fun questRowRect(localIndex: Int): RectF {
        val top = QUEST_LIST_TOP + localIndex * QUEST_ROW_STRIDE
        return RectF(QUEST_LIST_LEFT, top, QUEST_LIST_RIGHT, top + QUEST_ROW_HEIGHT)
    }

    private fun questAt(x: Float, y: Float): Quest? {
        if (x !in QUEST_LIST_LEFT..QUEST_LIST_RIGHT ||
            y !in QUEST_LIST_TOP..QUEST_LIST_BOTTOM
        ) return null
        val local = ((y - QUEST_LIST_TOP) / QUEST_ROW_STRIDE).toInt()
        if (local !in 0 until QUEST_VISIBLE_ROWS ||
            !questRowRect(local).contains(x, y)
        ) return null
        return filteredQuests().getOrNull(questScroll + local)
    }

    private enum class QuestCategory(val type: String, val label: String) {
        Main("Main", "Main"), Shrine("Sub", "Shrine"), Side("Mini", "Side")
    }

    private fun questCategory(quest: Quest): QuestCategory? =
        QuestCategory.entries.firstOrNull { it.type.equals(quest.type.trim(), ignoreCase = true) }

    private fun filteredQuests(): List<Quest> {
        val category = QuestCategory.entries.getOrElse(questFilter) { QuestCategory.Main }
        return snapshot.quests.filter { !it.complete && questCategory(it) == category }
    }

    private fun selectedQuest(): Quest? {
        val quests = filteredQuests()
        return quests.firstOrNull { it.id == selectedQuestId } ?: quests.firstOrNull()
    }

    private fun clampQuestSelection() {
        val quests = filteredQuests()
        if (quests.none { it.id == selectedQuestId }) selectedQuestId = quests.firstOrNull()?.id
        questScroll = questScroll.coerceIn(0, max(0, quests.size - QUEST_VISIBLE_ROWS))
    }

    private fun drawQuestScrollbar(canvas: Canvas, count: Int, visibleRows: Int) {
        if (count <= visibleRows) return
        val track = RectF(522f, QUEST_LIST_TOP, 528f, QUEST_LIST_BOTTOM)
        paint.color = Color.argb(80, 39, 185, 198)
        canvas.drawRoundRect(track, 3f, 3f, paint)
        val thumbHeight = max(52f, track.height() * visibleRows / count)
        val maximum = max(1, count - visibleRows)
        val top = track.top + (track.height() - thumbHeight) * questScroll / maximum
        paint.color = cyan
        canvas.drawRoundRect(RectF(track.left - 1f, top, track.right + 1f, top + thumbHeight), 4f, 4f, paint)
    }

    private fun questKind(quest: Quest): String = when (questCategory(quest)) {
        QuestCategory.Main -> "Main Quest"
        QuestCategory.Shrine -> "Shrine Quest"
        QuestCategory.Side -> "Side Quest"
        null -> "Quest"
    }

    private fun drawQuestDetails(canvas: Canvas, panel: RectF, quest: Quest) {
        val visibleQuests = filteredQuests()
        val questNumber = visibleQuests.indexOfFirst { it.id == quest.id } + 1
        text(canvas, questKind(quest).uppercase(), panel.left + 28f, panel.top + 38f, 13f, bronze)
        text(
            canvas, ellipsizeToWidth(quest.name, panel.width() - 220f, 32f),
            panel.left + 28f, panel.top + 81f, 32f, warmWhite
        )
        badge(
            canvas, RectF(panel.right - 145f, panel.top + 26f, panel.right - 24f, panel.top + 64f),
            if (quest.complete) "COMPLETE" else "ACTIVE",
            if (quest.complete) green else cyan, 17f
        )
        if (questNumber > 0) {
            text(
                canvas, "$questNumber of ${visibleQuests.size}", panel.right - 28f,
                panel.top + 87f, 13f, muted, Paint.Align.RIGHT
            )
        }
        stroke.color = bronzeDark
        canvas.drawLine(panel.left + 28f, panel.top + 105f, panel.right - 28f, panel.top + 105f, stroke)
        text(canvas, "CURRENT STEP", panel.left + 28f, panel.top + 145f, 13f, bronze)
        text(
            canvas, quest.step.ifBlank { if (quest.complete) "Finished" else "In progress" },
            panel.left + 28f, panel.top + 177f, 21f,
            if (quest.complete) green else cyan
        )
        if (quest.location.isNotBlank()) {
            text(canvas, "Region", panel.right - 250f, panel.top + 145f, 13f, bronze)
            text(canvas, quest.location, panel.right - 28f, panel.top + 177f, 17f, muted, Paint.Align.RIGHT)
        }
        text(canvas, "OBJECTIVE", panel.left + 28f, panel.top + 235f, 13f, bronze)
        multilineText(
            canvas,
            quest.objective.ifBlank { "Quest objective is updating from the loaded save." },
            panel.left + 28f, panel.top + 274f, panel.width() - 56f, 20.5f, warmWhite, 21
        )
        val footer = RectF(panel.left + 24f, panel.bottom - 88f, panel.right - 24f, panel.bottom - 24f)
        paint.color = Color.argb(95, 3, 28, 32)
        canvas.drawRect(footer, paint)
        stroke.color = bronzeDark
        canvas.drawRect(footer, stroke)
        text(
            canvas,
            if (quest.complete) "This quest is complete in the current save."
            else "Live quest state - Updates automatically",
            footer.centerX(), footer.centerY() + 6f, 16f,
            if (quest.complete) green else muted, Paint.Align.CENTER
        )
    }

    private fun ellipsize(value: String, maximum: Int): String =
        if (value.length <= maximum) value else value.take(maximum - 3).trimEnd() + "..."

    private fun ellipsizeToWidth(value: String, maximumWidth: Float, textSize: Float): String {
        paint.textSize = textSize
        paint.typeface = normalTypeface
        if (paint.measureText(value) <= maximumWidth) return value
        val suffix = "..."
        var low = 0
        var high = value.length
        while (low < high) {
            val middle = (low + high + 1) / 2
            if (paint.measureText(value.take(middle).trimEnd() + suffix) <= maximumWidth) {
                low = middle
            } else {
                high = middle - 1
            }
        }
        return value.take(low).trimEnd() + suffix
    }

    private fun questSummary(quest: Quest): String =
        quest.objective.lineSequence().firstOrNull { it.isNotBlank() }
            ?: quest.step.takeIf { it.isNotBlank() }
            ?: if (quest.complete) "Quest completed" else "Objective updating"

    private fun drawCalibrating(canvas: Canvas, rect: RectF, label: String) {
        drawSheikahMedallion(canvas, rect.centerX(), rect.centerY() - 20f, 50f)
        text(canvas, label, rect.centerX(), rect.centerY() + 65f, 18f, muted, Paint.Align.CENTER)
    }

    private fun pageTabRect(index: Int): RectF {
        val gap = 8f
        val margin = 10f
        val width = (DESIGN_WIDTH - margin * 2f - gap * 2f) / 3f
        val left = margin + index * (width + gap)
        return RectF(left, 9f, left + width, 79f)
    }

    private fun contextItem(): Item? =
        contextItemId?.let { id -> snapshot.inventory.firstOrNull { it.id == id } }

    private fun contextActions(item: Item): List<ContextAction> = buildList {
        if (item.holdable &&
            snapshot.capabilities and CAP_HOLD_ACTIONS != 0 &&
            (snapshot.heldItemCount ?: MAX_HELD_ITEMS) < MAX_HELD_ITEMS
        ) add(ContextAction.Hold)
        if (item.edible && snapshot.capabilities and CAP_EAT_ACTIONS != 0) {
            add(ContextAction.Eat)
        }
    }

    private fun contextPanelRect() = RectF(390f, 230f, 850f, 850f)

    private fun contextActionRects(actionCount: Int): List<RectF> {
        val width = 320f
        val height = 70f
        val gap = 16f
        val firstTop = contextPanelRect().top + 380f -
            (actionCount - 1) * (height + gap) / 2f
        return List(actionCount) { index ->
            RectF(
                DESIGN_WIDTH / 2f - width / 2f,
                firstTop + index * (height + gap),
                DESIGN_WIDTH / 2f + width / 2f,
                firstTop + index * (height + gap) + height
            )
        }
    }

    private fun drawItemContextMenu(canvas: Canvas, item: Item) {
        val actions = contextActions(item)
        paint.color = Color.argb(184, 0, 7, 9)
        canvas.drawRect(0f, 0f, DESIGN_WIDTH, DESIGN_HEIGHT, paint)
        val popup = contextPanelRect()
        panel(canvas, popup)
        cornerMarks(canvas, popup, cyan)
        drawItemGlyph(canvas, item, popup.centerX(), popup.top + 125f, 1.5f)
        text(
            canvas, ellipsizeToWidth(item.name, popup.width() - 64f, 26f), popup.centerX(),
            popup.top + 235f, 26f, warmWhite, Paint.Align.CENTER
        )
        text(
            canvas, item.category.uppercase(), popup.centerX(), popup.top + 270f,
            13f, bronze, Paint.Align.CENTER
        )
        val pouchCount = max(0, item.count ?: 1)
        val heldCount = (snapshot.heldItemCount ?: 0).coerceIn(0, MAX_HELD_ITEMS)
        text(
            canvas, "IN POUCH  x$pouchCount     HELD  $heldCount/$MAX_HELD_ITEMS",
            popup.centerX(), popup.top + 304f, 14f, muted, Paint.Align.CENTER
        )
        contextActionRects(actions.size).forEachIndexed { index, rect ->
            val selected = controllerActive && controllerFocus == ControllerFocus.ItemMenu &&
                contextActionIndex == index
            paint.color = if (selected) Color.argb(82, 28, 214, 229)
                else Color.argb(170, 3, 29, 33)
            canvas.drawRect(rect, paint)
            stroke.color = if (selected) cyan else bronzeDark
            stroke.strokeWidth = if (selected) 2.5f else 1f
            canvas.drawRect(rect, stroke)
            text(
                canvas, actions[index].label.uppercase(), rect.centerX(),
                rect.centerY() + 8f, 20f, if (selected) cyan else warmWhite,
                Paint.Align.CENTER
            )
        }
        stroke.strokeWidth = 1f
        text(canvas, "BACK", popup.centerX(), popup.bottom - 28f, 13f, muted, Paint.Align.CENTER)
    }

    private fun openItemContext(item: Item) {
        if (contextActions(item).isEmpty()) return
        contextItemId = item.id
        contextActionIndex = 0
        controllerFocus = ControllerFocus.ItemMenu
        invalidate()
    }

    private fun closeItemContext() {
        contextItemId = null
        contextActionIndex = 0
        controllerFocus = ControllerFocus.Inventory
        invalidate()
    }

    private fun performContextAction(item: Item, action: ContextAction): Boolean {
        Log.info(
            "[BOTW-ITEM-ACTION] ui-press action=${action.label} actor=${item.actorName} " +
                "id=${item.id} type=${item.itemType} category=${item.category} " +
                "quantity=${actorQuantity(item.actorName)} " +
                "equipped=${item.equipped} source=bottom-screen"
        )
        if (pendingContextAction != null) {
            Log.error("[BOTW-ITEM-ACTION] rejected action=${action.label} reason=context-action-pending")
            return false
        }
        if (action == ContextAction.Hold &&
            snapshot.capabilities and CAP_HOLD_ACTIONS == 0) {
            Log.error("[BOTW-ITEM-ACTION] rejected action=Hold reason=capability-unavailable")
            return false
        }
        if (action == ContextAction.Eat &&
            snapshot.capabilities and CAP_EAT_ACTIONS == 0) {
            Log.error("[BOTW-EAT] rejected reason=capability-unavailable")
            return false
        }
        val nativeAction = when (action) {
            ContextAction.Eat -> ACTION_EAT_ITEM
            ContextAction.Hold -> ACTION_HOLD_ITEM
        }
        val queued = NativeLibrary.performBotwCompanionAction(nativeAction, item.id)
        Log.info(
            "[BOTW-ITEM-ACTION] native-dispatch action=${action.label} actor=${item.actorName} " +
                "id=${item.id} result=${if (queued) "queued" else "rejected"} " +
                "mailboxState=${snapshot.equipState} mailboxType=${snapshot.actionType}"
        )
        if (queued) {
            pendingContextAction = PendingContextAction(
                item = item,
                action = action,
                initialQuantity = actorQuantity(item.actorName),
                initialHeldCount = snapshot.heldItemCount ?: 0,
                initialHealth = snapshot.stats?.health,
                initialStamina = snapshot.stats?.stamina,
                initialEffects = snapshot.effects,
                deadline = SystemClock.uptimeMillis() + CONTEXT_ACTION_TIMEOUT_MS
            )
            contextItemId = null
            lastRawLiveSnapshot = ""
            handler.removeCallbacks(poll)
            handler.removeCallbacks(actionRefresh)
            handler.postDelayed(actionRefresh, ACTION_REFRESH_DELAY_MS)
        }
        invalidate()
        return queued
    }

    private fun actorQuantity(actorName: String): Int = snapshot.inventory.asSequence()
        .filter { it.actorName == actorName }
        .sumOf { max(0, it.count ?: 1) }

    private fun queueEquip(item: Item): Boolean {
        val actionName = if (item.equipped) "Unequip" else "Equip"
        val nativeAction = if (item.equipped) ACTION_UNEQUIP_ITEM else ACTION_EQUIP_ITEM
        val expectedEquipped = !item.equipped
        Log.info(
            "[BOTW-ITEM-ACTION] ui-press action=$actionName actor=${item.actorName} id=${item.id} " +
                "type=${item.itemType} category=${item.category} " +
                "equippedBefore=${item.equipped} source=bottom-screen"
        )
        if (snapshot.capabilities and CAP_ITEM_ACTIONS == 0 || !item.equippable) {
            Log.error(
                "[BOTW-EQUIP] rejected action=$actionName actor=${item.actorName} " +
                    "reason=${if (!item.equippable) "not-equippable" else "capability-unavailable"}"
            )
            return false
        }
        val queued = NativeLibrary.performBotwCompanionAction(nativeAction, item.id)
        Log.info(
            "[BOTW-EQUIP] native-dispatch action=$actionName actor=${item.actorName} id=${item.id} " +
                "result=${if (queued) "queued" else "rejected"}"
        )
        if (queued) requestActionRefresh(
            equipItemId = item.id,
            expectedEquipped = expectedEquipped,
            equipActionName = actionName
        )
        invalidate()
        return queued
    }

    private fun queueRune(rune: Rune): Boolean {
        if (snapshot.capabilities and CAP_RUNES == 0) return false
        val queued = NativeLibrary.performBotwCompanionAction(ACTION_SELECT_RUNE, rune.type.toLong())
        Log.info(
            "[BOTW Companion] Select rune ${rune.name}: " +
                if (queued) "queued" else "rejected"
        )
        if (queued) requestActionRefresh(rune.type)
        invalidate()
        return queued
    }

    private fun queueTravel(shrine: ShrinePoint): Boolean {
        if (snapshot.capabilities and CAP_FAST_TRAVEL == 0) return false
        val queued = NativeLibrary.performBotwCompanionAction(ACTION_FAST_TRAVEL, shrine.index.toLong())
        Log.info(
            "[BOTW Companion] Travel ${shrine.name}: " +
                if (queued) "queued" else "rejected"
        )
        if (queued) selectedShrine = null
        invalidate()
        return queued
    }

    private fun performInventoryAction(item: Item) {
        if (contextActions(item).isNotEmpty()) openItemContext(item) else queueEquip(item)
    }

    private fun handleContextMenuTouch(event: MotionEvent, x: Float, y: Float, item: Item): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                downX = x
                downY = y
            }
            MotionEvent.ACTION_UP -> {
                val isTap = max(abs(x - downX), abs(y - downY)) <= 12f
                if (!isTap) return true
                performClick()
                val actions = contextActions(item)
                val index = contextActionRects(actions.size).indexOfFirst { rect ->
                    rect.contains(x, y) && rect.contains(downX, downY)
                }
                if (index >= 0) {
                    contextActionIndex = index
                    performContextAction(item, actions[index])
                } else if (!contextPanelRect().contains(x, y)) {
                    closeItemContext()
                }
            }
        }
        return true
    }

    override fun onBotwControllerKey(event: KeyEvent): Boolean {
        val toggle = event.keyCode == KeyEvent.KEYCODE_BUTTON_MODE
        if (toggle) {
            if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
                controllerActive = !controllerActive
                controllerFocus = ControllerFocus.Pages
                if (!controllerActive) contextItemId = null
                invalidate()
            }
            return true
        }
        if (!controllerActive || !isSaveLoaded()) return false
        if (event.action == KeyEvent.ACTION_UP) return isControllerNavigationKey(event.keyCode)
        if (event.action != KeyEvent.ACTION_DOWN) return false
        if (isControllerNavigationKey(event.keyCode)) stopInventoryMotion()
        val handled = when (event.keyCode) {
            KeyEvent.KEYCODE_DPAD_LEFT -> moveControllerFocus(-1, 0)
            KeyEvent.KEYCODE_DPAD_RIGHT -> moveControllerFocus(1, 0)
            KeyEvent.KEYCODE_DPAD_UP -> moveControllerFocus(0, -1)
            KeyEvent.KEYCODE_DPAD_DOWN -> moveControllerFocus(0, 1)
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_ENTER,
            KeyEvent.KEYCODE_DPAD_CENTER -> activateControllerFocus()
            KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BACK -> controllerBack()
            KeyEvent.KEYCODE_BUTTON_L1 -> {
                cyclePage(-1)
                true
            }
            KeyEvent.KEYCODE_BUTTON_R1 -> {
                cyclePage(1)
                true
            }
            KeyEvent.KEYCODE_BUTTON_L2 -> {
                if (page == Page.Map) setMapZoom(mapZoom / 1.35f)
                true
            }
            KeyEvent.KEYCODE_BUTTON_R2 -> {
                if (page == Page.Map) setMapZoom(mapZoom * 1.35f)
                true
            }
            KeyEvent.KEYCODE_BUTTON_X -> {
                if (page == Page.Map) recenterMap()
                true
            }
            else -> false
        }
        if (handled) invalidate()
        return handled
    }

    override fun onBotwControllerMotion(event: MotionEvent): Boolean {
        if (!controllerActive || !isSaveLoaded() || event.action != MotionEvent.ACTION_MOVE) {
            return false
        }
        if (event.source and InputDevice.SOURCE_JOYSTICK != InputDevice.SOURCE_JOYSTICK) {
            return false
        }
        val horizontal = event.getAxisValue(MotionEvent.AXIS_HAT_X)
            .takeIf { abs(it) > .55f } ?: 0f
        val vertical = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
            .takeIf { abs(it) > .55f } ?: 0f
        if (horizontal == 0f && vertical == 0f) return false
        val now = SystemClock.uptimeMillis()
        if (now - controllerMotionAt < 170L) return true
        controllerMotionAt = now
        stopInventoryMotion()
        if (abs(horizontal) >= abs(vertical)) {
            moveControllerFocus(if (horizontal < 0f) -1 else 1, 0)
        } else {
            moveControllerFocus(0, if (vertical < 0f) -1 else 1)
        }
        invalidate()
        return true
    }

    private fun isControllerNavigationKey(keyCode: Int): Boolean = keyCode in setOf(
        KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT,
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN,
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B,
        KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_BUTTON_L1,
        KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_BUTTON_L2,
        KeyEvent.KEYCODE_BUTTON_R2, KeyEvent.KEYCODE_ENTER,
        KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_BACK
    )

    private fun cyclePage(delta: Int) {
        val next = (page.ordinal + delta + Page.entries.size) % Page.entries.size
        page = Page.entries[next]
        selectedShrine = null
        contextItemId = null
        controllerFocus = ControllerFocus.Pages
        notifyPlayerAppearance()
    }

    private fun moveControllerFocus(dx: Int, dy: Int): Boolean {
        when (controllerFocus) {
            ControllerFocus.Pages -> {
                if (dx != 0) cyclePage(dx)
                if (dy > 0) controllerFocus = when (page) {
                    Page.Inventory -> ControllerFocus.Categories
                    Page.Map -> ControllerFocus.Map
                    Page.Quests -> ControllerFocus.QuestFilters
                }
            }
            ControllerFocus.Categories -> {
                if (dx != 0) {
                    categoryIndex = (categoryIndex + dx).coerceIn(categories.indices)
                    selectedItem = 0
                    rowOffset = 0
                    ensureCategoryVisible(categoryIndex)
                }
                if (dy < 0) controllerFocus = ControllerFocus.Pages
                if (dy > 0) controllerFocus = ControllerFocus.Inventory
            }
            ControllerFocus.Inventory -> moveInventorySelection(dx, dy)
            ControllerFocus.Runes -> {
                val runes = snapshot.runes.filter { it.available }
                if (dx != 0 && runes.isNotEmpty()) {
                    controllerRuneIndex = (controllerRuneIndex + dx).coerceIn(runes.indices)
                }
                if (dy < 0) controllerFocus = ControllerFocus.Inventory
            }
            ControllerFocus.Map -> {
                if (dy < 0 && mapCenterZ <= MAP_WORLD_MIN) {
                    controllerFocus = ControllerFocus.Pages
                } else {
                    val step = (MAP_WORLD_MAX - MAP_WORLD_MIN) / mapZoom * .08f
                    mapCenterX += dx * step
                    mapCenterZ += dy * step
                    mapCentered = true
                    clampMapCenter()
                }
            }
            ControllerFocus.QuestFilters -> {
                if (dx != 0) {
                    questFilter = (questFilter + dx).coerceIn(QuestCategory.entries.indices)
                    questScroll = 0
                    clampQuestSelection()
                }
                if (dy < 0) controllerFocus = ControllerFocus.Pages
                if (dy > 0) controllerFocus = ControllerFocus.QuestList
            }
            ControllerFocus.QuestList -> moveQuestSelection(dy)
            ControllerFocus.ShrinePopup -> {
                if (dx != 0) controllerPopupAction = (controllerPopupAction + dx).coerceIn(0, 1)
            }
            ControllerFocus.ItemMenu -> {
                val item = contextItem() ?: return controllerBack()
                val actions = contextActions(item)
                if (dy != 0 && actions.isNotEmpty()) {
                    contextActionIndex = (contextActionIndex + dy).coerceIn(actions.indices)
                }
            }
        }
        return true
    }

    private fun moveInventorySelection(dx: Int, dy: Int) {
        val items = filteredItems()
        if (items.isEmpty()) {
            if (dy < 0) controllerFocus = ControllerFocus.Categories
            return
        }
        val current = selectedItem.coerceIn(items.indices)
        val currentRow = current / GRID_COLUMNS
        val currentColumn = current % GRID_COLUMNS
        if (dy < 0 && currentRow == 0) {
            controllerFocus = ControllerFocus.Categories
            return
        }
        if (dy > 0 && currentRow == (items.lastIndex / GRID_COLUMNS)) {
            val runes = snapshot.runes.filter { it.available }
            if (runes.isNotEmpty()) {
                controllerRuneIndex = runes.indexOfFirst { it.selected }.coerceAtLeast(0)
                controllerFocus = ControllerFocus.Runes
            }
            return
        }
        var nextRow = (currentRow + dy).coerceIn(0, items.lastIndex / GRID_COLUMNS)
        var nextColumn = (currentColumn + dx).coerceIn(0, GRID_COLUMNS - 1)
        var next = nextRow * GRID_COLUMNS + nextColumn
        if (next > items.lastIndex) {
            nextColumn = min(nextColumn, items.lastIndex % GRID_COLUMNS)
            next = nextRow * GRID_COLUMNS + nextColumn
        }
        selectedItem = next.coerceIn(items.indices)
        nextRow = selectedItem / GRID_COLUMNS
        val stride = GRID_CELL_HEIGHT + GRID_GAP
        if (nextRow * stride < inventoryScrollY) rowOffset = nextRow
        if (nextRow * stride + GRID_CELL_HEIGHT > inventoryScrollY + GRID_BOTTOM - GRID_START_Y) {
            rowOffset = nextRow - GRID_ROWS_VISIBLE + 1
        }
    }

    private fun moveQuestSelection(dy: Int) {
        if (dy == 0) return
        val quests = filteredQuests()
        if (quests.isEmpty()) {
            if (dy < 0) controllerFocus = ControllerFocus.QuestFilters
            return
        }
        val current = quests.indexOfFirst { it.id == selectedQuestId }.coerceAtLeast(0)
        if (dy < 0 && current == 0) {
            controllerFocus = ControllerFocus.QuestFilters
            return
        }
        val next = (current + dy).coerceIn(quests.indices)
        selectedQuestId = quests[next].id
        if (next < questScroll) questScroll = next
        if (next >= questScroll + QUEST_VISIBLE_ROWS) questScroll = next - QUEST_VISIBLE_ROWS + 1
    }

    private fun activateControllerFocus(): Boolean {
        when (controllerFocus) {
            ControllerFocus.Pages -> controllerFocus = when (page) {
                Page.Inventory -> ControllerFocus.Categories
                Page.Map -> ControllerFocus.Map
                Page.Quests -> ControllerFocus.QuestFilters
            }
            ControllerFocus.Categories -> controllerFocus = ControllerFocus.Inventory
            ControllerFocus.Inventory -> filteredItems().getOrNull(selectedItem)?.let(::performInventoryAction)
            ControllerFocus.Runes -> snapshot.runes.filter { it.available }
                .getOrNull(controllerRuneIndex)?.let(::queueRune)
            ControllerFocus.Map -> {
                selectedShrine = nearestVisibleClearedShrine()
                if (selectedShrine != null) {
                    controllerPopupAction = 0
                    controllerFocus = ControllerFocus.ShrinePopup
                }
            }
            ControllerFocus.QuestFilters -> controllerFocus = ControllerFocus.QuestList
            ControllerFocus.QuestList -> Unit
            ControllerFocus.ShrinePopup -> {
                val shrine = selectedShrine ?: return controllerBack()
                if (controllerPopupAction == 0) queueTravel(shrine) else {
                    selectedShrine = null
                    controllerFocus = ControllerFocus.Map
                }
            }
            ControllerFocus.ItemMenu -> {
                val item = contextItem() ?: return controllerBack()
                contextActions(item).getOrNull(contextActionIndex)?.let {
                    performContextAction(item, it)
                }
            }
        }
        return true
    }

    private fun controllerBack(): Boolean {
        when {
            contextItemId != null -> closeItemContext()
            selectedShrine != null -> {
                selectedShrine = null
                controllerFocus = ControllerFocus.Map
            }
            controllerFocus != ControllerFocus.Pages -> controllerFocus = ControllerFocus.Pages
            else -> controllerActive = false
        }
        invalidate()
        return true
    }

    private fun nearestVisibleClearedShrine(): ShrinePoint? {
        val half = (MAP_WORLD_MAX - MAP_WORLD_MIN) / mapZoom / 2f
        return shrines.asSequence()
            .filter { isShrineDiscovered(it) }
            .filter { it.id in snapshot.enteredShrines || it.id in snapshot.clearedShrines }
            .filter { abs(it.x - mapCenterX) <= half && abs(it.z - mapCenterZ) <= half }
            .minByOrNull { (it.x - mapCenterX) * (it.x - mapCenterX) +
                (it.z - mapCenterZ) * (it.z - mapCenterZ) }
    }

    private fun drawControllerFocus(canvas: Canvas) {
        if (!controllerActive) return
        val rect = when (controllerFocus) {
            ControllerFocus.Pages -> pageTabRect(page.ordinal)
            ControllerFocus.Categories -> {
                val left = CATEGORY_LEFT + categoryIndex * CATEGORY_CELL_WIDTH - categoryScrollX
                RectF(left + 3f, CATEGORY_TOP + 3f, left + CATEGORY_CELL_WIDTH - 3f, CATEGORY_BOTTOM - 3f)
            }
            ControllerFocus.Inventory -> {
                val local = selectedItem - rowOffset * GRID_COLUMNS
                if (local !in 0 until GRID_COLUMNS * (GRID_ROWS_VISIBLE + 1)) null else {
                    val column = local % GRID_COLUMNS
                    val row = local / GRID_COLUMNS
                    RectF(
                        GRID_START_X + column * (GRID_CELL_WIDTH + GRID_GAP),
                        GRID_START_Y + row * (GRID_CELL_HEIGHT + GRID_GAP) - inventoryRowRemainder,
                        GRID_START_X + column * (GRID_CELL_WIDTH + GRID_GAP) + GRID_CELL_WIDTH,
                        GRID_START_Y + row * (GRID_CELL_HEIGHT + GRID_GAP) + GRID_CELL_HEIGHT -
                            inventoryRowRemainder
                    ).takeIf { it.bottom > GRID_START_Y && it.top < GRID_BOTTOM }
                }
            }
            ControllerFocus.Runes -> {
                val runes = snapshot.runes.filter { it.available }
                if (runes.isEmpty()) null else {
                    controllerRuneIndex = controllerRuneIndex.coerceIn(runes.indices)
                    val strip = runeStripRect()
                    val width = strip.width() / runes.size
                    RectF(
                        strip.left + controllerRuneIndex * width, strip.top,
                        strip.left + (controllerRuneIndex + 1) * width, strip.bottom
                    )
                }
            }
            ControllerFocus.Map -> mapViewportRect()
            ControllerFocus.QuestFilters -> questFilterRects()[questFilter]
            ControllerFocus.QuestList -> {
                val quests = filteredQuests()
                val index = quests.indexOfFirst { it.id == selectedQuestId }
                val local = index - questScroll
                if (local in 0 until QUEST_VISIBLE_ROWS) questRowRect(local) else null
            }
            ControllerFocus.ShrinePopup ->
                if (controllerPopupAction == 0) shrineTravelRect() else shrineCancelRect()
            ControllerFocus.ItemMenu -> contextItem()?.let { item ->
                contextActionRects(contextActions(item).size).getOrNull(contextActionIndex)
            }
        }
        rect?.let {
            canvas.save()
            if (controllerFocus == ControllerFocus.Inventory) {
                canvas.clipRect(GRID_START_X - 5f, GRID_START_Y, 656f, GRID_BOTTOM)
            }
            stroke.color = warmWhite
            stroke.strokeWidth = 3f
            canvas.drawRect(RectF(it.left - 3f, it.top - 3f, it.right + 3f, it.bottom + 3f), stroke)
            stroke.strokeWidth = 1f
            cornerMarks(canvas, RectF(it.left - 5f, it.top - 5f, it.right + 5f, it.bottom + 5f), cyan)
            canvas.restore()
        }
        paint.color = Color.argb(205, 0, 15, 18)
        canvas.drawRoundRect(RectF(1010f, 1019f, 1224f, 1063f), 5f, 5f, paint)
        text(canvas, "SELECT  GAME / UI", 1212f, 1047f, 12f, cyan, Paint.Align.RIGHT)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!isSaveLoaded()) return true
        val scale = min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT)
        if (scale <= 0f || !scale.isFinite()) return true
        val x = (event.x - (width - DESIGN_WIDTH * scale) / 2f) / scale
        val y = (event.y - (height - DESIGN_HEIGHT * scale) / 2f) / scale
        if (event.actionMasked == MotionEvent.ACTION_DOWN) {
            rippleX = x; rippleY = y
            touchRipple.animateTo(1f, 260L, 0f)
        } else if (event.actionMasked == MotionEvent.ACTION_CANCEL ||
            event.actionMasked == MotionEvent.ACTION_POINTER_DOWN ||
            (event.actionMasked == MotionEvent.ACTION_MOVE &&
                (abs(x-rippleX) > 12f || abs(y-rippleY) > 12f))) {
            touchRipple.stop()
            touchRipple.animateTo(1f, 0L)
        }
        if (event.actionMasked == MotionEvent.ACTION_DOWN) stopInventoryMotion()
        inventoryVelocity?.addMovement(event)
        val releaseVelocity = if (event.actionMasked == MotionEvent.ACTION_UP) {
            inventoryVelocity?.let { tracker ->
                tracker.computeCurrentVelocity(1000, inventoryTouchConfig.scaledMaximumFlingVelocity.toFloat())
                val velocity = -tracker.yVelocity
                if (abs(velocity) >= inventoryTouchConfig.scaledMinimumFlingVelocity) velocity / scale else 0f
            } ?: 0f
        } else 0f
        if (event.actionMasked == MotionEvent.ACTION_UP ||
            event.actionMasked == MotionEvent.ACTION_CANCEL ||
            event.actionMasked == MotionEvent.ACTION_POINTER_DOWN
        ) {
            inventoryVelocity?.recycle()
            inventoryVelocity = null
        }
        contextItem()?.let { return handleContextMenuTouch(event, x, y, it) }
        if (selectedShrine == null) mapScaleDetector.onTouchEvent(event)
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                downX = x
                downY = y
                downInventoryScrollY = inventoryScrollY
                downCategoryScrollX = categoryScrollX
                downQuestScroll = questScroll
                downMapCenterX = mapCenterX
                downMapCenterZ = mapCenterZ
                inventoryMoved = false
                categoryMoved = false
                questMoved = false
                mapMoved = false
                multiTouchGesture = false
                if (page == Page.Map && selectedShrine != null) {
                    draggingInventory = false
                    draggingScrollbar = false
                    draggingCategories = false
                    draggingMap = false
                    draggingQuests = false
                    return true
                }
                draggingCategories = page == Page.Inventory &&
                    x in CATEGORY_LEFT..CATEGORY_RIGHT && y in CATEGORY_TOP..CATEGORY_BOTTOM
                draggingInventory = !draggingCategories && page == Page.Inventory &&
                    x in GRID_START_X..671f &&
                    y in GRID_START_Y..GRID_BOTTOM
                draggingScrollbar = draggingInventory && x >= 650f
                if (draggingInventory && !draggingScrollbar) {
                    inventoryVelocity = VelocityTracker.obtain().also { it.addMovement(event) }
                }
                draggingMap = page == Page.Map && mapViewportRect().contains(x, y)
                draggingQuests = page == Page.Quests &&
                    x in QUEST_LIST_LEFT..QUEST_LIST_RIGHT &&
                    y in QUEST_LIST_TOP..QUEST_LIST_BOTTOM
                downInventoryCell = inventoryCellAt(x, y)
                downInventoryItemId = downInventoryCell?.let(::inventoryItemAtCell)?.second?.id
                downCategoryIndex = if (draggingCategories) categoryAt(x, y) else null
                downQuestId = if (draggingQuests) questAt(x, y)?.id else null
                downRuneType = if (page == Page.Inventory &&
                    snapshot.capabilities and CAP_RUNES != 0
                ) runeAt(x, y)?.type else null
                if (draggingScrollbar) {
                    updateScrollbarPosition(y)
                    inventoryMoved = true
                }
                return true
            }
            MotionEvent.ACTION_POINTER_DOWN -> {
                multiTouchGesture = true
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                if (page == Page.Map && selectedShrine != null) return true
                if (mapScaleDetector.isInProgress) return true
                if (draggingMap) {
                    if (max(abs(x - downX), abs(y - downY)) > 8f) mapMoved = true
                    if (mapMoved) {
                        val visibleWorld = (MAP_WORLD_MAX - MAP_WORLD_MIN) / mapZoom
                        val viewport = mapViewportRect()
                        mapCenterX = downMapCenterX - (x - downX) / viewport.width() * visibleWorld
                        mapCenterZ = downMapCenterZ - (y - downY) / viewport.height() * visibleWorld
                        mapCentered = true
                        invalidate()
                    }
                    return true
                }
                if (draggingQuests) {
                    if (max(abs(x - downX), abs(y - downY)) > 10f) questMoved = true
                    if (questMoved) {
                        val deltaRows = ((downY - y) / QUEST_ROW_STRIDE).roundToInt()
                        questScroll = (downQuestScroll + deltaRows).coerceIn(
                            0, max(0, filteredQuests().size - QUEST_VISIBLE_ROWS)
                        )
                        invalidate()
                    }
                    return true
                }
                if (draggingCategories) {
                    if (max(abs(x - downX), abs(y - downY)) > 9f) categoryMoved = true
                    if (categoryMoved) {
                        setCategoryScroll(downCategoryScrollX + downX - x)
                    }
                    return true
                }
                if (draggingScrollbar) {
                    updateScrollbarPosition(y)
                    return true
                }
                if (draggingInventory && max(abs(x - downX), abs(y - downY)) > 12f) {
                    inventoryMoved = true
                    setInventoryScroll(downInventoryScrollY + downY - y)
                    return true
                }
            }
            MotionEvent.ACTION_UP -> {
                if (page == Page.Map && selectedShrine != null) {
                    val shrine = selectedShrine ?: return true
                    val isTap = max(abs(x - downX), abs(y - downY)) <= 12f
                    if (isTap) performClick()
                    if (isTap && shrineCancelRect().contains(x, y) &&
                        shrineCancelRect().contains(downX, downY)
                    ) {
                        selectedShrine = null
                        invalidate()
                    } else if (isTap && shrineTravelRect().contains(x, y) &&
                        shrineTravelRect().contains(downX, downY) &&
                        snapshot.capabilities and CAP_FAST_TRAVEL != 0
                    ) {
                        queueTravel(shrine)
                    }
                    return true
                }
                val wasInventoryScroll = inventoryMoved
                val wasInventoryGesture = draggingInventory
                val wasScrollbarGesture = draggingScrollbar
                val wasCategoryGesture = draggingCategories
                val wasMapGesture = draggingMap
                val wasQuestGesture = draggingQuests
                val pressedRuneType = downRuneType
                val isTap = max(abs(x - downX), abs(y - downY)) <= 12f
                draggingInventory = false
                draggingScrollbar = false
                draggingCategories = false
                draggingMap = false
                draggingQuests = false
                downRuneType = null
                if (multiTouchGesture) {
                    multiTouchGesture = false
                    return true
                }
                if (isTap) performClick()
                if (wasInventoryScroll) {
                    if (!wasScrollbarGesture && releaseVelocity != 0f) {
                        inventoryScroller.fling(
                            0, inventoryScrollY.roundToInt(), 0, releaseVelocity.roundToInt(),
                            0, 0, 0, (maximumRowOffset() * (GRID_CELL_HEIGHT + GRID_GAP)).roundToInt()
                        )
                        postInvalidateOnAnimation()
                    }
                    return true
                }
                if (wasMapGesture) {
                    if (!mapMoved && isTap) {
                        shrineAt(x, y)?.let {
                            selectedShrine = it
                            invalidate()
                        }
                    }
                    mapMoved = false
                    return true
                }
                if (wasQuestGesture) {
                    if (!questMoved && isTap) {
                        val quest = questAt(x, y)
                        if (quest != null && quest.id == downQuestId) {
                            selectedQuestId = quest.id
                            invalidate()
                        }
                    }
                    questMoved = false
                    return true
                }
                if (wasCategoryGesture) {
                    if (categoryMoved) {
                        snapCategoryScroll()
                        categoryMoved = false
                        return true
                    }
                    if (x in CATEGORY_LEFT..CATEGORY_RIGHT && y in CATEGORY_TOP..CATEGORY_BOTTOM) {
                        val index = categoryAt(x, y)
                        if (index != null) {
                            if (index != downCategoryIndex) return true
                            categoryIndex = index
                            selectedItem = 0
                            rowOffset = 0
                            ensureCategoryVisible(index)
                        }
                        invalidate()
                    }
                    return true
                }
                if (isTap && page == Page.Inventory &&
                    snapshot.capabilities and CAP_RUNES != 0
                ) {
                    val rune = runeAt(x, y)
                    if (rune != null && rune.type == pressedRuneType) {
                        queueRune(rune)
                        return true
                    }
                }
                if (isTap && downY in 9f..79f && y in 9f..79f) {
                    val gap = 8f
                    val margin = 10f
                    val tabWidth = (DESIGN_WIDTH - margin * 2f - gap * 2f) / 3f
                    Page.entries.forEachIndexed { index, candidate ->
                        val left = margin + index * (tabWidth + gap)
                        if (downX in left..(left + tabWidth) && x in left..(left + tabWidth)) {
                            page = candidate
                            selectedShrine = null
                            notifyPlayerAppearance()
                            invalidate()
                            return true
                        }
                    }
                }
                if (isTap && page == Page.Map) {
                    when {
                        mapZoomOutRect().contains(x, y) && mapZoomOutRect().contains(downX, downY) ->
                            setMapZoom(mapZoom / 1.5f)
                        mapZoomInRect().contains(x, y) && mapZoomInRect().contains(downX, downY) ->
                            setMapZoom(mapZoom * 1.5f)
                        mapCenterRect().contains(x, y) && mapCenterRect().contains(downX, downY) ->
                            recenterMap()
                        else -> return true
                    }
                    return true
                }
                if (isTap && page == Page.Quests) {
                    val filters = questFilterRects()
                    val index = filters.indexOfFirst { it.contains(x, y) && it.contains(downX, downY) }
                    if (index >= 0) {
                        questFilter = index
                        questScroll = 0
                        clampQuestSelection()
                        invalidate()
                    }
                    return true
                }
                if (wasInventoryGesture && isTap && page == Page.Inventory &&
                    snapshot.capabilities and CAP_INVENTORY != 0
                ) {
                    val cell = inventoryCellAt(x, y)
                    val hit = cell?.let(::inventoryItemAtCell)
                    if (cell != null && cell == downInventoryCell && hit != null &&
                        hit.second.id == downInventoryItemId
                    ) {
                        val (index, item) = hit
                        selectedItem = index
                        performInventoryAction(item)
                        invalidate()
                    }
                    return true
                }
            }
            MotionEvent.ACTION_CANCEL -> {
                draggingInventory = false
                draggingScrollbar = false
                draggingCategories = false
                draggingMap = false
                draggingQuests = false
                categoryMoved = false
                mapMoved = false
                questMoved = false
                multiTouchGesture = false
                downRuneType = null
            }
        }
        return true
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }

    private fun stopInventoryMotion() {
        inventoryScroller.forceFinished(true)
        inventoryVelocity?.recycle()
        inventoryVelocity = null
    }

    override fun computeScroll() {
        if (inventoryScroller.isFinished) return
        if (!isSaveLoaded() || page != Page.Inventory || contextItemId != null) {
            stopInventoryMotion()
            return
        }
        if (inventoryScroller.computeScrollOffset()) {
            val maximum = maximumRowOffset() * (GRID_CELL_HEIGHT + GRID_GAP)
            setInventoryScroll(inventoryScroller.currY.toFloat())
            if (inventoryScroller.currY < 0 || inventoryScroller.currY > maximum) {
                inventoryScroller.forceFinished(true)
            } else if (!inventoryScroller.isFinished) {
                postInvalidateOnAnimation()
            }
        }
    }

    private fun runeStripRect(panel: RectF = RectF(687f, 350f, 1230f, 1070f)) =
        RectF(panel.left + 14f, panel.bottom - 108f, panel.right - 14f, panel.bottom - 14f)

    private fun runeAt(x: Float, y: Float): Rune? {
        val visible = snapshot.runes.filter { it.available }
        if (visible.isEmpty()) return null
        val strip = runeStripRect()
        if (x !in strip.left..strip.right || y !in strip.top..strip.bottom) return null


        val index = ((x - strip.left) / (strip.width() / visible.size)).toInt()
            .coerceIn(0, visible.lastIndex)
        return visible.getOrNull(index)
    }

    private fun requestActionRefresh(
        runeType: Int? = null,
        equipItemId: Long? = null,
        expectedEquipped: Boolean? = null,
        equipActionName: String = "Equip"
    ) {
        pendingRuneType = runeType
        pendingRuneRefreshDeadline = if (runeType != null) {
            SystemClock.uptimeMillis() + RUNE_REFRESH_TIMEOUT_MS
        } else {
            0L
        }
        pendingEquipItemId = equipItemId
        pendingEquipExpectedState = expectedEquipped
        pendingEquipActionName = equipActionName
        pendingEquipNeedsFullSnapshot = false
        pendingEquipRefreshDeadline = if (equipItemId != null) {
            SystemClock.uptimeMillis() + EQUIP_REFRESH_TIMEOUT_MS
        } else {
            0L
        }
        if (runeType != null || equipItemId != null) {
            lastRawLiveSnapshot = ""
        } else {
            lastRawSnapshot = ""
            lastRawLiveSnapshot = ""
            lastFullSnapshotAt = 0L
        }
        handler.removeCallbacks(poll)
        handler.removeCallbacks(actionRefresh)
        handler.postDelayed(actionRefresh, ACTION_REFRESH_DELAY_MS)
    }

    private fun schedulePendingActionRefresh(fullSnapshot: Boolean): Boolean {
        pendingRuneType?.let { target ->
            val confirmed = snapshot.runes.any { rune ->
                rune.selected && (rune.type == target || (rune.id == "bombs" && target == 0))
            }
            if (confirmed || SystemClock.uptimeMillis() >= pendingRuneRefreshDeadline) {
                if (confirmed) confirmActionFeedback()
                pendingRuneType = null
                pendingRuneRefreshDeadline = 0L
            } else {
                lastRawLiveSnapshot = ""
                handler.removeCallbacks(actionRefresh)
                handler.postDelayed(actionRefresh, ACTION_REFRESH_DELAY_MS)
                return true
            }
        }

        pendingEquipItemId?.let { targetId ->
            val target = snapshot.inventory.firstOrNull { it.id == targetId }
            val expectedEquipped = pendingEquipExpectedState ?: true
            val expectedType = if (pendingEquipActionName == "Unequip") {
                MAILBOX_TYPE_UNEQUIP
            } else {
                target?.itemType?.toLong()
            }
            val mailboxBusy = snapshot.equipState == 1 || snapshot.equipState == 2 ||
                snapshot.equipState == 5 || snapshot.equipState == 6
            val timedOut = SystemClock.uptimeMillis() >= pendingEquipRefreshDeadline
            val confirmed = fullSnapshot && snapshot.equipState == 3 &&
                target?.equipped == expectedEquipped &&
                mailboxAddressMatches(snapshot.actionCompleted, targetId) &&
                expectedType != null && snapshot.actionType == expectedType
            if (confirmed) {
                confirmActionFeedback()
                Log.info(
                    "[BOTW-EQUIP] completed action=$pendingEquipActionName id=$targetId " +
                        "equippedAfter=${target?.equipped} state=${snapshot.equipState} " +
                        "request=${snapshot.actionRequest} completed=${snapshot.actionCompleted} " +
                        "type=${snapshot.actionType} stage=${snapshot.actionStage} " +
                        "stageName=${actionStageName(snapshot.actionStage)}"
                )
                pendingEquipItemId = null
                pendingEquipExpectedState = null
                pendingEquipRefreshDeadline = 0L
                pendingEquipNeedsFullSnapshot = false
            } else if (snapshot.equipState == 4 || timedOut) {
                Log.error(
                    "[BOTW-EQUIP] failed action=$pendingEquipActionName id=$targetId " +
                        "expectedEquipped=$expectedEquipped actualEquipped=${target?.equipped} " +
                        "state=${snapshot.equipState} attempts=${snapshot.equipAttempts} " +
                        "request=${snapshot.actionRequest} completed=${snapshot.actionCompleted} " +
                        "type=${snapshot.actionType} stage=${snapshot.actionStage} " +
                        "stageName=${actionStageName(snapshot.actionStage)} " +
                        "reason=${if (timedOut) "confirmation-timeout" else "guest-rejected"}"
                )
                pendingEquipItemId = null
                pendingEquipExpectedState = null
                pendingEquipRefreshDeadline = 0L
                pendingEquipNeedsFullSnapshot = false
            } else if (snapshot.equipState == 3) {
                pendingEquipNeedsFullSnapshot = true
                lastRawSnapshot = ""
                lastFullSnapshotAt = 0L
                handler.removeCallbacks(actionRefresh)
                handler.postDelayed(actionRefresh, EQUIP_REFRESH_RETRY_MS)
                return true
            } else {
                if (mailboxBusy || snapshot.equipState == 0) lastRawLiveSnapshot = ""
                handler.removeCallbacks(actionRefresh)
                handler.postDelayed(actionRefresh, EQUIP_REFRESH_RETRY_MS)
                return true
            }
        }

        pendingContextAction?.let { pending ->
            val now = SystemClock.uptimeMillis()
            val quantityAfter = actorQuantity(pending.item.actorName)
            val heldAfter = snapshot.heldItemCount ?: 0
            val expectedType = when (pending.action) {
                ContextAction.Eat -> MAILBOX_TYPE_EAT
                ContextAction.Hold -> MAILBOX_TYPE_HOLD
            }
            val confirmed = fullSnapshot && snapshot.equipState == 3 && when (pending.action) {
                ContextAction.Eat -> quantityAfter == pending.initialQuantity - 1
                ContextAction.Hold -> quantityAfter == pending.initialQuantity - 1 &&
                    heldAfter == pending.initialHeldCount + 1
            } && mailboxAddressMatches(snapshot.actionCompleted, pending.item.id) &&
                snapshot.actionType == expectedType
            if (confirmed) {
                confirmActionFeedback()
                val previewIcon = loadItemBitmap(pending.item)?.let { bitmap ->
                    runCatching {
                        bitmap.copy(bitmap.config ?: Bitmap.Config.ARGB_8888, false)
                    }.getOrNull()
                }
                onPlayerItemAction(
                    pending.item.actorName, previewIcon,
                    pending.action == ContextAction.Eat
                )
                val tag = if (pending.action == ContextAction.Eat) "BOTW-EAT" else "BOTW-ITEM-ACTION"
                Log.info(
                    "[$tag] completed action=${pending.action.label} actor=${pending.item.actorName} " +
                        "quantityBefore=${pending.initialQuantity} quantityAfter=$quantityAfter " +
                        "heldBefore=${pending.initialHeldCount} heldAfter=$heldAfter " +
                        "healthBefore=${pending.initialHealth} healthAfter=${snapshot.stats?.health} " +
                        "staminaBefore=${pending.initialStamina} staminaAfter=${snapshot.stats?.stamina} " +
                        "effectsBefore=${pending.initialEffects} effectsAfter=${snapshot.effects} " +
                        "state=${snapshot.equipState} completed=${snapshot.actionCompleted} " +
                        "type=${snapshot.actionType} stage=${snapshot.actionStage} " +
                        "stageName=${actionStageName(snapshot.actionStage)} " +
                        "diagnosticSequence=${snapshot.actionDiagnostic.sequence} " +
                        "nativeResult=${snapshot.actionDiagnostic.nativeResult} " +
                        "nativeQuantityBefore=${snapshot.actionDiagnostic.quantityBefore} " +
                        "guestQuantityAfter=${snapshot.actionDiagnostic.guestQuantityAfter} " +
                        "guestInInventoryAfter=${snapshot.actionDiagnostic.guestInInventoryAfter} " +
                        "nativeQuantityAfter=${snapshot.actionDiagnostic.quantityAfter} " +
                        "nativeInInventoryAfter=${snapshot.actionDiagnostic.inInventoryAfter} " +
                        "nativeHeldBefore=${snapshot.actionDiagnostic.heldBefore} " +
                        "nativeHeldAfter=${snapshot.actionDiagnostic.heldAfter}"
                )
                if (pending.action == ContextAction.Eat) {
                    Log.info(
                        "[BOTW-ANIMATION] completed action=Eat actor=${pending.item.actorName} " +
                            "nativeEvent=main+00D4279C result=returned"
                    )
                }
                pendingContextAction = null
            } else if (snapshot.equipState == 4 || now >= pending.deadline) {
                Log.error(
                    "[BOTW-ITEM-ACTION] failed action=${pending.action.label} " +
                        "actor=${pending.item.actorName} quantityBefore=${pending.initialQuantity} " +
                        "quantityAfter=$quantityAfter heldBefore=${pending.initialHeldCount} " +
                        "heldAfter=$heldAfter state=${snapshot.equipState} " +
                        "attempts=${snapshot.equipAttempts} request=${snapshot.actionRequest} " +
                        "completed=${snapshot.actionCompleted} type=${snapshot.actionType} " +
                        "stage=${snapshot.actionStage} stageName=${actionStageName(snapshot.actionStage)} " +
                        "diagnosticSequence=${snapshot.actionDiagnostic.sequence} " +
                        "nativeResult=${snapshot.actionDiagnostic.nativeResult} " +
                        "nativeQuantityBefore=${snapshot.actionDiagnostic.quantityBefore} " +
                        "guestQuantityAfter=${snapshot.actionDiagnostic.guestQuantityAfter} " +
                        "guestInInventoryAfter=${snapshot.actionDiagnostic.guestInInventoryAfter} " +
                        "nativeQuantityAfter=${snapshot.actionDiagnostic.quantityAfter} " +
                        "nativeInInventoryAfter=${snapshot.actionDiagnostic.inInventoryAfter} " +
                        "nativeHeldBefore=${snapshot.actionDiagnostic.heldBefore} " +
                        "nativeHeldAfter=${snapshot.actionDiagnostic.heldAfter} " +
                        "reason=${if (now >= pending.deadline) "authoritative-state-timeout" else "guest-rejected"}"
                )
                pendingContextAction = null
            } else if (snapshot.equipState == 3) {
                pending.needsFullSnapshot = true
                lastRawSnapshot = ""
                lastFullSnapshotAt = 0L
                handler.removeCallbacks(actionRefresh)
                handler.postDelayed(actionRefresh, ACTION_REFRESH_DELAY_MS)
                return true
            } else {
                lastRawLiveSnapshot = ""
                handler.removeCallbacks(actionRefresh)
                handler.postDelayed(actionRefresh, ACTION_REFRESH_DELAY_MS)
                return true
            }
        }
        return false
    }

    private fun maximumRowOffset(): Int {
        val rows = ceil(filteredItems().size / GRID_COLUMNS.toFloat()).toInt()
        return max(0, rows - GRID_ROWS_VISIBLE)
    }

    private fun inventoryCellAt(x: Float, y: Float): Int? {
        if (x !in GRID_START_X..(GRID_START_X + GRID_COLUMNS * (GRID_CELL_WIDTH + GRID_GAP) -
                GRID_GAP) || y !in GRID_START_Y..GRID_BOTTOM
        ) {
            return null
        }
        val localX = x - GRID_START_X
        val localY = y - GRID_START_Y + inventoryRowRemainder
        val column = (localX / (GRID_CELL_WIDTH + GRID_GAP)).toInt()
        val row = (localY / (GRID_CELL_HEIGHT + GRID_GAP)).toInt()
        if (column !in 0 until GRID_COLUMNS || row !in 0..GRID_ROWS_VISIBLE ||
            localX % (GRID_CELL_WIDTH + GRID_GAP) > GRID_CELL_WIDTH ||
            localY % (GRID_CELL_HEIGHT + GRID_GAP) > GRID_CELL_HEIGHT
        ) {
            return null
        }
        return row * GRID_COLUMNS + column
    }

    private fun inventoryItemAtCell(cell: Int): Pair<Int, Item>? {
        val index = rowOffset * GRID_COLUMNS + cell
        return filteredItems().getOrNull(index)?.let { index to it }
    }

    private fun categoryAt(x: Float, y: Float): Int? {
        if (x !in CATEGORY_LEFT..CATEGORY_RIGHT || y !in CATEGORY_TOP..CATEGORY_BOTTOM) return null
        return ((x - CATEGORY_LEFT + categoryScrollX) / CATEGORY_CELL_WIDTH).toInt()
            .takeIf { it in categories.indices }
    }

    private fun setInventoryScroll(value: Float) {
        val next = value.coerceIn(0f, maximumRowOffset() * (GRID_CELL_HEIGHT + GRID_GAP))
        if (next == inventoryScrollY) return
        inventoryScrollY = next
        selectedItem = selectedItem.coerceIn(0, max(0, filteredItems().lastIndex))
        postInvalidateOnAnimation()
    }

    private fun updateScrollbarPosition(y: Float) {
        val maximum = maximumRowOffset()
        if (maximum == 0) return
        val fraction = ((y - GRID_START_Y) / (GRID_BOTTOM - GRID_START_Y)).coerceIn(0f, 1f)
        setInventoryScroll(fraction * maximum * (GRID_CELL_HEIGHT + GRID_GAP))
    }

    private fun notifyPlayerAppearance() {
        val equipment = snapshot.equipment.associateBy { it.slot }
        val next = BotwPlayerAppearance(
            head = equipment["Head"]?.item?.actorName,
            chest = equipment["Chest"]?.item?.actorName,
            legs = equipment["Legs"]?.item?.actorName,
            weapon = null,
            bow = equipment["Bow"]?.item?.actorName,
            shield = equipment["Shield"]?.item?.actorName
        )
        val visible = page == Page.Inventory && isSaveLoaded()
        if (next != lastPlayerAppearance || visible != lastPlayerAppearanceVisible) {
            lastPlayerAppearance = next
            lastPlayerAppearanceVisible = visible
            Log.info(
                "[BOTW Companion] Appearance visible=$visible, " +
                    "head=${next.head}, chest=${next.chest}, legs=${next.legs}, " +
                    "weapon=${next.weapon}, bow=${next.bow}, shield=${next.shield}"
            )
            onPlayerAppearanceChanged(next, visible)
        }
    }

    private fun clampSelection(preferredItemId: Long? = null) {
        val items = filteredItems()
        if (items.isEmpty()) {
            selectedItem = 0
            rowOffset = 0
            return
        }
        val preferredIndex = preferredItemId?.let { id -> items.indexOfFirst { it.id == id } } ?: -1
        selectedItem = if (preferredIndex >= 0) preferredIndex else selectedItem.coerceIn(items.indices)
        // Snapshot refresh must not round an in-progress touch drag back to a whole row.
        inventoryScrollY = inventoryScrollY.coerceIn(
            0f, maximumRowOffset() * (GRID_CELL_HEIGHT + GRID_GAP)
        )
    }

    private fun filteredItems(): List<Item> {
        val source = snapshot.inventory
        if (source !== filteredInventorySource || filteredInventoryCategory != categoryIndex) {
            val category = categories.getOrElse(categoryIndex) { categories.first() }
            filteredInventorySource = source
            filteredInventoryCategory = categoryIndex
            filteredInventoryItems = source.filter { it.category.equals(category, true) }
        }
        return filteredInventoryItems
    }

    private fun visibleInventoryItems(): List<Item> {
        if (snapshot.capabilities and CAP_INVENTORY == 0) return emptyList()
        val items = filteredItems()
        if (items.isEmpty()) return emptyList()
        if (items === visibleInventorySource && visibleInventoryCategory == categoryIndex &&
            visibleInventoryRow == rowOffset && visibleInventorySelection == selectedItem
        ) {
            return visibleInventoryCache
        }
        val first = rowOffset * GRID_COLUMNS
        val last = min(items.size, first + GRID_COLUMNS * (GRID_ROWS_VISIBLE + 1))
        val visible = items.subList(first.coerceAtMost(items.size), last).toMutableList()
        items.getOrNull(selectedItem)?.let { selected ->
            if (visible.none { it.id == selected.id }) visible += selected
        }
        visibleInventorySource = items
        visibleInventoryCategory = categoryIndex
        visibleInventoryRow = rowOffset
        visibleInventorySelection = selectedItem
        visibleInventoryCache = visible
        // Warm one adjacent row in either direction on row changes, not on every pixel.
        // These are the same exact icons, still loaded by the existing bounded executor/cache.
        val warmFirst = max(0, first - GRID_COLUMNS)
        val warmLast = min(items.size, last + GRID_COLUMNS)
        for (index in warmFirst until warmLast) loadItemBitmap(items[index])
        return visibleInventoryCache
    }

    private fun inventoryCategoryCount(index: Int): Int {
        val source = snapshot.inventory
        if (source !== inventoryCountSource) {
            inventoryCountSource = source
            inventoryCategoryCounts = IntArray(categories.size)
            source.forEach { item ->
                val category = categories.indexOfFirst { it.equals(item.category, true) }
                if (category >= 0) inventoryCategoryCounts[category]++
            }
        }
        return inventoryCategoryCounts.getOrElse(index) { 0 }
    }

    private fun parseSnapshot(json: String): Snapshot {
        val root = JSONObject(json)
        val stats = parseStats(root)
        val inventory = root.optJSONArray("inventory").mapObjects { itemFromJson(it) }
        val equipment = root.optJSONArray("equipment").mapObjects { Equipment(it.optString("slot"), itemFromJson(it.getJSONObject("item"))) }
        val quests = root.optJSONArray("quests").mapObjects {
            Quest(
                it.optLong("id"), it.optString("actorName"), it.optString("name"),
                it.optString("objective"), it.optString("step"), it.optString("location"),
                it.optString("type"), it.optBoolean("complete")
            )
        }
        val effects = root.optJSONArray("effects").mapObjects {
            Effect(it.optString("name"), it.optInt("seconds"), it.optDouble("level").toFloat())
        }
        val runes = root.optJSONArray("runes").mapObjects {
            Rune(
                it.optString("id"), it.optString("name"), it.optInt("type", -1),
                it.optBoolean("available"), it.optBoolean("upgraded"),
                it.optBoolean("selected")
            )
        }
        val championPowers = root.optJSONArray("championPowers").mapObjects {
            ChampionPower(
                it.optString("id"), it.optString("name"), it.optBoolean("available"),
                it.optBoolean("enabled"), it.optInt("uses"), it.optInt("maxUses"),
                it.optInt("cooldownSeconds")
            )
        }
        val enteredShrines = root.optJSONArray("enteredShrines").toStringSet()
        val clearedShrines = root.optJSONArray("clearedShrines").toStringSet()
        val sensor = root.optJSONObject("sensor")?.let {
            SheikahSensor(
                it.optBoolean("unlocked"), it.optBoolean("upgraded"),
                it.optBoolean("enabled"), it.optInt("searchMode")
            )
        }
        val debug = root.optJSONObject("debug")
        return Snapshot(
            root.optString("status", "loading"), root.optBoolean("saveLoaded", false),
            root.optInt("capabilities"), stats,
            inventory, equipment, quests, effects, runes, championPowers,
            parseMap(root.optJSONObject("map")),
            enteredShrines, clearedShrines,
            debug?.optInt("equipState") ?: 0,
            debug?.optInt("equipAttempts") ?: 0,
            debug?.optString("actionRequest", "0x0") ?: "0x0",
            debug?.optString("actionCompleted", "0x0") ?: "0x0",
            debug?.optLong("actionType") ?: 0L,
            debug?.optInt("actionStage") ?: 0,
            sensor,
            root.optIntOrNull("heldItemCount"),
            parseActionDiagnostic(debug?.optJSONObject("actionDiagnostic"))
        )
    }

    private fun parseLiveSnapshot(json: String): LiveSnapshot {
        val root = JSONObject(json)
        val debug = root.optJSONObject("debug")
        return LiveSnapshot(
            root.optString("status", "loading"), parseStats(root),
            root.optIntOrNull("selectedRune"), parseMap(root.optJSONObject("map")),
            root.optIntOrNull("heldItemCount"),
            debug?.optInt("actionState") ?: 0,
            debug?.optInt("actionAttempts") ?: 0,
            debug?.optString("actionRequest", "0x0") ?: "0x0",
            debug?.optString("actionCompleted", "0x0") ?: "0x0",
            debug?.optLong("actionType") ?: 0L,
            debug?.optInt("actionStage") ?: 0,
            parseActionDiagnostic(debug?.optJSONObject("actionDiagnostic"))
        )
    }

    private fun parseActionDiagnostic(value: JSONObject?): ActionDiagnostic {
        if (value == null) return ActionDiagnostic()
        return ActionDiagnostic(
            value.optInt("sequence"), value.optLong("validationFlags"),
            value.optInt("itemType"), value.optInt("itemUse"),
            value.optInt("quantityBefore"), value.optBoolean("inInventoryBefore"),
            value.optIntOrNull("tab"), value.optIntOrNull("slot"),
            value.optIntOrNull("nodeOffset"), value.optIntOrNull("tabItemCount"),
            value.optIntOrNull("heldBefore"), value.optInt("nativeResult"),
            value.optString("target", "0x0"), value.optString("pouchManager", "0x0"),
            value.optString("player", "0x0"), value.optString("controller", "0x0"),
            value.optString("activeFlagPointer", "0x0"),
            value.optIntOrNull("controllerStateBefore"),
            value.optIntOrNull("controllerActionBefore"),
            value.optIntOrNull("controllerActiveBefore"),
            value.optString("controllerTargetBefore", "0x0"),
            value.optString("tabHead", "0x0"), value.optIntOrNull("guestQuantityAfter"),
            value.optBooleanOrNull("guestInInventoryAfter"),
            value.optIntOrNull("quantityAfter"),
            value.optBooleanOrNull("inInventoryAfter"), value.optIntOrNull("heldAfter")
        )
    }

    private fun parseMap(value: JSONObject?): PlayerMap? = value?.let {
        val x = it.optFloatOrNull("x")
        val y = it.optFloatOrNull("y")
        val z = it.optFloatOrNull("z")
        val heading = it.optFloatOrNull("heading") ?: 0f
        if (x != null && y != null && z != null) PlayerMap(x, y, z, heading) else null
    }

    private fun parseStats(root: JSONObject): Stats? {
        val statsObject = root.optJSONObject("stats")
        return statsObject?.let {
            Stats(
                it.optIntOrNull("health"), it.optIntOrNull("maxHealth"), it.optFloatOrNull("stamina"),
                it.optFloatOrNull("maxStamina"), it.optIntOrNull("rupees"),
                it.optIntOrNull("defense"), it.optIntOrNull("attack"),
                it.optIntOrNull("bowAttack"), it.optIntOrNull("shieldGuard")
            )
        }
    }

    private fun mergeLiveStats(live: Stats): Stats {
        val previous = snapshot.stats
        return Stats(
            live.health ?: previous?.health,
            live.maxHealth ?: previous?.maxHealth,
            live.stamina ?: previous?.stamina,
            live.maxStamina ?: previous?.maxStamina,
            live.rupees ?: previous?.rupees,
            previous?.defense,
            previous?.attack,
            previous?.bowAttack,
            previous?.shieldGuard
        )
    }

    private fun mergeLiveRuneSelection(selectedType: Int?): Boolean {
        if (selectedType == null || snapshot.runes.isEmpty()) return false
        val nextRunes = snapshot.runes.map { rune ->
            val selected = rune.type == selectedType ||
                (rune.id == "bombs" && selectedType == 1)
            if (rune.selected == selected) rune else rune.copy(selected = selected)
        }
        if (nextRunes == snapshot.runes) return false
        snapshot = snapshot.copy(runes = nextRunes)
        return true
    }

    private fun itemFromJson(item: JSONObject): Item {
        val actorName = item.optString("actorName")
        val displayName = when (actorName) {
            "Armor_Default_Head" -> "No Headgear"
            "Armor_Default_Upper" -> "No Tunic"
            "Armor_Default_Lower" -> "Default Trousers"
            else -> item.optString("name")
        }
        return Item(
            item.optLong("id"), item.optInt("itemType", -1),
            item.optInt("itemUse", -1), actorName,
            item.optString("iconActorName", actorName),
            displayName, item.optString("category"),
            item.optIntOrNull("power"), item.optIntOrNull("defense"),
            item.optIntOrNull("durability"), item.optIntOrNull("count"),
            item.optIntOrNull("modifierValue"), item.optIntOrNull("modifierFlags"),
            item.optBoolean("equippable"), item.optBoolean("holdable"),
            item.optBoolean("edible"), item.optBoolean("equipped"),
            item.optString("description")
        )
    }

    private inline fun <T> JSONArray?.mapObjects(transform: (JSONObject) -> T): List<T> {
        if (this == null) return emptyList()
        return buildList(length()) { for (index in 0 until length()) add(transform(getJSONObject(index))) }
    }

    private fun JSONArray?.toStringSet(): Set<String> {
        if (this == null) return emptySet()
        return buildSet(length()) {
            for (index in 0 until length()) {
                optString(index).takeIf { it.isNotBlank() }?.let(::add)
            }
        }
    }

    private fun JSONObject.optIntOrNull(name: String) = if (has(name) && !isNull(name)) getInt(name) else null
    private fun JSONObject.optFloatOrNull(name: String) = if (has(name) && !isNull(name)) getDouble(name).toFloat() else null
    private fun JSONObject.optBooleanOrNull(name: String) = if (has(name) && !isNull(name)) getBoolean(name) else null

    private fun panel(canvas: Canvas, rect: RectF, active: Boolean = false) {
        paint.color = if (active) Color.argb(180, 3, 31, 35) else Color.argb(188, 2, 18, 21)
        canvas.drawRect(rect, paint)
        stroke.color = if (active) cyan else bronzeDark
        stroke.strokeWidth = if (active) 2f else 1f
        canvas.drawRect(rect, stroke)
        if (active) glowRect(canvas, rect)
    }

    private fun glowRect(canvas: Canvas, rect: RectF) {
        stroke.color = Color.argb(80, 36, 230, 242)
        stroke.strokeWidth = 6f
        canvas.drawRect(rect, stroke)
        stroke.strokeWidth = 1f
    }

    private fun cornerMarks(canvas: Canvas, rect: RectF, color: Int) {
        stroke.color = color
        stroke.strokeWidth = 1.5f
        val d = 10f
        val inset = 7f
        listOf(
            floatArrayOf(rect.left + inset, rect.top + inset, 1f, 1f),
            floatArrayOf(rect.right - inset, rect.top + inset, -1f, 1f),
            floatArrayOf(rect.left + inset, rect.bottom - inset, 1f, -1f),
            floatArrayOf(rect.right - inset, rect.bottom - inset, -1f, -1f)
        ).forEach {
            canvas.drawLine(it[0], it[1], it[0] + d * it[2], it[1], stroke)
            canvas.drawLine(it[0], it[1], it[0], it[1] + d * it[3], stroke)
        }
    }

    private fun text(canvas: Canvas, value: String, x: Float, y: Float, size: Float, color: Int, align: Paint.Align = Paint.Align.LEFT) {
        paint.shader = null
        paint.style = Paint.Style.FILL
        paint.color = color
        paint.textSize = size
        paint.textAlign = align
        paint.typeface = normalTypeface
        canvas.drawText(value, x, y, paint)
    }

    private fun multilineText(canvas: Canvas, value: String, x: Float, y: Float, maxWidth: Float, size: Float, color: Int, lines: Int) {
        val paragraphs = value.replace("\r", "").split('\n')
        var lineIndex = 0
        paragraphs.forEach { paragraph ->
            if (lineIndex >= lines) return
            if (paragraph.isBlank()) {
                lineIndex++
                return@forEach
            }
            var line = ""
            paragraph.split(Regex("\\s+")).forEach { word ->
                val candidate = if (line.isEmpty()) word else "$line $word"
                paint.textSize = size
                if (paint.measureText(candidate) > maxWidth && line.isNotEmpty()) {
                    text(canvas, line, x, y + lineIndex * (size + 5f), size, color)
                    line = word
                    if (++lineIndex >= lines) return
                } else {
                    line = candidate
                }
            }
            if (lineIndex < lines && line.isNotEmpty()) {
                text(canvas, line, x, y + lineIndex * (size + 5f), size, color)
                lineIndex++
            }
        }
    }

    private fun drawHeart(
        canvas: Canvas, x: Float, y: Float, filled: Boolean, halfSize: Float = 16f
    ) {
        paint.alpha = if (filled) 255 else 48
        val icon = loadBitmap("botw/ui/heart.png")
        if (icon != null) {
            paint.colorFilter = heartIconTint
            canvas.drawBitmap(
                icon, null,
                RectF(x - halfSize, y - halfSize, x + halfSize, y + halfSize), paint
            )
            paint.colorFilter = null
        }
        paint.alpha = 255
    }

    private fun drawHealth(canvas: Canvas, x: Float, y: Float, value: Int, maximum: Int) {
        val ratio = if (maximum > 0) (value.toFloat() / maximum).coerceIn(0f, 1f) else 0f
        val percent = (ratio * 100f).roundToInt()
        drawHeart(canvas, x, y, filled = true, halfSize = 37f)
        text(canvas, "Health", x + 50f, y - 9f, 18f, muted)
        text(canvas, "$percent%", x + 278f, y - 9f, 15f, warmWhite, Paint.Align.RIGHT)
        val bar = RectF(x + 50f, y + 3f, x + 278f, y + 19f)
        paint.color = Color.rgb(60, 20, 23)
        canvas.drawRoundRect(bar, 6f, 6f, paint)
        if (ratio > 0f) {
            paint.color = Color.rgb(239, 61, 68)
            canvas.drawRoundRect(
                RectF(bar.left, bar.top, bar.left + bar.width() * ratio, bar.bottom),
                6f, 6f, paint
            )
        }
        stroke.color = bronzeDark
        stroke.strokeWidth = 1f
        canvas.drawRoundRect(bar, 6f, 6f, stroke)
    }

    private fun drawStamina(canvas: Canvas, x: Float, y: Float, value: Float, maximum: Float) {


        stroke.strokeWidth = 5f
        stroke.color = green
        canvas.drawCircle(x, y, 23f, stroke)
        stroke.strokeWidth = 1f
        val ratio = if (maximum > 0f) (value / maximum).coerceIn(0f, 1f) else 0f
        text(canvas, "Stamina", x + 50f, y - 7f, 18f, muted)
        val percent = (ratio * 100f).roundToInt()
        text(canvas, "$percent%", x + 278f, y - 7f, 15f, warmWhite, Paint.Align.RIGHT)
        val bar = RectF(x + 50f, y + 3f, x + 278f, y + 19f)
        paint.color = Color.rgb(23, 54, 30)
        canvas.drawRoundRect(bar, 6f, 6f, paint)
        paint.color = green
        if (ratio > 0f) {
            canvas.drawRoundRect(
                RectF(bar.left, bar.top, bar.left + bar.width() * ratio, bar.bottom),
                6f, 6f, paint
            )
        }
        stroke.color = bronzeDark
        stroke.strokeWidth = 1f
        canvas.drawRoundRect(bar, 6f, 6f, stroke)
    }

    private fun statLine(
        canvas: Canvas, x: Float, y: Float, label: String, value: Int?, iconPath: String,
        width: Float
    ) {
        val icon = loadBitmap(iconPath)
        icon?.let {
            paint.colorFilter = warmWhiteIconTint
            canvas.drawBitmap(it, null, RectF(x, y - 35f, x + 36f, y + 1f), paint)
            paint.colorFilter = null
        }
        val textX = x + 42f
        text(canvas, ellipsizeToWidth(label, width - 48f, 13f), textX, y - 14f, 13f, muted)
        text(canvas, "${value ?: 0}", textX, y + 12f, 22f, warmWhite)
    }

    private fun badge(
        canvas: Canvas, rect: RectF, value: String, color: Int = warmWhite, textSize: Float = 15f
    ) {
        paint.color = Color.argb(120, 3, 25, 28)
        canvas.drawRect(rect, paint)
        stroke.color = if (color == warmWhite) bronzeDark else color
        canvas.drawRect(rect, stroke)
        text(canvas, value, rect.centerX(), rect.centerY() + 7f, textSize, color, Paint.Align.CENTER)
    }

    private fun drawLabeledBadge(
        canvas: Canvas, rect: RectF, label: String, value: String
    ) {
        paint.color = Color.argb(120, 3, 25, 28)
        canvas.drawRect(rect, paint)
        stroke.color = bronzeDark
        canvas.drawRect(rect, stroke)
        text(canvas, label, rect.left + 8f, rect.top + 16f, 11f, muted)
        text(canvas, value, rect.left + 8f, rect.bottom - 7f, 17f, warmWhite)
    }

    private fun modifierLabel(item: Item): String {
        val flags = item.modifierFlags ?: return ""
        val value = item.modifierValue ?: return ""
        return when {
            flags and 0x1 != 0 -> "Attack +$value"
            flags and 0x100 != 0 -> "Guard +$value"
            value > 0 -> "+$value"
            else -> ""
        }
    }

    private fun drawCategoryIcon(canvas: Canvas, index: Int, x: Float, y: Float, active: Boolean) {
        val icon = categoryIconAssets.getOrNull(index)?.let(::loadBitmap) ?: return
        val halfSize = 24f
        paint.alpha = 255
        paint.colorFilter = categoryIconTintFilters[if (active) 1 else 0]
        canvas.drawBitmap(
            icon, null, RectF(x - halfSize, y - halfSize, x + halfSize, y + halfSize), paint
        )
        paint.colorFilter = null
    }

    private fun categoryMaskTint(color: Int) = ColorMatrixColorFilter(
        floatArrayOf(
            0f, 0f, 0f, 0f, Color.red(color).toFloat(),
            0f, 0f, 0f, 0f, Color.green(color).toFloat(),
            0f, 0f, 0f, 0f, Color.blue(color).toFloat(),
            1f, 0f, 0f, 0f, 0f
        )
    )

    private fun mapMaskTint(color: Int) = mapMaterialTint(Color.BLACK, color)

    private fun mapMaterialTint(black: Int, white: Int): ColorMatrixColorFilter {
        val redScale = (Color.red(white) - Color.red(black)) / 255f
        val greenScale = (Color.green(white) - Color.green(black)) / 255f
        val blueScale = (Color.blue(white) - Color.blue(black)) / 255f
        return ColorMatrixColorFilter(
            floatArrayOf(
                redScale, 0f, 0f, 0f, Color.red(black).toFloat(),
                0f, greenScale, 0f, 0f, Color.green(black).toFloat(),
                0f, 0f, blueScale, 0f, Color.blue(black).toFloat(),
                0f, 0f, 0f, 1f, 0f
            )
        )
    }

    private fun mailboxAddressMatches(encoded: String, expected: Long): Boolean = runCatching {
        encoded.removePrefix("0x").toULong(16) == expected.toULong()
    }.getOrDefault(false)

    private fun logActionSnapshot(
        status: String, capabilities: Int, inventorySize: Int, equipmentSize: Int,
        runeCount: Int, rupees: Int?, state: Int, attempts: Int, request: String,
        completed: String, type: Long, stage: Int, diagnostic: ActionDiagnostic
    ) {
        if (lastLoggedStatus == status && lastLoggedCapabilities == capabilities &&
            lastLoggedEquipState == state && lastLoggedEquipAttempts == attempts &&
            lastLoggedActionRequest == request && lastLoggedActionCompleted == completed &&
            lastLoggedActionType == type && lastLoggedActionStage == stage &&
            lastLoggedActionDiagnosticSequence == diagnostic.sequence
        ) return
        lastLoggedStatus = status
        lastLoggedCapabilities = capabilities
        lastLoggedEquipState = state
        lastLoggedEquipAttempts = attempts
        lastLoggedActionRequest = request
        lastLoggedActionCompleted = completed
        lastLoggedActionType = type
        lastLoggedActionStage = stage
        lastLoggedActionDiagnosticSequence = diagnostic.sequence
        Log.info(
            "[BOTW-ITEM-ACTION] sync status=$status capabilities=$capabilities " +
                "items=$inventorySize equipment=$equipmentSize runes=$runeCount " +
                "rupees=${rupees ?: "unavailable"} state=$state attempts=$attempts " +
                "request=$request completed=$completed type=$type stage=$stage " +
                "stageName=${actionStageName(stage)} diagnosticSequence=${diagnostic.sequence} " +
                "validationFlags=0x${diagnostic.validationFlags.toString(16)} " +
                "target=${diagnostic.target} itemType=${diagnostic.itemType} " +
                "itemUse=${diagnostic.itemUse} quantityBefore=${diagnostic.quantityBefore} " +
                "guestQuantityAfter=${diagnostic.guestQuantityAfter ?: "unavailable"} " +
                "guestInInventoryAfter=${diagnostic.guestInInventoryAfter ?: "unavailable"} " +
                "quantityAfter=${diagnostic.quantityAfter ?: "unavailable"} " +
                "inInventoryBefore=${diagnostic.inInventoryBefore} " +
                "inInventoryAfter=${diagnostic.inInventoryAfter ?: "unavailable"} " +
                "tab=${diagnostic.tab ?: "unavailable"} slot=${diagnostic.slot ?: "unavailable"} " +
                "nodeOffset=${diagnostic.nodeOffset ?: "unavailable"} " +
                "tabItemCount=${diagnostic.tabItemCount ?: "unavailable"} " +
                "tabHead=${diagnostic.tabHead} pouchManager=${diagnostic.pouchManager} " +
                "player=${diagnostic.player} heldBefore=${diagnostic.heldBefore ?: "unavailable"} " +
                "heldAfter=${diagnostic.heldAfter ?: "unavailable"} " +
                "nativeResult=${diagnostic.nativeResult} controller=${diagnostic.controller} " +
                "activeFlagPointer=${diagnostic.activeFlagPointer} " +
                "controllerStateBefore=${diagnostic.controllerStateBefore ?: "unavailable"} " +
                "controllerActionBefore=${diagnostic.controllerActionBefore ?: "unavailable"} " +
                "controllerActiveBefore=${diagnostic.controllerActiveBefore ?: "unavailable"} " +
                "controllerTargetBefore=${diagnostic.controllerTargetBefore}"
        )
    }

    private fun actionStageName(stage: Int): String = when (stage) {
        0x01 -> "claimed"
        0x02 -> "pouch-resolved"
        0x03 -> "item-validated"
        0x10 -> "armor-mutation"
        0x11 -> "armor-requested"
        0x12 -> "weapon-mutation"
        0x13 -> "weapon-requested"
        0x20 -> "equipment-check"
        0x21 -> "equipment-loading"
        0x22 -> "equipment-actors-wait"
        0x23 -> "player-resolve"
        0x24 -> "player-valid"
        0x25 -> "player-refresh-requested"
        0x26 -> "player-info-wait"
        0x27 -> "player-wait"
        0x30 -> "hold-dispatch"
        0x31 -> "hold-native-accepted"
        0x32 -> "hold-rejected-missing-active-flag"
        0x33 -> "hold-native-rejected"
        0x40 -> "eat-dispatch"
        0x41 -> "eat-complete"
        0x50 -> "unequip-mutation"
        0x51 -> "unequip-default-resolve"
        0x52 -> "unequip-player-switch"
        0x53 -> "unequip-confirmed"
        0x60 -> "fast-travel-dispatch"
        0x61 -> "fast-travel-returned"
        0x70 -> "rune-dispatch"
        0x71 -> "rune-returned"
        0x81 -> "rejected-missing-request"
        0x82 -> "rejected-timeout"
        0x83 -> "rejected-equipment-type"
        0x84 -> "rejected-equipment-idle"
        0x85 -> "rejected-equipment-status"
        0x86 -> "rejected-equipment-marker"
        else -> "unknown"
    }

    private fun drawPageIcon(canvas: Canvas, value: Page, x: Float, y: Float) {
        stroke.color = if (page == value) cyan else muted
        stroke.strokeWidth = 2f
        when (value) {
            Page.Map -> { path.reset(); path.moveTo(x - 16f, y - 15f); path.lineTo(x - 5f, y - 11f); path.lineTo(x + 6f, y - 15f); path.lineTo(x + 16f, y - 11f); path.lineTo(x + 16f, y + 15f); path.lineTo(x + 5f, y + 11f); path.lineTo(x - 6f, y + 15f); path.lineTo(x - 16f, y + 11f); path.close(); canvas.drawPath(path, stroke) }
            Page.Inventory -> { canvas.drawRoundRect(RectF(x - 16f, y - 11f, x + 16f, y + 14f), 3f, 3f, stroke); canvas.drawLine(x - 8f, y - 14f, x + 8f, y - 14f, stroke) }
            Page.Quests -> { canvas.drawRect(RectF(x - 13f, y - 16f, x + 13f, y + 15f), stroke); canvas.drawLine(x - 7f, y - 7f, x + 7f, y - 7f, stroke); canvas.drawLine(x - 7f, y, x + 5f, y, stroke) }
        }
        stroke.strokeWidth = 1f
    }

    private fun drawItemGlyph(canvas: Canvas, item: Item, x: Float, y: Float, scale: Float = 1f) {
        val icon = loadItemBitmap(item) ?: return
        val half = 46f * scale
        paint.alpha = 255
        canvas.drawBitmap(icon, null, RectF(x - half, y - half, x + half, y + half), paint)
    }

    private fun loadItemBitmap(item: Item): Bitmap? {
        if (item.actorName.contains('/') || item.actorName.contains("..") ||
            item.actorName.startsWith("Armor_Default_")
        ) {
            return null
        }
        val stockActor = stockIconAliases[item.iconActorName] ?: item.iconActorName
        return loadBitmap("botw/stock/$stockActor.png")
    }

    private fun exactTexturesReady(paths: Iterable<String>): Boolean {
        var ready = true
        paths.forEach { path ->
            if (loadBitmap(path) == null) ready = false
        }
        return ready
    }

    private fun exactItemTexturesReady(items: Iterable<Item>): Boolean {
        var ready = true
        items.forEach { item ->
            if (!item.actorName.startsWith("Armor_Default_") && loadItemBitmap(item) == null) {
                ready = false
            }
        }
        return ready
    }

    private fun drawRupee(canvas: Canvas, x: Float, y: Float) {
        val icon = loadBitmap("botw/ui/rupee.png")
        if (icon != null) {
            canvas.drawBitmap(icon, null, RectF(x - 21f, y - 21f, x + 21f, y + 21f), paint)
        }
    }

    private fun drawChampionPowers(canvas: Canvas) {
        if (snapshot.championPowers.isEmpty()) return
        val powers = snapshot.championPowers.take(4)
        val required = powers.flatMap { power ->
            val id = power.id.lowercase()
            listOf("botw/ui/champion/$id.png", "botw/ui/champion/${id}_shadow.png")
        }
        if (!exactTexturesReady(required)) {
            text(canvas, "Loading exact Champion textures", 343f, 966f, 15f, muted, Paint.Align.CENTER)
            return
        }
        val left = 18f
        val right = 669f
        val gap = 6f
        val width = (right - left - gap * 3f) / 4f
        powers.forEachIndexed { index, power ->
            val cellLeft = left + index * (width + gap)
            val cell = RectF(cellLeft, POWER_ROW_TOP, cellLeft + width, POWER_ROW_BOTTOM)
            val active = power.available && power.enabled
            val normalizedId = power.id.lowercase()
            val accent = when {
                "grace" in normalizedId -> Color.rgb(92, 172, 213)
                "fury" in normalizedId -> Color.rgb(181, 146, 53)
                "gale" in normalizedId -> Color.rgb(112, 202, 128)
                "protection" in normalizedId -> Color.rgb(226, 61, 55)
                else -> cyan
            }
            val icon = loadBitmap("botw/ui/champion/$normalizedId.png")
            val shadow = loadBitmap("botw/ui/champion/${normalizedId}_shadow.png")
            val iconRect = RectF(
                cell.centerX() - 32f, cell.top + 55f,
                cell.centerX() + 32f, cell.top + 119f
            )
            paint.alpha = if (power.available) 255 else 72
            paint.colorFilter = championIconTints[normalizedId] ?: cyanIconTint
            if (shadow != null) {
                val mainAlpha = paint.alpha
                paint.alpha = (mainAlpha * 0.48f).roundToInt()
                canvas.drawBitmap(shadow, null, iconRect, paint)
                paint.alpha = mainAlpha
            }
            paint.colorFilter = if (power.available) warmWhiteIconTint else mutedIconTint
            if (icon != null) {
                canvas.drawBitmap(icon, null, iconRect, paint)
            }
            paint.colorFilter = null
            paint.alpha = 255
            text(
                canvas, power.uses.coerceAtLeast(0).toString(),
                iconRect.right + 12f, iconRect.top + 28f, 28f,
                if (active) accent else muted, Paint.Align.LEFT
            )
        }
    }

    private fun drawRunes(canvas: Canvas, panel: RectF) {
        val visible = snapshot.runes.filter { it.available }
        if (visible.isEmpty()) return
        val strip = runeStripRect(panel)
        if (!exactTexturesReady(visible.map { "botw/ui/rune_${it.id}.png" })) {
            text(canvas, "Loading exact rune textures", strip.centerX(), strip.centerY() + 6f, 14f, muted, Paint.Align.CENTER)
            return
        }
        stroke.color = bronzeDark
        canvas.drawRect(strip, stroke)
        val width = strip.width() / visible.size
        visible.forEachIndexed { index, rune ->
            val centerX = strip.left + width * (index + .5f)
            val cell = RectF(
                strip.left + width * index, strip.top,
                strip.left + width * (index + 1), strip.bottom
            )
            if (rune.selected) {
                paint.color = Color.argb(58, 30, 221, 235)
                canvas.drawRect(cell, paint)
                stroke.color = cyan
                stroke.strokeWidth = 2.5f
                canvas.drawRect(RectF(cell.left + 2f, cell.top + 2f, cell.right - 2f, cell.bottom - 2f), stroke)
                stroke.strokeWidth = 1f
            } else if (index > 0) {
                stroke.color = bronzeDark
                canvas.drawLine(cell.left, cell.top + 7f, cell.left, cell.bottom - 7f, stroke)
            }
            val icon = loadBitmap("botw/ui/rune_${rune.id}.png")
            paint.alpha = 255
            if (icon != null) {
                canvas.drawBitmap(icon, null, RectF(centerX - 31f, strip.top + 5f, centerX + 31f, strip.top + 67f), paint)
            }
            text(
                canvas, if (rune.upgraded) "${rune.name}+" else rune.name,
                centerX, strip.bottom - 9f, 12f,
                if (rune.selected || rune.upgraded) cyan else warmWhite,
                Paint.Align.CENTER
            )
        }
        paint.alpha = 255
    }

    private fun loadBitmap(path: String): Bitmap? {
        if (path == MAP_TEXTURE_ASSET) {
            mapBitmap?.takeUnless(Bitmap::isRecycled)?.let { return it }
        }
        bitmapCache.get(path)?.let { return it }
        requestRuntimeTexture(path)
        return null
    }

    private fun requestRuntimeTexture(path: String) {
        if (!path.startsWith("botw/") || !path.endsWith(".png") ||
            !isAttachedToWindow || path in runtimeTextureMissing ||
            !runtimeTextureRequests.add(path)
        ) {
            return
        }
        val executor = textureExecutor ?: run {
            runtimeTextureRequests.remove(path)
            return
        }
        val generation = textureGeneration
        try {
            executor.execute {
                val bitmap = try {
                    loadRuntimeTexture(path)
                } catch (_: Exception) {
                    null
                }
                handler.post {
                    if (generation != textureGeneration || !isAttachedToWindow) {
                        bitmap?.recycle()
                        return@post
                    }
                    // A completion from an old view generation must not clear the new
                    // generation's request for the same icon and queue duplicate decodes.
                    runtimeTextureRequests.remove(path)
                    if (bitmap == null) {
                        runtimeTextureMissing += path
                        handler.postDelayed({
                            if (generation == textureGeneration && isAttachedToWindow) {
                                runtimeTextureMissing.remove(path)
                                invalidate()
                            }
                        }, RUNTIME_TEXTURE_RETRY_MS)
                    } else {
                        if (path == MAP_TEXTURE_ASSET) {
                            mapBitmap?.takeUnless(Bitmap::isRecycled)?.recycle()
                            mapBitmap = bitmap
                        } else {
                            bitmapCache.put(path, bitmap)
                        }
                        postInvalidateOnAnimation()
                    }
                }
            }
        } catch (_: RuntimeException) {
            runtimeTextureRequests.remove(path)
        }
    }

    private fun loadRuntimeTexture(path: String): Bitmap? {
        val cacheFile = runtimeTextureCacheFile(path)
        val maximumBytes = if (path == MAP_TEXTURE_ASSET) {
            MAX_MAP_TEXTURE_BYTES
        } else {
            MAX_RUNTIME_TEXTURE_BYTES
        }
        if (cacheFile?.isFile == true &&
            cacheFile.length() in 1..maximumBytes.toLong()
        ) {
            BitmapFactory.decodeFile(cacheFile.absolutePath)?.let { return it }
            cacheFile.delete()
        }
        val png = try {
            NativeLibrary.getDualScreenRomfsAsset(path)
        } catch (_: Throwable) {
            null
        } ?: return null
        if (png.isEmpty() || png.size > maximumBytes) return null
        val bitmap = BitmapFactory.decodeByteArray(png, 0, png.size) ?: return null
        if (cacheFile != null) {
            try {
                val parent = cacheFile.parentFile
                if (parent != null) {
                    parent.mkdirs()
                    val temporary = File(parent, "${cacheFile.name}.tmp")
                    FileOutputStream(temporary).use { output -> output.write(png) }
                    if (!temporary.renameTo(cacheFile)) {
                        temporary.delete()
                    }
                }
            } catch (_: Exception) {

            }
        }
        return bitmap
    }

    private fun runtimeTextureCacheFile(path: String): File? {
        var revision = textureRevision
        if (revision == null) {
            revision = try {
                NativeLibrary.getDualScreenRomfsRevision()
            } catch (_: Throwable) {
                ""
            }.ifBlank { "base" }.replace(Regex("[^A-Za-z0-9_.-]"), "_")
            textureRevision = revision
        }
        val digest = MessageDigest.getInstance("SHA-256").digest(path.toByteArray(Charsets.UTF_8))
        val name = buildString(digest.size * 2 + 4) {
            digest.forEach { byte -> append("%02x".format(byte.toInt() and 0xFF)) }
            append(".png")
        }
        return File(context.cacheDir, "botw-romfs-textures/v7-$revision/$name")
    }

    private fun drawEffectIcon(canvas: Canvas, x: Float, y: Float, index: Int) {
        paint.color = when (index) { 0 -> Color.rgb(239, 157, 26); 1 -> Color.rgb(100, 193, 225); else -> Color.rgb(238, 212, 51) }
        canvas.drawCircle(x, y, 7f, paint)
        stroke.color = paint.color
        for (i in 0 until 8) {
            val angle = Math.toRadians((i * 45).toDouble())
            canvas.drawLine(x + kotlin.math.cos(angle).toFloat() * 11f, y + kotlin.math.sin(angle).toFloat() * 11f, x + kotlin.math.cos(angle).toFloat() * 15f, y + kotlin.math.sin(angle).toFloat() * 15f, stroke)
        }
    }

    private fun drawSheikahMedallion(canvas: Canvas, x: Float, y: Float, radius: Float) {
        stroke.color = Color.argb(75, 31, 207, 220)
        stroke.strokeWidth = 2f
        canvas.drawCircle(x, y, radius, stroke)
        canvas.drawCircle(x, y, radius * .72f, stroke)
        canvas.drawCircle(x, y, radius * .2f, stroke)
        for (index in 0 until 12) {
            val angle = Math.toRadians((index * 30).toDouble())
            canvas.drawLine(x + kotlin.math.cos(angle).toFloat() * radius * .72f, y + kotlin.math.sin(angle).toFloat() * radius * .72f, x + kotlin.math.cos(angle).toFloat() * radius, y + kotlin.math.sin(angle).toFloat() * radius, stroke)
        }
        stroke.strokeWidth = 1f
    }

    private fun shortCategory(value: String) = if (value == "Key Items") "Key" else value
    private fun formatTime(seconds: Int) = "%d:%02d".format(seconds / 60, seconds % 60)
    private val cyan = Color.rgb(33, 219, 234)
    private val warmWhite = Color.rgb(225, 211, 187)
    private val muted = Color.rgb(177, 164, 142)
    private val bronze = Color.rgb(190, 145, 81)
    private val bronzeDark = Color.rgb(80, 75, 60)
    private val green = Color.rgb(104, 224, 62)
}
