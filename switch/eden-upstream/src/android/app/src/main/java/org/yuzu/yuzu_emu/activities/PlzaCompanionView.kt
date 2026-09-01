package org.yuzu.yuzu_emu.activities

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.os.Process
import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.charset.Charset
import java.util.LinkedHashMap
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.TimeUnit
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.utils.Log

internal class PlzaCompanionView(context: Context) : View(context), PlzaControllerTarget {
    private companion object {
        private const val DESIGN_WIDTH = 1240f
        private const val DESIGN_HEIGHT = 1080f
        private const val SNAPSHOT_SIZE = 20480
        private const val SNAPSHOT_VERSION = 4
        private const val INLINE_ITEM_CAPACITY = 20
        private const val ITEM_CAPACITY = 512
        private const val INLINE_QUEST_CAPACITY = 16
        private const val QUEST_CAPACITY = 24
        private const val INLINE_ITEM_OFFSET = 352
        private const val EXTENDED_ITEM_OFFSET = 4096
        private const val INLINE_QUEST_OFFSET = 992
        private const val EXTENDED_QUEST_OFFSET = 19840
        private const val SNAPSHOT_MAGIC = 0x415A4C50
        private const val POLL_INTERVAL_MS = 250L
        private const val STATUS_UNSUPPORTED = 2
        private const val STATUS_WAITING_GAME = 3
        private const val STATUS_WAITING_SAVE = 4
        private const val STATUS_ACTIVE = 5
        private const val AVAILABLE_PLAYER = 1 shl 0
        private const val AVAILABLE_PARTY = 1 shl 1
        private const val AVAILABLE_BAG = 1 shl 2
        private const val AVAILABLE_QUESTS = 1 shl 3
        private const val AVAILABLE_MAP = 1 shl 4
        private const val AVAILABLE_POKEDEX = 1 shl 5
        private const val AVAILABLE_REWARDS = 1 shl 6
        private const val ITEM_ACTION_SELECT_FIELD_BALL = 1
        private const val ITEM_ACTION_USE_ON_PARTY = 2
        private const val ITEM_ACTION_GIVE_TO_PARTY = 3
        private const val ITEM_ACTION_USE_KEY_ITEM = 4
        private const val ITEM_ACTION_TEACH_MOVE = 5
        private const val ITEM_ACTION_EQUIP_MEGA_STONE = 6
        private const val ITEM_ACTION_QUEUED = 0
        private val MAP_MARKER_COLORS = intArrayOf(
            Color.rgb(242, 74, 103),
            Color.rgb(24, 176, 182),
            Color.rgb(239, 166, 46),
            Color.rgb(108, 83, 190)
        )
        private val TABS = arrayOf("MAP", "TEAM", "BAG", "POKEDEX", "QUESTS", "REWARDS", "PROFILE")
        private val POCKETS = arrayOf(
            "ALL", "MEDICINE", "BALLS", "OTHER", "TREASURE", "KEY ITEMS", "BERRIES", "TMS",
            "MEGA STONES"
        )
        private val RESEARCH_REWARDS = listOf(
            ResearchReward(2, "TM004 Rock Smash"),
            ResearchReward(3, "TM046 Mud Shot"),
            ResearchReward(4, "TM017 Protect"),
            ResearchReward(5, "TM021 Thunder Fang"),
            ResearchReward(6, "TM014 Fire Fang"),
            ResearchReward(7, "TM015 Ice Fang"),
            ResearchReward(8, "TM070 Whirlpool"),
            ResearchReward(9, "TM061 Shadow Claw"),
            ResearchReward(10, "Exp. Candy S x10"),
            ResearchReward(11, "TM016 Light Screen"),
            ResearchReward(12, "TM042 Giga Drain"),
            ResearchReward(13, "TM024 Ice Punch"),
            ResearchReward(14, "TM003 Psyshock"),
            ResearchReward(15, "TM023 Thunder Punch"),
            ResearchReward(16, "TM032 Double Team"),
            ResearchReward(17, "TM029 Fire Punch"),
            ResearchReward(18, "TM078 Bulldoze"),
            ResearchReward(19, "TM082 U-turn"),
            ResearchReward(20, "TM059 Zen Headbutt"),
            ResearchReward(21, "TM030 Swords Dance"),
            ResearchReward(22, "TM084 Flash Cannon"),
            ResearchReward(23, "TM012 Rock Slide"),
            ResearchReward(24, "TM067 Thunderbolt"),
            ResearchReward(25, "Exp. Candy M x10"),
            ResearchReward(26, "TM006 Calm Mind"),
            ResearchReward(27, "TM077 Poison Jab"),
            ResearchReward(28, "TM068 Heat Wave"),
            ResearchReward(29, "TM073 Surf"),
            ResearchReward(30, "TM087 Iron Tail"),
            ResearchReward(31, "TM097 Heal Block"),
            ResearchReward(32, "TM053 Sludge Bomb"),
            ResearchReward(33, "TM106 Thunder"),
            ResearchReward(34, "TM069 Earthquake"),
            ResearchReward(35, "TM055 Giga Impact"),
            ResearchReward(36, "Bottle Cap x10"),
            ResearchReward(37, "TM107 Close Combat"),
            ResearchReward(38, "TM083 Nasty Plot"),
            ResearchReward(39, "TM064 Solar Beam"),
            ResearchReward(40, "TM096 Hydro Pump"),
            ResearchReward(41, "TM038 Fire Blast"),
            ResearchReward(42, "TM105 Blizzard"),
            ResearchReward(43, "Exp. Candy L x10"),
            ResearchReward(44, "TM102 Focus Blast"),
            ResearchReward(45, "TM050 Overheat"),
            ResearchReward(46, "TM093 Outrage"),
            ResearchReward(47, "Exp. Candy XL x10"),
            ResearchReward(48, "Gold Bottle Cap x3"),
            ResearchReward(49, "Master Ball"),
            ResearchReward(50, "Shiny Charm")
        )
    }

    private data class ResearchReward(val level: Int, val reward: String)

    private data class PartyEntry(
        val species: Int,
        val form: Int,
        val level: Int,
        val hp: Int,
        val maxHp: Int,
        val status: Int,
        val gender: Int,
        val flags: Int,
        val heldItem: Int,
        val name: String
    )

    private data class ItemEntry(
        val id: Int,
        val quantity: Int,
        val pocket: Int,
        val flags: Int,
        val name: String
    )

    private data class QuestEntry(
        val id: Int,
        val type: Int,
        val state: Int,
        val progress: Int,
        val total: Int,
        val title: String,
        val description: String
    )

    private data class QuestText(
        val title: String,
        val description: String
    )

    private data class PokedexInfo(
        val species: Int,
        val name: String,
        val category: String,
        val height: String,
        val weight: String,
        val description: String
    )

    private data class PokedexState(
        val species: Int,
        val form: Int,
        val flags: Int
    ) {
        val seen get() = flags and 1 != 0
        val caught get() = flags and 2 != 0
        val shiny get() = flags and 4 != 0
        val mega get() = flags and 8 != 0
        val alpha get() = flags and 16 != 0
    }

    private data class MarkerEntry(
        val id: Int,
        val type: Int,
        val state: Int,
        val icon: Int,
        val x: Float,
        val z: Float
    )

    private data class MapLocation(
        val kind: String,
        val x: Float,
        val z: Float,
        val name: String
    )

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val path = Path()
    private val regular = Typeface.create("sans-serif", Typeface.NORMAL)
    private val medium = Typeface.create("sans-serif", Typeface.BOLD)
    private val condensed = Typeface.create("sans-serif-condensed", Typeface.BOLD)
    private val frameLock = Any()
    private val snapshot = ByteBuffer.allocateDirect(SNAPSHOT_SIZE).order(ByteOrder.LITTLE_ENDIAN)
    private val itemNames = loadItemNames()
    private val questTexts = loadQuestTexts()
    private val questObjectives = loadQuestObjectives()
    private val questMaxPhases = loadQuestMaxPhases()
    private val pokedexInfo = loadPokedexInfo()
    private val nativeMapLocations = loadMapLocations()
    private val missingItemIcons = HashSet<Int>()
    private val itemIconCache = object : LinkedHashMap<Int, Bitmap>(32, .75f, true) {
        override fun removeEldestEntry(eldest: MutableMap.MutableEntry<Int, Bitmap>?): Boolean = size > 40
    }
    private val missingPokemonIcons = HashSet<String>()
    private val reportedMissingPokemonIcons = HashSet<String>()
    private val runtimePokemonRequests = ConcurrentHashMap.newKeySet<String>()
    private val runtimePokemonRetryAfter = ConcurrentHashMap<String, Long>()
    private val pokemonIconCache = object : LinkedHashMap<String, Bitmap>(18, .75f, true) {
        override fun removeEldestEntry(eldest: MutableMap.MutableEntry<String, Bitmap>?): Boolean = size > 18
    }
    private val cityMapBitmap = loadBitmap("plza/map_texture_00.png", 4)
    private val mapCenterIcon = loadBitmap("plza/map_icons/center.png")
    private val mapCafeIcon = loadBitmap("plza/map_icons/cafe.png")
    private val mapShopIcon = loadBitmap("plza/map_icons/shop.png")
    private val mapPlayerIcon = loadBitmap("plza/map_icons/player.png")
    private val mapPanelBounds = RectF(24f, 168f, 1216f, 1048f)
    private val mapViewportBounds = RectF(44f, 188f, 1196f, 1028f)
    private val mapBounds = RectF(209f, 198f, 1031f, 1020f)
    private val mapPlayerBounds = RectF()
    private var mapStaticBitmap: Bitmap? = null
    private val tabBounds = Array(TABS.size) { RectF() }
    private val leftArrowBounds = RectF()
    private val rightArrowBounds = RectF()
    private var poller: ScheduledExecutorService? = null
    private var assetExecutor: ExecutorService? = null
    private var backgroundShader: LinearGradient? = null
    private var status = 0
    private var availability = 0
    private var gameState = 0
    private var zoneId = 0
    private var money = 0
    private var playerX = 0f
    private var playerY = 0f
    private var playerZ = 0f
    private var playerHeading = 0f
    private var displayedPlayerHeading = 0f
    private var displayedHeadingInitialized = false
    private var displayedHeadingUpdatedAt = 0L
    private var pokedexSeen = 0
    private var pokedexCaught = 0
    private var pokedexMega = 0
    private var mableLevel = 0
    private var mableLevelViewed = 0
    private var researchPoints = 0
    private var ticketPoints = 0
    private var infiniteTicketPoints = 0
    private var royaleWins = 0
    private var prizeMedals = 0
    private var surveyPoints = 0
    private var selectedTab = 0
    private var pressedTab = -1
    private var pressedArrow = 0
    private var focusIndex = 0
    private var selectedPocket = 0
    private var selectedItem = 0
    private var bagScrollRow = 0
    private var bagDragging = false
    private var bagTouchMoved = false
    private var bagTouchStartY = 0f
    private var bagDragAccumulator = 0f
    private var pressedBagItem = -1
    private var lastItemActionId = -1
    private var lastItemActionTime = 0L
    private var itemActionMessage = ""
    private var itemActionMessageUntil = 0L
    private var pendingActionItem: ItemEntry? = null
    private var pendingActionOperation = 0
    private var pendingActionChoice = 0
    private var pendingPartyTarget = 0
    private var pendingActionChoices = intArrayOf()
    private val actionChoiceBounds = Array(2) { RectF() }
    private val partyTargetBounds = Array(6) { RectF() }
    private val itemActionCancelBounds = RectF()
    private var selectedQuest = 0
    private var questScroll = 0
    private var questDragging = false
    private var questTouchMoved = false
    private var questDragAccumulator = 0f
    private var questTouchStartY = 0f
    private var selectedPokedex = 0
    private var pokedexScroll = 0
    private var pokedexDragging = false
    private var pokedexTouchMoved = false
    private var pokedexDragAccumulator = 0f
    private var pokedexTouchStartY = 0f
    private var selectedReward = 0
    private var rewardScroll = 0
    private var rewardDragging = false
    private var rewardTouchMoved = false
    private var rewardDragAccumulator = 0f
    private var rewardTouchStartY = 0f
    private var rewardsInitialized = false
    private var lastContentHash = 0
    private var lastPartyHash = 0
    private var lastItemsHash = 0
    private var lastQuestHash = 0
    private var lastMarkerHash = 0
    private var lastPokedexHash = 0
    private var lastLoggedStatus = Int.MIN_VALUE
    private var lastSnapshotFailureLogAt = 0L
    private var lastMapRenderLogAt = 0L
    private var mapZoom = 1f
    private var mapPanX = 0f
    private var mapPanY = 0f
    private var lastTouchX = 0f
    private var lastTouchY = 0f
    private var mapDragging = false
    private var lastPinchDistance = 0f
    private var controllerActive = false
    private var controllerMotionAt = 0L
    private var party = emptyList<PartyEntry>()
    private var items = emptyList<ItemEntry>()
    private var quests = emptyList<QuestEntry>()
    private var markers = emptyList<MarkerEntry>()
    private var pokedexStates = emptyMap<Int, PokedexState>()

