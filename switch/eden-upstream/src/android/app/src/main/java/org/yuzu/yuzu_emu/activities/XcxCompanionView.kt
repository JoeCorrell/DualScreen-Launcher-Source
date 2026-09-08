package org.yuzu.yuzu_emu.activities

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import android.graphics.Typeface
import android.os.Process
import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.TimeUnit
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.min
import kotlin.math.sqrt
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.utils.Log

/**
 * Independent bottom-display UI for the verified XCX DE 1.0.2 executable.
 *
 * Page selection and view state stay entirely on the Android presentation. Game-derived fields
 * come from one validated native snapshot, and no bottom-display input is forwarded to gameplay.
 */
internal class XcxCompanionView(context: Context) : View(context), XcxControllerTarget {
    private companion object {
        private const val DESIGN_WIDTH = 1240f
        private const val DESIGN_HEIGHT = 1080f
        private const val SNAPSHOT_SIZE = 308
        private const val SNAPSHOT_MAGIC = 0x53584358
        private const val SNAPSHOT_VERSION = 9
        private const val POLL_INTERVAL_MS = 100L
        private const val ACTIVE_STATUS = 4

        private const val CAP_GAME_STATE = 1 shl 0
        private const val CAP_ZONE_STATE = 1 shl 1
        private const val CAP_QUEST_STATE = 1 shl 2
        private const val CAP_SEGMENT_STATE = 1 shl 3
        private const val CAP_PLAYER_TRANSFORM = 1 shl 4
        private const val CAP_PARTY_STATE = 1 shl 5
        private const val CAP_COMBAT_STATE = 1 shl 6
        private const val CAP_ACTION_BRIDGE = 1 shl 7
        private const val CAP_CREDITS_STATE = 1 shl 8
        private const val CAP_NAVIGATION_STATE = 1 shl 9

        private const val ACTION_SWITCH_CHARACTER = 1
        private const val ACTION_FAST_TRAVEL = 2
        private const val ACTION_INSTALL_PROBE = 3
        private const val ACTION_TRACK_MISSION = 4
        private const val ACTION_SWAP_PARTY = 5
        private const val ACTION_EQUIP_ITEM = 6
        private const val ACTION_STATE_QUEUED = 1
        private const val ACTION_STATE_RUNNING = 2

        private val PAGE_NAMES = arrayOf(
            "MAP", "FRONTIERNAV", "MISSIONS", "COMBAT", "PARTY", "SKELL"
        )

        private const val CONTROL_NONE = -1
        private const val CONTROL_PREVIOUS_GROUP = 6
        private const val CONTROL_NEXT_GROUP = 7
        private const val CONTROL_MAP_CENTER = 8
        private const val CONTROL_MAP_ZOOM_OUT = 9
        private const val CONTROL_MAP_ZOOM_IN = 10
        private const val CONTROL_MAP_LOCATIONS = 11
        private const val CONTROL_MAP_PROBES = 12
        private const val CONTROL_MAP_FAST_TRAVEL = 13
        private const val CONTROL_MISSIONS_PREVIOUS = 14
        private const val CONTROL_MISSIONS_NEXT = 15
        private const val CONTROL_PARTY_SWITCH = 16
        private const val CONTROL_MISSION_TRACK = 17
        private const val CONTROL_MISSION_MAP = 18
        private const val CONTROL_SEGMENT_FIRST = 20
        private const val CONTROL_PARTY_FIRST = 40
        private const val CONTROL_MISSION_FIRST = 60
        private const val CONTROL_PARTY_UP = 80
        private const val CONTROL_PARTY_DOWN = 81
        private const val CONTROL_EQUIPMENT = 82
        private const val CONTROL_EQUIPMENT_CLOSE = 83
        private const val CONTROL_FRONTIER_PREVIOUS = 84
        private const val CONTROL_FRONTIER_NEXT = 85
        private const val CONTROL_FRONTIER_MAP = 86
        private const val CONTROL_FRONTIER_SITE = 100
        private const val CONTROL_EQUIPMENT_SLOT = 120
        private const val CONTROL_EQUIPMENT_ROW = 140
        private const val CONTROL_EQUIPMENT_PREVIOUS = 150
        private const val CONTROL_EQUIPMENT_NEXT = 151
        private const val CONTROL_EQUIPMENT_APPLY = 152
        private val FRONTIER_ZONES = longArrayOf(1101, 1501, 1601, 1701, 2001, 2201)
        private const val MISSION_FLAG_POLL_MS = 750L
        private const val MAP_FLAG_POLL_MS = 1_000L
        private const val ASSET_RETRY_MS = 5_000L
    }

    private data class Snapshot(
        val status: Int = 0,
        val capabilities: Int = 0,
        val checkpoint: Int = 0,
        val zone: Long = 0,
        val miranium: Long = 0,
        val lastQuest: Int = -1,
        val segmentMask: Int = 0,
        val segmentScores: LongArray = LongArray(6),
        val playerTransformValid: Boolean = false,
        val playerHeadingValid: Boolean = false,
        val playerX: Float = 0f,
        val playerY: Float = 0f,
        val playerZ: Float = 0f,
        val playerRotationX: Float = 0f,
        val playerRotationY: Float = 0f,
        val playerRotationZ: Float = 0f,
        val playerRotationW: Float = 1f,
        val partyValidMask: Int = 0,
        val partyHandles: LongArray = LongArray(4),
        val partyCharacterIds: IntArray = IntArray(4),
        val partyActorSlots: IntArray = IntArray(4),
        val partyRecordStates: IntArray = IntArray(4),
        val partyRecordFlags: LongArray = LongArray(4),
        val activePartySlot: Int = -1,
        val playerCharacterId: Int = 0,
        val playerActorType: Int = 0,
        val battleEnemyCount: Long = 0,
        val pauseFlags: Long = 0,
        val actionState: Int = 0,
        val actionType: Int = 0,
        val actionArgument: Int = 0,
        val actionResult: Int = 0,
        val actionRequest: Long = 0,
        val actionCompleted: Long = 0,
        val sampleTimeUs: Long = 0,
        val partyVitalsMask: Int = 0,
        val partyHp: LongArray = LongArray(4),
        val partyMaximumHp: LongArray = LongArray(4),
        val credits: Long = 0,
        val skellFuelMask: Int = 0,
        val partyLevelMask: Int = 0,
        val partyLevels: IntArray = IntArray(4),
        val trackedMission: Int = 0,
        val skellFuel: FloatArray = FloatArray(4),
        val skellMaximumFuel: FloatArray = FloatArray(4)
    )

    private data class MapMarker(
        val infoId: Int,
        val discoveryFlag: Int,
        val openFlag: Int,
        val skipTravelId: Long,
        val formationId: Int,
        val probeId: Int,
        val kind: Int,
        val icon: Int,
        val locationType: Int,
        val nativeFlags: Int,
        val worldX: Float,
        val worldZ: Float,
        val offsetX: Float,
        val offsetY: Float,
        val name: String,
        val discovered: Boolean = false,
        val open: Boolean = false
    ) {
        val isProbe: Boolean get() = nativeFlags and 2 != 0
        val canFastTravel: Boolean get() = nativeFlags and 1 != 0 && formationId > 0
        val visible: Boolean get() = discoveryFlag == 0 || discovered || (isProbe && open)
    }

    private data class MapData(
        val zone: Long,
        val left: Float,
        val top: Float,
        val width: Float,
        val height: Float,
        val scale: Float,
        val worldSize: Float,
        val maxScale: Int,
        val midScale: Int,
        val minScale: Int,
        val startX: Float,
        val startY: Float,
        val moveTop: Int,
        val moveLeft: Int,
        val moveBottom: Int,
        val moveRight: Int,
        val regionName: String,
        val markers: List<MapMarker>,
        val visibleMarkers: List<MapMarker>
    )

    private data class Mission(
        val id: Int,
        val statusFlag: Int,
        val category: Int,
        val title: String,
        val summary: String
    )