    init {
        isClickable = true
        isFocusable = true
        isFocusableInTouchMode = true
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        if (poller != null) return
        PlzaControllerBridge.attach(this)
        Log.info("[PLZA Companion] Bottom-screen view attached")
        assetExecutor = Executors.newSingleThreadExecutor { runnable ->
            Thread(
                {
                    Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND)
                    runnable.run()
                },
                "PLZA-Companion-Assets"
            ).apply {
                isDaemon = true
                priority = Thread.NORM_PRIORITY - 1
            }
        }
        poller = Executors.newSingleThreadScheduledExecutor { runnable ->
            Thread(
                {
                    Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND)
                    runnable.run()
                },
                "PLZA-Companion-Telemetry"
            ).apply {
                isDaemon = true
                priority = Thread.NORM_PRIORITY - 1
            }
        }.also { executor ->
            executor.scheduleWithFixedDelay(
                {
                    try {
                        pollSnapshot()
                    } catch (error: Throwable) {
                        Log.error("[PLZA Companion] Snapshot poll failed: ${error.message}")
                    }
                },
                0,
                POLL_INTERVAL_MS,
                TimeUnit.MILLISECONDS
            )
        }
    }

    override fun onDetachedFromWindow() {
        PlzaControllerBridge.detach(this)
        controllerActive = false
        poller?.shutdownNow()
        poller = null
        assetExecutor?.shutdownNow()
        assetExecutor = null
        runtimePokemonRequests.clear()
        runtimePokemonRetryAfter.clear()
        Log.info("[PLZA Companion] Bottom-screen view detached")
        super.onDetachedFromWindow()
    }

    override fun onSizeChanged(width: Int, height: Int, oldWidth: Int, oldHeight: Int) {
        super.onSizeChanged(width, height, oldWidth, oldHeight)
        backgroundShader = LinearGradient(
            0f,
            0f,
            width.toFloat(),
            height.toFloat(),
            intArrayOf(Color.rgb(245, 250, 252), Color.rgb(226, 241, 244), Color.rgb(240, 246, 248)),
            floatArrayOf(0f, .55f, 1f),
            Shader.TileMode.CLAMP
        )
    }

    private fun pollSnapshot() {
        snapshot.clear()
        val bytesWritten = NativeLibrary.fillPlzaCompanionSnapshot(snapshot)
        if (bytesWritten != SNAPSHOT_SIZE) {
            logSnapshotFailure("size=$bytesWritten expected=$SNAPSHOT_SIZE")
            return
        }
        val snapshotMagic = snapshot.getInt(0)
        val snapshotVersion = snapshot.getShort(4).toInt() and 0xFFFF
        if (snapshotMagic != SNAPSHOT_MAGIC || snapshotVersion != SNAPSHOT_VERSION) {
            logSnapshotFailure(
                "magic=0x${snapshotMagic.toUInt().toString(16)} version=$snapshotVersion " +
                    "expectedVersion=$SNAPSHOT_VERSION"
            )
            return
        }
        val partyCount = snapshot.getInt(32).coerceIn(0, 6)
        val itemCount = snapshot.getInt(36).coerceIn(0, ITEM_CAPACITY)
        val questCount = snapshot.getInt(40).coerceIn(0, QUEST_CAPACITY)
        val markerCount = snapshot.getInt(44).coerceIn(0, 16)
        val pokedexEntryCount = snapshot.getInt(60).coerceIn(0, 256)

        fun hashRange(seed: Int, offset: Int, length: Int): Int {
            var result = seed
            for (index in offset until offset + length) result = result * 31 + snapshot.get(index)
            return result
        }

        val partySectionHash = hashRange(partyCount, 64, partyCount * 48)
        var itemSectionHash = itemCount
        repeat(itemCount) { index ->
            val offset = if (index < INLINE_ITEM_CAPACITY) {
                INLINE_ITEM_OFFSET + index * 32
            } else {
                EXTENDED_ITEM_OFFSET + (index - INLINE_ITEM_CAPACITY) * 32
            }
            itemSectionHash = hashRange(itemSectionHash, offset, 8)
        }
        var questSectionHash = questCount
        repeat(questCount) { index ->
            val offset = if (index < INLINE_QUEST_CAPACITY) {
                INLINE_QUEST_OFFSET + index * 80
            } else {
                EXTENDED_QUEST_OFFSET + (index - INLINE_QUEST_CAPACITY) * 80
            }
            questSectionHash = hashRange(questSectionHash, offset, 80)
        }
        val markerSectionHash = hashRange(markerCount, 2272, markerCount * 16)
        val pokedexSectionHash = hashRange(pokedexEntryCount, 2528, pokedexEntryCount * 4)
        var contentHash = 17
        intArrayOf(
            snapshot.getInt(12), snapshot.getInt(16), snapshot.getInt(20),
            snapshot.getInt(24), snapshot.getInt(28), snapshot.getInt(48),
            snapshot.getInt(52), snapshot.getInt(56), snapshot.getInt(3552),
            snapshot.getInt(3556), snapshot.getInt(3560), snapshot.getInt(3564),
            snapshot.getInt(3568), snapshot.getInt(3572), snapshot.getInt(3576),
            snapshot.getInt(3580), snapshot.getInt(3584), snapshot.getInt(3588),
            snapshot.getInt(3592), snapshot.getInt(3596), partySectionHash,
            itemSectionHash, questSectionHash, markerSectionHash, pokedexSectionHash
        ).forEach { contentHash = contentHash * 31 + it }
        if (contentHash == lastContentHash) return

        val nextParty: List<PartyEntry> = if (partySectionHash == lastPartyHash) {
            synchronized(frameLock) { party }
        } else {
            List(partyCount) { index ->
                val offset = 64 + index * 48
                PartyEntry(
                    snapshot.getShort(offset).toInt() and 0xFFFF,
                    snapshot.getShort(offset + 2).toInt() and 0xFFFF,
                    snapshot.getShort(offset + 4).toInt() and 0xFFFF,
                    snapshot.getShort(offset + 6).toInt() and 0xFFFF,
                    snapshot.getShort(offset + 8).toInt() and 0xFFFF,
                    snapshot.getShort(offset + 10).toInt() and 0xFFFF,
                    snapshot.get(offset + 12).toInt() and 0xFF,
                    snapshot.get(offset + 13).toInt() and 0xFF,
                    snapshot.getShort(offset + 14).toInt() and 0xFFFF,
                    repairLegacyUtf8(readCString(offset + 20, 28))
                )
            }
        }
        val nextItems: List<ItemEntry> = if (itemSectionHash == lastItemsHash) {
            synchronized(frameLock) { items }
        } else {
            List(itemCount) { index ->
                val offset = if (index < INLINE_ITEM_CAPACITY) {
                    INLINE_ITEM_OFFSET + index * 32
                } else {
                    EXTENDED_ITEM_OFFSET + (index - INLINE_ITEM_CAPACITY) * 32
                }
                val itemId = snapshot.getInt(offset)
                ItemEntry(
                    itemId,
                    snapshot.getShort(offset + 4).toInt() and 0xFFFF,
                    snapshot.get(offset + 6).toInt() and 0xFF,
                    snapshot.get(offset + 7).toInt() and 0xFF,
                    itemNames[itemId] ?: repairLegacyUtf8(readCString(offset + 8, 24))
                )
            }
        }
        val nextQuests: List<QuestEntry> = if (questSectionHash == lastQuestHash) {
            synchronized(frameLock) { quests }
        } else {
            buildList {
                repeat(questCount) { index ->
                    val offset = if (index < INLINE_QUEST_CAPACITY) {
                        INLINE_QUEST_OFFSET + index * 80
                    } else {
                        EXTENDED_QUEST_OFFSET + (index - INLINE_QUEST_CAPACITY) * 80
                    }
                    val questId = snapshot.getInt(offset)
                    val questType = snapshot.get(offset + 4).toInt() and 0xFF
                    val state = snapshot.get(offset + 5).toInt() and 0xFF
                    val key = questKey(questType, questId)
                    val localized = questTexts[key] ?: return@repeat
                    val maximumActivePhase = questMaxPhases[key]
                    if (state == 0 || maximumActivePhase != null && state > maximumActivePhase) {
                        return@repeat
                    }
                    add(
                        QuestEntry(
                            questId,
                            questType,
                            state,
                            snapshot.get(offset + 6).toInt() and 0xFF,
                            snapshot.get(offset + 7).toInt() and 0xFF,
                            repairLegacyUtf8(localized.title),
                            repairLegacyUtf8(localized.description)
                        )
                    )
                }
            }.sortedWith(compareBy<QuestEntry> { it.type }.thenBy { it.id })
        }
        val nextMarkers: List<MarkerEntry> = if (markerSectionHash == lastMarkerHash) {
            synchronized(frameLock) { markers }
        } else {
            List(markerCount) { index ->
                val offset = 2272 + index * 16
                MarkerEntry(
                    snapshot.getInt(offset),
                    snapshot.get(offset + 4).toInt() and 0xFF,
                    snapshot.get(offset + 5).toInt() and 0xFF,
                    snapshot.getShort(offset + 6).toInt() and 0xFFFF,
                    snapshot.getFloat(offset + 8),
                    snapshot.getFloat(offset + 12)
                )
            }
        }
        val nextPokedex: Map<Int, PokedexState> = if (pokedexSectionHash == lastPokedexHash) {
            synchronized(frameLock) { pokedexStates }
        } else {
            buildMap {
                repeat(pokedexEntryCount) { index ->
                    val offset = 2528 + index * 4
                    val state = PokedexState(
                        snapshot.getShort(offset).toInt() and 0xFFFF,
                        snapshot.get(offset + 2).toInt() and 0xFF,
                        snapshot.get(offset + 3).toInt() and 0xFF
                    )
                    if (state.species > 0) put(state.species, state)
                }
            }
        }
        val nextStatus = snapshot.getInt(12)
        if (nextStatus != lastLoggedStatus) {
            lastLoggedStatus = nextStatus
            val statusName = when (nextStatus) {
                STATUS_UNSUPPORTED -> "unsupported-build"
                STATUS_WAITING_GAME -> "waiting-game"
                STATUS_WAITING_SAVE -> "runtime-ready"
                STATUS_ACTIVE -> "live-data-active"
                else -> "status-$nextStatus"
            }
            Log.info("[PLZA Companion] Snapshot status changed: $statusName")
        }
        synchronized(frameLock) {
            status = nextStatus
            availability = snapshot.getInt(16)
            gameState = snapshot.getInt(20)
            zoneId = snapshot.getInt(24)
            money = snapshot.getInt(28)
            playerX = snapshot.getFloat(48)
            playerY = snapshot.getFloat(52)
            playerZ = snapshot.getFloat(56)
            playerHeading = snapshot.getFloat(3596).takeIf { it.isFinite() } ?: 0f
            pokedexSeen = snapshot.getInt(3552).coerceAtLeast(0)
            pokedexCaught = snapshot.getInt(3556).coerceAtLeast(0)
            pokedexMega = snapshot.getInt(3560).coerceAtLeast(0)
            mableLevel = snapshot.getInt(3564).coerceAtLeast(0)
            mableLevelViewed = snapshot.getInt(3568).coerceAtLeast(0)
            researchPoints = snapshot.getInt(3572).coerceAtLeast(0)
            ticketPoints = snapshot.getInt(3576).coerceAtLeast(0)
            infiniteTicketPoints = snapshot.getInt(3580).coerceAtLeast(0)
            royaleWins = snapshot.getInt(3584).coerceAtLeast(0)
            prizeMedals = snapshot.getInt(3588).coerceAtLeast(0)
            surveyPoints = snapshot.getInt(3592).coerceAtLeast(0)
            party = nextParty
            lastPartyHash = partySectionHash
            items = nextItems
            lastItemsHash = itemSectionHash
            quests = nextQuests
            lastQuestHash = questSectionHash
            markers = nextMarkers
            lastMarkerHash = markerSectionHash
            pokedexStates = nextPokedex
            lastPokedexHash = pokedexSectionHash
            selectedItem = selectedItem.coerceIn(0, max(0, visibleItems(nextItems).lastIndex))
            keepItemSelectionVisible(visibleItems(nextItems).size)
            selectedQuest = selectedQuest.coerceIn(0, max(0, nextQuests.lastIndex))
            keepQuestSelectionVisible()
            selectedPokedex = selectedPokedex.coerceIn(0, max(0, pokedexInfo.lastIndex))
            keepPokedexSelectionVisible()
            if (!rewardsInitialized) {
                selectedReward = (mableLevel - 2).coerceIn(RESEARCH_REWARDS.indices)
                rewardsInitialized = true
            }
            selectedReward = selectedReward.coerceIn(RESEARCH_REWARDS.indices)
            keepRewardSelectionVisible()
            lastContentHash = contentHash
        }
        postInvalidate()
    }

    private fun logSnapshotFailure(reason: String) {
        val now = SystemClock.uptimeMillis()
        if (now - lastSnapshotFailureLogAt < 5_000L) return
        lastSnapshotFailureLogAt = now
        Log.warning("[PLZA-INIT] Snapshot rejected: $reason")
    }

    private fun readCString(offset: Int, length: Int): String {
        val bytes = ByteArray(length)
        var used = 0
        while (used < length) {
            val value = snapshot.get(offset + used)
            if (value.toInt() == 0) break
            bytes[used++] = value
        }
        return bytes.copyOf(used).toString(Charsets.UTF_8)
    }

    private fun loadItemNames(): Map<Int, String> {
        return try {
            context.assets.open("plza/item_names.tsv").bufferedReader(Charsets.UTF_8).useLines { lines ->
                lines.mapNotNull { line ->
                    val fields = line.split('\t', limit = 2)
                    val id = fields.firstOrNull()?.toIntOrNull()
                    if (id == null || fields.size != 2 || fields[1].isBlank()) null else
                        id to repairLegacyUtf8(fields[1])
                }.toMap()
            }
        } catch (error: Exception) {
            Log.error("[PLZA Companion] Unable to load item localization: ${error.message}")
            emptyMap()
        }
    }

    private fun repairLegacyUtf8(value: String): String {
        fun corruptionScore(text: String): Int = text.count {
            it == '\u00C3' || it == '\u00C2' || it == '\u00E2' || it == '\u00F0' ||
                it == '\uFFFD'
        }
        var repaired = value
        val windows1252 = Charset.forName("windows-1252")
        repeat(2) {
            if (corruptionScore(repaired) == 0) return repaired
            val candidate = runCatching {
                repaired.toByteArray(windows1252).toString(Charsets.UTF_8)
            }.getOrNull() ?: return@repeat
            if (corruptionScore(candidate) >= corruptionScore(repaired)) return@repeat
            repaired = candidate
        }
        return repaired
    }

    private fun loadQuestTexts(): Map<Int, QuestText> {
        return try {
            context.assets.open("plza/quests.tsv").bufferedReader(Charsets.UTF_8).useLines { lines ->
                lines.mapNotNull { line ->
                    val fields = line.split('\t', limit = 4)
                    if (fields.size != 4) return@mapNotNull null
                    val type = when (fields[0]) {
                        "main" -> 0
                        "side" -> 1
                        "research" -> 2
                        else -> return@mapNotNull null
                    }
                    val id = if (type == 0) {
                        val parts = fields[1].split('-', limit = 2)
                        if (parts.size != 2) return@mapNotNull null
                        (parts[0].toIntOrNull() ?: return@mapNotNull null) * 100 +
                            (parts[1].toIntOrNull() ?: return@mapNotNull null)
                    } else {
                        fields[1].toIntOrNull() ?: return@mapNotNull null
                    }
                    questKey(type, id) to QuestText(
                        repairLegacyUtf8(fields[2]),
                        repairLegacyUtf8(fields[3])
                    )
                }.toMap()
            }
        } catch (error: Exception) {
            Log.error("[PLZA Companion] Unable to load quest localization: ${error.message}")
            emptyMap()
        }
    }

    private fun loadQuestObjectives(): Map<Long, String> {
        return try {
            context.assets.open("plza/quest_objectives.tsv").bufferedReader(Charsets.UTF_8).useLines { lines ->
                lines.mapNotNull { line ->
                    val fields = line.split('\t', limit = 4)
                    if (fields.size != 4) return@mapNotNull null
                    val type = when (fields[0]) {
                        "main" -> 0
                        "side" -> 1
                        "research" -> 2
                        else -> return@mapNotNull null
                    }
                    val id = if (type == 0) {
                        val parts = fields[1].split('-', limit = 2)
                        if (parts.size != 2) return@mapNotNull null
                        (parts[0].toIntOrNull() ?: return@mapNotNull null) * 100 +
                            (parts[1].toIntOrNull() ?: return@mapNotNull null)
                    } else {
                        fields[1].toIntOrNull() ?: return@mapNotNull null
                    }
                    val phase = fields[2].toIntOrNull() ?: return@mapNotNull null
                    questObjectiveKey(type, id, phase) to repairLegacyUtf8(fields[3])
                }.toMap()
            }
        } catch (error: Exception) {
            Log.error("[PLZA Companion] Unable to load quest objectives: ${error.message}")
            emptyMap()
        }
    }

    private fun loadQuestMaxPhases(): Map<Int, Int> {
        return try {
            context.assets.open("plza/quest_objectives.tsv").bufferedReader(Charsets.UTF_8).useLines { lines ->
                val maximums = HashMap<Int, Int>()
                lines.forEach { line ->
                    val fields = line.split('\t', limit = 4)
                    if (fields.size != 4) return@forEach
                    val type = when (fields[0]) {
                        "main" -> 0
                        "side" -> 1
                        "research" -> 2
                        else -> return@forEach
                    }
                    val id = if (type == 0) {
                        val parts = fields[1].split('-', limit = 2)
                        if (parts.size != 2) return@forEach
                        (parts[0].toIntOrNull() ?: return@forEach) * 100 +
                            (parts[1].toIntOrNull() ?: return@forEach)
                    } else {
                        fields[1].toIntOrNull() ?: return@forEach
                    }
                    val phase = fields[2].toIntOrNull() ?: return@forEach
                    val key = questKey(type, id)
                    maximums[key] = max(maximums[key] ?: 0, phase)
                }
                maximums
            }
        } catch (error: Exception) {
            Log.error("[PLZA Companion] Unable to load quest phase limits: ${error.message}")
            emptyMap()
        }
    }

    private fun loadPokedexInfo(): List<PokedexInfo> {
        return try {
            context.assets.open("plza/pokedex.tsv").bufferedReader(Charsets.UTF_8).useLines { lines ->
                lines.mapNotNull { line ->
                    val fields = line.split('\t', limit = 6)
                    val species = fields.firstOrNull()?.toIntOrNull()
                    if (species == null || fields.size != 6) null else PokedexInfo(
                        species,
                        repairLegacyUtf8(fields[1]),
                        repairLegacyUtf8(fields[2]),
                        repairLegacyUtf8(fields[3]),
                        repairLegacyUtf8(fields[4]),
                        repairLegacyUtf8(fields[5])
                    )
                }.sortedBy { it.species }.toList()
            }
        } catch (error: Exception) {
            Log.error("[PLZA Companion] Unable to load Pokedex localization: ${error.message}")
            emptyList()
        }
    }

    private fun loadMapLocations(): List<MapLocation> {
        return try {
            context.assets.open("plza/map_locations.tsv").bufferedReader(Charsets.UTF_8).useLines { lines ->
                lines.mapNotNull { line ->
                    val fields = line.split('\t', limit = 4)
                    val x = fields.getOrNull(1)?.toFloatOrNull()
                    val z = fields.getOrNull(2)?.toFloatOrNull()
                    if (fields.size != 4 || x == null || z == null) null else
                        MapLocation(fields[0], x, z, repairLegacyUtf8(fields[3]))
                }.toList()
            }
        } catch (error: Exception) {
            Log.error("[PLZA Companion] Unable to load native map locations: ${error.message}")
            emptyList()
        }
    }

    private fun questKey(type: Int, id: Int): Int = (type shl 24) or (id and 0x00FFFFFF)

    private fun questObjectiveKey(type: Int, id: Int, phase: Int): Long =
        ((type.toLong() and 0xFF) shl 56) or
            ((id.toLong() and 0x00FFFFFF) shl 16) or
            (phase.toLong() and 0xFFFF)

    private fun questObjective(quest: QuestEntry): String {
        questObjectives[questObjectiveKey(quest.type, quest.id, quest.state)]?.let { return it }
        for (phase in quest.state downTo 0) {
            questObjectives[questObjectiveKey(quest.type, quest.id, phase)]?.let { return it }
        }
        return quest.description
    }

    private fun visibleItems(source: List<ItemEntry> = items): List<ItemEntry> =
        if (selectedPocket == 0) source else source.filter { it.pocket == selectedPocket }

    private fun keepPokedexSelectionVisible() {
        val maxStart = max(0, pokedexInfo.size - 7)
        if (selectedPokedex < pokedexScroll) pokedexScroll = selectedPokedex
        if (selectedPokedex >= pokedexScroll + 7) pokedexScroll = selectedPokedex - 6
        pokedexScroll = pokedexScroll.coerceIn(0, maxStart)
    }

    private fun scrollPokedexBy(rows: Int): Boolean {
        if (rows == 0 || pokedexInfo.isEmpty()) return false
        val maxStart = max(0, pokedexInfo.size - 7)
        val nextScroll = (pokedexScroll + rows).coerceIn(0, maxStart)
        if (nextScroll == pokedexScroll) return false
        pokedexScroll = nextScroll
        selectedPokedex = selectedPokedex.coerceIn(
            pokedexScroll,
            min(pokedexScroll + 6, pokedexInfo.lastIndex)
        )
        invalidate()
        return true
    }

    private fun keepRewardSelectionVisible() {
        val maxStart = max(0, RESEARCH_REWARDS.size - 7)
        if (selectedReward < rewardScroll) rewardScroll = selectedReward
        if (selectedReward >= rewardScroll + 7) rewardScroll = selectedReward - 6
        rewardScroll = rewardScroll.coerceIn(0, maxStart)
    }

    private fun scrollRewardsBy(rows: Int): Boolean {
        if (rows == 0) return false
        val maxStart = max(0, RESEARCH_REWARDS.size - 7)
        val nextScroll = (rewardScroll + rows).coerceIn(0, maxStart)
        if (nextScroll == rewardScroll) return false
        rewardScroll = nextScroll
        selectedReward = selectedReward.coerceIn(
            rewardScroll,
            min(rewardScroll + 6, RESEARCH_REWARDS.lastIndex)
        )
        invalidate()
        return true
    }

    private fun keepQuestSelectionVisible() {
        val questCount = synchronized(frameLock) { quests.size }
        val maxStart = max(0, questCount - 7)
        if (selectedQuest < questScroll) questScroll = selectedQuest
        if (selectedQuest >= questScroll + 7) questScroll = selectedQuest - 6
        questScroll = questScroll.coerceIn(0, maxStart)
    }

    private fun scrollQuestsBy(rows: Int): Boolean {
        if (rows == 0) return false
        val questCount = synchronized(frameLock) { quests.size }
        val maxStart = max(0, questCount - 7)
        val nextScroll = (questScroll + rows).coerceIn(0, maxStart)
        if (nextScroll == questScroll) return false
        questScroll = nextScroll
        selectedQuest = selectedQuest.coerceIn(
            questScroll,
            min(questScroll + 6, max(0, questCount - 1))
        )
        invalidate()
        return true
    }

    private fun keepItemSelectionVisible(itemCount: Int) {
        val selectedRow = selectedItem / 3
        if (selectedRow < bagScrollRow) bagScrollRow = selectedRow
        if (selectedRow >= bagScrollRow + 4) bagScrollRow = selectedRow - 3
        bagScrollRow = bagScrollRow.coerceIn(0, max(0, (itemCount - 1) / 3 - 3))
    }

    private fun scrollBagBy(rows: Int): Boolean {
        if (rows == 0) return false
        val itemCount = synchronized(frameLock) { visibleItems().size }
        val maxRow = max(0, (itemCount - 1) / 3 - 3)
        val nextRow = (bagScrollRow + rows).coerceIn(0, maxRow)
        if (nextRow == bagScrollRow) return false
        bagScrollRow = nextRow
        val first = bagScrollRow * 3
        selectedItem = selectedItem.coerceIn(first, min(max(0, itemCount - 1), first + 11))
        invalidate()
        return true
    }

    private fun cyclePocket(direction: Int) {
        selectedPocket = (selectedPocket + direction + POCKETS.size) % POCKETS.size
        selectedItem = 0
        bagScrollRow = 0
        invalidate()
    }

    private fun requestItemAction(item: ItemEntry): Boolean {
        Log.info(
            "[PLZA-ITEM-ACTION] requested action=activate item=${item.id} " +
                "name=${item.name} quantity=${item.quantity} pocket=${item.pocket} flags=${item.flags}"
        )
        return when (item.pocket) {
            1 -> openPartyTarget(item, ITEM_ACTION_USE_ON_PARTY)
            2 -> executeItemAction(item, ITEM_ACTION_SELECT_FIELD_BALL, -1)
            3 -> openPartyTarget(item, ITEM_ACTION_GIVE_TO_PARTY)
            4 -> showItemActionMessage("THIS ITEM HAS NO FIELD ACTION")
            5 -> executeItemAction(item, ITEM_ACTION_USE_KEY_ITEM, -1)
            6 -> {
                pendingActionItem = item
                pendingActionOperation = 0
                pendingActionChoice = 0
                pendingPartyTarget = 0
                pendingActionChoices = intArrayOf(ITEM_ACTION_USE_ON_PARTY, ITEM_ACTION_GIVE_TO_PARTY)
                invalidate()
                true
            }
            7 -> openPartyTarget(item, ITEM_ACTION_TEACH_MOVE)
            8 -> openPartyTarget(item, ITEM_ACTION_EQUIP_MEGA_STONE)
            else -> showItemActionMessage("UNKNOWN ITEM POCKET")
        }
    }

    private fun openPartyTarget(item: ItemEntry, operation: Int): Boolean {
        val partyCount = synchronized(frameLock) { party.size }
        if (partyCount == 0) return showItemActionMessage("PARTY DATA IS NOT READY")
        pendingActionItem = item
        pendingActionOperation = operation
        pendingActionChoice = 0
        pendingPartyTarget = pendingPartyTarget.coerceIn(0, partyCount - 1)
        pendingActionChoices = intArrayOf()
        invalidate()
        return true
    }

    private fun executeItemAction(item: ItemEntry, operation: Int, partyTarget: Int): Boolean {
        val now = SystemClock.uptimeMillis()
        if (item.id == lastItemActionId && now - lastItemActionTime < 600L) return false
        lastItemActionId = item.id
        lastItemActionTime = now
        Log.info(
            "[PLZA-ITEM-ACTION] dispatch operation=$operation item=${item.id} " +
                "quantity=${item.quantity} pocket=${item.pocket} partyTarget=$partyTarget"
        )
        val result = NativeLibrary.performPlzaCompanionItemAction(operation, item.id, partyTarget)
        val message = when (result) {
            ITEM_ACTION_QUEUED -> "ACTION QUEUED"
            1 -> "GAME SESSION IS NOT RUNNING"
            2 -> "WRONG GAME IS ACTIVE"
            3 -> "GAME BUILD IS NOT SUPPORTED"
            4 -> "INVALID ITEM ACTION"
            5 -> "BAG DATA IS NOT READY"
            6 -> "BAG DATA COULD NOT BE READ"
            7 -> "ITEM IS NO LONGER IN THE BAG"
            8 -> "SELECT A POKEMON FIRST"
            9 -> "INVALID PARTY TARGET"
            10 -> "THIS ITEM TYPE IS NOT SUPPORTED"
            11 -> "THE GAME BAG PRESENTER IS UNAVAILABLE"
            12 -> "THE GAME ITEM CONTEXT IS UNAVAILABLE"
            13 -> "NATIVE GAME-THREAD BRIDGE IS NOT READY"
            14 -> "AN ITEM ACTION IS ALREADY RUNNING"
            else -> "ITEM ACTION FAILED ($result)"
        }
        itemActionMessage = message
        itemActionMessageUntil = now + 3_000L
        pendingActionItem = null
        pendingActionChoices = intArrayOf()
        if (result == ITEM_ACTION_QUEUED) {
            Log.info("[PLZA-ITEM-ACTION] accepted operation=$operation item=${item.id} target=$partyTarget")
        } else {
            Log.warning(
                "[PLZA-ITEM-ACTION] rejected operation=$operation item=${item.id} " +
                    "target=$partyTarget result=$result message=$message"
            )
        }
        invalidate()
        return result == ITEM_ACTION_QUEUED
    }

    private fun showItemActionMessage(message: String): Boolean {
        itemActionMessage = message
        itemActionMessageUntil = SystemClock.uptimeMillis() + 3_000L
        Log.warning("[PLZA-ITEM-ACTION] rejected reason=$message")
        invalidate()
        return false
    }

    private fun cancelPendingItemAction() {
        val item = pendingActionItem
        if (item != null) Log.info("[PLZA-ITEM-ACTION] cancelled item=${item.id}")
        pendingActionItem = null
        pendingActionChoices = intArrayOf()
        invalidate()
    }

    private fun loadBitmap(path: String, sampleSize: Int = 1): Bitmap? {
        return try {
            val options = BitmapFactory.Options().apply {
                inSampleSize = sampleSize.coerceAtLeast(1)
                inPreferredConfig = Bitmap.Config.ARGB_8888
            }
            context.assets.open(path).use { stream -> BitmapFactory.decodeStream(stream, null, options) }
        } catch (error: Exception) {
            Log.error("[PLZA Companion] Unable to load asset $path: ${error.message}")
            null
        }
    }

    private fun itemIcon(itemId: Int): Bitmap? {
        itemIconCache[itemId]?.let { return it }
        if (itemId in missingItemIcons) return null
        val bitmap = loadBitmap("plza/items/item_${itemId.toString().padStart(4, '0')}.png")
        if (bitmap == null) {
            missingItemIcons += itemId
        } else {
            itemIconCache[itemId] = bitmap
        }
        return bitmap
    }

    private fun pokemonIcon(entry: PartyEntry): Bitmap? {
        return pokemonIcon(entry.species, entry.flags and 2 != 0, entry.form, entry.gender)
    }

    private fun pokemonIcon(
        speciesNumber: Int,
        shiny: Boolean,
        form: Int = 0,
        gender: Int = 2
    ): Bitmap? {
        val shinyNumber = if (shiny) 1 else 0
        val species = speciesNumber.toString().padStart(4, '0')
        val candidates = buildList {
            if (form > 0) {
                val megaCode = (50 + form.coerceIn(1, 49)).toString().padStart(2, '0')
                val regionalCode = (form.coerceIn(1, 9) * 10 + 1).toString().padStart(2, '0')
                val formCode = form.coerceIn(1, 99).toString().padStart(2, '0')
                add("pm${species}_${megaCode}_00_00_${shinyNumber}.png")
                add("pm${species}_00_${regionalCode}_00_${shinyNumber}.png")
                add("pm${species}_${formCode}_00_00_${shinyNumber}.png")
                add("pm${species}_00_${formCode}_00_${shinyNumber}.png")
            }
            if (gender == 1) add("pm${species}_01_00_00_${shinyNumber}.png")
            add("pm${species}_00_00_00_${shinyNumber}.png")
        }.distinct()
        var runtimeCandidateQueued = false
        for (assetName in candidates) {
            pokemonIconCache[assetName]?.let { return it }
            if (assetName !in missingPokemonIcons) {
                val bitmap = try {
                    context.assets.open("plza/pokemon/$assetName").use { stream ->
                        BitmapFactory.decodeStream(stream)
                    }
                } catch (_: Exception) {
                    null
                }
                if (bitmap != null) {
                    pokemonIconCache[assetName] = bitmap
                    return bitmap
                }
                missingPokemonIcons += assetName
            }
            if (!runtimeCandidateQueued) {
                runtimeCandidateQueued = requestRuntimePokemonIcon(assetName)
            }
        }
        val missingKey = "$speciesNumber:$form:$shinyNumber"
        if (reportedMissingPokemonIcons.add(missingKey)) {
            Log.error(
                "[PLZA-TEAM] Missing Pokemon render: species=$speciesNumber form=$form " +
                    "shiny=$shiny candidates=${candidates.joinToString()}"
            )
        }
        return null
    }

    private fun requestRuntimePokemonIcon(assetName: String): Boolean {
        val path = "plza/pokemon/$assetName"
        val now = SystemClock.uptimeMillis()
        if (!isAttachedToWindow || now < (runtimePokemonRetryAfter[path] ?: 0L)) {
            return false
        }
        if (!runtimePokemonRequests.add(path)) {
            return true
        }
        val executor = assetExecutor ?: run {
            runtimePokemonRequests.remove(path)
            return false
        }
        try {
            executor.execute {
                val bytes = try {
                    NativeLibrary.getDualScreenRomfsAsset(path)
                } catch (_: Throwable) {
                    null
                }
                val bitmap = if (bytes != null && bytes.size in 1..1_048_576) {
                    BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
                } else {
                    null
                }
                post {
                    runtimePokemonRequests.remove(path)
                    if (!isAttachedToWindow) {
                        bitmap?.recycle()
                        return@post
                    }
                    if (bitmap == null) {
                        runtimePokemonRetryAfter[path] = SystemClock.uptimeMillis() + 10_000L
                    } else {
                        runtimePokemonRetryAfter.remove(path)
                        pokemonIconCache[assetName]?.takeUnless(Bitmap::isRecycled)?.recycle()
                        pokemonIconCache[assetName] = bitmap
                        missingPokemonIcons.remove(assetName)
                    }
                    invalidate()
                }
            }
            return true
        } catch (_: RejectedExecutionException) {
            runtimePokemonRequests.remove(path)
            return false
        }
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val scale = min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT)
        val dx = (width - DESIGN_WIDTH * scale) * .5f
        val dy = (height - DESIGN_HEIGHT * scale) * .5f
        canvas.save()
        canvas.translate(dx, dy)
        canvas.scale(scale, scale)
        drawBackground(canvas)
        val currentStatus: Int
        synchronized(frameLock) { currentStatus = status }
        when {
            currentStatus == STATUS_UNSUPPORTED -> drawBlockingMessage(
                canvas,
                "UNSUPPORTED POKÉMON LEGENDS: Z-A BUILD",
                "Install update v393216. This mod is locked to build B1F12FD919EAE86AB8A978317677E64BCE443D1F."
            )
            currentStatus < STATUS_WAITING_SAVE -> drawBlockingMessage(
                canvas,
                "WAITING FOR POKÉMON LEGENDS: Z-A",
                "Start the game and load a save. The companion display will connect automatically."
            )
            else -> {
                drawHeader(canvas)
                when (selectedTab) {
                    0 -> drawMapPage(canvas)
                    1 -> drawTeamPage(canvas)
                    2 -> drawBagPage(canvas)
                    3 -> drawPokedexPage(canvas)
                    4 -> drawQuestPage(canvas)
                    5 -> drawRewardsPage(canvas)
                    else -> drawProfilePage(canvas)
                }
                if (currentStatus == STATUS_WAITING_SAVE) drawWaitingSaveBanner(canvas)
                if (pendingActionItem != null) drawItemActionOverlay(canvas)
            }
        }
        canvas.restore()
    }

    private fun drawBackground(canvas: Canvas) {
        paint.shader = backgroundShader
        canvas.drawRect(0f, 0f, DESIGN_WIDTH, DESIGN_HEIGHT, paint)
        paint.shader = null
        paint.color = Color.argb(28, 12, 53, 66)
        for (row in 0..9) {
            for (column in 0..11) {
                if ((row + column) and 1 == 0) {
                    canvas.drawRect(column * 108f, row * 108f, column * 108f + 108f, row * 108f + 108f, paint)
                }
            }
        }
    }

    private fun drawHeader(canvas: Canvas) {
        paint.color = Color.rgb(18, 36, 47)
        canvas.drawRect(0f, 0f, DESIGN_WIDTH, 142f, paint)
        paint.color = Color.rgb(23, 204, 201)
        canvas.drawRect(0f, 136f, DESIGN_WIDTH, 142f, paint)
        tabBounds.forEach { it.setEmpty() }
        leftArrowBounds.set(28f, 18f, 112f, 124f)
        rightArrowBounds.set(1128f, 18f, 1212f, 124f)
        drawNavigationArrow(canvas, leftArrowBounds, false, pressedArrow < 0)
        drawNavigationArrow(canvas, rightArrowBounds, true, pressedArrow > 0)
        val tabWidth = 316f
        val tabGap = 14f
        val start = 132f
        val visibleTabs = intArrayOf(
            (selectedTab - 1 + TABS.size) % TABS.size,
            selectedTab,
            (selectedTab + 1) % TABS.size
        )
        visibleTabs.forEachIndexed { visibleIndex, index ->
            val label = TABS[index]
            val left = start + visibleIndex * (tabWidth + tabGap)
            val bounds = tabBounds[index]
            bounds.set(left, 18f, left + tabWidth, 124f)
            if (index == selectedTab) {
                paint.color = Color.rgb(241, 251, 251)
                canvas.drawRoundRect(bounds, 22f, 22f, paint)
                paint.color = Color.rgb(242, 74, 103)
                canvas.drawRoundRect(RectF(bounds.left + 28f, 114f, bounds.right - 28f, 122f), 4f, 4f, paint)
            } else if (index == pressedTab) {
                paint.color = Color.rgb(38, 65, 76)
                canvas.drawRoundRect(bounds, 22f, 22f, paint)
            }
            drawTabIcon(canvas, index, left + 52f, 70f, index == selectedTab)
            drawText(canvas, label, left + 92f, 79f, 25f, if (index == selectedTab) Color.rgb(19, 53, 65) else Color.WHITE, medium)
        }
    }

    private fun drawNavigationArrow(canvas: Canvas, bounds: RectF, pointsRight: Boolean, pressed: Boolean) {
        paint.color = if (pressed) Color.rgb(47, 83, 94) else Color.rgb(27, 54, 66)
        canvas.drawRoundRect(bounds, 22f, 22f, paint)
        val centerX = bounds.centerX()
        val centerY = bounds.centerY()
        path.reset()
        if (pointsRight) {
            path.moveTo(centerX - 12f, centerY - 23f)
            path.lineTo(centerX + 14f, centerY)
            path.lineTo(centerX - 12f, centerY + 23f)
        } else {
            path.moveTo(centerX + 12f, centerY - 23f)
            path.lineTo(centerX - 14f, centerY)
            path.lineTo(centerX + 12f, centerY + 23f)
        }
        stroke.color = Color.rgb(89, 225, 220)
        stroke.strokeWidth = 7f
        stroke.strokeCap = Paint.Cap.ROUND
        stroke.strokeJoin = Paint.Join.ROUND
        canvas.drawPath(path, stroke)
    }

    private fun drawTabIcon(canvas: Canvas, index: Int, x: Float, y: Float, selected: Boolean) {
        val iconColor = if (selected) Color.rgb(242, 74, 103) else Color.rgb(89, 225, 220)
        stroke.strokeWidth = 4.5f
        stroke.strokeCap = Paint.Cap.ROUND
        stroke.strokeJoin = Paint.Join.ROUND
        stroke.color = iconColor
        paint.color = iconColor
        paint.style = Paint.Style.FILL
        when (index) {
            0 -> {
                path.reset()
                path.moveTo(x - 29f, y - 21f)
                path.lineTo(x - 10f, y - 29f)
                path.lineTo(x + 10f, y - 21f)
                path.lineTo(x + 29f, y - 29f)
                path.lineTo(x + 29f, y + 21f)
                path.lineTo(x + 10f, y + 29f)
                path.lineTo(x - 10f, y + 21f)
                path.lineTo(x - 29f, y + 29f)
                path.close()
                canvas.drawPath(path, stroke)
                canvas.drawLine(x - 10f, y - 29f, x - 10f, y + 21f, stroke)
                canvas.drawLine(x + 10f, y - 21f, x + 10f, y + 29f, stroke)
                canvas.drawCircle(x + 8f, y - 2f, 6f, paint)
                path.reset()
                path.moveTo(x + 3f, y + 2f)
                path.lineTo(x + 8f, y + 13f)
                path.lineTo(x + 13f, y + 2f)
                path.close()
                canvas.drawPath(path, paint)
            }
            1 -> {
                canvas.drawCircle(x, y - 9f, 12f, stroke)
                canvas.drawArc(RectF(x - 22f, y + 5f, x + 22f, y + 35f), 190f, 160f, false, stroke)
                canvas.drawCircle(x - 23f, y - 2f, 8f, stroke)
                canvas.drawArc(RectF(x - 37f, y + 9f, x - 9f, y + 31f), 195f, 150f, false, stroke)
                canvas.drawCircle(x + 23f, y - 2f, 8f, stroke)
                canvas.drawArc(RectF(x + 9f, y + 9f, x + 37f, y + 31f), 195f, 150f, false, stroke)
            }
            2 -> {
                canvas.drawRoundRect(RectF(x - 27f, y - 22f, x + 27f, y + 29f), 9f, 9f, stroke)
                canvas.drawArc(RectF(x - 15f, y - 34f, x + 15f, y - 7f), 180f, 180f, false, stroke)
                canvas.drawLine(x - 26f, y - 6f, x + 26f, y - 6f, stroke)
                canvas.drawCircle(x, y + 9f, 8f, stroke)
                canvas.drawLine(x - 8f, y + 9f, x + 8f, y + 9f, stroke)
            }
            3 -> {
                canvas.drawRoundRect(RectF(x - 27f, y - 31f, x + 27f, y + 31f), 7f, 7f, stroke)
                canvas.drawCircle(x - 10f, y - 10f, 8f, stroke)
                canvas.drawLine(x + 5f, y - 15f, x + 17f, y - 15f, stroke)
                canvas.drawLine(x + 5f, y - 5f, x + 17f, y - 5f, stroke)
                canvas.drawLine(x - 16f, y + 11f, x + 17f, y + 11f, stroke)
                canvas.drawLine(x - 16f, y + 21f, x + 10f, y + 21f, stroke)
            }
            4 -> {
                canvas.drawRoundRect(RectF(x - 25f, y - 29f, x + 25f, y + 30f), 5f, 5f, stroke)
                canvas.drawLine(x - 11f, y - 34f, x + 11f, y - 34f, stroke)
                repeat(3) { row ->
                    val rowY = y - 14f + row * 17f
                    path.reset()
                    path.moveTo(x - 15f, rowY)
                    path.lineTo(x - 10f, rowY + 5f)
                    path.lineTo(x - 3f, rowY - 5f)
                    canvas.drawPath(path, stroke)
                    canvas.drawLine(x + 4f, rowY, x + 16f, rowY, stroke)
                }
            }
            5 -> {
                canvas.drawCircle(x, y - 5f, 24f, stroke)
                path.reset()
                repeat(10) { point ->
                    val angle = Math.toRadians(-90.0 + point * 36.0)
                    val radius = if (point and 1 == 0) 15f else 7f
                    val px = x + kotlin.math.cos(angle).toFloat() * radius
                    val py = y - 5f + kotlin.math.sin(angle).toFloat() * radius
                    if (point == 0) path.moveTo(px, py) else path.lineTo(px, py)
                }
                path.close()
                canvas.drawPath(path, stroke)
                canvas.drawLine(x - 13f, y + 18f, x - 19f, y + 34f, stroke)
                canvas.drawLine(x + 13f, y + 18f, x + 19f, y + 34f, stroke)
            }
            else -> {
                canvas.drawCircle(x, y - 12f, 15f, stroke)
                canvas.drawArc(RectF(x - 28f, y + 3f, x + 28f, y + 38f), 190f, 160f, false, stroke)
                canvas.drawRoundRect(RectF(x - 32f, y - 35f, x + 32f, y + 34f), 10f, 10f, stroke)
            }
        }
    }

    private fun drawMapPage(canvas: Canvas) {
        val renderStarted = System.nanoTime()
        drawPanel(canvas, mapPanelBounds)
        paint.color = Color.rgb(202, 226, 226)
        canvas.drawRoundRect(mapViewportBounds, 28f, 28f, paint)
        canvas.save()
        canvas.clipRect(mapViewportBounds)
        val mapAvailable: Boolean
        val visibleMarkers: List<MarkerEntry>
        val px: Float
        val py: Float
        val rawHeading: Float
        synchronized(frameLock) {
            mapAvailable = availability and AVAILABLE_MAP != 0
            visibleMarkers = markers
            px = playerX
            py = playerY
            rawHeading = playerHeading
        }
        val heading = smoothPlayerHeading(rawHeading)
        val staticMap = if (mapAvailable) getMapStaticBitmap() else null
        if (mapAvailable && staticMap != null) {
            val centerX = 620f
            val centerY = 608f
            canvas.translate(centerX + mapPanX, centerY + mapPanY)
            canvas.scale(mapZoom, mapZoom)
            canvas.translate(-centerX, -centerY)
            paint.alpha = 255
            paint.isFilterBitmap = true
            canvas.save()
            path.reset()
            path.addOval(mapBounds, Path.Direction.CW)
            canvas.clipPath(path)
            canvas.drawBitmap(staticMap, null, mapBounds, paint)
            paint.isFilterBitmap = false
            visibleMarkers.forEach { marker ->
                val markerX = worldToMapX(marker.x, mapBounds)
                val markerY = worldToMapY(marker.z, mapBounds)
                drawMapMarker(canvas, markerX, markerY, marker.type)
            }
            var playerMarkerX = worldToMapX(px, mapBounds)
            var playerMarkerY = worldToMapY(py, mapBounds)
            val markerRadius = 32f
            val radiusX = mapBounds.width() * .5f - markerRadius
            val radiusY = mapBounds.height() * .5f - markerRadius
            val deltaX = playerMarkerX - mapBounds.centerX()
            val deltaY = playerMarkerY - mapBounds.centerY()
            val normalizedDistance =
                deltaX * deltaX / (radiusX * radiusX) + deltaY * deltaY / (radiusY * radiusY)
            if (normalizedDistance > 1f) {
                val edgeScale = (1.0 / Math.sqrt(normalizedDistance.toDouble())).toFloat()
                playerMarkerX = mapBounds.centerX() + deltaX * edgeScale
                playerMarkerY = mapBounds.centerY() + deltaY * edgeScale
            }
            paint.color = Color.argb(130, 255, 239, 64)
            canvas.drawCircle(playerMarkerX, playerMarkerY, 30f, paint)
            stroke.color = Color.WHITE
            stroke.strokeWidth = 4f
            canvas.drawCircle(playerMarkerX, playerMarkerY, 27f, stroke)
            mapPlayerIcon?.let { icon ->
                paint.alpha = 255
                paint.isFilterBitmap = true
                canvas.save()
                canvas.rotate(heading, playerMarkerX, playerMarkerY)
                mapPlayerBounds.set(
                    playerMarkerX - 24f,
                    playerMarkerY - 24f,
                    playerMarkerX + 24f,
                    playerMarkerY + 24f
                )
                canvas.drawBitmap(icon, null, mapPlayerBounds, paint)
                canvas.restore()
                paint.isFilterBitmap = false
            }
            canvas.restore()
            stroke.color = Color.argb(175, 24, 70, 75)
            stroke.strokeWidth = 5f
            canvas.drawOval(mapBounds, stroke)
        }
        canvas.restore()
        drawSectionTitle(canvas, "LUMIOSE CITY", 72f, 238f)
        if (!mapAvailable) drawEmptyState(canvas, "MAP DATA IS CONNECTING", "Load into Lumiose City to display the live map and markers.", 620f, 610f)
        drawPill(canvas, "ZONE ${if (zoneId > 0) zoneId else "—"}", 72f, 966f, 178f)
        drawPill(canvas, "×${String.format("%.1f", mapZoom)}", 1010f, 966f, 150f)
        val now = SystemClock.uptimeMillis()
        if (now - lastMapRenderLogAt >= 10_000L) {
            lastMapRenderLogAt = now
            val elapsed = (System.nanoTime() - renderStarted) / 1_000L
            Log.info(
                "[PLZA-MAP] renderUs=$elapsed staticLayer=${mapStaticBitmap != null} " +
                    "dynamicMarkers=${visibleMarkers.size} zoom=$mapZoom"
            )
        }
    }

    private fun drawMapMarker(canvas: Canvas, x: Float, y: Float, type: Int) {
        paint.color = MAP_MARKER_COLORS[type.coerceAtLeast(0) % MAP_MARKER_COLORS.size]
        canvas.drawCircle(x, y, 17f, paint)
        stroke.color = Color.WHITE
        stroke.strokeWidth = 4f
        canvas.drawCircle(x, y, 21f, stroke)
    }

    private fun smoothPlayerHeading(rawHeading: Float): Float {
        val target = ((rawHeading % 360f) + 360f) % 360f
        val now = SystemClock.elapsedRealtimeNanos()
        if (!displayedHeadingInitialized) {
            displayedPlayerHeading = target
            displayedHeadingInitialized = true
            displayedHeadingUpdatedAt = now
            return target
        }
        val elapsedSeconds = ((now - displayedHeadingUpdatedAt) / 1_000_000_000f).coerceIn(0f, .1f)
        displayedHeadingUpdatedAt = now
        val delta = ((target - displayedPlayerHeading + 540f) % 360f) - 180f
        val blend = (1.0 - kotlin.math.exp((-elapsedSeconds * 8f).toDouble())).toFloat()
        val maximumStep = 180f * elapsedSeconds
        val step = (delta * blend).coerceIn(-maximumStep, maximumStep)
        displayedPlayerHeading =
            ((displayedPlayerHeading + step) % 360f + 360f) % 360f
        if (kotlin.math.abs(delta) > .2f && selectedTab == 0) postInvalidateOnAnimation()
        return displayedPlayerHeading
    }

    private fun getMapStaticBitmap(): Bitmap? {
        mapStaticBitmap?.takeUnless(Bitmap::isRecycled)?.let { return it }
        val source = cityMapBitmap ?: return null
        val width = mapBounds.width().toInt()
        val height = mapBounds.height().toInt()
        if (width <= 0 || height <= 0) return null
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        val layerCanvas = Canvas(bitmap)
        val layerBounds = RectF(0f, 0f, width.toFloat(), height.toFloat())
        path.reset()
        path.addOval(layerBounds, Path.Direction.CW)
        layerCanvas.save()
        layerCanvas.clipPath(path)
        paint.alpha = 255
        paint.isFilterBitmap = true
        layerCanvas.drawBitmap(source, null, layerBounds, paint)
        paint.isFilterBitmap = false
        nativeMapLocations.forEach { location ->
            drawNativeMapMarker(
                layerCanvas,
                worldToMapX(location.x, layerBounds),
                worldToMapY(location.z, layerBounds),
                location.kind
            )
        }
        layerCanvas.restore()
        mapStaticBitmap = bitmap
        return bitmap
    }

    private fun worldToMapX(worldX: Float, bounds: RectF): Float =
        bounds.left + (worldX + 1050f) / 1100f * bounds.width()

    private fun worldToMapY(worldY: Float, bounds: RectF): Float =
        bounds.top + (worldY + 1050f) / 1100f * bounds.height()

    private fun drawNativeMapMarker(canvas: Canvas, x: Float, y: Float, kind: String) {
        val color = when (kind) {
            "center" -> Color.rgb(64, 145, 143)
            "cafe" -> Color.rgb(61, 111, 144)
            else -> Color.rgb(67, 105, 139)
        }
        path.reset()
        path.moveTo(x, y + 27f)
        path.cubicTo(x - 7f, y + 17f, x - 22f, y + 5f, x - 22f, y - 8f)
        path.cubicTo(x - 22f, y - 23f, x - 12f, y - 32f, x, y - 32f)
        path.cubicTo(x + 12f, y - 32f, x + 22f, y - 23f, x + 22f, y - 8f)
        path.cubicTo(x + 22f, y + 5f, x + 7f, y + 17f, x, y + 27f)
        path.close()
        paint.color = color
        canvas.drawPath(path, paint)
        stroke.color = Color.WHITE
        stroke.strokeWidth = 3f
        canvas.drawPath(path, stroke)
        val icon = when (kind) {
            "center" -> mapCenterIcon
            "cafe" -> mapCafeIcon
            else -> mapShopIcon
        }
        icon?.let {
            paint.alpha = 255
            paint.isFilterBitmap = true
            canvas.drawBitmap(it, null, RectF(x - 18f, y - 27f, x + 18f, y + 9f), paint)
            paint.isFilterBitmap = false
        }
    }

    private fun drawTeamPage(canvas: Canvas) {
        drawSectionTitle(canvas, "CURRENT TEAM", 38f, 208f)
        val currentParty: List<PartyEntry>
        val available: Boolean
        synchronized(frameLock) {
            currentParty = party
            available = availability and AVAILABLE_PARTY != 0
        }
        if (!available) {
            drawPanel(canvas, RectF(24f, 232f, 1216f, 1048f))
            drawEmptyState(canvas, "TEAM DATA IS CONNECTING", "Your six active Pokémon will appear here after a save is loaded.", 620f, 612f)
            return
        }
        if (currentParty.isEmpty()) {
            drawPanel(canvas, RectF(24f, 232f, 1216f, 1048f))
            drawEmptyState(canvas, "NO ACTIVE TEAM", "The live party block is connected, but it currently contains no Pokémon.", 620f, 612f)
            return
        }
        repeat(6) { index ->
            val column = index % 2
            val row = index / 2
            val rect = RectF(24f + column * 606f, 232f + row * 270f, 610f + column * 606f, 484f + row * 270f)
            drawPartyCard(canvas, rect, currentParty.getOrNull(index), index == focusIndex)
        }
    }

    private fun drawPartyCard(canvas: Canvas, rect: RectF, entry: PartyEntry?, focused: Boolean) {
        drawPanel(canvas, rect, focused)
        if (entry == null) {
            drawText(canvas, "EMPTY", rect.centerX(), rect.centerY() + 9f, 24f, Color.rgb(125, 145, 151), medium, Paint.Align.CENTER)
            return
        }
        pokemonIcon(entry)?.let { icon ->
            paint.alpha = 255
            paint.isFilterBitmap = true
            val portraitBounds = RectF(
                rect.left + 20f,
                rect.centerY() - 98f,
                rect.left + 204f,
                rect.centerY() + 86f
            )
            canvas.drawBitmap(icon, null, portraitBounds, paint)
            paint.isFilterBitmap = false
        }
        drawText(canvas, if (entry.name.isNotBlank()) entry.name else "#${entry.species}", rect.left + 214f, rect.top + 66f, 34f, Color.rgb(24, 48, 59), medium)
        drawText(canvas, "Lv. ${entry.level}", rect.right - 38f, rect.top + 65f, 25f, Color.rgb(67, 91, 99), medium, Paint.Align.RIGHT)
        val hpRatio = if (entry.maxHp > 0) entry.hp.toFloat() / entry.maxHp else 0f
        drawText(canvas, "HP", rect.left + 214f, rect.top + 130f, 20f, Color.rgb(68, 92, 99), medium)
        drawProgress(canvas, RectF(rect.left + 266f, rect.top + 111f, rect.right - 38f, rect.top + 136f), hpRatio, Color.rgb(40, 190, 125))
        drawText(canvas, "${entry.hp} / ${entry.maxHp}", rect.right - 38f, rect.top + 174f, 23f, Color.rgb(28, 57, 66), medium, Paint.Align.RIGHT)
        if (entry.heldItem != 0) {
            val heldName = itemNames[entry.heldItem] ?: "ITEM #${entry.heldItem}"
            drawPill(canvas, heldName.take(18), rect.left + 214f, rect.bottom - 54f, 190f)
        }
    }

    private fun drawBagPage(canvas: Canvas) {
        drawSectionTitle(canvas, "BAG", 38f, 208f)
        val now = android.os.SystemClock.uptimeMillis()
        val actionHint = if (now < itemActionMessageUntil) itemActionMessage else "SELECT AGAIN OR PRESS A TO USE"
        drawText(canvas, actionHint, 1200f, 208f, 18f,
            if (now < itemActionMessageUntil) Color.rgb(242, 74, 103) else Color.rgb(76, 99, 106),
            medium, Paint.Align.RIGHT)
        drawPanel(canvas, RectF(24f, 232f, 302f, 1048f))
        POCKETS.forEachIndexed { index, pocket ->
            val rect = RectF(42f, 254f + index * 85f, 284f, 324f + index * 85f)
            if (selectedPocket == index) {
                paint.color = Color.rgb(22, 183, 187)
                canvas.drawRoundRect(rect, 18f, 18f, paint)
            }
            drawText(canvas, pocket, rect.left + 18f, rect.centerY() + 8f, 19f, if (selectedPocket == index) Color.WHITE else Color.rgb(37, 63, 72), medium)
        }
        drawPanel(canvas, RectF(322f, 232f, 1216f, 1048f))
        val visibleItems: List<ItemEntry>
        val available: Boolean
        synchronized(frameLock) {
            available = availability and AVAILABLE_BAG != 0
            visibleItems = if (selectedPocket == 0) items else items.filter { it.pocket == selectedPocket }
        }
        if (!available) {
            drawEmptyState(canvas, "BAG DATA IS CONNECTING", "Items and quantities will appear here from the live save.", 770f, 620f)
            return
        }
        if (visibleItems.isEmpty()) {
            drawEmptyState(canvas, "NO ITEMS IN THIS POCKET", "The live bag is connected and this pocket is empty.", 770f, 620f)
            return
        }
        val firstItem = bagScrollRow * 3
        visibleItems.drop(firstItem).take(12).forEachIndexed { visibleIndex, item ->
            val index = firstItem + visibleIndex
            val column = visibleIndex % 3
            val row = visibleIndex / 3
            val rect = RectF(342f + column * 286f, 254f + row * 190f, 608f + column * 286f, 426f + row * 190f)
            drawItemCard(canvas, rect, item, index == selectedItem)
        }
        drawScrollIndicator(canvas, RectF(1201f, 254f, 1208f, 1014f), firstItem, visibleItems.size, 12)
    }

    private fun drawItemCard(canvas: Canvas, rect: RectF, item: ItemEntry, selected: Boolean) {
        drawPanel(canvas, rect, selected)
        itemIcon(item.id)?.let { icon ->
            paint.alpha = 255
            paint.isFilterBitmap = true
            canvas.drawBitmap(icon, null, RectF(rect.left + 20f, rect.top + 12f, rect.left + 122f, rect.top + 114f), paint)
            paint.isFilterBitmap = false
        }
        drawText(canvas, item.name.ifBlank { "UNKNOWN ITEM" }, rect.left + 26f, rect.bottom - 38f, 21f, Color.rgb(27, 52, 62), medium)
        drawText(canvas, "×${item.quantity}", rect.right - 20f, rect.top + 78f, 27f, Color.rgb(22, 166, 170), medium, Paint.Align.RIGHT)
    }

    private fun drawItemActionOverlay(canvas: Canvas) {
        val item = pendingActionItem ?: return
        paint.color = Color.argb(180, 7, 18, 24)
        canvas.drawRect(0f, 0f, DESIGN_WIDTH, DESIGN_HEIGHT, paint)
        val dialog = RectF(180f, 170f, 1060f, 930f)
        drawPanel(canvas, dialog)
        drawText(canvas, item.name.ifBlank { "ITEM #${item.id}" }, 620f, 242f, 34f, Color.rgb(24, 48, 59), medium, Paint.Align.CENTER)
        drawText(canvas, "×${item.quantity}", 620f, 282f, 23f, Color.rgb(22, 166, 170), medium, Paint.Align.CENTER)
        itemIcon(item.id)?.let { icon ->
            paint.alpha = 255
            paint.isFilterBitmap = true
            canvas.drawBitmap(icon, null, RectF(210f, 198f, 306f, 294f), paint)
            paint.isFilterBitmap = false
        }
        if (pendingActionChoices.isNotEmpty()) {
            drawText(canvas, "CHOOSE AN ACTION", 620f, 372f, 25f, Color.rgb(67, 91, 99), medium, Paint.Align.CENTER)
            pendingActionChoices.take(2).forEachIndexed { index, operation ->
                val rect = RectF(280f + index * 350f, 420f, 610f + index * 350f, 590f)
                actionChoiceBounds[index].set(rect)
                drawPanel(canvas, rect, index == pendingActionChoice)
                val label = if (operation == ITEM_ACTION_USE_ON_PARTY) "USE" else "GIVE"
                drawText(canvas, label, rect.centerX(), rect.centerY() + 12f, 34f, Color.rgb(24, 48, 59), medium, Paint.Align.CENTER)
            }
        } else {
            val currentParty = synchronized(frameLock) { party.toList() }
            drawText(canvas, "SELECT A POKEMON", 620f, 324f, 25f, Color.rgb(67, 91, 99), medium, Paint.Align.CENTER)
            partyTargetBounds.forEach { it.setEmpty() }
            currentParty.take(6).forEachIndexed { index, entry ->
                val column = index % 2
                val row = index / 2
                val rect = RectF(240f + column * 410f, 350f + row * 146f, 590f + column * 410f, 478f + row * 146f)
                partyTargetBounds[index].set(rect)
                drawPanel(canvas, rect, index == pendingPartyTarget)
                pokemonIcon(entry)?.let { icon ->
                    paint.alpha = 255
                    paint.isFilterBitmap = true
                    canvas.drawBitmap(icon, null, RectF(rect.left + 10f, rect.top + 10f, rect.left + 118f, rect.bottom - 10f), paint)
                    paint.isFilterBitmap = false
                }
                val name = entry.name.ifBlank { "#${entry.species}" }
                drawText(canvas, name.take(16), rect.left + 132f, rect.top + 55f, 24f, Color.rgb(24, 48, 59), medium)
                drawText(canvas, "Lv. ${entry.level}", rect.left + 132f, rect.top + 92f, 19f, Color.rgb(67, 91, 99), medium)
            }
        }
        itemActionCancelBounds.set(455f, 838f, 785f, 902f)
        paint.color = Color.rgb(25, 64, 74)
        canvas.drawRoundRect(itemActionCancelBounds, 26f, 26f, paint)
        drawText(canvas, "CANCEL", itemActionCancelBounds.centerX(), itemActionCancelBounds.centerY() + 9f, 22f, Color.WHITE, medium, Paint.Align.CENTER)
        drawText(canvas, "A  CONFIRM     B  BACK", 620f, 918f, 17f, Color.rgb(80, 105, 112), medium, Paint.Align.CENTER)
    }

    private fun drawPokedexPage(canvas: Canvas) {
        drawSectionTitle(canvas, "POKEDEX", 38f, 208f)
        drawPanel(canvas, RectF(24f, 232f, 508f, 1048f))
        drawPanel(canvas, RectF(528f, 232f, 1216f, 1048f))
        val available: Boolean
        val seen: Int
        val caught: Int
        val mega: Int
        val states: Map<Int, PokedexState>
        synchronized(frameLock) {
            available = availability and AVAILABLE_POKEDEX != 0
            seen = pokedexSeen
            caught = pokedexCaught
            mega = pokedexMega
            states = pokedexStates
        }
        drawCompactMetric(canvas, "CAUGHT", if (available) caught.toString() else "—", 42f, 252f, 142f, Color.rgb(242, 74, 103))
        drawCompactMetric(canvas, "SEEN", if (available) seen.toString() else "—", 194f, 252f, 142f, Color.rgb(22, 181, 185))
        drawCompactMetric(canvas, "MEGA", if (available) mega.toString() else "—", 346f, 252f, 142f, Color.rgb(112, 83, 190))
        if (pokedexInfo.isEmpty()) {
            drawEmptyState(canvas, "POKEDEX DATA UNAVAILABLE", "The native species catalog could not be loaded.", 620f, 620f)
            return
        }
        pokedexInfo.drop(pokedexScroll).take(7).forEachIndexed { row, info ->
            val index = pokedexScroll + row
            val state = states[info.species]
            val discovered = state?.seen == true || state?.caught == true
            val rect = RectF(42f, 350f + row * 96f, 484f, 434f + row * 96f)
            if (index == selectedPokedex) {
                paint.color = Color.rgb(22, 183, 187)
                canvas.drawRoundRect(rect, 18f, 18f, paint)
            }
            if (discovered) {
                pokemonIcon(info.species, state?.shiny == true, state?.form ?: 0)?.let { icon ->
                    paint.alpha = 255
                    paint.isFilterBitmap = true
                    canvas.drawBitmap(icon, null, RectF(rect.left + 5f, rect.top - 2f, rect.left + 91f, rect.bottom + 2f), paint)
                    paint.isFilterBitmap = false
                }
            } else {
                paint.color = Color.rgb(215, 230, 232)
                canvas.drawCircle(rect.left + 49f, rect.centerY(), 30f, paint)
                drawText(canvas, "?", rect.left + 49f, rect.centerY() + 11f, 30f, Color.rgb(117, 139, 145), medium, Paint.Align.CENTER)
            }
            val textColor = if (index == selectedPokedex) Color.WHITE else Color.rgb(28, 54, 64)
            drawText(canvas, "No. ${info.species.toString().padStart(4, '0')}", rect.left + 98f, rect.top + 28f, 17f, if (index == selectedPokedex) Color.argb(210, 255, 255, 255) else Color.rgb(93, 117, 123), medium)
            drawText(canvas, if (discovered) info.name else "?????", rect.left + 98f, rect.top + 64f, 27f, textColor, medium)
            if (state?.caught == true) {
                paint.color = if (index == selectedPokedex) Color.WHITE else Color.rgb(242, 74, 103)
                canvas.drawCircle(rect.right - 29f, rect.centerY(), 11f, paint)
            }
        }
        drawScrollIndicator(canvas, RectF(492f, 350f, 499f, 1022f), pokedexScroll, pokedexInfo.size, 7)
        val selected = pokedexInfo.getOrNull(selectedPokedex) ?: return
        val state = states[selected.species]
        val discovered = state?.seen == true || state?.caught == true
        drawPill(canvas, "No. ${selected.species.toString().padStart(4, '0')}", 558f, 260f, 160f)
        drawText(canvas, if (discovered) selected.name else "UNDISCOVERED", 558f, 356f, 42f, Color.rgb(24, 49, 59), condensed)
        if (discovered) {
            pokemonIcon(selected.species, state?.shiny == true, state?.form ?: 0)?.let { icon ->
                paint.alpha = 255
                paint.isFilterBitmap = true
                canvas.drawBitmap(icon, null, RectF(742f, 365f, 1058f, 681f), paint)
                paint.isFilterBitmap = false
            }
            drawText(canvas, selected.category, 558f, 424f, 25f, Color.rgb(57, 87, 95), medium)
            drawText(canvas, "HEIGHT", 558f, 500f, 18f, Color.rgb(96, 119, 125), medium)
            drawText(canvas, selected.height, 558f, 540f, 27f, Color.rgb(24, 54, 64), medium)
            drawText(canvas, "WEIGHT", 558f, 606f, 18f, Color.rgb(96, 119, 125), medium)
            drawText(canvas, selected.weight, 558f, 646f, 27f, Color.rgb(24, 54, 64), medium)
            var badgeX = 558f
            if (state?.caught == true) { drawPill(canvas, "CAUGHT", badgeX, 696f, 132f); badgeX += 144f }
            if (state?.shiny == true) { drawPill(canvas, "SHINY", badgeX, 696f, 126f); badgeX += 138f }
            if (state?.mega == true) { drawPill(canvas, "MEGA", badgeX, 696f, 118f); badgeX += 130f }
            if (state?.alpha == true) drawPill(canvas, "ALPHA", badgeX, 696f, 122f)
            paint.color = Color.rgb(235, 244, 245)
            canvas.drawRoundRect(RectF(558f, 774f, 1186f, 1018f), 22f, 22f, paint)
            drawText(canvas, "POKEDEX ENTRY", 586f, 822f, 19f, Color.rgb(22, 171, 175), medium)
            drawWrappedText(canvas, selected.description, 586f, 868f, 568f, 35f, 25f, Color.rgb(48, 74, 82), maxLines = 5)
        } else {
            drawEmptyState(canvas, "NO DATA RECORDED", "Encounter this Pokémon to reveal its Pokedex entry.", 872f, 632f)
        }
    }

    private fun drawCompactMetric(canvas: Canvas, label: String, value: String, x: Float, y: Float, width: Float, accent: Int) {
        paint.color = Color.rgb(237, 246, 247)
        canvas.drawRoundRect(RectF(x, y, x + width, y + 78f), 17f, 17f, paint)
        paint.color = accent
        canvas.drawRoundRect(RectF(x, y, x + 8f, y + 78f), 4f, 4f, paint)
        drawText(canvas, value, x + 26f, y + 48f, 31f, Color.rgb(25, 53, 63), condensed)
        drawText(canvas, label, x + width - 12f, y + 47f, 15f, Color.rgb(88, 111, 118), medium, Paint.Align.RIGHT)
    }

    private fun drawQuestPage(canvas: Canvas) {
        drawSectionTitle(canvas, "QUESTS", 38f, 208f)
        drawPanel(canvas, RectF(24f, 232f, 520f, 1048f))
        drawPanel(canvas, RectF(540f, 232f, 1216f, 1048f))
        val currentQuests: List<QuestEntry>
        val available: Boolean
        synchronized(frameLock) {
            currentQuests = quests
            available = availability and AVAILABLE_QUESTS != 0
        }
        if (!available) {
            drawEmptyState(canvas, "QUEST DATA IS CONNECTING", "Active story and side missions will appear from the current save.", 620f, 620f)
            return
        }
        if (currentQuests.isEmpty()) {
            drawEmptyState(canvas, "NO ACTIVE QUESTS", "The live quest block is connected and has no active entries.", 620f, 620f)
            return
        }
        currentQuests.drop(questScroll).take(7).forEachIndexed { row, quest ->
            val index = questScroll + row
            val rect = RectF(42f, 258f + row * 106f, 496f, 350f + row * 106f)
            if (index == selectedQuest) {
                paint.color = Color.rgb(22, 183, 187)
                canvas.drawRoundRect(rect, 16f, 16f, paint)
            }
            val category = when (quest.type) { 0 -> "MAIN"; 1 -> "SIDE"; else -> "RESEARCH" }
            drawText(canvas, category, rect.left + 18f, rect.top + 27f, 15f, if (index == selectedQuest) Color.argb(210, 255, 255, 255) else Color.rgb(22, 169, 173), medium)
            val title = quest.title.ifBlank { "UNKNOWN QUEST" }
            drawText(canvas, if (title.length > 31) title.take(30) + "…" else title, rect.left + 18f, rect.top + 66f, 23f, if (index == selectedQuest) Color.WHITE else Color.rgb(31, 57, 66), medium)
            drawText(canvas, "ACTIVE", rect.right - 16f, rect.top + 28f, 14f, if (index == selectedQuest) Color.WHITE else Color.rgb(103, 125, 131), medium, Paint.Align.RIGHT)
        }
        drawScrollIndicator(canvas, RectF(504f, 258f, 511f, 1000f), questScroll, currentQuests.size, 7)
        val selected = currentQuests.getOrNull(selectedQuest) ?: return
        drawCompactMetric(canvas, "TICKET", ticketPoints.toString(), 564f, 252f, 194f, Color.rgb(242, 74, 103))
        drawCompactMetric(canvas, "MEDALS", prizeMedals.toString(), 772f, 252f, 194f, Color.rgb(239, 168, 48))
        drawCompactMetric(canvas, "RESEARCH", researchPoints.toString(), 980f, 252f, 208f, Color.rgb(24, 179, 183))
        drawPill(canvas, when (selected.type) { 0 -> "MAIN QUEST"; 1 -> "SIDE QUEST"; else -> "RESEARCH" }, 570f, 354f, 184f)
        drawText(canvas, selected.title, 570f, 454f, 36f, Color.rgb(24, 49, 59), medium)
        drawWrappedText(canvas, selected.description, 570f, 512f, 596f, 34f, 25f, Color.rgb(64, 88, 95), maxLines = 5)
        paint.color = Color.rgb(232, 244, 245)
        canvas.drawRoundRect(RectF(570f, 724f, 1186f, 914f), 22f, 22f, paint)
        drawText(canvas, "CURRENT OBJECTIVE", 598f, 772f, 19f, Color.rgb(22, 171, 175), medium)
        drawWrappedText(canvas, questObjective(selected), 598f, 818f, 560f, 34f, 25f, Color.rgb(31, 61, 70), maxLines = 3)
        if (selected.total > 0) {
            drawText(canvas, "PROGRESS  ${selected.progress} / ${selected.total}", 570f, 966f, 20f, Color.rgb(60, 85, 92), medium)
            drawProgress(canvas, RectF(570f, 986f, 1186f, 1014f), selected.progress.toFloat() / selected.total, Color.rgb(242, 74, 103))
        }
    }

    private fun drawRewardsPage(canvas: Canvas) {
        drawSectionTitle(canvas, "MABLE'S RESEARCH REWARDS", 38f, 208f)
        val available: Boolean
        val level: Int
        val viewedLevel: Int
        val points: Int
        synchronized(frameLock) {
            available = availability and AVAILABLE_REWARDS != 0
            level = mableLevel
            viewedLevel = mableLevelViewed
            points = researchPoints
        }
        if (!available) {
            drawPanel(canvas, RectF(24f, 232f, 1216f, 1048f))
            drawEmptyState(canvas, "REWARD DATA IS CONNECTING", "Your live research level and unlocked rewards will appear here.", 620f, 620f)
            return
        }

        val currentLevel = level.coerceIn(1, 50)
        drawPanel(canvas, RectF(24f, 232f, 340f, 1048f), true)
        paint.color = Color.rgb(242, 74, 103)
        canvas.drawCircle(182f, 380f, 102f, paint)
        stroke.color = Color.WHITE
        stroke.strokeWidth = 8f
        canvas.drawCircle(182f, 380f, 77f, stroke)
        drawText(canvas, "LEVEL", 182f, 352f, 24f, Color.WHITE, medium, Paint.Align.CENTER)
        drawText(canvas, currentLevel.toString(), 182f, 430f, 70f, Color.WHITE, condensed, Paint.Align.CENTER)
        drawText(canvas, "RESEARCH POINTS", 182f, 552f, 20f, Color.rgb(54, 84, 92), medium, Paint.Align.CENTER)
        drawText(canvas, points.toString(), 182f, 614f, 46f, Color.rgb(22, 171, 175), condensed, Paint.Align.CENTER)
        drawText(canvas, "REWARDS UNLOCKED", 182f, 708f, 19f, Color.rgb(54, 84, 92), medium, Paint.Align.CENTER)
        drawText(canvas, "${(currentLevel - 1).coerceIn(0, RESEARCH_REWARDS.size)} / ${RESEARCH_REWARDS.size}", 182f, 766f, 38f, Color.rgb(24, 52, 62), condensed, Paint.Align.CENTER)
        val selected = RESEARCH_REWARDS[selectedReward]
        val selectedState = when {
            selected.level > currentLevel -> "LOCKED"
            selected.level > viewedLevel -> "NEW"
            else -> "UNLOCKED"
        }
        drawText(canvas, "SELECTED", 182f, 866f, 19f, Color.rgb(54, 84, 92), medium, Paint.Align.CENTER)
        drawWrappedText(canvas, selected.reward, 52f, 914f, 260f, 34f, 26f, Color.rgb(24, 52, 62), Paint.Align.CENTER, 2)
        drawPill(canvas, selectedState, 102f, 980f, 160f)

        drawPanel(canvas, RectF(364f, 232f, 1216f, 1048f))
        val visibleCount = 7
        RESEARCH_REWARDS.drop(rewardScroll).take(visibleCount).forEachIndexed { row, reward ->
            val index = rewardScroll + row
            val top = 250f + row * 109f
            val bounds = RectF(382f, top, 1187f, top + 94f)
            val isSelected = index == selectedReward
            val unlocked = reward.level <= currentLevel
            val isNew = unlocked && reward.level > viewedLevel
            paint.color = when {
                isSelected -> Color.rgb(224, 248, 247)
                unlocked -> Color.rgb(244, 251, 251)
                else -> Color.rgb(230, 237, 239)
            }
            canvas.drawRoundRect(bounds, 20f, 20f, paint)
            stroke.strokeWidth = if (isSelected) 5f else 2f
            stroke.color = when {
                isSelected -> Color.rgb(242, 74, 103)
                unlocked -> Color.rgb(32, 185, 183)
                else -> Color.rgb(173, 190, 194)
            }
            canvas.drawRoundRect(bounds, 20f, 20f, stroke)
            paint.color = if (unlocked) Color.rgb(242, 74, 103) else Color.rgb(151, 165, 169)
            canvas.drawCircle(bounds.left + 54f, bounds.centerY(), 32f, paint)
            drawText(canvas, reward.level.toString(), bounds.left + 54f, bounds.centerY() + 12f, 29f, Color.WHITE, condensed, Paint.Align.CENTER)
            drawText(canvas, reward.reward, bounds.left + 106f, bounds.centerY() + 10f, 27f, if (unlocked) Color.rgb(25, 53, 62) else Color.rgb(112, 127, 131), medium)
            val stateText = when {
                !unlocked -> "LOCKED"
                isNew -> "NEW"
                else -> "UNLOCKED"
            }
            drawText(canvas, stateText, bounds.right - 26f, bounds.centerY() + 9f, 19f, when {
                isNew -> Color.rgb(242, 74, 103)
                unlocked -> Color.rgb(20, 161, 161)
                else -> Color.rgb(132, 145, 149)
            }, medium, Paint.Align.RIGHT)
        }
        drawScrollIndicator(canvas, RectF(1193f, 250f, 1202f, 998f), rewardScroll, RESEARCH_REWARDS.size, visibleCount)
    }

    private fun drawProfilePage(canvas: Canvas) {
        drawSectionTitle(canvas, "TRAINER PROFILE", 38f, 208f)
        val profileParty: List<PartyEntry>
        val liveMoney: Int
        val liveZone: Int
        val level: Int
        val points: Int
        val seen: Int
        val caught: Int
        val mega: Int
        val ticket: Int
        val infiniteTicket: Int
        val wins: Int
        val medals: Int
        val surveys: Int
        synchronized(frameLock) {
            profileParty = party
            liveMoney = money
            liveZone = zoneId
            level = mableLevel
            points = researchPoints
            seen = pokedexSeen
            caught = pokedexCaught
            mega = pokedexMega
            ticket = ticketPoints
            infiniteTicket = infiniteTicketPoints
            wins = royaleWins
            medals = prizeMedals
            surveys = surveyPoints
        }

        drawPanel(canvas, RectF(24f, 232f, 1216f, 452f), true)
        paint.color = Color.rgb(242, 74, 103)
        canvas.drawCircle(142f, 342f, 72f, paint)
        drawText(canvas, "Lv.", 142f, 326f, 22f, Color.WHITE, medium, Paint.Align.CENTER)
        drawText(canvas, level.coerceAtLeast(1).toString(), 142f, 378f, 45f, Color.WHITE, condensed, Paint.Align.CENTER)
        drawText(canvas, "MABLE RESEARCHER", 246f, 306f, 22f, Color.rgb(22, 171, 175), medium)
        drawText(canvas, "Research Points  $points", 246f, 354f, 31f, Color.rgb(25, 53, 62), condensed)
        drawText(canvas, "Current Zone  ${if (liveZone > 0) liveZone else "--"}", 246f, 402f, 23f, Color.rgb(75, 98, 105), medium)
        drawCompactMetric(canvas, "MONEY", liveMoney.toString(), 694f, 276f, 220f, Color.rgb(239, 168, 48))
        drawCompactMetric(canvas, "ROYALE WINS", wins.toString(), 932f, 276f, 244f, Color.rgb(112, 83, 190))

        drawPanel(canvas, RectF(24f, 476f, 590f, 760f))
        drawText(canvas, "POKEDEX PROGRESS", 54f, 526f, 23f, Color.rgb(25, 53, 62), medium)
        drawCompactMetric(canvas, "CAUGHT", caught.toString(), 54f, 556f, 156f, Color.rgb(242, 74, 103))
        drawCompactMetric(canvas, "SEEN", seen.toString(), 224f, 556f, 156f, Color.rgb(22, 181, 185))
        drawCompactMetric(canvas, "MEGA", mega.toString(), 394f, 556f, 156f, Color.rgb(112, 83, 190))
        val completion = if (seen > 0) caught.toFloat() / seen else 0f
        drawText(canvas, "CAUGHT / SEEN", 54f, 700f, 19f, Color.rgb(76, 99, 106), medium)
        drawProgress(canvas, RectF(214f, 681f, 550f, 710f), completion, Color.rgb(242, 74, 103))

        drawPanel(canvas, RectF(614f, 476f, 1216f, 760f))
        drawText(canvas, "ADVENTURE PROGRESS", 644f, 526f, 23f, Color.rgb(25, 53, 62), medium)
        drawCompactMetric(canvas, "TICKET", ticket.toString(), 644f, 556f, 162f, Color.rgb(242, 74, 103))
        drawCompactMetric(canvas, "INFINITE", infiniteTicket.toString(), 820f, 556f, 170f, Color.rgb(22, 181, 185))
        drawCompactMetric(canvas, "MEDALS", medals.toString(), 1004f, 556f, 174f, Color.rgb(239, 168, 48))
        drawText(canvas, "HYPERSPACE SURVEY POINTS", 644f, 706f, 19f, Color.rgb(76, 99, 106), medium)
        drawText(canvas, surveys.toString(), 1178f, 711f, 32f, Color.rgb(24, 52, 62), condensed, Paint.Align.RIGHT)

        drawPanel(canvas, RectF(24f, 784f, 1216f, 1048f))
        drawText(canvas, "CURRENT TEAM", 54f, 834f, 23f, Color.rgb(25, 53, 62), medium)
        repeat(6) { index ->
            val entry = profileParty.getOrNull(index)
            val left = 50f + index * 191f
            val card = RectF(left, 858f, left + 170f, 1020f)
            paint.color = if (entry == null) Color.rgb(231, 239, 241) else Color.rgb(240, 250, 250)
            canvas.drawRoundRect(card, 20f, 20f, paint)
            if (entry != null) {
                pokemonIcon(entry)?.let { icon ->
                    paint.isFilterBitmap = true
                    canvas.drawBitmap(icon, null, RectF(left + 26f, 865f, left + 144f, 983f), paint)
                    paint.isFilterBitmap = false
                }
                drawText(canvas, "Lv.${entry.level}", card.centerX(), 1005f, 20f, Color.rgb(25, 53, 62), medium, Paint.Align.CENTER)
            } else {
                drawText(canvas, "EMPTY", card.centerX(), card.centerY() + 8f, 18f, Color.rgb(135, 151, 155), medium, Paint.Align.CENTER)
            }
        }
    }

    private fun drawScrollIndicator(canvas: Canvas, bounds: RectF, first: Int, total: Int, visible: Int) {
        if (total <= visible) return
        paint.color = Color.rgb(210, 226, 228)
        canvas.drawRoundRect(bounds, bounds.width() * .5f, bounds.width() * .5f, paint)
        val thumbHeight = bounds.height() * visible / total
        val travel = bounds.height() - thumbHeight
        val maxFirst = max(1, total - visible)
        val top = bounds.top + travel * first.coerceIn(0, maxFirst) / maxFirst
        paint.color = Color.rgb(22, 181, 185)
        canvas.drawRoundRect(RectF(bounds.left, top, bounds.right, top + thumbHeight), bounds.width() * .5f, bounds.width() * .5f, paint)
    }

    private fun drawBlockingMessage(canvas: Canvas, title: String, body: String) {
        paint.color = Color.rgb(18, 36, 47)
        canvas.drawRoundRect(RectF(132f, 274f, 1108f, 806f), 42f, 42f, paint)
        paint.color = Color.rgb(27, 200, 198)
        canvas.drawCircle(620f, 414f, 66f, paint)
        stroke.color = Color.WHITE
        stroke.strokeWidth = 10f
        canvas.drawCircle(620f, 414f, 28f, stroke)
        canvas.drawLine(554f, 414f, 686f, 414f, stroke)
        drawText(canvas, title, 620f, 560f, 34f, Color.WHITE, condensed, Paint.Align.CENTER)
        drawWrappedText(canvas, body, 245f, 624f, 750f, 32f, 24f, Color.rgb(189, 215, 220), Paint.Align.CENTER)
    }

    private fun drawWaitingSaveBanner(canvas: Canvas) {
        paint.color = Color.argb(238, 18, 36, 47)
        canvas.drawRoundRect(RectF(348f, 950f, 892f, 1028f), 28f, 28f, paint)
        paint.color = Color.rgb(34, 209, 204)
        canvas.drawCircle(388f, 989f, 10f, paint)
        drawText(canvas, "Waiting for live save data", 422f, 999f, 25f, Color.WHITE, medium)
    }

    private fun drawPanel(canvas: Canvas, rect: RectF, focused: Boolean = false) {
        paint.color = Color.argb(242, 255, 255, 255)
        canvas.drawRoundRect(rect, 24f, 24f, paint)
        stroke.strokeWidth = if (focused) 6f else 2f
        stroke.color = if (focused) Color.rgb(242, 74, 103) else Color.rgb(174, 205, 209)
        canvas.drawRoundRect(rect, 24f, 24f, stroke)
    }

    private fun drawSectionTitle(canvas: Canvas, title: String, x: Float, y: Float) {
        drawText(canvas, title, x, y, 31f, Color.rgb(21, 48, 58), condensed)
    }

    private fun drawEmptyState(canvas: Canvas, title: String, body: String, x: Float, y: Float) {
        paint.color = Color.rgb(220, 237, 238)
        canvas.drawCircle(x, y - 82f, 62f, paint)
        stroke.color = Color.rgb(37, 183, 184)
        stroke.strokeWidth = 7f
        canvas.drawCircle(x, y - 82f, 48f, stroke)
        canvas.drawCircle(x, y - 82f, 13f, stroke)
        drawText(canvas, title, x, y + 28f, 30f, Color.rgb(31, 60, 70), medium, Paint.Align.CENTER)
        drawText(canvas, body, x, y + 78f, 22f, Color.rgb(80, 105, 112), regular, Paint.Align.CENTER)
    }

    private fun drawProgress(canvas: Canvas, rect: RectF, progress: Float, color: Int) {
        paint.color = Color.rgb(210, 225, 227)
        canvas.drawRoundRect(rect, rect.height() * .5f, rect.height() * .5f, paint)
        val clamped = progress.coerceIn(0f, 1f)
        if (clamped > 0f) {
            paint.color = color
            canvas.drawRoundRect(RectF(rect.left, rect.top, rect.left + rect.width() * clamped, rect.bottom), rect.height() * .5f, rect.height() * .5f, paint)
        }
    }

    private fun drawPill(canvas: Canvas, text: String, x: Float, y: Float, width: Float) {
        paint.color = Color.rgb(25, 64, 74)
        canvas.drawRoundRect(RectF(x, y, x + width, y + 52f), 26f, 26f, paint)
        drawText(canvas, text, x + width * .5f, y + 35f, 20f, Color.WHITE, medium, Paint.Align.CENTER)
    }

    private fun drawText(canvas: Canvas, text: String, x: Float, y: Float, size: Float, color: Int, typeface: Typeface, align: Paint.Align = Paint.Align.LEFT) {
        paint.shader = null
        paint.style = Paint.Style.FILL
        paint.color = color
        paint.textSize = size
        paint.typeface = typeface
        paint.textAlign = align
        canvas.drawText(text, x, y, paint)
    }

    private fun drawWrappedText(canvas: Canvas, text: String, x: Float, y: Float, width: Float, lineHeight: Float, size: Float, color: Int, align: Paint.Align = Paint.Align.LEFT, maxLines: Int = Int.MAX_VALUE) {
        paint.textSize = size
        paint.typeface = regular
        paint.textAlign = align
        paint.color = color
        val words = text.trim().split(Regex("\\s+")).filter { it.isNotEmpty() }
        val lines = ArrayList<String>()
        var line = ""
        for (word in words) {
            val candidate = if (line.isEmpty()) word else "$line $word"
            if (paint.measureText(candidate) > width && line.isNotEmpty()) {
                lines += line
                line = word
            } else {
                line = candidate
            }
        }
        if (line.isNotEmpty()) lines += line
        val visibleLines = lines.take(maxLines).toMutableList()
        if (lines.size > maxLines && visibleLines.isNotEmpty()) {
            var last = visibleLines.last()
            while (last.isNotEmpty() && paint.measureText("$last…") > width) last = last.dropLast(1)
            visibleLines[visibleLines.lastIndex] = "$last…"
        }
        visibleLines.forEachIndexed { index, value ->
            canvas.drawText(value, if (align == Paint.Align.CENTER) x + width * .5f else x, y + index * lineHeight, paint)
        }
    }

    private fun confirmPendingItemAction() {
        val item = pendingActionItem ?: return
        if (pendingActionChoices.isNotEmpty()) {
            val operation = pendingActionChoices[pendingActionChoice.coerceIn(pendingActionChoices.indices)]
            openPartyTarget(item, operation)
            return
        }
        val partyCount = synchronized(frameLock) { party.size }
        if (partyCount == 0) {
            cancelPendingItemAction()
            showItemActionMessage("PARTY DATA IS NOT READY")
            return
        }
        executeItemAction(item, pendingActionOperation, pendingPartyTarget.coerceIn(0, partyCount - 1))
    }

    private fun movePendingItemSelection(horizontal: Int, vertical: Int) {
        if (pendingActionChoices.isNotEmpty()) {
            pendingActionChoice = (pendingActionChoice + horizontal).coerceIn(pendingActionChoices.indices)
        } else {
            val partyCount = synchronized(frameLock) { party.size.coerceAtMost(6) }
            if (partyCount == 0) return
            val column = (pendingPartyTarget % 2 + horizontal).coerceIn(0, 1)
            val row = (pendingPartyTarget / 2 + vertical).coerceIn(0, 2)
            pendingPartyTarget = (row * 2 + column).coerceIn(0, partyCount - 1)
        }
        invalidate()
    }

    private fun handlePendingItemTouch(event: MotionEvent, x: Float, y: Float): Boolean {
        if (pendingActionItem == null) return false
        if (event.actionMasked != MotionEvent.ACTION_UP) return true
        if (itemActionCancelBounds.contains(x, y)) {
            cancelPendingItemAction()
            return true
        }
        if (pendingActionChoices.isNotEmpty()) {
            val index = actionChoiceBounds.indexOfFirst { it.contains(x, y) }
            if (index in pendingActionChoices.indices) {
                pendingActionChoice = index
                confirmPendingItemAction()
            }
            return true
        }
        val partyIndex = partyTargetBounds.indexOfFirst { it.contains(x, y) }
        if (partyIndex >= 0) {
            pendingPartyTarget = partyIndex
            confirmPendingItemAction()
        }
        return true
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val scale = min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT)
        val dx = (width - DESIGN_WIDTH * scale) * .5f
        val dy = (height - DESIGN_HEIGHT * scale) * .5f
        val x = (event.x - dx) / scale
        val y = (event.y - dy) / scale
        if (handlePendingItemTouch(event, x, y)) return true
        if (event.pointerCount >= 2 && selectedTab == 0) {
            val distance = kotlin.math.hypot(event.getX(1) - event.getX(0), event.getY(1) - event.getY(0))
            if (lastPinchDistance > 0f) mapZoom = (mapZoom * distance / lastPinchDistance).coerceIn(.7f, 5f)
            lastPinchDistance = distance
            invalidate()
            return true
        }
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                requestFocus()
                pressedTab = tabBounds.indexOfFirst { it.contains(x, y) }
                pressedArrow = when {
                    leftArrowBounds.contains(x, y) -> -1
                    rightArrowBounds.contains(x, y) -> 1
                    else -> 0
                }
                if (selectedTab == 0 && y > 188f) {
                    mapDragging = true
                    lastTouchX = x
                    lastTouchY = y
                }
                if (selectedTab == 3 && x in 42f..499f && y in 350f..1022f) {
                    pokedexDragging = true
                    pokedexTouchMoved = false
                    pokedexDragAccumulator = 0f
                    pokedexTouchStartY = y
                    lastTouchY = y
                }
                if (selectedTab == 4 && x in 42f..511f && y in 258f..1000f) {
                    questDragging = true
                    questTouchMoved = false
                    questDragAccumulator = 0f
                    questTouchStartY = y
                    lastTouchY = y
                }
                if (selectedTab == 2 && x in 342f..1200f && y in 254f..1014f) {
                    bagDragging = true
                    bagTouchMoved = false
                    bagTouchStartY = y
                    bagDragAccumulator = 0f
                    lastTouchY = y
                    val column = ((x - 342f) / 286f).toInt().coerceIn(0, 2)
                    val row = ((y - 254f) / 190f).toInt().coerceIn(0, 3)
                    pressedBagItem = bagScrollRow * 3 + row * 3 + column
                }
                if (selectedTab == 5 && x in 382f..1187f && y in 250f..1013f) {
                    rewardDragging = true
                    rewardTouchMoved = false
                    rewardDragAccumulator = 0f
                    rewardTouchStartY = y
                    lastTouchY = y
                }
                invalidate()
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                if (mapDragging && selectedTab == 0) {
                    mapPanX += x - lastTouchX
                    mapPanY += y - lastTouchY
                    lastTouchX = x
                    lastTouchY = y
                    invalidate()
                }
                if (pokedexDragging && selectedTab == 3) {
                    if (kotlin.math.abs(y - pokedexTouchStartY) > 12f) pokedexTouchMoved = true
                    pokedexDragAccumulator += lastTouchY - y
                    lastTouchY = y
                    val rowThreshold = 56f
                    while (pokedexDragAccumulator >= rowThreshold) {
                        scrollPokedexBy(1)
                        pokedexDragAccumulator -= rowThreshold
                    }
                    while (pokedexDragAccumulator <= -rowThreshold) {
                        scrollPokedexBy(-1)
                        pokedexDragAccumulator += rowThreshold
                    }
                }
                if (rewardDragging && selectedTab == 5) {
                    if (kotlin.math.abs(y - rewardTouchStartY) > 12f) rewardTouchMoved = true
                    rewardDragAccumulator += lastTouchY - y
                    lastTouchY = y
                    while (rewardDragAccumulator >= 55f) {
                        scrollRewardsBy(1)
                        rewardDragAccumulator -= 55f
                    }
                    while (rewardDragAccumulator <= -55f) {
                        scrollRewardsBy(-1)
                        rewardDragAccumulator += 55f
                    }
                }
                if (questDragging && selectedTab == 4) {
                    if (kotlin.math.abs(y - questTouchStartY) > 12f) questTouchMoved = true
                    questDragAccumulator += lastTouchY - y
                    lastTouchY = y
                    while (questDragAccumulator >= 53f) {
                        scrollQuestsBy(1)
                        questDragAccumulator -= 53f
                    }
                    while (questDragAccumulator <= -53f) {
                        scrollQuestsBy(-1)
                        questDragAccumulator += 53f
                    }
                }
                if (bagDragging && selectedTab == 2) {
                    if (kotlin.math.abs(y - bagTouchStartY) > 12f) bagTouchMoved = true
                    bagDragAccumulator += lastTouchY - y
                    lastTouchY = y
                    while (bagDragAccumulator >= 95f) {
                        scrollBagBy(1)
                        bagDragAccumulator -= 95f
                    }
                    while (bagDragAccumulator <= -95f) {
                        scrollBagBy(-1)
                        bagDragAccumulator += 95f
                    }
                }
                return true
            }
            MotionEvent.ACTION_UP -> {
                val tab = tabBounds.indexOfFirst { it.contains(x, y) }
                if (tab >= 0 && tab == pressedTab) selectTab(tab)
                if (pressedArrow < 0 && leftArrowBounds.contains(x, y)) cycleTab(-1)
                if (pressedArrow > 0 && rightArrowBounds.contains(x, y)) cycleTab(1)
                if (selectedTab == 2 && x in 42f..284f && y in 254f..1019f) {
                    selectedPocket = ((y - 254f) / 85f).toInt().coerceIn(0, POCKETS.lastIndex)
                    selectedItem = 0
                    bagScrollRow = 0
                }
                when (selectedTab) {
                    1 -> if (x in 24f..1216f && y in 232f..1024f) {
                        val column = ((x - 24f) / 606f).toInt().coerceIn(0, 1)
                        val row = ((y - 232f) / 270f).toInt().coerceIn(0, 2)
                        focusIndex = row * 2 + column
                    }
                    2 -> if (!bagTouchMoved && x in 342f..1200f && y in 254f..1014f) {
                        val column = ((x - 342f) / 286f).toInt().coerceIn(0, 2)
                        val row = ((y - 254f) / 190f).toInt().coerceIn(0, 3)
                        val index = bagScrollRow * 3 + row * 3 + column
                        val visible = synchronized(frameLock) { visibleItems() }
                        if (index < visible.size && index == pressedBagItem) {
                            if (index == selectedItem) requestItemAction(visible[index])
                            else selectedItem = index
                        }
                    }
                    3 -> if (!pokedexTouchMoved && x in 42f..484f && y in 350f..1022f) {
                        val index = pokedexScroll + ((y - 350f) / 96f).toInt().coerceIn(0, 6)
                        if (index in pokedexInfo.indices) {
                            selectedPokedex = index
                            keepPokedexSelectionVisible()
                        }
                    }
                    4 -> if (!questTouchMoved && x in 42f..496f && y in 258f..1000f) {
                        val index = questScroll + ((y - 258f) / 106f).toInt().coerceIn(0, 6)
                        val count = synchronized(frameLock) { quests.size }
                        if (index < count) {
                            selectedQuest = index
                            keepQuestSelectionVisible()
                        }
                    }
                    5 -> if (!rewardTouchMoved && x in 382f..1187f && y in 250f..1013f) {
                        val index = rewardScroll + ((y - 250f) / 109f).toInt().coerceIn(0, 6)
                        if (index in RESEARCH_REWARDS.indices) {
                            selectedReward = index
                            keepRewardSelectionVisible()
                        }
                    }
                }
                pressedTab = -1
                pressedArrow = 0
                mapDragging = false
                bagDragging = false
                bagTouchMoved = false
                bagDragAccumulator = 0f
                pressedBagItem = -1
                pokedexDragging = false
                pokedexTouchMoved = false
                pokedexDragAccumulator = 0f
                questDragging = false
                questTouchMoved = false
                questDragAccumulator = 0f
                rewardDragging = false
                rewardTouchMoved = false
                rewardDragAccumulator = 0f
                lastPinchDistance = 0f
                invalidate()
                performClick()
                return true
            }
            MotionEvent.ACTION_CANCEL -> {
                pressedTab = -1
                pressedArrow = 0
                mapDragging = false
                bagDragging = false
                bagTouchMoved = false
                bagDragAccumulator = 0f
                pressedBagItem = -1
                pokedexDragging = false
                pokedexTouchMoved = false
                pokedexDragAccumulator = 0f
                questDragging = false
                questTouchMoved = false
                questDragAccumulator = 0f
                rewardDragging = false
                rewardTouchMoved = false
                rewardDragAccumulator = 0f
                lastPinchDistance = 0f
                invalidate()
                return true
            }
        }
        return super.onTouchEvent(event)
    }

    override fun onGenericMotionEvent(event: MotionEvent): Boolean {
        if ((selectedTab == 2 || selectedTab == 3 || selectedTab == 4 || selectedTab == 5) &&
            event.actionMasked == MotionEvent.ACTION_SCROLL) {
            val vertical = event.getAxisValue(MotionEvent.AXIS_VSCROLL)
            if (vertical != 0f) {
                when (selectedTab) {
                    2 -> scrollBagBy(if (vertical < 0f) 1 else -1)
                    3 -> scrollPokedexBy(if (vertical < 0f) 1 else -1)
                    4 -> scrollQuestsBy(if (vertical < 0f) 1 else -1)
                    5 -> scrollRewardsBy(if (vertical < 0f) 1 else -1)
                }
                return true
            }
        }
        return super.onGenericMotionEvent(event)
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        return handleControllerKey(keyCode) || super.onKeyDown(keyCode, event)
    }

    override fun onPlzaControllerKey(event: KeyEvent): Boolean {
        if (event.keyCode == KeyEvent.KEYCODE_BUTTON_MODE) {
            if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
                controllerActive = !controllerActive
                focusIndex = 0
                invalidate()
            }
            return true
        }
        val controllerReady = synchronized(frameLock) { status >= STATUS_WAITING_SAVE }
        if (!controllerActive || !controllerReady) return false
        if (event.action == KeyEvent.ACTION_UP) return isControllerNavigationKey(event.keyCode)
        if (event.action != KeyEvent.ACTION_DOWN) return false
        return handleControllerKey(event.keyCode)
    }

    override fun onPlzaControllerMotion(event: MotionEvent): Boolean {
        val controllerReady = synchronized(frameLock) { status >= STATUS_WAITING_SAVE }
        if (!controllerActive || !controllerReady || event.action != MotionEvent.ACTION_MOVE) {
            return false
        }
        if (event.source and InputDevice.SOURCE_JOYSTICK != InputDevice.SOURCE_JOYSTICK) {
            return false
        }
        val horizontal = event.getAxisValue(MotionEvent.AXIS_HAT_X).takeIf { abs(it) > .55f } ?: 0f
        val vertical = event.getAxisValue(MotionEvent.AXIS_HAT_Y).takeIf { abs(it) > .55f } ?: 0f
        if (horizontal == 0f && vertical == 0f) return false
        val now = SystemClock.uptimeMillis()
        if (now - controllerMotionAt < 170L) return true
        controllerMotionAt = now
        if (pendingActionItem != null) {
            if (abs(horizontal) >= abs(vertical)) {
                movePendingItemSelection(if (horizontal < 0f) -1 else 1, 0)
            } else {
                movePendingItemSelection(0, if (vertical < 0f) -1 else 1)
            }
            return true
        }
        if (abs(horizontal) >= abs(vertical)) {
            moveFocus(if (horizontal < 0f) -1 else 1, 0)
        } else {
            moveFocus(0, if (vertical < 0f) -1 else 1)
        }
        invalidate()
        return true
    }

    private fun handleControllerKey(keyCode: Int): Boolean {
        if (pendingActionItem != null) {
            when (keyCode) {
                KeyEvent.KEYCODE_DPAD_LEFT -> movePendingItemSelection(-1, 0)
                KeyEvent.KEYCODE_DPAD_RIGHT -> movePendingItemSelection(1, 0)
                KeyEvent.KEYCODE_DPAD_UP -> movePendingItemSelection(0, -1)
                KeyEvent.KEYCODE_DPAD_DOWN -> movePendingItemSelection(0, 1)
                KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_DPAD_CENTER -> confirmPendingItemAction()
                KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BACK -> cancelPendingItemAction()
                else -> return isControllerNavigationKey(keyCode)
            }
            invalidate()
            return true
        }
        when (keyCode) {
            KeyEvent.KEYCODE_BUTTON_L1 -> cycleTab(-1)
            KeyEvent.KEYCODE_BUTTON_R1 -> cycleTab(1)
            KeyEvent.KEYCODE_BUTTON_L2 -> when (selectedTab) {
                0 -> mapZoom = (mapZoom / 1.18f).coerceIn(.7f, 5f)
                2 -> cyclePocket(-1)
                else -> return false
            }
            KeyEvent.KEYCODE_BUTTON_R2 -> when (selectedTab) {
                0 -> mapZoom = (mapZoom * 1.18f).coerceIn(.7f, 5f)
                2 -> cyclePocket(1)
                else -> return false
            }
            KeyEvent.KEYCODE_BUTTON_X -> if (selectedTab == 0) {
                mapPanX = 0f
                mapPanY = 0f
                mapZoom = 1f
            } else return false
            KeyEvent.KEYCODE_DPAD_LEFT -> moveFocus(-1, 0)
            KeyEvent.KEYCODE_DPAD_RIGHT -> moveFocus(1, 0)
            KeyEvent.KEYCODE_DPAD_UP -> moveFocus(0, -1)
            KeyEvent.KEYCODE_DPAD_DOWN -> moveFocus(0, 1)
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_DPAD_CENTER -> confirmFocus()
            KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BACK -> {
                if (selectedTab != 0) selectTab(0) else controllerActive = false
            }
            else -> return false
        }
        invalidate()
        return true
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
        KeyEvent.KEYCODE_BUTTON_X,
        KeyEvent.KEYCODE_ENTER,
        KeyEvent.KEYCODE_DPAD_CENTER,
        KeyEvent.KEYCODE_BACK -> true
        else -> false
    }

    private fun selectTab(tab: Int) {
        selectedTab = tab.coerceIn(TABS.indices)
        focusIndex = 0
        pressedTab = -1
        invalidate()
    }

    private fun cycleTab(direction: Int) {
        selectTab((selectedTab + direction + TABS.size) % TABS.size)
    }

    private fun moveFocus(horizontal: Int, vertical: Int) {
        when (selectedTab) {
            1 -> {
                val column = (focusIndex % 2 + horizontal).coerceIn(0, 1)
                val row = (focusIndex / 2 + vertical).coerceIn(0, 2)
                focusIndex = row * 2 + column
            }
            2 -> {
                val visibleCount = synchronized(frameLock) { visibleItems().size }
                if (horizontal < 0 && selectedItem % 3 == 0) {
                    selectedPocket = (selectedPocket - 1).coerceAtLeast(0)
                    selectedItem = 0
                    bagScrollRow = 0
                } else if (horizontal > 0 || vertical != 0) {
                    selectedItem = (selectedItem + horizontal + vertical * 3).coerceIn(0, max(0, visibleCount - 1))
                    keepItemSelectionVisible(visibleCount)
                }
            }
            3 -> {
                selectedPokedex = (selectedPokedex + vertical).coerceIn(0, max(0, pokedexInfo.lastIndex))
                keepPokedexSelectionVisible()
            }
            4 -> {
                val questCount = synchronized(frameLock) { quests.size }
                selectedQuest = (selectedQuest + vertical).coerceIn(0, max(0, questCount - 1))
                keepQuestSelectionVisible()
            }
            5 -> {
                selectedReward = (selectedReward + vertical).coerceIn(RESEARCH_REWARDS.indices)
                keepRewardSelectionVisible()
            }
            0 -> {
                mapPanX -= horizontal * 48f
                mapPanY -= vertical * 48f
            }
        }
    }

    private fun confirmFocus() {
        val filteredItems = synchronized(frameLock) { visibleItems() }
        if (selectedTab == 2 && filteredItems.isNotEmpty()) {
            requestItemAction(filteredItems[selectedItem.coerceIn(filteredItems.indices)])
        }
    }
}