    private val buffer = ByteBuffer.allocateDirect(SNAPSHOT_SIZE).order(ByteOrder.LITTLE_ENDIAN)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 2f
    }
    private val regular = Typeface.create("sans-serif-condensed", Typeface.NORMAL)
    private val bold = Typeface.create("sans-serif-condensed", Typeface.BOLD)
    private var poller: ScheduledExecutorService? = null
    @Volatile private var snapshot = Snapshot()
    private var stateHash = 0
    private val touchSlop = ViewConfiguration.get(context).scaledTouchSlop.toFloat()
    private var selectedPage = 0
    private var pageGroup = 0
    private var pressedControl = CONTROL_NONE
    private var touchDownX = 0f
    private var touchDownY = 0f
    private var lastTouchDesignX = 0f
    private var lastTouchDesignY = 0f
    private var mapDragging = false
    private var mapWasDragged = false
    private var mapPinching = false
    private var mapPointerDistance = 0f
    private var mapFollowsPlayer = true
    private var mapPanX = 0f
    private var mapPanY = 0f
    private var mapZoom = 1f
    private var nativeMapRenderer: XcxMapRenderer? = null
    private var nativeMapTouch = false
    @Volatile private var viewedMapZone = 0L
    @Volatile private var mapData: MapData? = null
    private var mapLoadZone = 0L
    private var mapRetryAt = 0L
    private var mapFlagPollAt = 0L
    private var mapShowLocations = true
    private var mapShowProbes = true
    private var selectedMapMarkerId = 0
    private val mapMarkerHitRegions = ArrayList<Pair<RectF, MapMarker>>()
    @Volatile private var partyNames: Map<Int, String> = emptyMap()
    private var partyRetryAt = 0L
    @Volatile private var missions: List<Mission> = emptyList()
    @Volatile private var activeMissions: List<Mission> = emptyList()
    private var missionRetryAt = 0L
    private var missionFlagPollAt = 0L
    private var selectedMissionId = 0
    private var missionOpenedFromMap = false
    private val missionWindow = XcxListWindow(7)
    private var pressedRecordId = 0
    private var selectedSegment = 0
    private var selectedFrontierProbe = 0
    private val frontierWindow = XcxListWindow(4)
    private var selectedPartyMember = 0
    @Volatile private var equipmentCharacter = -1
    @Volatile private var equipmentSlot = -1
    @Volatile private var equipmentInventory: XcxEquipment.Inventory? = null
    private var equipmentSelectedSlot = 0
    private var equipmentSelectedKey = -1L
    private val equipmentWindow = XcxListWindow(6)
    @Volatile private var equipmentFrame: XcxEquipment.Frame? = null
    @Volatile private var equipmentCatalog: XcxEquipment.Catalog? = null
    private var equipmentPollAt = 0L
    private var equipmentCatalogRetryAt = 0L
    @Volatile private var actionMessage = ""
    @Volatile private var actionMessageUntil = 0L
    private var lastActionCompleted = 0L
    @Volatile private var pendingSwitchCharacter = -1
    @Volatile private var pendingSwitchUntil = 0L
    private var controllerActive = false
    private val companionHeldKeys = mutableSetOf<Pair<Int, Int>>()
    private var controllerMotionAt = 0L

    init {
        isFocusable = true
        isClickable = true
        contentDescription = "Xenoblade Chronicles X dual-screen companion"
        setBackgroundColor(Color.rgb(3, 9, 14))
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        nativeMapRenderer = XcxMapRenderer(
            { postInvalidateOnAnimation() }, { task -> post { task() } },
            { zone -> viewedMapZone = zone },
            { formation -> requestNativeAction(ACTION_FAST_TRAVEL, formation) },
            { argument -> requestNativeAction(ACTION_INSTALL_PROBE, argument) },
            { quest ->
                if (missions.any { it.id == quest }) {
                    selectedMissionId = quest
                    selectPage(2)
                    missionOpenedFromMap = true
                    postInvalidateOnAnimation()
                } else showActionMessage("MISSION DETAILS ARE NOT AVAILABLE YET")
            })
        XcxControllerBridge.attach(this)
        if (poller != null) return
        poller = Executors.newSingleThreadScheduledExecutor { runnable ->
            Thread(
                {
                    Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND)
                    runnable.run()
                },
                "XCX-Companion-Telemetry"
            ).apply { isDaemon = true }
        }.also { executor ->
            executor.scheduleWithFixedDelay(
                {
                    runCatching { pollSnapshot() }
                        .onFailure {
                            Log.error("[XCX Companion] Snapshot poll failed: ${it.message}")
                        }
                },
                0,
                POLL_INTERVAL_MS,
                TimeUnit.MILLISECONDS
            )
        }
        Log.info("[XCX Companion] Verification surface attached")
    }

    override fun onDetachedFromWindow() {
        nativeMapRenderer?.close()
        nativeMapRenderer = null
        XcxControllerBridge.detach(this)
        poller?.shutdownNow()
        poller = null
        // RenderThread can still hold the last display list. Let Bitmap's lifetime follow
        // its references rather than recycling pixels while a queued frame draws them.
        mapData = null
        Log.info("[XCX Companion] Verification surface detached")
        super.onDetachedFromWindow()
    }

    private fun pollSnapshot() {
        buffer.clear()
        val received = NativeLibrary.fillXcxCompanionSnapshot(buffer)
        if (received != SNAPSHOT_SIZE || buffer.getInt(0) != SNAPSHOT_MAGIC ||
            buffer.getShort(4).toInt() != SNAPSHOT_VERSION ||
            buffer.getShort(6).toInt() != SNAPSHOT_SIZE
        ) {
            // A stale packaged JNI library used to leave the UI waiting forever, even
            // while the game was running. Never parse or retain a mismatched frame.
            if (snapshot.status != -1) {
                Log.error("[XCX Companion] Incompatible snapshot: returned=$received " +
                    "expected=$SNAPSHOT_SIZE/v$SNAPSHOT_VERSION")
                snapshot = Snapshot(status = -1)
                stateHash = 0
                postInvalidate()
            }
            return
        }

        var nextHash = 17
        for (offset in 12 until 200) nextHash = 31 * nextHash + buffer.get(offset)
        for (offset in 208 until SNAPSHOT_SIZE) nextHash = 31 * nextHash + buffer.get(offset)
        val changed = nextHash != stateHash
        if (changed) {
            stateHash = nextHash
            snapshot = Snapshot(
                status = buffer.getInt(12),
                capabilities = buffer.getInt(16),
                checkpoint = buffer.getInt(20),
                zone = buffer.u32(24),
                miranium = buffer.u32(28),
                lastQuest = buffer.getInt(32),
                segmentMask = buffer.getInt(36),
                segmentScores = LongArray(6) { buffer.u32(40 + it * 4) },
                playerTransformValid = buffer.getInt(64) and 1 != 0,
                playerHeadingValid = buffer.getInt(64) and 2 != 0,
                playerX = buffer.getFloat(68),
                playerY = buffer.getFloat(72),
                playerZ = buffer.getFloat(76),
                playerRotationX = buffer.getFloat(80),
                playerRotationY = buffer.getFloat(84),
                playerRotationZ = buffer.getFloat(88),
                playerRotationW = buffer.getFloat(92),
                partyValidMask = buffer.getInt(96),
                partyHandles = LongArray(4) { buffer.u32(100 + it * 4) },
                partyCharacterIds = IntArray(4) {
                    buffer.getShort(116 + it * 2).toInt() and 0xFFFF
                },
                partyActorSlots = IntArray(4) {
                    buffer.getShort(124 + it * 2).toInt() and 0xFFFF
                },
                partyRecordStates = IntArray(4) { buffer.get(132 + it).toInt() and 0xFF },
                partyRecordFlags = LongArray(4) { buffer.u32(136 + it * 4) },
                activePartySlot = buffer.getInt(152),
                playerCharacterId = buffer.getShort(156).toInt() and 0xFFFF,
                playerActorType = buffer.getShort(158).toInt() and 0xFFFF,
                battleEnemyCount = buffer.u32(160),
                pauseFlags = buffer.u32(164),
                actionState = buffer.getInt(168),
                actionType = buffer.getInt(172),
                actionArgument = buffer.getInt(176),
                actionResult = buffer.getInt(180),
                actionRequest = buffer.getLong(184),
                actionCompleted = buffer.getLong(192),
                sampleTimeUs = buffer.getLong(200),
                partyVitalsMask = buffer.getInt(208),
                partyHp = LongArray(4) { buffer.u32(212 + it * 4) },
                partyMaximumHp = LongArray(4) { buffer.u32(228 + it * 4) },
                credits = buffer.u32(244),
                skellFuelMask = buffer.getInt(248),
                partyLevelMask = buffer.getInt(284),
                partyLevels = IntArray(4) { buffer.getInt(288 + it * 4) },
                trackedMission = buffer.getInt(304),
                skellFuel = FloatArray(4) { buffer.getFloat(252 + it * 4) },
                skellMaximumFuel = FloatArray(4) { buffer.getFloat(268 + it * 4) }
            )
        }
        val current = snapshot
        if (current.status == ACTIVE_STATUS) {
            ensureMapData(if (selectedPage == 1) FRONTIER_ZONES[selectedSegment]
                else viewedMapZone.takeIf { it != 0L } ?: current.zone)
            ensureMissionData()
            ensurePartyData()
            refreshEquipment()
            refreshMapMarkerFlags()
            refreshMissionFlags()
            consumeActionCompletion(current)
            val expectedCharacter = pendingSwitchCharacter
            if (expectedCharacter >= 0 && current.playerCharacterId == expectedCharacter &&
                current.activePartySlot in 0..3 &&
                current.partyValidMask and (1 shl current.activePartySlot) != 0) {
                pendingSwitchCharacter = -1
                pendingSwitchUntil = 0L
                showActionMessage("NOW CONTROLLING ${characterName(expectedCharacter)}")
            } else if (expectedCharacter >= 0 &&
                SystemClock.uptimeMillis() > pendingSwitchUntil
            ) {
                pendingSwitchCharacter = -1
                pendingSwitchUntil = 0L
                showActionMessage("CHARACTER SWITCH TRANSITION DID NOT COMPLETE")
            }
        }
        if (changed) postInvalidateOnAnimation()
    }

    private fun ensureMapData(zone: Long) {
        if (zone <= 0L || mapData?.zone == zone || mapLoadZone == zone) return
        val now = SystemClock.uptimeMillis()
        if (now < mapRetryAt) return
        mapLoadZone = zone
        try {
            val metadataBytes = NativeLibrary.getDualScreenRomfsAsset("xcx/maps/meta_${zone}.bin")
            val metadata = metadataBytes?.let { parseMapData(zone, it) }
            if (metadata == null) {
                mapRetryAt = now + ASSET_RETRY_MS
                Log.warning("[XCX-MAP] Native map data unavailable for zone $zone")
                return
            }
            mapData = metadata
            mapPanX = metadata.startX
            mapPanY = metadata.startY
            mapZoom = 1f
            mapFollowsPlayer = true
            selectedMapMarkerId = 0
            mapFlagPollAt = 0L
            mapRetryAt = 0L
            Log.info("[XCX-MAP] Loaded native map zone=$zone " +
                "markers=${metadata.markers.size}")
            postInvalidateOnAnimation()
        } finally {
            mapLoadZone = 0L
        }
    }

    private fun parseMapData(zone: Long, bytes: ByteArray): MapData? {
        if (bytes.size < 80) return null
        val data = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        if (data.int != 0x50414D58 || data.short.toInt() != 2 ||
            (data.short.toInt() and 0xFFFF) != 80
        ) return null
        if ((data.int.toLong() and 0xFFFFFFFFL) != zone) return null
        val left = data.float
        val top = data.float
        val mapWidth = data.float
        val mapHeight = data.float
        val mapScale = data.float
        val worldSize = data.float
        val maxScale = data.int
        val midScale = data.int
        val minScale = data.int
        val startX = data.float
        val startY = data.float
        val moveTop = data.int
        val moveLeft = data.int
        val moveBottom = data.int
        val moveRight = data.int
        val count = data.int
        val regionNameLength = data.int
        if (!listOf(left, top, mapWidth, mapHeight, mapScale, worldSize, startX, startY)
                .all { it.isFinite() } || mapWidth <= 0f || mapHeight <= 0f ||
            mapScale <= 0f || worldSize <= 0f || count < 0 || count > 4096 ||
            regionNameLength < 0 || regionNameLength > data.remaining()
        ) return null
        val regionName = String(ByteArray(regionNameLength).also(data::get), Charsets.UTF_8)
            .let(::cleanGameText)
        val markers = ArrayList<MapMarker>(count)
        repeat(count) {
            if (data.remaining() < 44) return null
            val infoId = data.int
            val discoveryFlag = data.short.toInt() and 0xFFFF
            val openFlag = data.short.toInt() and 0xFFFF
            val skipTravelId = data.int.toLong() and 0xFFFFFFFFL
            val formationId = data.int
            val probeId = data.short.toInt() and 0xFFFF
            val kind = data.get().toInt() and 0xFF
            val icon = data.get().toInt() and 0xFF
            val locationType = data.get().toInt() and 0xFF
            val nativeFlags = data.get().toInt() and 0xFF
            data.short
            val worldX = data.float
            val worldZ = data.float
            val offsetX = data.float
            val offsetY = data.float
            val nameLength = data.int
            if (nameLength < 0 || nameLength > data.remaining()) return null
            val name = String(ByteArray(nameLength).also(data::get), Charsets.UTF_8)
                .let(::cleanGameText)
            if (worldX.isFinite() && worldZ.isFinite() && offsetX.isFinite() && offsetY.isFinite()) {
                markers += MapMarker(infoId, discoveryFlag, openFlag, skipTravelId,
                    formationId, probeId, kind, icon, locationType, nativeFlags,
                    worldX, worldZ, offsetX, offsetY, name,
                    discovered = discoveryFlag == 0, open = false)
            }
        }
        if (data.hasRemaining()) return null
        return MapData(zone, left, top, mapWidth, mapHeight, mapScale, worldSize,
            maxScale, midScale, minScale, startX, startY, moveTop, moveLeft, moveBottom,
            moveRight, regionName, markers, markers.filter { it.visible })
    }

    private fun refreshMapMarkerFlags() {
        val current = mapData ?: return
        val now = SystemClock.uptimeMillis()
        if (now < mapFlagPollAt) return
        mapFlagPollAt = now + MAP_FLAG_POLL_MS
        val flags = current.markers.flatMap { marker ->
            listOf(marker.discoveryFlag, marker.openFlag)
        }.filter { it != 0 }.distinct()
        if (flags.isEmpty()) return
        val values = NativeLibrary.readXcxGameFlags(1, flags.toIntArray()) ?: return
        if (values.size != flags.size) return
        val liveFlags = HashMap<Int, Boolean>(flags.size)
        flags.forEachIndexed { index, flag ->
            liveFlags[flag] = (values[index].toInt() and 0xFF) != 0
        }
        val updated = current.markers.map { marker ->
            marker.copy(
                discovered = marker.discoveryFlag == 0 || liveFlags[marker.discoveryFlag] == true,
                open = marker.openFlag != 0 && liveFlags[marker.openFlag] == true
            )
        }
        val visible = updated.filter { it.visible }
        if (updated != current.markers || visible != current.visibleMarkers) {
            mapData = current.copy(markers = updated, visibleMarkers = visible)
            Log.info("[XCX-MAP] Unlocked markers=${visible.size}/${current.markers.size}")
            postInvalidateOnAnimation()
        }
    }

    private fun ensurePartyData() {
        if (partyNames.isNotEmpty()) return
        val now = SystemClock.uptimeMillis()
        if (now < partyRetryAt) return
        val parsed = NativeLibrary.getDualScreenRomfsAsset("xcx/party/data.bin")
            ?.let(::parsePartyNames).orEmpty()
        if (parsed.isEmpty()) {
            partyRetryAt = now + ASSET_RETRY_MS
            return
        }
        partyNames = parsed
        partyRetryAt = 0L
        postInvalidateOnAnimation()
    }

    private fun parsePartyNames(bytes: ByteArray): Map<Int, String> {
        if (bytes.size < 12) return emptyMap()
        val data = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        if (data.int != 0x54504358 || data.short.toInt() != 1 ||
            (data.short.toInt() and 0xFFFF) != 12
        ) return emptyMap()
        val count = data.int
        if (count < 0 || count > 64) return emptyMap()
        val result = LinkedHashMap<Int, String>(count)
        repeat(count) {
            if (data.remaining() < 4) return emptyMap()
            val id = data.short.toInt() and 0xFFFF
            val length = data.short.toInt() and 0xFFFF
            if (length > data.remaining()) return emptyMap()
            val name = String(ByteArray(length).also(data::get), Charsets.UTF_8)
                .let(::cleanGameText)
            if (name.isNotBlank()) result[id] = name
        }
        return if (data.hasRemaining()) emptyMap() else result
    }

    private fun ensureMissionData() {
        if (missions.isNotEmpty()) return
        val now = SystemClock.uptimeMillis()
        if (now < missionRetryAt) return
        val bytes = NativeLibrary.getDualScreenRomfsAsset("xcx/missions/data.bin")
        val parsed = bytes?.let(::parseMissions).orEmpty()
        if (parsed.isEmpty()) {
            missionRetryAt = now + ASSET_RETRY_MS
            return
        }
        missions = parsed
        missionRetryAt = 0L
        missionFlagPollAt = 0L
        Log.info("[XCX-MISSIONS] Loaded ${parsed.size} localized mission records")
        postInvalidateOnAnimation()
    }

    private fun parseMissions(bytes: ByteArray): List<Mission> {
        if (bytes.size < 12) return emptyList()
        val data = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        if (data.int != 0x54535158 || data.short.toInt() != 1 ||
            (data.short.toInt() and 0xFFFF) != 12
        ) return emptyList()
        val count = data.int
        if (count < 0 || count > 4096) return emptyList()
        val result = ArrayList<Mission>(count)
        repeat(count) {
            if (data.remaining() < 12) return emptyList()
            val id = data.int
            val statusFlag = data.short.toInt() and 0xFFFF
            val category = data.get().toInt() and 0xFF
            data.get()
            val titleLength = data.short.toInt() and 0xFFFF
            val summaryLength = data.short.toInt() and 0xFFFF
            if (titleLength + summaryLength > data.remaining()) return emptyList()
            val titleBytes = ByteArray(titleLength).also(data::get)
            val summaryBytes = ByteArray(summaryLength).also(data::get)
            val title = cleanGameText(String(titleBytes, Charsets.UTF_8))
            val summary = cleanGameText(String(summaryBytes, Charsets.UTF_8))
            if (title.isNotBlank()) result += Mission(id, statusFlag, category, title, summary)
        }
        return if (data.hasRemaining()) emptyList() else result
    }

    private fun refreshMissionFlags() {
        val definitions = missions
        if (definitions.isEmpty()) return
        val now = SystemClock.uptimeMillis()
        if (now < missionFlagPollAt) return
        missionFlagPollAt = now + MISSION_FLAG_POLL_MS
        val values = NativeLibrary.readXcxGameFlags(
            2, definitions.map { it.statusFlag }.toIntArray()
        ) ?: return
        if (values.size != definitions.size) return
        val next = definitions.filterIndexed { index, _ ->
            (values[index].toInt() and 0x03) == 1
        }
        if (next != activeMissions) {
            activeMissions = next
            Log.info("[XCX-MISSIONS] Active missions=${next.size}")
            postInvalidateOnAnimation()
        }
    }

    private fun cleanGameText(value: String): String = value
        .replace(Regex("\\[ST:[^]]*]"), "")
        .replace("\\r", "")
        .trim()

    private fun refreshEquipment() {
        val character = equipmentCharacter
        if (character !in 0..22) return
        val now = SystemClock.uptimeMillis()
        if (now < equipmentPollAt) return
        equipmentPollAt = now + 500L
        if (equipmentCatalog == null && now >= equipmentCatalogRetryAt) {
            equipmentCatalogRetryAt = now + ASSET_RETRY_MS
            equipmentCatalog = NativeLibrary.getDualScreenRomfsAsset("xcx/equipment/data.bin")
                ?.let(XcxEquipment::catalog)
        }
        val live = NativeLibrary.readXcxEquipment(character)?.let { XcxEquipment.frame(it, character) }
        if (equipmentCharacter == character && equipmentFrame != live) {
            equipmentFrame = live
            postInvalidateOnAnimation()
        }
        val slot = equipmentSlot
        if (slot in 0..11) {
            val category = XcxEquipment.categories[slot]
            val inventory = NativeLibrary.readXcxEquipmentInventory(category)
                ?.let { XcxEquipment.inventory(it, category) }
            if (equipmentCharacter == character && equipmentSlot == slot && equipmentInventory != inventory) {
                equipmentInventory = inventory
                postInvalidateOnAnimation()
            }
        }
    }

    private fun openEquipment() {
        val state = snapshot
        if (state.partyValidMask and (1 shl selectedPartyMember) == 0) return
        val character = state.partyCharacterIds[selectedPartyMember]
        if (character !in 0..22) return
        equipmentFrame = null
        equipmentSlot = -1
        equipmentInventory = null
        equipmentCharacter = character
    }

    private fun openEquipmentSlot(slot: Int) {
        if (slot !in 0..11 || equipmentFrame?.character != equipmentCharacter) return
        equipmentSelectedSlot = slot
        equipmentSelectedKey = -1L
        equipmentInventory = null
        equipmentWindow.reconcile(0, -1)
        equipmentSlot = slot
        invalidate()
    }

    private fun closeEquipment() {
        if (equipmentSlot >= 0) {
            equipmentSlot = -1
            equipmentInventory = null
        } else equipmentCharacter = -1
    }

    private fun equipmentItems() = equipmentInventory
        ?.takeIf { equipmentSlot in 0..11 && it.category == XcxEquipment.categories[equipmentSlot] }
        ?.items.orEmpty()

    private fun equipmentPage(direction: Int) {
        val items = equipmentItems()
        equipmentSelectedKey = items.getOrNull(equipmentWindow.movePage(items.size, direction))?.instance ?: -1L
        invalidate()
    }

    private fun applyEquipment(expectedKey: Long = equipmentSelectedKey) {
        val slot = equipmentSlot
        val character = equipmentCharacter
        if (slot !in 0..11 || character !in 0..22 || expectedKey != equipmentSelectedKey ||
            equipmentItems().none { it.instance == expectedKey }) return
        if (equipmentFrame?.items?.getOrNull(slot)?.instance == expectedKey) {
            showActionMessage("THIS ITEM IS ALREADY EQUIPPED")
            return
        }
        requestNativeAction(ACTION_EQUIP_ITEM, expectedKey.toInt(), character or (slot shl 8))
    }

    private fun characterName(characterId: Int): String =
        if (characterId == 0) "Avatar" else partyNames[characterId] ?: "Character $characterId"

    private fun consumeActionCompletion(state: Snapshot) {
        if (state.actionCompleted == 0L || state.actionCompleted == lastActionCompleted) return
        lastActionCompleted = state.actionCompleted
        if (state.actionType == 7) return // Background native condition batch has no user notification.
        actionMessage = when (state.actionResult) {
            1 -> when (state.actionType) {
                ACTION_SWITCH_CHARACTER -> {
                    pendingSwitchCharacter = state.actionArgument
                    pendingSwitchUntil = SystemClock.uptimeMillis() + 10_000L
                    if (state.playerCharacterId == state.actionArgument) {
                        pendingSwitchCharacter = -1
                        pendingSwitchUntil = 0L
                        "NOW CONTROLLING ${characterName(state.actionArgument)}"
                    } else {
                        "CHARACTER SWITCH TRANSITION STARTED"
                    }
                }
                ACTION_FAST_TRAVEL -> "FAST TRAVEL STARTED"
                ACTION_INSTALL_PROBE -> "PROBE UPDATED"
                ACTION_TRACK_MISSION -> if (state.actionArgument == 0)
                    "MISSION TRACKING CLEARED" else "MISSION NAVIGATION UPDATED"
                ACTION_SWAP_PARTY -> "PARTY ORDER TRANSITION STARTED"
                ACTION_EQUIP_ITEM -> "EQUIPMENT CHANGE STARTED"
                else -> "ACTION COMPLETED"
            }
            2 -> "NATIVE ACTION WAS NOT RECOGNIZED"
            3 -> if (state.actionType == ACTION_EQUIP_ITEM)
                "ITEM IS ALREADY EQUIPPED OR NO LONGER AVAILABLE" else "TARGET IS NO LONGER VALID"
            4 -> "UNAVAILABLE DURING A CUTSCENE OR EVENT"
            5 -> "UNAVAILABLE IN A MENU OR SCRIPTED PAUSE"
            6 -> "GAMEPLAY SYSTEM IS STILL LOADING"
            7 -> if (state.actionType == ACTION_INSTALL_PROBE)
                "PROBE UNAVAILABLE: CHECK INVENTORY, CREDITS, OR CURRENT INSTALLATION"
                else if (state.actionType == ACTION_EQUIP_ITEM)
                    "ITEM DOES NOT MEET THIS CHARACTER'S EQUIPMENT REQUIREMENTS"
                else "THE GAME REJECTED THAT TRANSITION"
            8 -> "UNAVAILABLE DURING COMBAT"
            9 -> "EXIT THE SKELL BEFORE CHANGING CHARACTERS OR EQUIPMENT"
            else -> "ACTION DID NOT COMPLETE"
        }
        if (state.actionResult != 1 && state.actionType == ACTION_SWITCH_CHARACTER) {
            pendingSwitchCharacter = -1
            pendingSwitchUntil = 0L
        }
        actionMessageUntil = SystemClock.uptimeMillis() + 4_000L
        postInvalidateOnAnimation()
    }

    private fun requestNativeAction(action: Int, argument: Int, context: Int = 0) {
        val state = snapshot
        if (state.capabilities and CAP_ACTION_BRIDGE == 0) {
            showActionMessage("NATIVE ACTION BRIDGE IS NOT READY")
            return
        }
        if (state.battleEnemyCount > 0) {
            showActionMessage("UNAVAILABLE DURING COMBAT")
            return
        }
        if (state.pauseFlags != 0L) {
            showActionMessage("UNAVAILABLE IN A MENU OR SCRIPTED PAUSE")
            return
        }
        if ((action == ACTION_SWITCH_CHARACTER || action == ACTION_SWAP_PARTY ||
                action == ACTION_EQUIP_ITEM) && state.playerActorType == 4) {
            showActionMessage("EXIT THE SKELL BEFORE CHANGING CHARACTERS OR EQUIPMENT")
            return
        }
        if (state.actionState == ACTION_STATE_QUEUED || state.actionState == ACTION_STATE_RUNNING ||
            state.actionRequest != 0L
        ) {
            showActionMessage("ANOTHER GAME ACTION IS IN PROGRESS")
            return
        }
        showActionMessage("SENDING REQUEST TO THE GAME...", 2_000L)
        poller?.execute {
            val accepted = if (action == ACTION_EQUIP_ITEM)
                NativeLibrary.equipXcxItem(context and 255, (context ushr 8) and 255,
                    argument.toLong() and 0xffffffffL)
                else NativeLibrary.performXcxCompanionAction(action, argument)
            if (!accepted) showActionMessage("ACTION IS NOT AVAILABLE IN THE CURRENT GAME STATE")
        }
    }

    private fun toggleMissionTracking(missionId: Int) {
        if (snapshot.capabilities and CAP_NAVIGATION_STATE == 0) {
            showActionMessage("MISSION NAVIGATION IS STILL LOADING")
            return
        }
        if (missionId <= 0 || (snapshot.trackedMission != missionId &&
            activeMissions.none { it.id == missionId })) {
            showActionMessage("THIS MISSION IS NO LONGER ACTIVE")
            return
        }
        requestNativeAction(ACTION_TRACK_MISSION,
            if (snapshot.trackedMission == missionId) 0 else missionId)
    }

    private fun partyOrderArgument(delta: Int): Int? = XcxPartyOrder.argument(
        selectedPartyMember, selectedPartyMember + delta,
        snapshot.partyValidMask, snapshot.partyCharacterIds)

    private fun movePartyMember(delta: Int, expected: Int? = partyOrderArgument(delta)) {
        val argument = partyOrderArgument(delta)
        if (argument == null || argument != expected) {
            showActionMessage("SELECT TWO OCCUPIED COMPANION SLOTS")
            return
        }
        requestNativeAction(ACTION_SWAP_PARTY, argument)
    }

    private fun showActionMessage(message: String, duration: Long = 4_000L) {
        actionMessage = message
        actionMessageUntil = SystemClock.uptimeMillis() + duration
        postInvalidateOnAnimation()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.drawColor(Color.rgb(3, 9, 14))
        val scale = min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT)
        val left = (width - DESIGN_WIDTH * scale) * .5f
        val top = (height - DESIGN_HEIGHT * scale) * .5f
        canvas.save()
        canvas.translate(left, top)
        canvas.scale(scale, scale)
        drawFrame(canvas, snapshot)
        canvas.restore()
    }

    private fun drawFrame(canvas: Canvas, state: Snapshot) {
        paint.style = Paint.Style.FILL
        paint.color = Color.rgb(7, 19, 28)
        canvas.drawRect(0f, 0f, DESIGN_WIDTH, DESIGN_HEIGHT, paint)

        if (state.status != ACTIVE_STATUS) {
            drawWaiting(canvas, state)
            drawPageRail(canvas, false)
            return
        }

        when (selectedPage) {
            0 -> drawMapPage(canvas, state)
            1 -> drawFrontierNavPage(canvas, state)
            2 -> drawMissionsPage(canvas, state)
            3 -> drawCombatPage(canvas, state)
            4 -> drawPartyPage(canvas, state)
            5 -> drawSkellPage(canvas, state)
        }
        if (actionMessage.isNotBlank() && SystemClock.uptimeMillis() < actionMessageUntil) {
            paint.color = Color.argb(242, 7, 26, 36)
            canvas.drawRect(330f, 876f, 910f, 926f, paint)
            stroke.color = Color.rgb(76, 220, 239)
            canvas.drawRect(330f, 876f, 910f, 926f, stroke)
            drawFittedText(canvas, actionMessage, 352f, 908f, 536f, 18f, 14f,
                Color.WHITE, true)
        }
        drawPageRail(canvas, true)
    }

    private fun drawPageTitle(canvas: Canvas, title: String, trailing: String = "") {
        text(canvas, title, 48f, 62f, 34f, Color.WHITE, true)
        if (trailing.isNotBlank()) {
            text(canvas, trailing, 1192f, 62f, 20f, Color.rgb(118, 169, 187), true,
                Paint.Align.RIGHT)
        }
        paint.color = Color.rgb(76, 220, 239)
        canvas.drawRect(48f, 82f, 1192f, 86f, paint)
    }

    private fun drawMapPage(canvas: Canvas, state: Snapshot) {
        drawPageTitle(canvas, "MAP")
        val yaw = atan2(2f * (state.playerRotationW * state.playerRotationY +
            state.playerRotationX * state.playerRotationZ),
            1f - 2f * (state.playerRotationY * state.playerRotationY +
                state.playerRotationZ * state.playerRotationZ))
        nativeMapRenderer?.apply {
            sitesZone = mapData?.zone ?: 0L
            update(state.zone, state.playerX, state.playerZ,
                if (state.playerHeadingValid) 180f - Math.toDegrees(yaw.toDouble()).toFloat()
                else Float.NaN, state.playerTransformValid &&
                    state.capabilities and CAP_PLAYER_TRANSFORM != 0,
                state.credits.takeIf { state.capabilities and CAP_CREDITS_STATE != 0 },
                state.actionState == ACTION_STATE_QUEUED || state.actionState == ACTION_STATE_RUNNING ||
                    state.actionRequest != 0L)
            sites = mapData?.visibleMarkers.orEmpty().filter {
                it.name.isNotBlank() && (it.kind == 1 || it.isProbe)
            }.map { XcxMapRenderer.Site(it.infoId, it.name, it.worldX, it.worldZ,
                if (it.canFastTravel && it.discovered) it.formationId else 0, it.isProbe) }
            draw(canvas)
        }
    }

    private fun drawFrontierNavPage(canvas: Canvas, state: Snapshot) {
        drawPageTitle(canvas, "FRONTIERNAV")
        drawMetric(canvas, 48f, 112f, 350f, "MIRANIUM",
            if (state.capabilities and CAP_GAME_STATE != 0) "%,d".format(state.miranium) else "--")
        panel(canvas, RectF(48f, 278f, 398f, 908f))
        panel(canvas, RectF(426f, 112f, 1192f, 908f))
        text(canvas, "CONTINENTS", 80f, 326f, 22f, Color.WHITE, true)
        text(canvas, "SEGMENT NETWORK", 458f, 166f, 22f, Color.WHITE, true)
        val hasSegments = state.capabilities and CAP_SEGMENT_STATE != 0
        val continentNames = arrayOf("NLA", "PRIMORDIA", "NOCTILUM", "OBLIVIA", "SYLVALUM",
            "CAULDROS")
        for (index in state.segmentScores.indices) {
            val row = segmentRect(index)
            paint.color = if (selectedSegment == index) Color.rgb(15, 71, 89)
                else Color.rgb(11, 39, 53)
            canvas.drawRect(row, paint)
            if (selectedSegment == index) {
                stroke.color = Color.rgb(76, 220, 239)
                canvas.drawRect(row, stroke)
            }
            text(canvas, continentNames[index], row.left + 18f, row.centerY() + 7f, 19f,
                Color.rgb(151, 183, 194), true)
        }
        val valid = hasSegments && (state.segmentMask and (1 shl selectedSegment)) != 0
        text(canvas, continentNames[selectedSegment], 458f, 242f, 31f, Color.WHITE, true)
        text(canvas, "SURVEY SCORE", 458f, 292f, 18f, Color.rgb(118, 169, 187), true)
        text(canvas, if (valid) "%,d".format(state.segmentScores[selectedSegment]) else "--",
            458f, 354f, 42f, Color.WHITE, true)
        paint.color = Color.rgb(13, 48, 64)
        canvas.drawRect(458f, 402f, 1160f, 406f, paint)
        text(canvas, "LIVE CONTINENT STATE", 458f, 462f, 18f,
            Color.rgb(118, 169, 187), true)
        text(canvas, if (valid) "SURVEY DATA AVAILABLE" else "NO SURVEY DATA",
            458f, 510f, 24f, if (valid) Color.rgb(76, 220, 239)
            else Color.rgb(151, 183, 194), true)
        val currentMap = mapData?.takeIf { it.zone == FRONTIER_ZONES[selectedSegment] }
        val probes = currentMap?.markers.orEmpty()
            .filter { it.isProbe }
        val visibleProbes = probes.filter { it.visible }
        val openProbes = visibleProbes.count { it.open }
        text(canvas, currentMap?.regionName?.takeIf { it.isNotBlank() }?.let { "$it SITES" }
            ?: "SELECTED REGION SITES", 458f, 580f, 18f,
            Color.rgb(118, 169, 187), true)
        text(canvas, if (currentMap == null) "WAITING FOR REGION DATA" else
            "$openProbes OPEN / ${visibleProbes.size} DISCOVERED / ${probes.size} TOTAL",
            458f, 624f, 22f, Color.WHITE, true)
        frontierWindow.reconcile(visibleProbes.size,
            visibleProbes.indexOfFirst { it.probeId == selectedFrontierProbe })
        visibleProbes.drop(frontierWindow.first).take(4).forEachIndexed { index, probe ->
            val y = 676f + index * 48f
            if (probe.probeId == selectedFrontierProbe) {
                paint.color = Color.rgb(15, 71, 89)
                canvas.drawRect(frontierSiteRect(index), paint)
            }
            paint.color = if (probe.open) Color.rgb(105, 232, 137) else Color.rgb(73, 158, 102)
            canvas.drawCircle(470f, y - 6f, 7f, paint)
            drawFittedText(canvas, probe.name.ifBlank { "FrontierNav Site ${probe.probeId}" },
                490f, y, 620f, 19f, 15f, Color.rgb(190, 211, 219), false)
        }
        if (frontierWindow.first > 0) drawCompactButton(canvas, frontierPreviousRect(),
            "PREVIOUS", false, pressedControl == CONTROL_FRONTIER_PREVIOUS)
        if (frontierWindow.first + 4 < visibleProbes.size) drawCompactButton(canvas, frontierNextRect(),
            "NEXT", false, pressedControl == CONTROL_FRONTIER_NEXT)
        drawCompactButton(canvas, frontierMapRect(), "VIEW MAP", false,
            pressedControl == CONTROL_FRONTIER_MAP)
    }

    private fun frontierProbes(): List<MapMarker> = mapData
        ?.takeIf { it.zone == FRONTIER_ZONES[selectedSegment] }?.markers.orEmpty()
        .filter { it.isProbe && it.visible }

    private fun selectContinent(index: Int) {
        selectedSegment = (index + 6) % 6
        selectedFrontierProbe = 0
        frontierWindow.reconcile(0, -1)
    }

    private fun frontierPage(direction: Int) {
        val probes = frontierProbes()
        val index = frontierWindow.movePage(probes.size, direction)
        selectedFrontierProbe = probes.getOrNull(index)?.probeId ?: 0
    }

    private fun openFrontierMap(probe: Int = 0) {
        val zone = FRONTIER_ZONES[selectedSegment]
        if (probe != 0 && frontierProbes().none { it.probeId == probe }) return
        viewedMapZone = zone
        selectPage(0)
        nativeMapRenderer?.showRegion(zone, probe)
    }

    private fun drawMissionsPage(canvas: Canvas, state: Snapshot) {
        drawPageTitle(canvas, "MISSIONS")
        panel(canvas, RectF(48f, 112f, 432f, 908f))
        panel(canvas, RectF(460f, 112f, 1192f, 908f))
        text(canvas, "ACTIVE MISSIONS", 80f, 166f, 22f, Color.WHITE, true)
        if ((state.capabilities and CAP_QUEST_STATE) == 0) {
            emptyState(canvas, "WAITING FOR MISSION STATE", 826f)
            return
        }
        val active = activeMissions
        if (missions.isEmpty()) {
            emptyState(canvas, "LOADING MISSION RECORDS", 826f)
            return
        }
        if (active.isEmpty()) {
            text(canvas, "NO ACTIVE MISSIONS", 240f, 500f, 22f,
                Color.rgb(151, 183, 194), true, Paint.Align.CENTER)
            if (missions.none { it.id == selectedMissionId }) {
                selectedMissionId = if (state.capabilities and CAP_NAVIGATION_STATE != 0)
                    state.trackedMission else 0
                text(canvas, "Select a mission marker on the map for details.", 826f, 500f, 22f,
                    Color.rgb(151, 183, 194), false, Paint.Align.CENTER)
                if (selectedMissionId != 0) drawCompactButton(canvas, missionTrackRect(),
                    "STOP TRACKING", true, pressedControl == CONTROL_MISSION_TRACK)
                return
            }
        }
        if (selectedMissionId == 0 || missions.none { it.id == selectedMissionId }) {
            selectedMissionId = active.first().id
        }
        missionWindow.reconcile(active.size, active.indexOfFirst { it.id == selectedMissionId })
        val visible = active.drop(missionWindow.first).take(7)
        visible.forEachIndexed { index, mission ->
            val row = missionRect(index)
            val selected = mission.id == selectedMissionId
            paint.color = if (selected) Color.rgb(15, 71, 89) else Color.rgb(11, 39, 53)
            canvas.drawRect(row, paint)
            if (selected) {
                stroke.color = Color.rgb(76, 220, 239)
                canvas.drawRect(row, stroke)
            }
            canvas.save()
            canvas.clipRect(row)
            drawFittedText(canvas, if (state.capabilities and CAP_NAVIGATION_STATE != 0 &&
                state.trackedMission == mission.id) "• ${mission.title}" else mission.title,
                row.left + 16f, row.centerY() + 7f,
                row.width() - 32f, 18f, 13f,
                if (selected) Color.WHITE else Color.rgb(151, 183, 194), true)
            canvas.restore()
        }
        if (active.isNotEmpty()) {
            drawCompactButton(canvas, missionPreviousRect(), "PREV", false,
                pressedControl == CONTROL_MISSIONS_PREVIOUS)
            drawCompactButton(canvas, missionNextRect(), "NEXT", false,
                pressedControl == CONTROL_MISSIONS_NEXT)
            text(canvas, "${missionWindow.first + 1}-${missionWindow.first + visible.size} / ${active.size}",
                240f, 898f, 16f, Color.rgb(151, 183, 194), false, Paint.Align.CENTER)
        }
        val selected = missions.firstOrNull { it.id == selectedMissionId } ?: return
        text(canvas, missionCategory(selected.category), 492f, 166f, 17f,
            Color.rgb(76, 220, 239), true)
        drawFittedText(canvas, selected.title, 492f, 224f, 660f, 34f, 23f,
            Color.WHITE, true)
        paint.color = Color.rgb(13, 48, 64)
        canvas.drawRect(492f, 250f, 1160f, 254f, paint)
        drawWrappedText(canvas, selected.summary, 492f, 304f, 650f, 25f, 8,
            Color.rgb(190, 211, 219))
        val navigationReady = state.capabilities and CAP_NAVIGATION_STATE != 0
        if (missionOpenedFromMap) drawCompactButton(canvas, missionMapRect(), "RETURN TO MAP",
            false, pressedControl == CONTROL_MISSION_MAP)
        val tracked = navigationReady && state.trackedMission == selected.id
        val selectedActive = active.any { it.id == selected.id }
        text(canvas, when {
            !navigationReady -> "WAITING FOR NAVIGATION STATE"
            tracked -> "TRACKED IN THE GAME'S NAVIGATION PANEL"
            !selectedActive -> "THIS MISSION IS NOT ACTIVE"
            state.trackedMission == 0 -> "NO MISSION TRACKED"
            else -> "ANOTHER MISSION IS TRACKED"
        }, 492f, 785f, 18f, Color.rgb(118, 169, 187), true)
        if (navigationReady && (selectedActive || tracked)) drawCompactButton(canvas, missionTrackRect(),
            if (tracked) "STOP TRACKING" else "TRACK MISSION", tracked,
            pressedControl == CONTROL_MISSION_TRACK)
    }

    private fun drawCombatPage(canvas: Canvas, state: Snapshot) {
        val engaged = (state.capabilities and CAP_COMBAT_STATE) != 0 &&
            state.battleEnemyCount > 0
        drawPageTitle(canvas, "COMBAT", if (engaged) "ENGAGED" else "EXPLORATION")
        drawMetric(canvas, 48f, 112f, 350f, "ENEMIES IN BATTLE",
            if ((state.capabilities and CAP_COMBAT_STATE) != 0)
                state.battleEnemyCount.toString() else "--")
        drawMetric(canvas, 48f, 278f, 350f, "CREDITS",
            if (state.capabilities and CAP_CREDITS_STATE != 0) "%,d".format(state.credits) else "--")
        panel(canvas, RectF(426f, 112f, 1192f, 908f))
        text(canvas, "ACTIVE PARTY", 458f, 166f, 22f, Color.WHITE, true)
        for (index in 0 until 4) {
            val valid = state.partyValidMask and (1 shl index) != 0
            val active = index == state.activePartySlot
            val rowTop = 202f + index * 142f
            paint.color = if (active) Color.rgb(15, 71, 89) else Color.rgb(11, 39, 53)
            canvas.drawRect(458f, rowTop, 1160f, rowTop + 112f, paint)
            text(canvas, if (active) "CONTROLLED - SLOT ${index + 1}" else "SLOT ${index + 1}",
                484f, rowTop + 42f, 18f,
                Color.rgb(118, 169, 187), true)
            text(canvas, if (valid) characterName(state.partyCharacterIds[index]) else "EMPTY",
                484f, rowTop + 82f, 25f, Color.WHITE, true)
            text(canvas, partyHealth(state, index), 1138f, rowTop + 82f, 21f,
                Color.rgb(105, 232, 137), false, Paint.Align.RIGHT)
        }
    }

    private fun drawPartyPage(canvas: Canvas, state: Snapshot) {
        if (equipmentCharacter >= 0) {
            drawEquipment(canvas)
            return
        }
        drawPageTitle(canvas, "PARTY")
        panel(canvas, RectF(48f, 112f, 720f, 908f))
        panel(canvas, RectF(748f, 112f, 1192f, 908f))
        text(canvas, "CURRENT PARTY", 80f, 166f, 22f, Color.WHITE, true)
        if (state.partyValidMask and (1 shl selectedPartyMember) != 0)
            drawCompactButton(canvas, equipmentRect(), "EQUIPMENT", false,
                pressedControl == CONTROL_EQUIPMENT)
        for (index in 0 until 4) {
            val rect = partyRect(index)
            val valid = state.partyValidMask and (1 shl index) != 0
            val active = index == state.activePartySlot
            paint.color = when {
                active -> Color.rgb(18, 82, 73)
                selectedPartyMember == index -> Color.rgb(15, 71, 89)
                else -> Color.rgb(11, 39, 53)
            }
            canvas.drawRect(rect, paint)
            if (selectedPartyMember == index) {
                stroke.color = Color.rgb(76, 220, 239)
                canvas.drawRect(rect, stroke)
            }
            text(canvas, if (active) "CONTROLLED - PARTY ${index + 1}" else "PARTY ${index + 1}",
                rect.left + 22f, rect.top + 43f, 19f,
                Color.rgb(118, 169, 187), true)
            text(canvas, if (valid) characterName(state.partyCharacterIds[index]) else "EMPTY",
                rect.left + 22f, rect.top + 87f, 28f, Color.WHITE, true)
        }
        val selectedValid = state.partyValidMask and (1 shl selectedPartyMember) != 0
        val selectedCharacter = state.partyCharacterIds.getOrElse(selectedPartyMember) { 0 }
        val selectedActive = selectedPartyMember == state.activePartySlot
        val nativeSlot = state.partyActorSlots.getOrElse(selectedPartyMember) { 0xFFFF }
        text(canvas, "MEMBER DETAILS", 780f, 166f, 22f, Color.WHITE, true)
        drawFittedText(canvas,
            if (selectedValid) characterName(selectedCharacter) else "EMPTY",
            780f, 238f, 380f, 34f, 22f, Color.WHITE, true)
        if (selectedValid) {
            text(canvas, "HEALTH", 780f, 298f, 18f, Color.rgb(118, 169, 187), true)
            text(canvas, partyHealth(state, selectedPartyMember), 780f, 338f, 23f,
                Color.rgb(151, 183, 194), true)
            text(canvas, "PARTY STATE", 780f, 394f, 18f, Color.rgb(118, 169, 187), true)
            text(canvas, if (selectedActive) "PLAYER CONTROLLED" else
                if (nativeSlot == 0xFFFF) "JOINING PARTY" else "PARTY MEMBER",
                780f, 434f, 21f, if (selectedActive) Color.rgb(105, 232, 137)
                else Color.rgb(151, 183, 194), true)
            text(canvas, "LEVEL", 780f, 478f, 18f, Color.rgb(118, 169, 187), true)
            text(canvas, if (state.partyLevelMask and (1 shl selectedPartyMember) != 0)
                state.partyLevels[selectedPartyMember].toString() else "--",
                780f, 514f, 26f, Color.WHITE, true)
            val switchStatus = when {
                selectedActive -> "ALREADY CONTROLLED"
                state.battleEnemyCount > 0 -> "BLOCKED DURING COMBAT"
                state.pauseFlags != 0L -> "BLOCKED BY CURRENT GAME STATE"
                state.playerActorType == 4 -> "EXIT SKELL BEFORE SWITCHING"
                state.capabilities and CAP_ACTION_BRIDGE == 0 -> "NATIVE BRIDGE LOADING"
                else -> "READY TO TAKE CONTROL"
            }
            text(canvas, switchStatus, 780f, 560f, 19f,
                if (!selectedActive && state.battleEnemyCount == 0L && state.pauseFlags == 0L &&
                    state.playerActorType != 4 && state.capabilities and CAP_ACTION_BRIDGE != 0)
                    Color.rgb(76, 220, 239) else Color.rgb(151, 183, 194), true)
            if (!selectedActive) drawCompactButton(canvas, partySwitchRect(), "TAKE CONTROL",
                false, pressedControl == CONTROL_PARTY_SWITCH)
            if (partyOrderArgument(-1) != null) drawCompactButton(canvas, partyOrderRect(-1),
                "X: MOVE UP", false, pressedControl == CONTROL_PARTY_UP)
            if (partyOrderArgument(1) != null) drawCompactButton(canvas, partyOrderRect(1),
                "Y: MOVE DOWN", false, pressedControl == CONTROL_PARTY_DOWN)
        }
    }

    private fun drawEquipment(canvas: Canvas) {
        drawPageTitle(canvas, "EQUIPMENT", characterName(equipmentCharacter))
        panel(canvas, RectF(48f, 112f, 1192f, 908f))
        text(canvas, if (equipmentSlot >= 0) "OWNED INVENTORY" else "CURRENT EQUIPMENT",
            80f, 164f, 22f, Color.WHITE, true)
        drawCompactButton(canvas, equipmentCloseRect(), "BACK", false,
            pressedControl == CONTROL_EQUIPMENT_CLOSE)
        if (equipmentSlot >= 0) {
            drawEquipmentInventory(canvas)
            return
        }
        val frame = equipmentFrame?.takeIf { it.character == equipmentCharacter }
        if (frame == null) {
            text(canvas, "LIVE EQUIPMENT DATA UNAVAILABLE", 80f, 246f, 23f,
                Color.rgb(151, 183, 194), false)
            return
        }
        val catalog = equipmentCatalog
        frame.items.forEach { item ->
            val x = 80f + (item.slot / 6) * 548f
            val y = 204f + (item.slot % 6) * 110f
            paint.color = if (item.slot == equipmentSelectedSlot) Color.rgb(22, 70, 88)
                else Color.rgb(11, 39, 53)
            canvas.drawRect(x, y, x + 520f, y + 98f, paint)
            val slot = catalog?.slots?.getOrNull(item.slot)?.takeIf { it.isNotBlank() }
                ?: XcxEquipment.slotLabels[item.slot]
            drawFittedText(canvas, cleanGameText(slot), x + 16f, y + 32f, 488f, 19f, 15f,
                Color.rgb(118, 169, 187), true)
            drawFittedText(canvas, cleanGameText(catalog?.name(item) ?:
                if (item.id == 0) "Empty" else "Item ${item.id}"),
                x + 16f, y + 72f, 488f, 25f, 16f, Color.WHITE, true)
        }
    }

    private fun drawEquipmentInventory(canvas: Canvas) {
        val slot = equipmentSlot
        val items = equipmentItems()
        val catalog = equipmentCatalog
        val label = catalog?.slots?.getOrNull(slot)?.takeIf { it.isNotBlank() }
            ?: XcxEquipment.slotLabels[slot]
        drawFittedText(canvas, cleanGameText(label), 80f, 216f, 1050f, 23f, 16f, Color.WHITE, true)
        if (equipmentInventory == null) {
            text(canvas, "LIVE INVENTORY DATA UNAVAILABLE", 80f, 310f, 23f,
                Color.rgb(151, 183, 194), false)
            return
        }
        equipmentWindow.reconcile(items.size, items.indexOfFirst { it.instance == equipmentSelectedKey })
        if (items.isEmpty()) text(canvas, "NO OWNED ITEMS FOR THIS SLOT", 80f, 310f, 23f,
            Color.rgb(151, 183, 194), false)
        repeat(6) { row ->
            val item = equipmentWindow.index(row, items.size)?.let(items::get) ?: return@repeat
            val bounds = equipmentItemRect(row)
            paint.color = if (item.instance == equipmentSelectedKey) Color.rgb(22, 70, 88)
                else Color.rgb(11, 39, 53)
            canvas.drawRect(bounds, paint)
            val name = catalog?.name(XcxEquipment.Item(slot, XcxEquipment.categories[slot],
                item.id, item.instance)) ?: "Item ${item.id}"
            drawFittedText(canvas, cleanGameText(name), bounds.left + 16f, bounds.top + 35f,
                bounds.width() - 32f, 25f, 16f, Color.WHITE, true)
            val current = equipmentFrame?.items?.getOrNull(slot)?.instance == item.instance
            text(canvas, if (current) "CURRENTLY EQUIPPED" else "OWNED ITEM",
                bounds.left + 16f, bounds.top + 62f, 16f, Color.rgb(118, 169, 187), false)
        }
        drawCompactButton(canvas, equipmentInventoryPreviousRect(), "PREVIOUS", false,
            pressedControl == CONTROL_EQUIPMENT_PREVIOUS)
        drawCompactButton(canvas, equipmentInventoryNextRect(), "NEXT", false,
            pressedControl == CONTROL_EQUIPMENT_NEXT)
        if (items.any { it.instance == equipmentSelectedKey })
            drawCompactButton(canvas, equipmentApplyRect(), "EQUIP", false,
                pressedControl == CONTROL_EQUIPMENT_APPLY)
        text(canvas, "${if (items.isEmpty()) 0 else equipmentWindow.first + 1}" +
            "–${min(items.size, equipmentWindow.first + 6)} / ${items.size}",
            620f, 882f, 18f, Color.rgb(118, 169, 187), false, Paint.Align.CENTER)
    }

    private fun drawSkellPage(canvas: Canvas, state: Snapshot) {
        drawPageTitle(canvas, "SKELL", if (state.playerActorType == 4) "PILOTING" else "ON FOOT")
        panel(canvas, RectF(48f, 112f, 1192f, 908f))
        text(canvas, "PARTY SKELLS", 80f, 166f, 22f, Color.WHITE, true)
        for (index in 0 until 4) {
            val top = 202f + index * 162f
            paint.color = Color.rgb(11, 39, 53)
            canvas.drawRect(80f, top, 1160f, top + 142f, paint)
            val present = state.partyValidMask and (1 shl index) != 0
            text(canvas, if (present) characterName(state.partyCharacterIds[index]) else "EMPTY SLOT",
                104f, top + 40f, 24f, Color.WHITE, true)
            if (state.skellFuelMask and (1 shl index) != 0) {
                val fuel = state.skellFuel[index]
                val maximum = state.skellMaximumFuel[index]
                text(canvas, "FUEL ${fuel.toInt()} / ${maximum.toInt()}", 1136f, top + 40f,
                    22f, Color.rgb(76, 220, 239), true, Paint.Align.RIGHT)
                paint.color = Color.rgb(22, 62, 75)
                canvas.drawRect(104f, top + 76f, 1136f, top + 108f, paint)
                paint.color = Color.rgb(76, 220, 239)
                canvas.drawRect(104f, top + 76f, 104f + 1032f * (fuel / maximum).coerceIn(0f, 1f),
                    top + 108f, paint)
            } else {
                text(canvas, if (present) "NO DEPLOYED SKELL DATA" else "--", 104f, top + 94f,
                    20f, Color.rgb(151, 183, 194), false)
            }
        }
    }

    private fun partyHealth(state: Snapshot, index: Int): String =
        if (index in 0..3 && state.partyVitalsMask and (1 shl index) != 0)
            "${state.partyHp[index]} / ${state.partyMaximumHp[index]} HP" else "HP UNAVAILABLE"

    private fun emptyState(canvas: Canvas, label: String, centerX: Float = 620f) {
        text(canvas, label, centerX, 510f, 25f, Color.rgb(151, 183, 194), true,
            Paint.Align.CENTER)
    }

    private fun drawWaiting(canvas: Canvas, state: Snapshot) {
        val title = when (state.status) {
            -1 -> "COMPANION UPDATE REQUIRED"
            0 -> "WAITING FOR EMULATION"
            1 -> "WAITING FOR XENOBLADE X"
            2 -> "UNSUPPORTED GAME VERSION"
            3 -> "READING LIVE GAME STATE"
            else -> "STATE BRIDGE UNAVAILABLE"
        }
        val detail = when (state.status) {
            -1 -> "The installed companion components do not match. A complete build is required."
            2 -> "This build is disabled safely. Supported: XCX DE 1.0.2 / update v131072."
            3 -> "The executable is verified; waiting for the world and save-state roots."
            else -> "The bottom display will connect automatically when the game is ready."
        }
        text(canvas, title, DESIGN_WIDTH * .5f, 475f, 38f, Color.WHITE, true, Paint.Align.CENTER)
        text(canvas, detail, DESIGN_WIDTH * .5f, 525f, 22f, Color.rgb(151, 183, 194), false,
            Paint.Align.CENTER)
        if (state.status != -1) text(canvas, "Checkpoint ${state.checkpoint}",
            DESIGN_WIDTH * .5f, 567f, 18f, Color.rgb(76, 220, 239), true, Paint.Align.CENTER)
    }

    private fun drawMetric(canvas: Canvas, x: Float, y: Float, width: Float, label: String,
                           value: String) {
        panel(canvas, RectF(x, y, x + width, y + 142f))
        text(canvas, label, x + 22f, y + 38f, 18f, Color.rgb(118, 169, 187), true)
        text(canvas, value, x + 22f, y + 102f, 32f, Color.WHITE, true)
    }

    private fun drawCompactButton(canvas: Canvas, rect: RectF, label: String, selected: Boolean,
                                  pressed: Boolean) {
        paint.color = when {
            pressed -> Color.rgb(22, 94, 111)
            selected -> Color.rgb(15, 71, 89)
            else -> Color.rgb(8, 31, 44)
        }
        canvas.drawRect(rect, paint)
        stroke.color = if (selected) Color.rgb(76, 220, 239) else Color.rgb(43, 120, 144)
        canvas.drawRect(rect, stroke)
        text(canvas, label, rect.centerX(), rect.centerY() + 7f,
            if (label.length == 1) 28f else 18f, Color.WHITE, true, Paint.Align.CENTER)
    }

    private fun mapCenterRect() = RectF(876f, 130f, 1018f, 184f)

    private fun mapZoomOutRect() = RectF(1030f, 130f, 1086f, 184f)

    private fun mapZoomInRect() = RectF(1098f, 130f, 1154f, 184f)

    private fun mapLocationsRect() = RectF(406f, 130f, 540f, 184f)

    private fun mapProbesRect() = RectF(552f, 130f, 700f, 184f)

    private fun mapFastTravelRect() = RectF(930f, 826f, 1154f, 882f)

    private fun missionPreviousRect() = RectF(80f, 818f, 228f, 868f)
    private fun missionNextRect() = RectF(252f, 818f, 400f, 868f)
    private fun missionTrackRect() = RectF(492f, 818f, 1160f, 878f)
    private fun missionMapRect() = RectF(492f, 708f, 1160f, 768f)
    private fun partySwitchRect() = RectF(780f, 602f, 1160f, 666f)
    private fun equipmentRect() = RectF(482f, 128f, 688f, 182f)
    private fun equipmentCloseRect() = RectF(1000f, 128f, 1160f, 182f)
    private fun equipmentSlotRect(slot: Int): RectF {
        val x = 80f + slot / 6 * 548f
        val y = 204f + slot % 6 * 110f
        return RectF(x, y, x + 520f, y + 98f)
    }
    private fun equipmentItemRect(row: Int) = RectF(80f, 238f + row * 86f, 1160f, 318f + row * 86f)
    private fun equipmentInventoryPreviousRect() = RectF(80f, 788f, 300f, 842f)
    private fun equipmentInventoryNextRect() = RectF(320f, 788f, 540f, 842f)
    private fun equipmentApplyRect() = RectF(920f, 788f, 1160f, 842f)
    private fun frontierSiteRect(row: Int) = RectF(458f, 648f + row * 48f, 1160f, 690f + row * 48f)
    private fun frontierPreviousRect() = RectF(458f, 842f, 656f, 896f)
    private fun frontierNextRect() = RectF(676f, 842f, 864f, 896f)
    private fun frontierMapRect() = RectF(884f, 842f, 1160f, 896f)
    private fun partyOrderRect(delta: Int) = if (delta < 0) RectF(780f, 690f, 1160f, 754f)
        else RectF(780f, 774f, 1160f, 838f)

    private fun mapContentRect() = RectF(48f, 112f, 1192f, 908f)

    private fun mapZoomRange(map: MapData): ClosedFloatingPointRange<Float> {
        val middle = map.midScale.coerceAtLeast(1).toFloat()
        val minimum = (map.minScale / middle).coerceIn(.25f, 1f)
        val maximum = (map.maxScale / middle).coerceIn(1f, 4f)
        return minimum..maximum
    }

    private fun adjustMapZoom(factor: Float) {
        nativeMapRenderer?.adjustZoom(factor)
        val map = mapData ?: return
        val range = mapZoomRange(map)
        mapZoom = (mapZoom * factor).coerceIn(range.start, range.endInclusive)
        clampMapPan(map, min(mapContentRect().width(),
            mapContentRect().height()) * mapZoom)
    }

    private fun clampMapPan(map: MapData, drawSize: Float) {
        val leftLimit = maxOf(40f, map.moveLeft.coerceAtLeast(0) * drawSize / 16f)
        val rightLimit = maxOf(40f, map.moveRight.coerceAtLeast(0) * drawSize / 16f)
        val topLimit = maxOf(40f, map.moveTop.coerceAtLeast(0) * drawSize / 16f)
        val bottomLimit = maxOf(40f, map.moveBottom.coerceAtLeast(0) * drawSize / 16f)
        mapPanX = mapPanX.coerceIn(-rightLimit, leftLimit)
        mapPanY = mapPanY.coerceIn(-bottomLimit, topLimit)
    }

    private fun mapPosition(map: MapData, destination: RectF, worldX: Float,
                            worldZ: Float): Pair<Float, Float>? {
        val mapX = worldX * map.scale / map.worldSize
        val mapY = worldZ * map.scale / map.worldSize
        val normalizedX = (mapX - map.left) / map.width
        val normalizedY = (mapY - map.top) / map.height
        if (!normalizedX.isFinite() || !normalizedY.isFinite() ||
            normalizedX < 0f || normalizedX > 1f || normalizedY < 0f || normalizedY > 1f
        ) return null
        return Pair(
            destination.left + normalizedX * destination.width(),
            destination.top + normalizedY * destination.height()
        )
    }

    private fun segmentRect(index: Int): RectF {
        val top = 350f + index * 84f
        return RectF(80f, top, 366f, top + 66f)
    }

    private fun partyRect(index: Int): RectF {
        val top = 202f + index * 154f
        return RectF(80f, top, 688f, top + 128f)
    }

    private fun missionRect(index: Int): RectF {
        val top = 202f + index * 84f
        return RectF(72f, top, 408f, top + 72f)
    }

    private fun drawPageRail(canvas: Canvas, enabled: Boolean) {
        val previous = previousGroupRect()
        val next = nextGroupRect()
        drawRailButton(canvas, previous, "<", enabled, pressedControl == CONTROL_PREVIOUS_GROUP)
        drawRailButton(canvas, next, ">", enabled, pressedControl == CONTROL_NEXT_GROUP)

        val firstPage = pageGroup * 3
        repeat(3) { slot ->
            val index = firstPage + slot
            val rect = pageRect(slot)
            val selected = selectedPage == index
            val pressed = pressedControl == index
            paint.color = when {
                !enabled -> Color.rgb(8, 22, 31)
                pressed -> Color.rgb(18, 80, 96)
                selected -> Color.rgb(14, 61, 78)
                else -> Color.rgb(10, 36, 51)
            }
            canvas.drawRect(rect, paint)
            stroke.color = when {
                !enabled -> Color.rgb(24, 53, 65)
                selected -> Color.rgb(76, 220, 239)
                else -> Color.rgb(43, 120, 144)
            }
            stroke.strokeWidth = if (selected) 3f else 2f
            canvas.drawRect(rect, stroke)
            stroke.strokeWidth = 2f
            text(canvas, PAGE_NAMES[index], rect.centerX(), rect.centerY() + 8f, 20f,
                if (enabled) Color.rgb(126, 177, 193) else Color.rgb(61, 83, 91), true,
                Paint.Align.CENTER)
        }
    }

    private fun drawRailButton(canvas: Canvas, rect: RectF, label: String, enabled: Boolean,
                               pressed: Boolean) {
        paint.color = when {
            !enabled -> Color.rgb(8, 22, 31)
            pressed -> Color.rgb(18, 80, 96)
            else -> Color.rgb(10, 36, 51)
        }
        canvas.drawRect(rect, paint)
        stroke.color = if (enabled) Color.rgb(43, 120, 144) else Color.rgb(24, 53, 65)
        canvas.drawRect(rect, stroke)
        text(canvas, label, rect.centerX(), rect.centerY() + 10f, 30f,
            if (enabled) Color.rgb(126, 177, 193) else Color.rgb(61, 83, 91), true,
            Paint.Align.CENTER)
    }

    private fun previousGroupRect() = RectF(48f, 940f, 130f, 1022f)

    private fun nextGroupRect() = RectF(1110f, 940f, 1192f, 1022f)

    private fun pageRect(slot: Int): RectF {
        val gap = 12f
        val left = 142f + slot * (310f + gap)
        return RectF(left, 940f, left + 310f, 1022f)
    }

    private fun controlAt(x: Float, y: Float): Int {
        if (previousGroupRect().contains(x, y)) return CONTROL_PREVIOUS_GROUP
        if (nextGroupRect().contains(x, y)) return CONTROL_NEXT_GROUP
        repeat(3) { slot ->
            if (pageRect(slot).contains(x, y)) return pageGroup * 3 + slot
        }
        when (selectedPage) {
            0 -> {
                if (mapCenterRect().contains(x, y)) return CONTROL_MAP_CENTER
                if (mapZoomOutRect().contains(x, y)) return CONTROL_MAP_ZOOM_OUT
                if (mapZoomInRect().contains(x, y)) return CONTROL_MAP_ZOOM_IN
                if (mapLocationsRect().contains(x, y)) return CONTROL_MAP_LOCATIONS
                if (mapProbesRect().contains(x, y)) return CONTROL_MAP_PROBES
                val selected = mapData?.visibleMarkers?.firstOrNull {
                    it.infoId == selectedMapMarkerId
                }
                if (selected?.canFastTravel == true && selected.discovered &&
                    mapFastTravelRect().contains(x, y)
                ) return CONTROL_MAP_FAST_TRAVEL
            }
            1 -> {
                repeat(6) { index ->
                    if (segmentRect(index).contains(x, y)) return CONTROL_SEGMENT_FIRST + index
                }
                val probes = frontierProbes()
                if (frontierWindow.first > 0 && frontierPreviousRect().contains(x, y)) return CONTROL_FRONTIER_PREVIOUS
                if (frontierWindow.first + 4 < probes.size && frontierNextRect().contains(x, y)) return CONTROL_FRONTIER_NEXT
                if (frontierMapRect().contains(x, y)) return CONTROL_FRONTIER_MAP
                repeat(4) { row ->
                    if (frontierWindow.index(row, probes.size) != null && frontierSiteRect(row).contains(x, y))
                        return CONTROL_FRONTIER_SITE + row
                }
            }
            2 -> {
                if (missionPreviousRect().contains(x, y)) return CONTROL_MISSIONS_PREVIOUS
                if (missionOpenedFromMap && missionMapRect().contains(x, y)) return CONTROL_MISSION_MAP
                if (snapshot.capabilities and CAP_NAVIGATION_STATE != 0 &&
                    (activeMissions.any { it.id == selectedMissionId } ||
                        selectedMissionId != 0 && snapshot.trackedMission == selectedMissionId) &&
                    missionTrackRect().contains(x, y)) return CONTROL_MISSION_TRACK
                if (missionNextRect().contains(x, y)) return CONTROL_MISSIONS_NEXT
                repeat(7) { index ->
                    if (missionWindow.index(index, activeMissions.size) != null &&
                        missionRect(index).contains(x, y)) return CONTROL_MISSION_FIRST + index
                }
            }
            4 -> {
                if (equipmentCharacter >= 0) {
                    if (equipmentCloseRect().contains(x, y)) return CONTROL_EQUIPMENT_CLOSE
                    if (equipmentSlot < 0) {
                        repeat(12) { if (equipmentSlotRect(it).contains(x, y)) return CONTROL_EQUIPMENT_SLOT + it }
                    } else {
                        val items = equipmentItems()
                        repeat(6) { row ->
                            if (equipmentWindow.index(row, items.size) != null && equipmentItemRect(row).contains(x, y))
                                return CONTROL_EQUIPMENT_ROW + row
                        }
                        if (equipmentInventoryPreviousRect().contains(x, y)) return CONTROL_EQUIPMENT_PREVIOUS
                        if (equipmentInventoryNextRect().contains(x, y)) return CONTROL_EQUIPMENT_NEXT
                        if (equipmentApplyRect().contains(x, y) && items.any { it.instance == equipmentSelectedKey })
                            return CONTROL_EQUIPMENT_APPLY
                    }
                    return CONTROL_NONE
                }
                if (equipmentRect().contains(x, y)) return CONTROL_EQUIPMENT
                if (partySwitchRect().contains(x, y)) return CONTROL_PARTY_SWITCH
                if (partyOrderArgument(-1) != null && partyOrderRect(-1).contains(x, y)) return CONTROL_PARTY_UP
                if (partyOrderArgument(1) != null && partyOrderRect(1).contains(x, y)) return CONTROL_PARTY_DOWN
                repeat(4) { index ->
                    if (partyRect(index).contains(x, y)) return CONTROL_PARTY_FIRST + index
                }
            }
        }
        return CONTROL_NONE
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val scale = min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT)
        if (scale <= 0f || !scale.isFinite()) return true
        val x = (event.x - (width - DESIGN_WIDTH * scale) * .5f) / scale
        val y = (event.y - (height - DESIGN_HEIGHT * scale) * .5f) / scale
        if (event.actionMasked == MotionEvent.ACTION_DOWN) {
            nativeMapTouch = selectedPage == 0 && snapshot.status == ACTIVE_STATUS &&
                mapContentRect().contains(x, y)
        }
        if (nativeMapTouch) {
            nativeMapRenderer?.touch(event, 1f / scale, 1f / scale,
                (width - DESIGN_WIDTH * scale) * .5f,
                (height - DESIGN_HEIGHT * scale) * .5f)
            if (event.actionMasked == MotionEvent.ACTION_UP ||
                event.actionMasked == MotionEvent.ACTION_CANCEL) nativeMapTouch = false
            return true
        }
        val hit = if (snapshot.status == ACTIVE_STATUS) controlAt(x, y) else CONTROL_NONE

        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                pressedRecordId = when (hit) {
                    CONTROL_MISSION_TRACK -> selectedMissionId
                    in CONTROL_MISSION_FIRST until CONTROL_MISSION_FIRST + 7 ->
                        missionWindow.index(hit - CONTROL_MISSION_FIRST, activeMissions.size)
                            ?.let { activeMissions.getOrNull(it)?.id } ?: 0
                    CONTROL_PARTY_SWITCH -> snapshot.partyCharacterIds[selectedPartyMember]
                    CONTROL_EQUIPMENT -> snapshot.partyCharacterIds[selectedPartyMember]
                    CONTROL_EQUIPMENT_APPLY -> equipmentSelectedKey.toInt()
                    in CONTROL_EQUIPMENT_ROW until CONTROL_EQUIPMENT_ROW + 6 ->
                        equipmentWindow.index(hit - CONTROL_EQUIPMENT_ROW, equipmentItems().size)
                            ?.let { equipmentItems().getOrNull(it)?.instance?.toInt() } ?: 0
                    in CONTROL_FRONTIER_SITE until CONTROL_FRONTIER_SITE + 4 ->
                        frontierWindow.index(hit - CONTROL_FRONTIER_SITE, frontierProbes().size)
                            ?.let { frontierProbes().getOrNull(it)?.probeId } ?: 0
                    CONTROL_PARTY_UP -> partyOrderArgument(-1) ?: -1
                    CONTROL_PARTY_DOWN -> partyOrderArgument(1) ?: -1
                    CONTROL_MAP_FAST_TRAVEL -> selectedMapMarkerId
                    else -> 0
                }
                touchDownX = event.x
                touchDownY = event.y
                lastTouchDesignX = x
                lastTouchDesignY = y
                mapDragging = selectedPage == 0 && mapContentRect().contains(x, y) &&
                    hit == CONTROL_NONE
                mapWasDragged = false
                mapPinching = false
                pressedControl = hit
                invalidate()
            }
            MotionEvent.ACTION_POINTER_DOWN -> {
                if (selectedPage == 0 && event.pointerCount >= 2) {
                    mapPinching = true
                    mapDragging = false
                    mapWasDragged = true
                    mapPointerDistance = pointerDistance(event)
                    pressedControl = CONTROL_NONE
                }
            }
            MotionEvent.ACTION_MOVE -> {
                if (mapPinching && event.pointerCount >= 2) {
                    val distance = pointerDistance(event)
                    if (mapPointerDistance > 0f && distance > 0f) {
                        adjustMapZoom(distance / mapPointerDistance)
                        mapPointerDistance = distance
                        mapFollowsPlayer = false
                        invalidate()
                    }
                    return true
                }
                if (mapDragging) {
                    if (abs(event.x - touchDownX) > touchSlop ||
                        abs(event.y - touchDownY) > touchSlop
                    ) mapWasDragged = true
                    val deltaX = x - lastTouchDesignX
                    val deltaY = y - lastTouchDesignY
                    if (mapWasDragged) {
                        mapPanX += deltaX
                        mapPanY += deltaY
                        mapFollowsPlayer = false
                    }
                    lastTouchDesignX = x
                    lastTouchDesignY = y
                    invalidate()
                    return true
                }
                if (abs(event.x - touchDownX) > touchSlop ||
                    abs(event.y - touchDownY) > touchSlop || pressedControl != hit
                ) {
                    pressedControl = CONTROL_NONE
                    invalidate()
                }
            }
            MotionEvent.ACTION_POINTER_UP -> {
                mapPinching = false
                mapDragging = false
                pressedControl = CONTROL_NONE
            }
            MotionEvent.ACTION_UP -> {
                if (mapDragging) {
                    mapDragging = false
                    if (!mapWasDragged) {
                        mapMarkerHitRegions.lastOrNull { it.first.contains(x, y) }?.second?.let {
                            selectedMapMarkerId = it.infoId
                        }
                    }
                    pressedControl = CONTROL_NONE
                    invalidate()
                    performClick()
                    return true
                }
                val selected = pressedControl
                pressedControl = CONTROL_NONE
                if (selected != CONTROL_NONE && selected == hit) {
                    when (selected) {
                        CONTROL_PREVIOUS_GROUP, CONTROL_NEXT_GROUP -> {
                            pageGroup = 1 - pageGroup
                            selectPage(pageGroup * 3)
                        }
                        in PAGE_NAMES.indices -> selectPage(selected)
                        CONTROL_MAP_CENTER -> {
                            mapPanX = 0f
                            mapPanY = 0f
                            mapZoom = 1f
                            mapFollowsPlayer = true
                        }
                        CONTROL_MAP_ZOOM_OUT -> adjustMapZoom(1f / 1.35f)
                        CONTROL_MAP_ZOOM_IN -> adjustMapZoom(1.35f)
                        CONTROL_MAP_LOCATIONS -> mapShowLocations = !mapShowLocations
                        CONTROL_MAP_PROBES -> mapShowProbes = !mapShowProbes
                        CONTROL_MAP_FAST_TRAVEL -> {
                            mapData?.visibleMarkers?.firstOrNull {
                                it.infoId == pressedRecordId && it.infoId == selectedMapMarkerId &&
                                    it.canFastTravel && it.discovered && mapData?.zone == snapshot.zone
                            }?.let { requestNativeAction(ACTION_FAST_TRAVEL, it.formationId) }
                        }
                        CONTROL_MISSIONS_PREVIOUS, CONTROL_MISSIONS_NEXT -> {
                            val active = activeMissions
                            val index = missionWindow.movePage(active.size,
                                if (selected == CONTROL_MISSIONS_PREVIOUS) -1 else 1)
                            selectedMissionId = active.getOrNull(index)?.id ?: 0
                        }
                        CONTROL_MISSION_TRACK -> if (selectedMissionId == pressedRecordId)
                            toggleMissionTracking(pressedRecordId)
                        CONTROL_MISSION_MAP -> selectPage(0)
                        in CONTROL_SEGMENT_FIRST until CONTROL_SEGMENT_FIRST + 6 ->
                            selectContinent(selected - CONTROL_SEGMENT_FIRST)
                        CONTROL_FRONTIER_PREVIOUS -> frontierPage(-1)
                        CONTROL_FRONTIER_NEXT -> frontierPage(1)
                        CONTROL_FRONTIER_MAP -> openFrontierMap()
                        in CONTROL_FRONTIER_SITE until CONTROL_FRONTIER_SITE + 4 ->
                            if (pressedRecordId != 0) openFrontierMap(pressedRecordId)
                        in CONTROL_MISSION_FIRST until CONTROL_MISSION_FIRST + 7 -> {
                            activeMissions.firstOrNull { it.id == pressedRecordId }?.let {
                                selectedMissionId = it.id
                            }
                        }
                        in CONTROL_PARTY_FIRST until CONTROL_PARTY_FIRST + 4 -> {
                            selectedPartyMember = selected - CONTROL_PARTY_FIRST
                        }
                        CONTROL_PARTY_SWITCH -> {
                            val characterId = snapshot.partyCharacterIds[selectedPartyMember]
                            if (characterId == pressedRecordId &&
                                snapshot.partyValidMask and (1 shl selectedPartyMember) != 0) {
                                if (selectedPartyMember == snapshot.activePartySlot) {
                                    showActionMessage("${characterName(characterId)} IS ALREADY CONTROLLED")
                                } else {
                                    requestNativeAction(ACTION_SWITCH_CHARACTER, characterId)
                                }
                            }
                        }
                        CONTROL_PARTY_UP -> movePartyMember(-1, pressedRecordId)
                        CONTROL_PARTY_DOWN -> movePartyMember(1, pressedRecordId)
                        CONTROL_EQUIPMENT -> if (pressedRecordId == snapshot.partyCharacterIds[selectedPartyMember])
                            openEquipment()
                        CONTROL_EQUIPMENT_CLOSE -> closeEquipment()
                        in CONTROL_EQUIPMENT_SLOT until CONTROL_EQUIPMENT_SLOT + 12 ->
                            openEquipmentSlot(selected - CONTROL_EQUIPMENT_SLOT)
                        in CONTROL_EQUIPMENT_ROW until CONTROL_EQUIPMENT_ROW + 6 -> {
                            val key = pressedRecordId.toLong() and 0xffffffffL
                            if (equipmentItems().any { it.instance == key }) equipmentSelectedKey = key
                        }
                        CONTROL_EQUIPMENT_PREVIOUS -> equipmentPage(-1)
                        CONTROL_EQUIPMENT_NEXT -> equipmentPage(1)
                        CONTROL_EQUIPMENT_APPLY -> applyEquipment(pressedRecordId.toLong() and 0xffffffffL)
                    }
                    if (selected in PAGE_NAMES.indices || selected == CONTROL_PREVIOUS_GROUP ||
                        selected == CONTROL_NEXT_GROUP
                    ) {
                        Log.info("[XCX Companion] Touch selected page ${PAGE_NAMES[selectedPage]}")
                    }
                    performClick()
                }
                invalidate()
            }
            MotionEvent.ACTION_CANCEL -> {
                mapDragging = false
                mapPinching = false
                pressedControl = CONTROL_NONE
                invalidate()
            }
        }
        // The Presentation owns the bottom display. Always consume its touches so they can never
        // leak into Eden's gameplay touch surface on the primary display.
        return true
    }

    private fun pointerDistance(event: MotionEvent): Float {
        if (event.pointerCount < 2) return 0f
        val dx = event.getX(0) - event.getX(1)
        val dy = event.getY(0) - event.getY(1)
        return sqrt(dx * dx + dy * dy)
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }

    override fun onXcxControllerKey(event: KeyEvent): Boolean {
        val key = event.deviceId to event.keyCode
        // Focus may be released by B while other companion keys are still held.
        // Keep their releases paired with the downs consumed here.
        if (event.action == KeyEvent.ACTION_UP && companionHeldKeys.remove(key)) return true
        if (event.keyCode == KeyEvent.KEYCODE_BUTTON_MODE) {
            if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
                companionHeldKeys.add(key)
                controllerActive = !controllerActive
                invalidate()
            }
            return true
        }
        if (!controllerActive) return key in companionHeldKeys
        if (event.action == KeyEvent.ACTION_DOWN) companionHeldKeys.add(key)
        if (snapshot.status == ACTIVE_STATUS && event.action == KeyEvent.ACTION_DOWN &&
            event.repeatCount == 0) handleControllerKey(event.keyCode)
        // Companion focus owns both halves of each key, including unavailable page actions.
        return true
    }

    override fun onXcxControllerMotion(event: MotionEvent): Boolean {
        if (!controllerActive ||
            event.action != MotionEvent.ACTION_MOVE ||
            event.source and InputDevice.SOURCE_JOYSTICK != InputDevice.SOURCE_JOYSTICK
        ) {
            return false
        }
        if (snapshot.status != ACTIVE_STATUS) return true
        if (selectedPage == 4 && equipmentCharacter >= 0) return true
        val horizontal = event.getAxisValue(MotionEvent.AXIS_HAT_X).takeIf { abs(it) > .55f }
            ?: event.getAxisValue(MotionEvent.AXIS_X).takeIf { abs(it) > .55f } ?: 0f
        val vertical = event.getAxisValue(MotionEvent.AXIS_HAT_Y).takeIf { abs(it) > .55f }
            ?: event.getAxisValue(MotionEvent.AXIS_Y).takeIf { abs(it) > .55f } ?: 0f
        if (horizontal == 0f && vertical == 0f) return true
        val now = SystemClock.uptimeMillis()
        if (now - controllerMotionAt < 170L) return true
        controllerMotionAt = now
        if (abs(horizontal) >= abs(vertical)) {
            handleDirectionalInput(if (horizontal < 0f) -1 else 1, 0)
        } else {
            handleDirectionalInput(0, if (vertical < 0f) -1 else 1)
        }
        invalidate()
        return true
    }

    private fun handleControllerKey(keyCode: Int): Boolean {
        if (selectedPage == 4 && equipmentCharacter >= 0) {
            when (keyCode) {
                KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BACK -> closeEquipment()
                KeyEvent.KEYCODE_BUTTON_A -> if (equipmentSlot < 0) openEquipmentSlot(equipmentSelectedSlot)
                    else applyEquipment()
                KeyEvent.KEYCODE_BUTTON_L2 -> if (equipmentSlot >= 0) equipmentPage(-1)
                KeyEvent.KEYCODE_BUTTON_R2 -> if (equipmentSlot >= 0) equipmentPage(1)
                KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN,
                KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT -> {
                    if (equipmentSlot < 0) {
                        val delta = when (keyCode) {
                            KeyEvent.KEYCODE_DPAD_UP -> -1
                            KeyEvent.KEYCODE_DPAD_DOWN -> 1
                            KeyEvent.KEYCODE_DPAD_LEFT -> -6
                            else -> 6
                        }
                        equipmentSelectedSlot = (equipmentSelectedSlot + delta).coerceIn(0, 11)
                    } else {
                        val items = equipmentItems()
                        val delta = if (keyCode == KeyEvent.KEYCODE_DPAD_UP || keyCode == KeyEvent.KEYCODE_DPAD_LEFT) -1 else 1
                        val next = (items.indexOfFirst { it.instance == equipmentSelectedKey } + delta)
                            .coerceIn(0, (items.size - 1).coerceAtLeast(0))
                        equipmentSelectedKey = items.getOrNull(next)?.instance ?: -1L
                        equipmentWindow.reconcile(items.size, next)
                    }
                }
                KeyEvent.KEYCODE_BUTTON_L1 -> selectPage(selectedPage - 1)
                KeyEvent.KEYCODE_BUTTON_R1 -> selectPage(selectedPage + 1)
            }
            invalidate()
            return true
        }
        when (keyCode) {
            KeyEvent.KEYCODE_BUTTON_R2 -> if (selectedPage == 4) openEquipment()
                else if (selectedPage == 1) frontierPage(1)
                else if (selectedPage == 0) adjustMapZoom(1.35f) else return false
            KeyEvent.KEYCODE_BUTTON_X -> if (selectedPage == 4) movePartyMember(-1) else return false
            KeyEvent.KEYCODE_BUTTON_Y -> if (selectedPage == 4) movePartyMember(1) else return false
            KeyEvent.KEYCODE_BUTTON_L1 -> selectPage(selectedPage - 1)
            KeyEvent.KEYCODE_BUTTON_R1 -> selectPage(selectedPage + 1)
            KeyEvent.KEYCODE_BUTTON_L2 -> if (selectedPage == 0) {
                adjustMapZoom(1f / 1.35f)
            } else if (selectedPage == 1) frontierPage(-1) else return false
            KeyEvent.KEYCODE_DPAD_LEFT -> handleDirectionalInput(-1, 0)
            KeyEvent.KEYCODE_DPAD_RIGHT -> handleDirectionalInput(1, 0)
            KeyEvent.KEYCODE_DPAD_UP -> handleDirectionalInput(0, -1)
            KeyEvent.KEYCODE_DPAD_DOWN -> handleDirectionalInput(0, 1)
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_ENTER,
            KeyEvent.KEYCODE_DPAD_CENTER -> when (selectedPage) {
                0 -> nativeMapRenderer?.activateCenter()
                1 -> openFrontierMap(selectedFrontierProbe)
                2 -> toggleMissionTracking(selectedMissionId)
                4 -> {
                    val characterId = snapshot.partyCharacterIds[selectedPartyMember]
                    if (snapshot.partyValidMask and (1 shl selectedPartyMember) == 0) return false
                    if (selectedPartyMember == snapshot.activePartySlot) {
                        showActionMessage("${characterName(characterId)} IS ALREADY CONTROLLED")
                    } else {
                        requestNativeAction(ACTION_SWITCH_CHARACTER, characterId)
                    }
                }
                else -> return false
            }
            KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BACK -> {
                if (selectedPage == 2 && missionOpenedFromMap) selectPage(0)
                else if (selectedPage != 0 || nativeMapRenderer?.back() != true) controllerActive = false
            }
            else -> return false
        }
        invalidate()
        return true
    }

    private fun selectPage(next: Int) {
        selectedPage = (next + PAGE_NAMES.size) % PAGE_NAMES.size
        if (selectedPage != 4) equipmentCharacter = -1
        if (selectedPage != 2) missionOpenedFromMap = false
        pageGroup = selectedPage / 3
        Log.info("[XCX Companion] Controller selected page ${PAGE_NAMES[selectedPage]}")
    }

    private fun handleDirectionalInput(horizontal: Int, vertical: Int) {
        when (selectedPage) {
            0 -> {
                nativeMapRenderer?.pan(horizontal * 90f, vertical * 90f)
                val step = 90f
                mapPanX -= horizontal * step
                mapPanY -= vertical * step
                mapFollowsPlayer = false
            }
            1 -> {
                if (horizontal != 0) selectContinent(selectedSegment + horizontal)
                else if (vertical != 0) {
                    val probes = frontierProbes()
                    if (probes.isNotEmpty()) {
                        val current = probes.indexOfFirst { it.probeId == selectedFrontierProbe }
                        val index = if (current < 0) if (vertical > 0) 0 else probes.lastIndex
                            else (current + vertical + probes.size) % probes.size
                        selectedFrontierProbe = probes[index].probeId
                    }
                }
            }
            2 -> {
                val active = activeMissions
                if (active.isNotEmpty()) {
                    val current = active.indexOfFirst { it.id == selectedMissionId }
                        .takeIf { it >= 0 } ?: 0
                    val delta = if (vertical != 0) vertical else horizontal
                    selectedMissionId = active[(current + delta + active.size) % active.size].id
                }
            }
            4 -> {
                val delta = if (vertical != 0) vertical else horizontal
                selectedPartyMember = (selectedPartyMember + delta + 4) % 4
            }
        }
    }

    private fun isControllerNavigationKey(keyCode: Int): Boolean = when (keyCode) {
        KeyEvent.KEYCODE_DPAD_LEFT,
        KeyEvent.KEYCODE_DPAD_RIGHT,
        KeyEvent.KEYCODE_DPAD_UP,
        KeyEvent.KEYCODE_DPAD_DOWN,
        KeyEvent.KEYCODE_BUTTON_A,
        KeyEvent.KEYCODE_BUTTON_B,
        KeyEvent.KEYCODE_BUTTON_L1,
        KeyEvent.KEYCODE_BUTTON_R1,
        KeyEvent.KEYCODE_BUTTON_L2,
        KeyEvent.KEYCODE_BUTTON_R2,
        KeyEvent.KEYCODE_ENTER,
        KeyEvent.KEYCODE_DPAD_CENTER,
        KeyEvent.KEYCODE_BACK -> true
        else -> false
    }

    private fun panel(canvas: Canvas, rect: RectF) {
        paint.style = Paint.Style.FILL
        paint.color = Color.rgb(8, 27, 39)
        canvas.drawRect(rect, paint)
        stroke.color = Color.rgb(32, 86, 105)
        canvas.drawRect(rect, stroke)
    }

    private fun text(canvas: Canvas, value: String, x: Float, y: Float, size: Float, color: Int,
                     strong: Boolean, align: Paint.Align = Paint.Align.LEFT) {
        paint.shader = null
        paint.style = Paint.Style.FILL
        paint.typeface = if (strong) bold else regular
        paint.textAlign = align
        paint.textSize = size
        paint.color = color
        canvas.drawText(value, x, y, paint)
    }

    private fun drawFittedText(canvas: Canvas, value: String, x: Float, y: Float,
                               maxWidth: Float, preferredSize: Float, minimumSize: Float,
                               color: Int, strong: Boolean) {
        paint.typeface = if (strong) bold else regular
        paint.textSize = preferredSize
        val size = if (paint.measureText(value) <= maxWidth) preferredSize else
            (preferredSize * maxWidth / paint.measureText(value)).coerceAtLeast(minimumSize)
        text(canvas, value, x, y, size, color, strong)
    }

    private fun drawWrappedText(canvas: Canvas, value: String, x: Float, y: Float,
                                maxWidth: Float, textSize: Float, maxLines: Int, color: Int) {
        paint.typeface = regular
        paint.textSize = textSize
        val lines = ArrayList<String>()
        value.lines().forEach { paragraph ->
            var line = ""
            paragraph.trim().split(Regex("\\s+")).filter { it.isNotEmpty() }.forEach { word ->
                val candidate = if (line.isEmpty()) word else "$line $word"
                if (paint.measureText(candidate) <= maxWidth || line.isEmpty()) {
                    line = candidate
                } else {
                    lines += line
                    line = word
                }
            }
            if (line.isNotEmpty()) lines += line
        }
        lines.take(maxLines).forEachIndexed { index, line ->
            val shown = if (index == maxLines - 1 && lines.size > maxLines) "$line..." else line
            text(canvas, shown, x, y + index * (textSize + 9f), textSize, color, false)
        }
    }

    private fun missionCategory(category: Int): String = when (category) {
        1 -> "STORY MISSION"
        2 -> "NORMAL MISSION"
        3 -> "BASIC MISSION"
        4 -> "AFFINITY MISSION"
        else -> "MISSION"
    }

    private fun ByteBuffer.u32(offset: Int): Long = getInt(offset).toLong() and 0xFFFFFFFFL
}
