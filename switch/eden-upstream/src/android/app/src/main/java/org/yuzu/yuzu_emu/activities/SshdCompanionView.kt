package org.yuzu.yuzu_emu.activities

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.os.Process
import android.os.SystemClock
import android.view.HapticFeedbackConstants
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.charset.Charset
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.TimeUnit
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.features.input.NativeInput
import org.yuzu.yuzu_emu.features.input.model.NativeAnalog
import org.yuzu.yuzu_emu.features.input.model.NativeButton
import org.yuzu.yuzu_emu.utils.Log

internal class SshdCompanionView(
    context: Context,
    private val onPlayerVisibilityChanged: (Boolean) -> Unit = {},
    private val onPlayerModelChanged: (Int) -> Unit = {},
    private val onMapVisibilityChanged: (Boolean) -> Unit = {},
    private val onMapAspectChanged: (Float) -> Unit = {}
) : View(context), SshdControllerTarget {
    private companion object {
        private const val DESIGN_WIDTH = 1240f
        private const val DESIGN_HEIGHT = 1080f
        private const val SNAPSHOT_SIZE = 2070
        private const val SNAPSHOT_VERSION = 5
        private const val SNAPSHOT_MAGIC = 0x44485353
        private const val MAP_IMAGE_MAGIC = 0x504D5353
        private const val MAP_IMAGE_HEADER_SIZE = 36
        private const val MAP_IMAGE_VERSION = 3
        private const val MAP_IMAGE_PIXEL_FORMAT_RGBA8888 = 1
        private const val MAX_MAP_IMAGE_BYTES = MAP_IMAGE_HEADER_SIZE + 2048 * 2048 * 4
        private const val MAP_IMAGE_WAIT_POLL_INTERVAL_MS = 180L
        private const val MAP_IMAGE_READY_POLL_INTERVAL_MS = 650L
        private const val SNAPSHOT_INTERVAL_MS = 100L
        private const val ACTIVE_STATUS = 4
        private const val ITEM_COUNT = 216
        private const val CAPABILITY_SAVE_STATE = 1 shl 0
        private const val CAPABILITY_FLAG_STATE = 1 shl 1
        private const val CAPABILITY_PLAYER_STATE = 1 shl 2
        private const val CAPABILITY_PROGRESS_STATE = 1 shl 3
        private const val CAPABILITY_PLAYER_TRANSFORM_STATE = 1 shl 4
        private const val CAPABILITY_SCENE_FLAG_STATE = 1 shl 5
        private const val CAPABILITY_DUNGEON_FLAG_STATE = 1 shl 6
        private const val CAPABILITY_LIFECYCLE_STATE = 1 shl 7
        private const val CAPABILITY_MAP_TITLE_STATE = 1 shl 8
        private const val CAPABILITY_MAP_TRANSFORM_STATE = 1 shl 9
        private const val CAPABILITY_MAP_MARKER_STATE = 1 shl 10
        private const val CAPABILITY_LINK_APPEARANCE_STATE = 1 shl 11
        private const val CAPABILITY_MAP_IMAGE_STATE = 1 shl 12
        private const val CAPABILITY_MAP_PLAYER_HEADING_STATE = 1 shl 13
        private const val CAPABILITY_ITEM_OWNERSHIP_STATE = 1 shl 14
        private const val CAPABILITY_PROGRESS_HISTORY_STATE = 1 shl 15
        private const val ACTION_EQUIP_B_WHEEL = 1
        private const val ACTION_SELECT_POUCH = 2
        private const val ACTION_EQUIP_SHIELD = 3
        private const val ACTION_SELECT_DOWSING = 4
        private const val INVENTORY_GRID_COLUMNS = 5
        private const val INVENTORY_GRID_ROWS = 4
        private const val INVENTORY_PAGE_SIZE = INVENTORY_GRID_COLUMNS * INVENTORY_GRID_ROWS
        private const val FULL_STAMINA_AMOUNT = 1_000_000L
        private const val SKYWARD_HOLD_DELAY_MS = 260L
        private const val SKYWARD_HOLD_MIN_DISTANCE = 118f

        // Skyward Sword's pause UI is warm parchment framed with carved brown-gold
        // ornament and a brighter tone of the same warm palette for selection. These are intentionally drawn
        // rather than stretching tr_kami: the source texture contains transparent bands
        // intended for the original BRLYT panes and produced the broken background seen
        // in the first companion build.
        private val COLOR_BACKGROUND_TOP = Color.rgb(79, 54, 28)
        private val COLOR_BACKGROUND_BOTTOM = Color.rgb(125, 94, 48)
        private val COLOR_SURFACE = Color.rgb(218, 197, 148)
        private val COLOR_SURFACE_RAISED = Color.rgb(235, 219, 176)
        private val COLOR_BORDER = Color.rgb(112, 78, 38)
        private val COLOR_PRIMARY = Color.rgb(151, 103, 39)
        private val COLOR_PRIMARY_DARK = Color.rgb(88, 56, 24)
        private val COLOR_PRIMARY_LIGHT = Color.rgb(229, 191, 105)
        private val COLOR_GOLD = Color.rgb(201, 151, 55)
        private val COLOR_TEXT = Color.rgb(55, 37, 20)
        private val COLOR_TEXT_SECONDARY = Color.rgb(99, 72, 40)
        private val COLOR_TEXT_MUTED = Color.rgb(145, 112, 68)
        private val COLOR_GREEN = Color.rgb(91, 142, 51)
        private val COLOR_RED = Color.rgb(184, 61, 43)

        private val PAGE_TITLES = arrayOf("Inventory", "Map", "Combat", "Progress")
        private val CATEGORY_TITLES = arrayOf("Pouch", "Items", "Quest Items", "Treasures", "Bugs")
        private val ITEM_IDS = intArrayOf(
            9, 10, 11, 12, 13, 14, 15, 16, 19, 20, 49, 52, 53, 56, 68, 71,
            75, 76, 77, 90, 91, 92, 99, 105, 108, 109, 110, 111, 137, 138,
            140, 199
        )
        private val QUEST_ITEM_IDS = intArrayOf(
            158, 159, 160, 48,
            5, 74, 95, 96, 97, 177, 178, 179, 180, 186, 187, 188, 189, 190,
            191, 192, 193, 197, 198
        )
        private val TREASURE_IDS = IntArray(16) { 161 + it }
        private val BUG_IDS = intArrayOf(143, 146, 149, 152, 142, 145, 148, 151, 141, 144, 147, 150)
        private val FLAG_CATEGORY_IDS = arrayOf(ITEM_IDS, QUEST_ITEM_IDS, TREASURE_IDS, BUG_IDS)
        private val SHIELD_MAX_DURABILITY = intArrayOf(16, 24, 32, 24, 36, 48, 12, 16, 20, 48)

        // SaveFile stores DowsingTarget::DowsingSlot, not the LYT icon index.
        private val DOWSING_SLOT_NAMES = arrayOf(
            "Story Target", "Rupees", "Quest Target", "Gratitude Crystals",
            "Hearts", "Goddess Cubes", "Look", "Treasure", "None"
        )
        private val DOWSING_SLOT_ICON_IDS = intArrayOf(19, 4, 15, 3, 1, 16, 9, 6, -1)
        private val DOWSING_MENU_SLOTS = intArrayOf(0, 7, 4, 5, 6, 3, 2, 1, 8)
        private val DOWSING_ICON_ASSETS = Array(22) { "sshd/dowsing/target_%02d.png".format(it) }
        // These IDs are the game's ITEMFLAGS enum, not an invented companion index. Every
        // mapping points to a verified matching original texture in MenuPause/DoButton.
        private val ITEM_ICON_ASSETS = mapOf(
            5 to "sshd/item/triforce.png",
            9 to "sshd/item/goddess_white_sword.png",
            10 to "sshd/item/practice_sword.png",
            11 to "sshd/item/goddess_sword.png",
            12 to "sshd/item/goddess_longsword.png",
            13 to "sshd/item/master_sword.png",
            14 to "sshd/item/true_master_sword.png",
            15 to "sshd/item/sailcloth.png",
            16 to "sshd/item/harp.png",
            19 to "sshd/item/bow.png",
            20 to "sshd/item/clawshots.png",
            25 to "sshd/item/ancient_cistern_boss_key.png",
            26 to "sshd/item/fire_sanctuary_boss_key.png",
            27 to "sshd/item/sandship_boss_key.png",
            28 to "sshd/item/key_piece.png",
            29 to "sshd/item/skyview_boss_key.png",
            30 to "sshd/item/earth_temple_boss_key.png",
            31 to "sshd/item/lanayru_mining_facility_boss_key.png",
            36 to "sshd/item/glittering_spores.png",
            48 to "sshd/item/gratitude_crystal.png",
            49 to "sshd/item/gust_bellows.png",
            52 to "sshd/item/slingshot.png",
            53 to "sshd/item/beetle.png",
            54 to "sshd/item/water.png",
            55 to "sshd/item/mushroom_spores.png",
            56 to "sshd/item/digging_mitts.png",
            65 to "sshd/item/guardian_potion.png",
            66 to "sshd/item/guardian_potion_plus.png",
            68 to "sshd/item/water_dragon_scale.png",
            70 to "sshd/item/bug_medal.png",
            71 to "sshd/item/bug_net.png",
            74 to "sshd/item/sacred_water.png",
            75 to "sshd/item/hook_beetle.png",
            76 to "sshd/item/quick_beetle.png",
            77 to "sshd/item/tough_beetle.png",
            78 to "sshd/item/heart_potion.png",
            79 to "sshd/item/heart_potion_plus.png",
            81 to "sshd/item/heart_potion_plus_plus.png",
            84 to "sshd/item/stamina_potion.png",
            85 to "sshd/item/stamina_potion_plus.png",
            86 to "sshd/item/air_potion.png",
            87 to "sshd/item/air_potion_plus.png",
            88 to "sshd/item/fairy_bottle.png",
            90 to "sshd/item/iron_bow.png",
            91 to "sshd/item/sacred_bow.png",
            92 to "sshd/item/bomb_bag.png",
            93 to "sshd/item/heart_container.png",
            94 to "sshd/item/heart_piece.png",
            95 to "sshd/item/triforce.png",
            96 to "sshd/item/triforce.png",
            97 to "sshd/item/triforce.png",
            99 to "sshd/item/mogma_mitts.png",
            100 to "sshd/item/heart_medal.png",
            101 to "sshd/item/rupee_medal.png",
            102 to "sshd/item/treasure_medal.png",
            103 to "sshd/item/potion_medal.png",
            104 to "sshd/item/cursed_medal.png",
            105 to "sshd/item/scattershot.png",
            108 to "sshd/item/wallet_b.png",
            109 to "sshd/item/wallet_c.png",
            110 to "sshd/item/wallet_d.png",
            111 to "sshd/item/wallet_e.png",
            114 to "sshd/item/life_medal.png",
            116 to "sshd/item/wooden_shield.png",
            117 to "sshd/item/banded_shield.png",
            118 to "sshd/item/braced_shield.png",
            119 to "sshd/item/iron_shield.png",
            120 to "sshd/item/reinforced_shield.png",
            121 to "sshd/item/fortified_shield.png",
            122 to "sshd/item/sacred_shield.png",
            123 to "sshd/item/divine_shield.png",
            124 to "sshd/item/goddess_shield.png",
            125 to "sshd/item/hylian_shield.png",
            126 to "sshd/item/revitalizing_potion.png",
            127 to "sshd/item/revitalizing_potion_plus.png",
            128 to "sshd/item/small_seed_satchel.png",
            129 to "sshd/item/medium_seed_satchel.png",
            130 to "sshd/item/large_seed_satchel.png",
            131 to "sshd/item/small_quiver.png",
            132 to "sshd/item/medium_quiver.png",
            133 to "sshd/item/large_quiver.png",
            134 to "sshd/item/small_bomb_bag.png",
            135 to "sshd/item/medium_bomb_bag.png",
            136 to "sshd/item/large_bomb_bag.png",
            137 to "sshd/item/whip.png",
            138 to "sshd/item/fireshield_earrings.png",
            140 to "sshd/item/big_bug_net.png",
            141 to "sshd/item/faron_grasshopper.png",
            142 to "sshd/item/woodland_rhino_beetle.png",
            143 to "sshd/item/deku_hornet.png",
            144 to "sshd/item/skyloft_mantis.png",
            145 to "sshd/item/volcanic_ladybug.png",
            146 to "sshd/item/blessed_butterfly.png",
            147 to "sshd/item/lanayru_ant.png",
            148 to "sshd/item/sand_cicada.png",
            149 to "sshd/item/gerudo_dragonfly.png",
            150 to "sshd/item/eldin_roller.png",
            151 to "sshd/item/sky_stag_beetle.png",
            152 to "sshd/item/starry_firefly.png",
            153 to "sshd/item/empty_bottle.png",
            154 to "sshd/item/rupee_medal.png",
            155 to "sshd/item/heart_medal.png",
            158 to "sshd/item/cawlin_letter.png",
            159 to "sshd/item/insect_cage.png",
            160 to "sshd/item/rattle.png",
            161 to "sshd/item/hornet_larvae.png",
            162 to "sshd/item/bird_feather.png",
            163 to "sshd/item/tumbleweed.png",
            164 to "sshd/item/lizard_tail.png",
            165 to "sshd/item/eldin_ore.png",
            166 to "sshd/item/ancient_flower.png",
            167 to "sshd/item/amber_relic.png",
            168 to "sshd/item/dusk_relic.png",
            169 to "sshd/item/jelly_blob.png",
            170 to "sshd/item/monster_claw.png",
            171 to "sshd/item/monster_horn.png",
            172 to "sshd/item/ornamental_skull.png",
            173 to "sshd/item/evil_crystal.png",
            174 to "sshd/item/blue_bird_feather.png",
            175 to "sshd/item/golden_skull.png",
            176 to "sshd/item/goddess_plume.png",
            177 to "sshd/item/emerald_tablet.png",
            178 to "sshd/item/ruby_tablet.png",
            179 to "sshd/item/amber_tablet.png",
            180 to "sshd/item/stone_of_trials.png",
            186 to "sshd/item/ballad_goddess.png",
            187 to "sshd/item/farores_courage.png",
            188 to "sshd/item/nayrus_wisdom.png",
            189 to "sshd/item/dins_power.png",
            190 to "sshd/item/faron_song_hero.png",
            191 to "sshd/item/eldin_song_hero.png",
            192 to "sshd/item/lanayru_song_hero.png",
            193 to "sshd/item/song_of_hero.png",
            194 to "sshd/item/revitalizing_potion_plus_plus.png",
            195 to "sshd/item/hot_pumpkin_soup.png",
            196 to "sshd/item/cold_pumpkin_soup.png",
            197 to "sshd/item/life_tree_seedling.png",
            198 to "sshd/item/life_tree_fruit.png",
            199 to "sshd/item/wallet_a.png",
            200 to "sshd/item/small_key.png",
            201 to "sshd/item/small_key.png",
            202 to "sshd/item/small_key.png",
            203 to "sshd/item/small_key.png",
            204 to "sshd/item/small_key.png",
            205 to "sshd/item/small_key.png",
            206 to "sshd/item/small_key.png",
            207 to "sshd/item/dungeon_map.png",
            208 to "sshd/item/dungeon_map.png",
            209 to "sshd/item/dungeon_map.png",
            210 to "sshd/item/dungeon_map.png",
            211 to "sshd/item/dungeon_map.png",
            212 to "sshd/item/dungeon_map.png",
            213 to "sshd/item/dungeon_map.png"
        )
    }

    private enum class Page {
        Inventory,
        Map,
        Combat,
        Progress
    }

    private data class CombatPoint(val x: Float, val y: Float, val time: Long)

    private enum class CombatDirection(val x: Float, val y: Float, val displayName: String) {
        Right(1f, 0f, "right"),
        UpRight(.70710677f, .70710677f, "up-right"),
        Up(0f, 1f, "up"),
        UpLeft(-.70710677f, .70710677f, "up-left"),
        Left(-1f, 0f, "left"),
        DownLeft(-.70710677f, -.70710677f, "down-left"),
        Down(0f, -1f, "down"),
        DownRight(.70710677f, -.70710677f, "down-right")
    }

    private enum class InventoryFocus {
        Grid,
        Status,
        Shield,
        BItem,
        Selected,
        Dowsing
    }

    private data class MapMarker(
        val originalCommandIndex: Int,
        val paneType: Int,
        val flags: Int,
        val x: Float,
        val y: Float,
        val commandRotation: Int,
        val saveSubtype: Int,
        val pass: Int
    )

    private data class Snapshot(
        val status: Int = 0,
        val capabilities: Int = 0,
        val currentHealth: Int = 0,
        val healthCapacity: Int = 0,
        val stamina: Long = 0,
        val rupees: Int = 0,
        val seeds: Int = 0,
        val arrows: Int = 0,
        val bombs: Int = 0,
        val gratitude: Int = 0,
        val keyPieces: Int = 0,
        val heartPieces: Int = 0,
        val pouchExpansions: Int = 0,
        val extraWallets: Int = 0,
        val summaryId: Int = -1,
        val objectiveId: Int = -1,
        val equippedBItem: Int = 0xFF,
        val selectedPouchSlot: Int = 0xFF,
        val shieldPouchSlot: Int = 0xFF,
        val selectedDowsingSlot: Int = 0xFF,
        val room: Int = 0xFF,
        val layer: Int = 0xFF,
        val entrance: Int = 0xFF,
        val night: Boolean = false,
        val trial: Int = 0xFF,
        val newFile: Boolean = false,
        val potionTimers: IntArray = IntArray(6),
        val stage: String = "",
        val area: String = "",
        val pouch: IntArray = IntArray(8) { -1 },
        val itemCheck: IntArray = IntArray(60) { -1 },
        val itemValues: IntArray = IntArray(ITEM_COUNT),
        val playerX: Float = Float.NaN,
        val playerY: Float = Float.NaN,
        val playerZ: Float = Float.NaN,
        val playerYawRadians: Float = Float.NaN,
        val playerAction: Long = 0xFFFFFFFFL,
        val lifecycleEpoch: Long = 0,
        val sceneIndex: Int = -1,
        val dungeonIndex: Int = -1,
        val playerRoom: Int = 0xFF,
        val sceneFlags: IntArray = IntArray(8),
        val dungeonFlags: IntArray = IntArray(8),
        val mapTitleId: Int = -1,
        val stifArea: Int = 0xFF,
        val stifField: Int = 0xFF,
        val stifRoomType: Int = 0xFF,
        val mapFreshness: Int = 0,
        val mapAlpha: Int = 0,
        val linkOutfit: Int = 0xFF,
        val currentMapMode: Int = -1,
        val nextMapMode: Int = -1,
        val mapFloor: Int = Int.MIN_VALUE,
        val mapRotation: Int = 0,
        val mapScroll: FloatArray = FloatArray(3) { Float.NaN },
        val mapRotationCenter: FloatArray = FloatArray(3) { Float.NaN },
        val mapScale1: Float = Float.NaN,
        val mapScale2: Float = Float.NaN,
        val mapAspectX: Float = Float.NaN,
        val playerMapX: Float = Float.NaN,
        val playerMapY: Float = Float.NaN,
        val playerHeadingRadians: Float = Float.NaN,
        val mapMarkers: List<MapMarker> = emptyList(),
        val itemOwnedBits: ByteArray = ByteArray(27),
        val completedQuestIds: IntArray = IntArray(0)
    )

    private data class Assets(
        val itemCell: Bitmap? = null,
        val selectedCell: Bitmap? = null,
        val heart: Bitmap? = null,
        val fiIcon: Bitmap? = null,
        val purse: Bitmap? = null,
        val itemIcons: Map<Int, Bitmap?> = emptyMap(),
        val dowsingIcons: Map<Int, Bitmap?> = emptyMap(),
        val mapImages: Map<String, Bitmap?> = emptyMap(),
        val itemText: Map<String, String> = emptyMap(),
        val fiText: Map<String, String> = emptyMap()
    )

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val linePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 2f
    }
    private val regular = Typeface.create("serif", Typeface.NORMAL)
    private val bold = Typeface.create("serif", Typeface.BOLD)
    private val backgroundGradient = LinearGradient(
        0f,
        0f,
        DESIGN_WIDTH,
        DESIGN_HEIGHT,
        COLOR_BACKGROUND_TOP,
        COLOR_BACKGROUND_BOTTOM,
        Shader.TileMode.CLAMP
    )
    private val snapshotBuffer = ByteBuffer.allocateDirect(SNAPSHOT_SIZE).order(ByteOrder.LITTLE_ENDIAN)
    private var mapImageBuffer = ByteBuffer.allocateDirect(MAP_IMAGE_HEADER_SIZE)
        .order(ByteOrder.LITTLE_ENDIAN)
    private val mapBitmapLock = Any()
    @Volatile private var mapGpuAvailable = false
    @Volatile private var mapGpuReady = false
    @Volatile private var mapDisplayAspect = 1f
    private var mapBitmapSequence = 0L
    private var mapBitmapStage = ""
    private var mapBitmapFloor = Int.MIN_VALUE
    private var mapBitmapLifecycleEpoch = 0L
    private var mapCaptureStatus = "Waiting for the live game map"
    private var lastMapImagePollAt = 0L
    private var mapViewScale = 1f
    private var mapViewOffsetX = 0f
    private var mapViewOffsetY = 0f
    private var mapTouchCaptured = false
    private var mapTouchMoved = false
    private var mapLastTouchX = 0f
    private var mapLastTouchY = 0f
    private var mapLastPinchDistance = 0f
    private val completedQuestBounds = RectF(32f, 500f, 490f, 1048f)
    private var completedQuestScroll = 0f
    private var completedQuestScrollMax = 0f
    private var completedQuestTouchCaptured = false
    private var completedQuestLastY = 0f
    private val tabBounds = Array(Page.entries.size) { index ->
        val gap = 8f
        val tabWidth = (DESIGN_WIDTH - 24f - gap * (Page.entries.size - 1)) / Page.entries.size
        val left = 12f + index * (tabWidth + gap)
        RectF(left, 12f, left + tabWidth, 82f)
    }
    private val categoryBounds = Array(5) { index ->
        RectF(32f + index * 131f, 106f, 155f + index * 131f, 166f)
    }
    private val inventoryPreviousBounds = RectF(588f, 620f, 634f, 646f)
    private val inventoryNextBounds = RectF(642f, 620f, 688f, 646f)
    private val inventoryDetailBounds = RectF(32f, 654f, 694f, 1000f)
    private val inventoryStatusBounds = RectF(742f, 108f, 1208f, 324f)
    private val shieldEquipmentBounds = RectF(1054f, 360f, 1208f, 514f)
    private val bItemEquipmentBounds = RectF(1054f, 522f, 1208f, 676f)
    private val selectedEquipmentBounds = RectF(1054f, 684f, 1208f, 838f)
    private val dowsingEquipmentBounds = RectF(1054f, 846f, 1208f, 1000f)
    private val combatPadBounds = RectF(32f, 160f, 882f, 1050f)
    private val combatStabBounds = RectF(930f, 176f, 1206f, 288f)
    private val combatShieldBounds = RectF(930f, 296f, 1206f, 408f)
    private val combatHorizontalSpinBounds = RectF(930f, 416f, 1206f, 528f)
    private val combatVerticalSpinBounds = RectF(930f, 536f, 1206f, 648f)
    private val combatFinalBlowBounds = RectF(930f, 656f, 1206f, 768f)
    private val combatJumpSlashBounds = RectF(930f, 776f, 1206f, 888f)
    private val combatSkywardBounds = RectF(930f, 896f, 1206f, 1008f)
    private val mapContentBounds = RectF(12f, 92f, 1228f, 1068f)
    private var poller: ScheduledExecutorService? = null
    private var assetExecutor: ExecutorService? = null
    private val requestedItemIcons = ConcurrentHashMap.newKeySet<Int>()
    private val requestedDowsingIcons = ConcurrentHashMap.newKeySet<Int>()
    @Volatile private var snapshot = Snapshot()
    @Volatile private var assets = Assets()
    private var stateHash = 0
    @Volatile private var page = Page.Inventory
    private var category = 0
    private var selectedIndex = 0
    private var inventoryFocus = InventoryFocus.Grid
    private var playerVisible = false
    private var playerOutfit = -1
    private var controllerActive = false
    private var controllerMotionAt = 0L
    private var downX = 0f
    private var downY = 0f
    private var pageChangedAt = 0L
    private var actionFeedback = ""
    private var actionFeedbackAt = 0L
    private var itemInputGeneration = 0
    private var itemInputPending = false
    private val combatPath = mutableListOf<CombatPoint>()
    private var combatTouchCaptured = false
    private var combatInputGeneration = 0
    private var skywardChargeHeld = false
    private var skywardTouchChargeHeld = false
    private var skywardHoldCandidate = false
    private var skywardHoldGeneration = 0
    private var skywardHoldAnchorX = 0f
    private var skywardHoldAnchorY = 0f
    private var combatLatestX = 0f
    private var combatLatestY = 0f
    private var skywardTouchChargeStartedAt = 0L

    init {
        setBackgroundColor(COLOR_BACKGROUND_TOP)
        isClickable = true
        isFocusable = true
        isFocusableInTouchMode = true
        keepScreenOn = true
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        if (poller != null) return
        SshdControllerBridge.attach(this)
        assetExecutor = Executors.newSingleThreadExecutor { runnable ->
            Thread(
                {
                    Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND)
                    runnable.run()
                },
                "SSHD-RomFS-Assets"
            ).apply { isDaemon = true }
        }.also { executor ->
            executor.execute {
                runCatching { loadAssets() }
                    .onFailure { Log.error("[SSHD Companion] Asset load failed: ${it.message}") }
            }
        }
        poller = Executors.newSingleThreadScheduledExecutor { runnable ->
            Thread(
                {
                    Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND)
                    runnable.run()
                },
                "SSHD-Companion-Telemetry"
            ).apply { isDaemon = true }
        }.also { executor ->
            executor.scheduleWithFixedDelay(
                {
                    runCatching { pollSnapshot() }
                        .onFailure {
                            Log.error("[SSHD Companion] Snapshot poll failed: ${it.message}")
                        }
                },
                0,
                SNAPSHOT_INTERVAL_MS,
                TimeUnit.MILLISECONDS
            )
        }
        Log.info("[SSHD Companion] Bottom-screen view attached")
    }

    override fun onDetachedFromWindow() {
        SshdControllerBridge.detach(this)
        cancelItemInput()
        releaseCombatControls()
        setPlayerVisible(false)
        onMapVisibilityChanged(false)
        playerOutfit = -1
        poller?.shutdownNow()
        poller = null
        assetExecutor?.shutdownNow()
        assetExecutor = null
        val bitmaps = buildList {
            add(assets.itemCell)
            add(assets.selectedCell)
            add(assets.heart)
            add(assets.fiIcon)
            add(assets.purse)
            addAll(assets.itemIcons.values)
            addAll(assets.dowsingIcons.values)
            addAll(assets.mapImages.values)
        }
        bitmaps.filterNotNull().distinct().forEach { it.recycle() }
        assets = Assets()
        clearMapImage("Map page closed")
        requestedItemIcons.clear()
        requestedDowsingIcons.clear()
        Log.info("[SSHD Companion] Bottom-screen view detached")
        super.onDetachedFromWindow()
    }

    override fun onWindowVisibilityChanged(visibility: Int) {
        super.onWindowVisibilityChanged(visibility)
        if (visibility != VISIBLE) {
            releaseCombatControls()
            cancelItemInput()
        }
    }

    private fun loadAssets() {
        fun bitmap(path: String): Bitmap? {
            val bytes = NativeLibrary.getDualScreenRomfsAsset(path) ?: return null
            return BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
        }

        fun messages(path: String): Map<String, String> {
            val bytes = NativeLibrary.getDualScreenRomfsAsset(path) ?: return emptyMap()
            return parseMsbt(bytes)
        }

        val decodedIcons = mutableMapOf<String, Bitmap?>()
        fun icon(path: String): Bitmap? = decodedIcons.getOrPut(path) { bitmap(path) }
        assets = Assets(
            itemCell = bitmap("sshd/ui/item_cell.png"),
            selectedCell = bitmap("sshd/ui/item_cell_selected.png"),
            heart = bitmap("sshd/ui/heart.png"),
            fiIcon = icon("sshd/ui/fi.png"),
            purse = icon("sshd/ui/rupee_purse.png"),
            itemText = messages("sshd/raw/items.msbt"),
            fiText = messages("sshd/raw/fi.msbt") +
                messages("sshd/raw/fi_objectives.msbt")
        )
        postInvalidateOnAnimation()
        queueSnapshotIcons(snapshot)
        Log.info(
            "[SSHD Companion] Loaded original assets: " +
                "items=${assets.itemText.size} fi=${assets.fiText.size}"
        )
    }

    private fun dowsingIconId(slot: Int): Int? = DOWSING_SLOT_ICON_IDS
        .getOrNull(slot)
        ?.takeIf { it in DOWSING_ICON_ASSETS.indices }

    private fun dowsingSlotName(slot: Int): String =
        DOWSING_SLOT_NAMES.getOrNull(slot) ?: "Unavailable"

    private fun nextDowsingSlot(current: Int): Int {
        val index = DOWSING_MENU_SLOTS.indexOf(current)
        return DOWSING_MENU_SLOTS[(index + 1).coerceAtLeast(0) % DOWSING_MENU_SLOTS.size]
    }

    private fun queueSnapshotIcons(frame: Snapshot) {
        val itemIds = buildSet {
            if (frame.capabilities and CAPABILITY_FLAG_STATE != 0) {
                FLAG_CATEGORY_IDS.getOrNull(category - 1)?.forEach { id ->
                    if (isItemOwned(frame, id)) add(id)
                }
                equippedBItemId(frame)?.let { add(it) }
            }
            frame.pouch.forEach { packed ->
                packedItemId(packed)
                    .takeIf { it in 0 until ITEM_COUNT }
                    ?.let { add(it) }
            }
        }.filter { it in ITEM_ICON_ASSETS && requestedItemIcons.add(it) }
        val dowsingId = dowsingIconId(frame.selectedDowsingSlot)
            ?.takeIf { requestedDowsingIcons.add(it) }
        if (itemIds.isEmpty() && dowsingId == null) return
        assetExecutor?.execute {
            val decodedPaths = mutableMapOf<String, Bitmap?>()
            val itemIcons = itemIds.associateWith { id ->
                ITEM_ICON_ASSETS[id]?.let { path ->
                    decodedPaths.getOrPut(path) {
                        NativeLibrary.getDualScreenRomfsAsset(path)?.let { bytes ->
                            BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
                        }
                    }
                }
            }
            val dowsingIcons = if (dowsingId != null) {
                val path = DOWSING_ICON_ASSETS[dowsingId]
                val bitmap = NativeLibrary.getDualScreenRomfsAsset(path)?.let { bytes ->
                    BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
                }
                mapOf(dowsingId to bitmap)
            } else {
                emptyMap()
            }
            if (!isAttachedToWindow) {
                (itemIcons.values + dowsingIcons.values).filterNotNull().forEach { it.recycle() }
                return@execute
            }
            val current = assets
            assets = current.copy(
                itemIcons = current.itemIcons + itemIcons,
                dowsingIcons = current.dowsingIcons + dowsingIcons
            )
            postInvalidateOnAnimation()
        }
    }

    private fun pollSnapshot() {
        snapshotBuffer.clear()
        if (NativeLibrary.fillSshdCompanionSnapshot(snapshotBuffer) != SNAPSHOT_SIZE) return
        if (snapshotBuffer.getInt(0) != SNAPSHOT_MAGIC ||
            snapshotBuffer.getShort(4).toInt() != SNAPSHOT_VERSION ||
            snapshotBuffer.getShort(6).toInt() != SNAPSHOT_SIZE
        ) {
            return
        }
        var nextHash = 17
        for (offset in 12 until SNAPSHOT_SIZE) {
            nextHash = 31 * nextHash + snapshotBuffer.get(offset)
        }
        if (nextHash == stateHash) {
            pollMapImage(snapshot)
            return
        }
        stateHash = nextHash
        val previousLifecycleEpoch = snapshot.lifecycleEpoch
        val capabilities = snapshotBuffer.getInt(16)
        val mapFreshness = snapshotBuffer.u8(884)
        val mapMarkerCount = if (capabilities and CAPABILITY_MAP_MARKER_STATE != 0 &&
            capabilities and CAPABILITY_MAP_IMAGE_STATE != 0 && mapFreshness == 2
        ) {
            snapshotBuffer.u8(885).coerceAtMost(64)
        } else {
            0
        }
        val mapMarkers = List(mapMarkerCount) { index ->
            val offset = 952 + index * 16
            MapMarker(
                originalCommandIndex = snapshotBuffer.u16(offset),
                paneType = snapshotBuffer.u8(offset + 2),
                flags = snapshotBuffer.u8(offset + 3),
                x = snapshotBuffer.getFloat(offset + 4),
                y = snapshotBuffer.getFloat(offset + 8),
                commandRotation = snapshotBuffer.getShort(offset + 12).toInt(),
                saveSubtype = snapshotBuffer.u8(offset + 14),
                pass = snapshotBuffer.u8(offset + 15)
            )
        }
        val next = Snapshot(
            status = snapshotBuffer.getInt(12),
            capabilities = capabilities,
            currentHealth = snapshotBuffer.u16(20),
            healthCapacity = snapshotBuffer.u16(22),
            stamina = snapshotBuffer.getInt(24).toLong() and 0xFFFFFFFFL,
            rupees = snapshotBuffer.u16(28),
            seeds = snapshotBuffer.u16(30),
            arrows = snapshotBuffer.u16(32),
            bombs = snapshotBuffer.u16(34),
            gratitude = snapshotBuffer.u16(36),
            keyPieces = snapshotBuffer.u16(38),
            heartPieces = snapshotBuffer.u16(40),
            pouchExpansions = snapshotBuffer.u16(42),
            extraWallets = snapshotBuffer.u16(44),
            summaryId = snapshotBuffer.optionalU16(46),
            objectiveId = snapshotBuffer.optionalU16(48),
            equippedBItem = snapshotBuffer.u8(50),
            selectedPouchSlot = snapshotBuffer.u8(51),
            shieldPouchSlot = snapshotBuffer.u8(52),
            selectedDowsingSlot = snapshotBuffer.u8(53),
            room = snapshotBuffer.u8(54),
            layer = snapshotBuffer.u8(55),
            entrance = snapshotBuffer.u8(56),
            night = snapshotBuffer.u8(57) != 0,
            trial = snapshotBuffer.u8(58),
            newFile = snapshotBuffer.u8(59) != 0,
            potionTimers = IntArray(6) { snapshotBuffer.u16(60 + it * 2) },
            stage = snapshotBuffer.ascii(72, 8),
            area = snapshotBuffer.ascii(80, 32),
            pouch = IntArray(8) { snapshotBuffer.getInt(112 + it * 4) },
            itemCheck = IntArray(60) { snapshotBuffer.getInt(144 + it * 4) },
            itemValues = IntArray(ITEM_COUNT) { snapshotBuffer.u16(384 + it * 2) },
            playerX = snapshotBuffer.getFloat(816),
            playerY = snapshotBuffer.getFloat(820),
            playerZ = snapshotBuffer.getFloat(824),
            playerYawRadians = snapshotBuffer.getFloat(828),
            playerAction = snapshotBuffer.getInt(832).toLong() and 0xFFFFFFFFL,
            lifecycleEpoch = snapshotBuffer.getInt(836).toLong() and 0xFFFFFFFFL,
            sceneIndex = snapshotBuffer.getShort(840).toInt(),
            dungeonIndex = snapshotBuffer.getShort(842).toInt(),
            playerRoom = snapshotBuffer.u8(844),
            sceneFlags = IntArray(8) { snapshotBuffer.u16(848 + it * 2) },
            dungeonFlags = IntArray(8) { snapshotBuffer.u16(864 + it * 2) },
            mapTitleId = snapshotBuffer.get(880).toInt(),
            stifArea = snapshotBuffer.u8(881),
            stifField = snapshotBuffer.u8(882),
            stifRoomType = snapshotBuffer.u8(883),
            mapFreshness = mapFreshness,
            mapAlpha = snapshotBuffer.u8(886),
            linkOutfit = snapshotBuffer.u8(887),
            currentMapMode = snapshotBuffer.getInt(888),
            nextMapMode = snapshotBuffer.getInt(892),
            mapFloor = snapshotBuffer.getInt(896),
            mapRotation = snapshotBuffer.getShort(900).toInt(),
            mapScroll = FloatArray(3) { snapshotBuffer.getFloat(904 + it * 4) },
            mapRotationCenter = FloatArray(3) { snapshotBuffer.getFloat(916 + it * 4) },
            mapScale1 = snapshotBuffer.getFloat(928),
            mapScale2 = snapshotBuffer.getFloat(932),
            mapAspectX = snapshotBuffer.getFloat(936),
            playerMapX = snapshotBuffer.getFloat(940),
            playerMapY = snapshotBuffer.getFloat(944),
            playerHeadingRadians = snapshotBuffer.getFloat(948),
            mapMarkers = mapMarkers,
            itemOwnedBits = ByteArray(27) { snapshotBuffer.get(1976 + it) },
            completedQuestIds = IntArray(
                (snapshotBuffer.get(2003).toInt() and 0xFF).coerceAtMost(33)
            ) { index -> snapshotBuffer.u16(2004 + index * 2) }
        )
        snapshot = next
        val lifecycleChanged =
            next.capabilities and CAPABILITY_LIFECYCLE_STATE != 0 &&
                next.lifecycleEpoch != previousLifecycleEpoch
        if (next.status != ACTIVE_STATUS || next.lifecycleEpoch != previousLifecycleEpoch ||
            next.stage != mapBitmapStage && mapBitmapStage.isNotEmpty()
        ) {
            clearMapImage(
                if (next.status == ACTIVE_STATUS) "Synchronizing the active area's map" else
                    "Waiting for a playable save"
            )
        }
        if (next.status != ACTIVE_STATUS || next.currentHealth == 0 ||
            next.lifecycleEpoch != previousLifecycleEpoch
        ) {
            post { releaseCombatControls(); cancelItemInput() }
        }
        post {
            if (lifecycleChanged) {
                category = 0
                selectedIndex = 0
                inventoryFocus = InventoryFocus.Grid
                actionFeedback = ""
                actionFeedbackAt = 0L
                resetMapView()
            } else {
                selectedIndex = when (page) {
                    Page.Inventory -> selectedIndex.coerceIn(
                        0,
                        (inventoryItems().size - 1).coerceAtLeast(0)
                    )
                    Page.Map, Page.Combat, Page.Progress -> 0
                }
            }
            queueSnapshotIcons(next)
            updatePlayerPresentation()
            invalidate()
        }
        pollMapImage(next)
    }

    private fun clearMapImage(status: String) {
        synchronized(mapBitmapLock) {
            mapGpuAvailable = false
            mapGpuReady = false
            mapBitmapSequence = 0L
            mapBitmapStage = ""
            mapBitmapFloor = Int.MIN_VALUE
            mapBitmapLifecycleEpoch = 0L
            mapCaptureStatus = status
        }
    }

    private fun pollMapImage(frame: Snapshot) {
        if (page != Page.Map || frame.status != ACTIVE_STATUS || frame.stage.isBlank()) return
        val now = SystemClock.uptimeMillis()
        if (now - lastMapImagePollAt < SNAPSHOT_INTERVAL_MS) return
        lastMapImagePollAt = now
        mapImageBuffer.clear()
        val result = NativeLibrary.copySshdCompanionMapImage(mapImageBuffer)
        // A busy metadata seqlock is not a destroyed GPU resource. Keep the
        // last frame for this same stage/epoch while the writer completes.
        if (result == -6) return
        if (result != MAP_IMAGE_HEADER_SIZE || mapImageBuffer.getInt(0) != MAP_IMAGE_MAGIC ||
            mapImageBuffer.u16(4) != MAP_IMAGE_VERSION || mapImageBuffer.u16(6) != MAP_IMAGE_HEADER_SIZE
        ) {
            clearMapImage(when (result) {
                -2 -> "Waiting for the current area"
                -5 -> "Waiting for the game's local map initialization"
                else -> "Waiting for the game map renderer"
            })
            post { updateMapPresentation(); invalidate() }
            return
        }
        val stage = mapImageBuffer.ascii(28, 8)
        val nativeAspect = mapImageBuffer.getFloat(16)
        if (stage != frame.stage || frame.lifecycleEpoch != snapshot.lifecycleEpoch ||
            !nativeAspect.isFinite() || nativeAspect <= 0f ||
            mapImageBuffer.u16(12) == 0 || mapImageBuffer.u16(14) == 0
        ) {
            clearMapImage("Waiting for the new area's map")
            post { updateMapPresentation(); invalidate() }
            return
        }
        synchronized(mapBitmapLock) {
            mapGpuAvailable = true
            mapDisplayAspect = nativeAspect
            mapGpuReady = mapImageBuffer.u16(26) and 1 != 0
            mapBitmapStage = stage
            mapBitmapFloor = mapImageBuffer.get(25).toInt()
            mapBitmapLifecycleEpoch = frame.lifecycleEpoch
            mapCaptureStatus = if (mapGpuReady) "Game map texture connected" else "Connecting the game map texture"
        }
        // Resource readiness changes independently of the inventory snapshot
        // hash. Publish visibility here, including the first cold map frame.
        post { updateMapPresentation(); invalidate() }
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val scale = min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT)
        val offsetX = (width - DESIGN_WIDTH * scale) * .5f
        val offsetY = (height - DESIGN_HEIGHT * scale) * .5f
        canvas.save()
        canvas.translate(offsetX, offsetY)
        canvas.scale(scale, scale)
        if (snapshot.status != ACTIVE_STATUS) {
            canvas.drawColor(Color.rgb(40, 28, 17))
            drawText(
                canvas,
                "Waiting for save",
                DESIGN_WIDTH * .5f,
                DESIGN_HEIGHT * .5f + 12f,
                32f,
                true,
                Paint.Align.CENTER,
                Color.rgb(239, 220, 174)
            )
            canvas.restore()
            return
        }
        drawBackground(canvas)
        drawTabs(canvas)
        updatePlayerPresentation()
        val transition = pageTransitionProgress()
        canvas.save()
        canvas.translate((1f - transition) * 34f, 0f)
        when (page) {
            Page.Inventory -> drawInventory(canvas)
            Page.Map -> drawMap(canvas)
            Page.Combat -> drawCombat(canvas)
            Page.Progress -> drawProgress(canvas)
        }
        canvas.restore()
        canvas.restore()
        if (transition < 1f) postInvalidateOnAnimation()
    }

    private fun drawBackground(canvas: Canvas) {
        canvas.drawColor(COLOR_BACKGROUND_TOP)
        paint.alpha = 255
        paint.shader = backgroundGradient
        canvas.drawRect(5f, 5f, 1235f, 1075f, paint)
        paint.shader = null

        // Subtle, deterministic fibers keep the parchment alive without allocating a bitmap
        // or introducing frame-to-frame shimmer.
        for (row in 0..28) {
            val y = 12f + row * 37f
            paint.color = if (row % 3 == 0) Color.argb(16, 255, 236, 182)
                else Color.argb(13, 65, 40, 19)
            canvas.drawRect(12f + (row % 4) * 7f, y, 1228f - (row % 5) * 9f, y + 1f, paint)
        }
        linePaint.color = Color.rgb(59, 38, 18)
        linePaint.strokeWidth = 8f
        canvas.drawRoundRect(RectF(8f, 8f, 1232f, 1072f), 7f, 7f, linePaint)
        linePaint.color = COLOR_GOLD
        linePaint.strokeWidth = 2f
        canvas.drawRoundRect(RectF(15f, 15f, 1225f, 1065f), 5f, 5f, linePaint)
    }

    private fun drawTabs(canvas: Canvas) {
        tabBounds.forEachIndexed { index, bounds ->
            val active = page.ordinal == index
            paint.color = if (active) COLOR_PRIMARY else COLOR_SURFACE_RAISED
            canvas.drawRoundRect(bounds, 4f, 4f, paint)
            linePaint.color = if (active) COLOR_GOLD else Color.rgb(128, 93, 48)
            linePaint.strokeWidth = if (active) 3f else 1f
            canvas.drawRoundRect(bounds, 4f, 4f, linePaint)
            paint.textSize = 28f
            paint.typeface = bold
            val baseline = bounds.centerY() - (paint.ascent() + paint.descent()) * .5f
            drawText(
                canvas,
                PAGE_TITLES[index],
                bounds.centerX(),
                baseline,
                28f,
                true,
                Paint.Align.CENTER,
                color = if (active) Color.rgb(248, 231, 187) else COLOR_TEXT
            )
        }
    }

    private fun drawInventory(canvas: Canvas) {
        panel(canvas, RectF(12f, 92f, 714f, 1068f), 112)
        panel(canvas, RectF(722f, 92f, 1228f, 1068f), 112)
        categoryBounds.forEachIndexed { index, bounds ->
            paint.color = if (category == index) COLOR_PRIMARY else COLOR_SURFACE_RAISED
            canvas.drawRoundRect(bounds, 6f, 6f, paint)
            linePaint.color = if (category == index) COLOR_GOLD else COLOR_BORDER
            linePaint.strokeWidth = if (category == index) 2f else 1f
            canvas.drawRoundRect(bounds, 6f, 6f, linePaint)
            drawText(
                canvas,
                CATEGORY_TITLES[index],
                bounds.centerX(),
                bounds.top + 27f,
                16f,
                true,
                Paint.Align.CENTER,
                if (category == index) Color.rgb(248, 231, 187) else COLOR_TEXT_SECONDARY
            )
            val categoryCount = if (index == 0 && hasCapability(CAPABILITY_SAVE_STATE)) {
                snapshot.pouch.count { packedItemId(it) in 0 until ITEM_COUNT }.toString()
            } else if (index > 0 && hasCapability(CAPABILITY_FLAG_STATE)) {
                FLAG_CATEGORY_IDS[index - 1].count {
                    it in ITEM_ICON_ASSETS && isItemOwned(snapshot, it)
                }.toString()
            } else {
                "--"
            }
            drawText(
                canvas,
                "$categoryCount shown",
                bounds.centerX(),
                bounds.bottom - 8f,
                12f,
                false,
                Paint.Align.CENTER,
                if (category == index) Color.rgb(231, 208, 153) else COLOR_TEXT_MUTED
            )
        }
        val items = inventoryItems()
        if (category > 0 && !hasCapability(CAPABILITY_FLAG_STATE)) {
            drawText(
                canvas,
                "Live item flags are temporarily unavailable",
                363f,
                450f,
                25f,
                false,
                Paint.Align.CENTER,
                COLOR_TEXT_SECONDARY
            )
        }
        val pageOffset = inventoryPageOffset(items.size)
        val visible = items.drop(pageOffset).take(INVENTORY_PAGE_SIZE)
        visible.forEachIndexed { index, id ->
            val absoluteIndex = pageOffset + index
            val column = index % INVENTORY_GRID_COLUMNS
            val row = index / INVENTORY_GRID_COLUMNS
            val bounds = RectF(
                32f + column * 132f,
                178f + row * 109.5f,
                156f + column * 132f,
                279.5f + row * 109.5f
            )
            if (category == 0) {
                drawPackedSlot(
                    canvas,
                    bounds,
                    snapshot.pouch[absoluteIndex],
                    absoluteIndex == selectedIndex && inventoryFocus == InventoryFocus.Grid
                )
                drawPouchEquippedIndicators(canvas, bounds, absoluteIndex)
            } else {
                drawItemSlot(
                    canvas,
                    bounds,
                    id,
                    itemDisplayValue(id),
                    absoluteIndex == selectedIndex && inventoryFocus == InventoryFocus.Grid,
                    id == equippedBItemId() || id == equippedShieldId()
                )
            }
        }
        if (visible.isEmpty() && category > 0 && hasCapability(CAPABILITY_FLAG_STATE)) {
            drawText(
                canvas,
                "No displayable items obtained in this category",
                363f,
                450f,
                25f,
                false,
                Paint.Align.CENTER,
                COLOR_TEXT_SECONDARY
            )
        }
        if (items.size > INVENTORY_PAGE_SIZE) {
            val page = pageOffset / INVENTORY_PAGE_SIZE + 1
            val pages = (items.size + INVENTORY_PAGE_SIZE - 1) / INVENTORY_PAGE_SIZE
            drawText(canvas, "$page / $pages", 564f, 640f, 14f, true, Paint.Align.RIGHT, COLOR_TEXT_SECONDARY)
            drawPagerButton(canvas, inventoryPreviousBounds, "<", pageOffset > 0)
            drawPagerButton(canvas, inventoryNextBounds, ">", pageOffset + INVENTORY_PAGE_SIZE < items.size)
        }
        val selected = items.getOrNull(selectedIndex)?.takeIf { it in 0 until ITEM_COUNT }
        drawDetail(canvas, inventoryDetailBounds, selected)
        drawStatus(
            canvas,
            inventoryStatusBounds,
            compact = true,
            focused = inventoryFocus == InventoryFocus.Status
        )
        drawPlayerCard(canvas, selected)
    }

    private fun drawPlayerCard(canvas: Canvas, selected: Int?) {
        val equippedBItem = equippedBItemId()
        drawText(canvas, "LINK", 742f, 350f, 20f, true, color = COLOR_PRIMARY_DARK)
        drawText(
            canvas,
            if (hasCapability(CAPABILITY_LINK_APPEARANCE_STATE) && snapshot.linkOutfit in 0..1) {
                "Drag the 3D model to rotate"
            } else {
                "Preview unavailable for the current outfit"
            },
            1208f,
            350f,
            15f,
            false,
            Paint.Align.RIGHT,
            COLOR_TEXT_SECONDARY
        )
        panel(canvas, RectF(742f, 360f, 1046f, 1000f), 30)

        val shield = equippedShieldId()
        drawEquipmentSlot(
            canvas,
            shieldEquipmentBounds,
            "SHIELD",
            shield,
            inventoryFocus == InventoryFocus.Shield
        )
        drawEquipmentSlot(
            canvas,
            bItemEquipmentBounds,
            "B ITEM",
            equippedBItem,
            inventoryFocus == InventoryFocus.BItem
        )
        drawEquipmentSlot(
            canvas,
            selectedEquipmentBounds,
            "SELECTED",
            selected,
            inventoryFocus == InventoryFocus.Selected
        )
        drawDowsingSlot(
            canvas,
            dowsingEquipmentBounds,
            inventoryFocus == InventoryFocus.Dowsing
        )

    }

    private fun drawEquipmentSlot(
        canvas: Canvas,
        bounds: RectF,
        label: String,
        itemId: Int?,
        focused: Boolean = false
    ) {
        paint.color = Color.argb(205, 229, 210, 164)
        canvas.drawRoundRect(bounds, 5f, 5f, paint)
        linePaint.color = COLOR_BORDER
        linePaint.strokeWidth = 2f
        canvas.drawRoundRect(bounds, 5f, 5f, linePaint)
        drawText(canvas, label, bounds.centerX(), bounds.top + 22f, 13f, true, Paint.Align.CENTER, COLOR_PRIMARY_DARK)
        val icon = itemId?.let { assets.itemIcons[it] }
        if (icon != null) {
            drawBitmapFit(canvas, icon, RectF(bounds.left + 12f, bounds.top + 28f, bounds.right - 12f, bounds.bottom - 27f))
        }
        drawTextFitted(
            canvas,
            itemId?.let(::itemName) ?: "None",
            bounds.centerX(),
            bounds.bottom - 7f,
            bounds.width() - 8f,
            12f,
            true,
            Paint.Align.CENTER,
            COLOR_TEXT
        )
        if (focused) drawFocusFrame(canvas, bounds)
    }

    private fun drawDowsingSlot(canvas: Canvas, bounds: RectF, focused: Boolean) {
        val dowsingSlot = snapshot.selectedDowsingSlot.takeIf { it in DOWSING_SLOT_NAMES.indices }
        val dowsingId = dowsingSlot?.let(::dowsingIconId)
        paint.color = Color.argb(205, 229, 210, 164)
        canvas.drawRoundRect(bounds, 5f, 5f, paint)
        linePaint.color = COLOR_BORDER
        linePaint.strokeWidth = 2f
        canvas.drawRoundRect(bounds, 5f, 5f, linePaint)
        drawText(canvas, "DOWSING", bounds.centerX(), bounds.top + 22f, 12f, true, Paint.Align.CENTER, COLOR_PRIMARY_DARK)
        val icon = dowsingId?.let { assets.dowsingIcons[it] }
        if (icon != null) {
            drawBitmapFit(canvas, icon, RectF(bounds.left + 12f, bounds.top + 28f, bounds.right - 12f, bounds.bottom - 27f))
        }
        drawTextFitted(
            canvas,
            dowsingSlot?.let(::dowsingSlotName) ?: "Unavailable",
            bounds.centerX(),
            bounds.bottom - 7f,
            bounds.width() - 8f,
            11f,
            true,
            Paint.Align.CENTER
        )
        if (focused) drawFocusFrame(canvas, bounds)
    }

    private fun resetMapView() {
        mapViewScale = 1f
        mapViewOffsetX = 0f
        mapViewOffsetY = 0f
        mapTouchCaptured = false
        mapTouchMoved = false
        mapLastPinchDistance = 0f
    }

    private fun constrainMapView() {
        val maxX = mapContentBounds.width() * .45f * mapViewScale
        val maxY = mapContentBounds.height() * .45f * mapViewScale
        mapViewOffsetX = mapViewOffsetX.coerceIn(-maxX, maxX)
        mapViewOffsetY = mapViewOffsetY.coerceIn(-maxY, maxY)
    }

    private fun zoomMap(factor: Float) {
        mapViewScale = (mapViewScale * factor).coerceIn(1f, 4f)
        constrainMapView()
    }

    private fun publishMapView() {
        val imageHeight = min(780f, 1206f / mapDisplayAspect)
        val imageWidth = imageHeight * mapDisplayAspect
        if (imageWidth > 0f && imageHeight > 0f) {
            NativeLibrary.setSshdMapView(
                mapViewScale, mapViewOffsetX / imageWidth, mapViewOffsetY / imageHeight
            )
        }
    }

    private fun drawMap(canvas: Canvas) {
        val mapRect = mapContentBounds
        paint.color = Color.rgb(233, 215, 153)
        canvas.drawRoundRect(mapRect, 8f, 8f, paint)
        val drewLiveMap = mapGpuReady && mapBitmapStage == snapshot.stage &&
            mapBitmapLifecycleEpoch == snapshot.lifecycleEpoch
        if (!drewLiveMap) {
            val captureStatus = synchronized(mapBitmapLock) { mapCaptureStatus }
            drawText(
                canvas,
                "LOADING LOCAL MAP",
                mapRect.centerX(),
                mapRect.centerY() - 26f,
                25f,
                true,
                Paint.Align.CENTER,
                Color.rgb(239, 221, 178)
            )
            drawWrappedText(
                canvas,
                captureStatus,
                RectF(mapRect.left + 120f, mapRect.centerY() + 4f, mapRect.right - 120f, mapRect.centerY() + 92f),
                19f,
                3,
                Color.rgb(220, 198, 151)
            )
        }
        linePaint.color = COLOR_GOLD
        linePaint.strokeWidth = 3f
        canvas.drawRoundRect(mapRect, 8f, 8f, linePaint)
    }

    private fun drawCombat(canvas: Canvas) {
        panel(canvas, RectF(12f, 92f, 906f, 1068f), 112)
        panel(canvas, RectF(914f, 92f, 1228f, 1068f), 112)
        sectionTitle(canvas, "TOUCH COMBAT", "Native button-only sword input", 32f, 132f)

        paint.color = Color.rgb(67, 49, 28)
        canvas.drawRoundRect(combatPadBounds, 12f, 12f, paint)
        linePaint.color = COLOR_GOLD
        linePaint.strokeWidth = 3f
        canvas.drawRoundRect(combatPadBounds, 12f, 12f, linePaint)
        val centerX = combatPadBounds.centerX()
        val centerY = combatPadBounds.centerY()
        val directions = arrayOf(
            Triple(0f, -1f, "UP"), Triple(.707f, -.707f, "UP-RIGHT"),
            Triple(1f, 0f, "RIGHT"), Triple(.707f, .707f, "DOWN-RIGHT"),
            Triple(0f, 1f, "DOWN"), Triple(-.707f, .707f, "DOWN-LEFT"),
            Triple(-1f, 0f, "LEFT"), Triple(-.707f, -.707f, "UP-LEFT")
        )
        directions.forEach { (dx, dy, label) ->
            val innerX = centerX + dx * 118f
            val innerY = centerY + dy * 118f
            val outerX = centerX + dx * 285f
            val outerY = centerY + dy * 285f
            linePaint.color = Color.argb(120, 238, 211, 146)
            linePaint.strokeWidth = 3f
            canvas.drawLine(innerX, innerY, outerX, outerY, linePaint)
            drawText(
                canvas,
                label,
                centerX + dx * 335f,
                centerY + dy * 335f + 6f,
                13f,
                true,
                Paint.Align.CENTER,
                Color.rgb(231, 208, 153)
            )
        }
        paint.color = COLOR_PRIMARY_DARK
        canvas.drawCircle(centerX, centerY, 82f, paint)
        linePaint.color = COLOR_GOLD
        linePaint.strokeWidth = 2f
        canvas.drawCircle(centerX, centerY, 82f, linePaint)
        drawText(canvas, "SWIPE", centerX, centerY - 3f, 23f, true, Paint.Align.CENTER, Color.WHITE)
        drawText(canvas, "Tap: thrust", centerX, centerY + 27f, 14f, false, Paint.Align.CENTER, Color.rgb(221, 205, 169))

        if (combatPath.size > 1) {
            linePaint.color = COLOR_PRIMARY_LIGHT
            linePaint.strokeWidth = 8f
            for (index in 1 until combatPath.size) {
                val previous = combatPath[index - 1]
                val current = combatPath[index]
                canvas.drawLine(previous.x, previous.y, current.x, current.y, linePaint)
            }
        }

        drawText(canvas, "NATIVE ACTIONS", 930f, 150f, 18f, true, color = COLOR_PRIMARY_DARK)
        drawCombatButton(canvas, combatStabBounds, "THRUST", "R3 / contextual attack")
        drawCombatButton(
            canvas,
            combatShieldBounds,
            "SHIELD BASH",
            "L3 / underwater spin",
            requireSword = false
        )
        drawCombatButton(canvas, combatHorizontalSpinBounds, "HORIZONTAL SPIN", "Left-right or right-left")
        drawCombatButton(canvas, combatVerticalSpinBounds, "VERTICAL SPIN", "Up-down or down-up")
        drawCombatButton(canvas, combatFinalBlowBounds, "FINAL BLOW", "ZL + vertical spin")
        drawCombatButton(canvas, combatJumpSlashBounds, "JUMP SLASH", "ZL + forward + A")
        drawCombatButton(
            canvas,
            combatSkywardBounds,
            if (skywardChargeHeld) "SWORD ALOFT" else "SKYWARD CHARGE",
            if (skywardChargeHeld) "Swipe to release the strike" else "Swipe up and hold",
            active = skywardChargeHeld
        )
    }

    private fun drawCombatButton(
        canvas: Canvas,
        bounds: RectF,
        title: String,
        subtitle: String,
        active: Boolean = false,
        requireSword: Boolean = true
    ) {
        val enabled = combatReady(requireSword)
        paint.color = when {
            active -> COLOR_GREEN
            enabled -> COLOR_PRIMARY
            else -> Color.rgb(170, 146, 101)
        }
        canvas.drawRoundRect(bounds, 6f, 6f, paint)
        linePaint.color = if (enabled) COLOR_GOLD else COLOR_BORDER
        linePaint.strokeWidth = 2f
        canvas.drawRoundRect(bounds, 6f, 6f, linePaint)
        drawText(
            canvas,
            title,
            bounds.centerX(),
            bounds.top + 31f,
            17f,
            true,
            Paint.Align.CENTER,
            if (enabled) Color.WHITE else COLOR_TEXT_MUTED
        )
        drawText(
            canvas,
            subtitle,
            bounds.centerX(),
            bounds.bottom - 13f,
            13f,
            false,
            Paint.Align.CENTER,
            if (enabled) Color.rgb(226, 207, 163) else COLOR_TEXT_MUTED
        )
    }

    private fun formatHeartUnits(value: Int): String {
        val whole = value / 4
        return when (value % 4) {
            1 -> "$whole.25"
            2 -> "$whole.5"
            3 -> "$whole.75"
            else -> whole.toString()
        }
    }

    private fun compactQuestText(value: String): String =
        value.lineSequence().map(String::trim).filter(String::isNotEmpty).joinToString(" ")

    private fun drawProgress(canvas: Canvas) {
        val location = snapshot.area.ifBlank { snapshot.stage }.ifBlank { "Current area" }
        val progressAvailable = hasCapability(CAPABILITY_PROGRESS_STATE)
        val summary = if (progressAvailable) {
            fiMessage("KEN0", snapshot.summaryId, "Fi has no additional report for this point in the journey.")
        } else {
            "Fi's report is unavailable while story data is synchronizing."
        }
        val objective = if (progressAvailable) {
            fiMessage("KEN2", snapshot.objectiveId, "Continue exploring $location and consult Fi for guidance.")
        } else {
            "The current objective will appear when story data is available."
        }

        val completedQuests = if (hasCapability(CAPABILITY_PROGRESS_HISTORY_STATE)) {
            snapshot.completedQuestIds.reversed().mapNotNull { id ->
                compactQuestText(fiMessage("KEN0", id, ""))
                    .takeIf(String::isNotBlank)
                    ?.let { id to it }
            }
        } else {
            emptyList()
        }

        panel(canvas, RectF(12f, 92f, 510f, 1068f), 112)
        panel(canvas, RectF(518f, 92f, 1228f, 1068f), 112)

        sectionTitle(canvas, "CURRENT QUEST", "FI'S REPORT", 32f, 136f)
        panel(canvas, RectF(32f, 164f, 490f, 434f), 54)
        drawWrappedText(
            canvas,
            summary,
            RectF(52f, 192f, 470f, 410f),
            19f,
            9
        )
        drawText(
            canvas,
            "COMPLETED QUESTS",
            32f,
            478f,
            18f,
            true,
            color = COLOR_PRIMARY_DARK
        )
        drawText(
            canvas,
            if (hasCapability(CAPABILITY_PROGRESS_HISTORY_STATE)) {
                "${completedQuests.size} recorded"
            } else {
                "Synchronizing"
            },
            490f,
            478f,
            14f,
            false,
            Paint.Align.RIGHT,
            COLOR_TEXT_SECONDARY
        )
        completedQuestScrollMax = (completedQuests.size * 108f - 12f - completedQuestBounds.height()).coerceAtLeast(0f)
        completedQuestScroll = completedQuestScroll.coerceIn(0f, completedQuestScrollMax)
        if (completedQuests.isEmpty()) {
            drawWrappedText(
                canvas,
                if (hasCapability(CAPABILITY_PROGRESS_HISTORY_STATE)) {
                    "No earlier completed quest records are available yet."
                } else {
                    "Completed quest records will appear with the live story state."
                },
                RectF(52f, 518f, 470f, 610f),
                18f,
                3,
                COLOR_TEXT_SECONDARY
            )
        } else {
            canvas.save()
            canvas.clipRect(completedQuestBounds)
            completedQuests.forEachIndexed { index, (id, text) ->
                val top = completedQuestBounds.top + index * 108f - completedQuestScroll
                if (top + 96f < completedQuestBounds.top || top > completedQuestBounds.bottom) return@forEachIndexed
                panel(canvas, RectF(32f, top, 490f, top + 96f), 24)
                paint.color = COLOR_GREEN
                canvas.drawCircle(56f, top + 30f, 13f, paint)
                drawText(canvas, "✓", 56f, top + 36f, 15f, true, Paint.Align.CENTER, Color.WHITE)
                drawText(
                    canvas,
                    "QUEST RECORD %02d".format(id),
                    80f,
                    top + 29f,
                    13f,
                    true,
                    color = COLOR_PRIMARY
                )
                drawWrappedText(
                    canvas,
                    text,
                    RectF(80f, top + 39f, 470f, top + 86f),
                    15f,
                    2,
                    COLOR_TEXT_SECONDARY
                )
            }
            canvas.restore()
            if (completedQuestScrollMax > 0f) {
                val track = completedQuestBounds.height()
                val thumb = (track * track / (track + completedQuestScrollMax)).coerceAtLeast(32f)
                val top = completedQuestBounds.top + (track - thumb) * completedQuestScroll / completedQuestScrollMax
                paint.color = COLOR_PRIMARY
                canvas.drawRoundRect(RectF(498f, top, 502f, top + thumb), 2f, 2f, paint)
            }
        }

        sectionTitle(canvas, "CURRENT OBJECTIVE", "FI'S RECOMMENDATION", 542f, 136f)
        assets.fiIcon?.let {
            drawBitmapFit(canvas, it, RectF(1124f, 116f, 1204f, 196f), 150)
        }
        paint.color = COLOR_PRIMARY
        canvas.drawRoundRect(RectF(542f, 166f, 680f, 194f), 14f, 14f, paint)
        drawText(canvas, "ACTIVE", 611f, 187f, 13f, true, Paint.Align.CENTER, COLOR_SURFACE_RAISED)
        drawTextFitted(
            canvas,
            location.uppercase(),
            696f,
            188f,
            404f,
            14f,
            true,
            color = COLOR_TEXT_SECONDARY
        )
        panel(canvas, RectF(542f, 220f, 1204f, 1046f), 62)
        drawWrappedText(
            canvas,
            objective,
            RectF(574f, 260f, 1172f, 1014f),
            26f,
            20
        )
    }

    private fun drawStatus(
        canvas: Canvas,
        bounds: RectF,
        compact: Boolean = false,
        focused: Boolean = false
    ) {
        panel(canvas, bounds, 92)
        drawText(
            canvas,
            "VITALS",
            bounds.left + 16f,
            bounds.top + 32f,
            17f,
            true,
            color = COLOR_PRIMARY_DARK
        )
        val saveAvailable = hasCapability(CAPABILITY_SAVE_STATE)
        val healthRatio = if (saveAvailable && snapshot.healthCapacity > 0) {
            (snapshot.currentHealth.toFloat() / snapshot.healthCapacity.toFloat()).coerceIn(0f, 1f)
        } else {
            0f
        }
        drawText(canvas, "HEALTH", bounds.left + 16f, bounds.top + 62f, 13f, true)
        drawText(
            canvas,
            if (saveAvailable) {
                "${formatHeartUnits(snapshot.currentHealth)} / ${formatHeartUnits(snapshot.healthCapacity)}"
            } else {
                "-- / --"
            },
            bounds.right - 16f,
            bounds.top + 62f,
            15f,
            true,
            Paint.Align.RIGHT,
            COLOR_RED
        )
        val healthBar = RectF(bounds.left + 16f, bounds.top + 72f, bounds.right - 16f, bounds.top + 86f)
        paint.color = Color.rgb(151, 128, 82)
        canvas.drawRoundRect(healthBar, 7f, 7f, paint)
        if (saveAvailable && healthRatio > 0f) {
            paint.color = COLOR_RED
            canvas.drawRoundRect(
                RectF(healthBar.left, healthBar.top, healthBar.left + healthBar.width() * healthRatio, healthBar.bottom),
                7f,
                7f,
                paint
            )
        }
        val hasPlayerState = hasCapability(CAPABILITY_PLAYER_STATE)
        val staminaRatio = if (hasPlayerState) {
            (snapshot.stamina.toFloat() / FULL_STAMINA_AMOUNT.toFloat()).coerceIn(0f, 1f)
        } else {
            0f
        }
        drawText(canvas, "STAMINA", bounds.left + 16f, bounds.top + 113f, 13f, true)
        drawText(
            canvas,
            if (hasPlayerState) "${(staminaRatio * 100f).toInt()}%" else "--",
            bounds.right - 16f,
            bounds.top + 113f,
            15f,
            true,
            Paint.Align.RIGHT,
            if (hasPlayerState) COLOR_GREEN else COLOR_TEXT_MUTED
        )
        val staminaBar = RectF(bounds.left + 16f, bounds.top + 123f, bounds.right - 16f, bounds.top + 137f)
        paint.color = Color.rgb(151, 128, 82)
        canvas.drawRoundRect(staminaBar, 6f, 6f, paint)
        if (hasPlayerState && staminaRatio > 0f) {
            paint.color = COLOR_GREEN
            canvas.drawRoundRect(
                RectF(
                    staminaBar.left,
                    staminaBar.top,
                    staminaBar.left + staminaBar.width() * staminaRatio,
                    staminaBar.bottom
                ),
                6f,
                6f,
                paint
            )
        }
        val resourceY = bounds.bottom - 18f
        val flagsAvailable = hasCapability(CAPABILITY_FLAG_STATE)
        val purse = assets.purse
        if (purse != null) {
            drawBitmapFit(canvas, purse, RectF(bounds.left + 14f, resourceY - 30f, bounds.left + 46f, resourceY + 6f))
        }
        drawText(
            canvas,
            if (flagsAvailable) snapshot.rupees.toString() else "--",
            bounds.left + if (purse != null) 56f else 16f,
            resourceY,
            27f,
            true
        )
        drawText(
            canvas,
            if (flagsAvailable) {
                "S ${snapshot.seeds}   A ${snapshot.arrows}   B ${snapshot.bombs}"
            } else {
                "S --   A --   B --"
            },
            bounds.right - 16f,
            resourceY,
            15f,
            false,
            Paint.Align.RIGHT
        )
        if (focused) drawFocusFrame(canvas, bounds)
    }

    private fun drawHeart(canvas: Canvas, bounds: RectF, fill: Float) {
        val amount = fill.coerceIn(0f, 1f)
        val heart = assets.heart
        if (heart != null) {
            paint.alpha = 48
            canvas.drawBitmap(heart, null, bounds, paint)
            if (amount > 0f) {
                canvas.save()
                canvas.clipRect(bounds.left, bounds.top, bounds.left + bounds.width() * amount, bounds.bottom)
                paint.alpha = 255
                canvas.drawBitmap(heart, null, bounds, paint)
                canvas.restore()
            }
        }
        paint.alpha = 255
    }

    private fun drawFocusFrame(canvas: Canvas, bounds: RectF) {
        linePaint.color = COLOR_GOLD
        linePaint.strokeWidth = 4f
        canvas.drawRoundRect(bounds, 6f, 6f, linePaint)
        linePaint.color = Color.argb(180, 255, 244, 204)
        linePaint.strokeWidth = 1.5f
        canvas.drawRoundRect(
            RectF(bounds.left + 5f, bounds.top + 5f, bounds.right - 5f, bounds.bottom - 5f),
            4f,
            4f,
            linePaint
        )
    }

    private fun drawDetail(canvas: Canvas, bounds: RectF, itemId: Int?) {
        panel(canvas, bounds, 75)
        drawText(canvas, "INSPECT", bounds.left + 18f, bounds.top + 32f, 16f, true, color = COLOR_PRIMARY_DARK)
        if (itemId == null) {
            drawText(canvas, "Select an obtained item", bounds.left + 18f, bounds.top + 72f, 23f, true)
            return
        }
        val icon = assets.itemIcons[itemId]
        val textLeft = if (icon != null) bounds.left + 146f else bounds.left + 18f
        if (icon != null) {
            drawBitmapFit(
                canvas,
                icon,
                RectF(bounds.left + 18f, bounds.top + 46f, bounds.left + 130f, bounds.bottom - 16f)
            )
        }
        drawTextFitted(
            canvas,
            itemName(itemId),
            textLeft,
            bounds.top + 72f,
            bounds.right - textLeft - 18f,
            27f,
            true
        )
        if (page == Page.Inventory) {
            drawText(
                canvas,
                itemLiveInfo(itemId),
                textLeft,
                bounds.top + 103f,
                15f,
                true,
                color = COLOR_TEXT_SECONDARY
            )
        }
        val actionBounds = detailActionBounds(bounds)
        val actionEnabled = selectionHasNativeAction(itemId)
        paint.color = if (actionEnabled) COLOR_PRIMARY else Color.rgb(191, 169, 120)
        canvas.drawRoundRect(actionBounds, 4f, 4f, paint)
        linePaint.color = if (actionEnabled) COLOR_GOLD else COLOR_BORDER
        linePaint.strokeWidth = 1.5f
        canvas.drawRoundRect(actionBounds, 4f, 4f, linePaint)
        drawText(
            canvas,
            selectionActionLabel(itemId),
            actionBounds.centerX(),
            actionBounds.centerY() + 5f,
            14f,
            true,
            Paint.Align.CENTER,
            if (actionEnabled) Color.WHITE else COLOR_TEXT_MUTED
        )
        itemDescription(itemId).takeIf(String::isNotBlank)?.let { description ->
            drawWrappedText(
                canvas,
                description,
                RectF(textLeft, bounds.top + 116f, bounds.right - 18f, bounds.bottom - 12f),
                19f,
                4
            )
        }
    }

    private fun drawItemSlot(
        canvas: Canvas,
        bounds: RectF,
        itemId: Int,
        count: Int,
        selected: Boolean,
        equipped: Boolean
    ) {
        paint.color = if (selected) COLOR_PRIMARY else COLOR_SURFACE_RAISED
        canvas.drawRoundRect(bounds, 5f, 5f, paint)
        val bitmap = if (selected) assets.selectedCell ?: assets.itemCell else assets.itemCell
        if (bitmap != null) {
            paint.alpha = if (selected) 72 else 42
            canvas.drawBitmap(bitmap, null, bounds, paint)
            paint.alpha = 255
        }
        val icon = assets.itemIcons[itemId]
        if (icon != null) {
            drawBitmapFit(
                canvas,
                icon,
                RectF(bounds.left + 14f, bounds.top + 10f, bounds.right - 14f, bounds.bottom - 26f)
            )
        }
        linePaint.color = if (selected) COLOR_GOLD else COLOR_BORDER
        linePaint.strokeWidth = if (selected) 4f else 2f
        canvas.drawRoundRect(bounds, 5f, 5f, linePaint)
        paint.color = if (selected) Color.rgb(241, 214, 142) else Color.rgb(166, 126, 65)
        canvas.drawRoundRect(RectF(bounds.left + 6f, bounds.top + 6f, bounds.right - 6f, bounds.top + 10f), 2f, 2f, paint)
        drawTextFitted(
            canvas,
            itemName(itemId),
            bounds.centerX(),
            bounds.bottom - 8f,
            bounds.width() - 12f,
            13f,
            true,
            Paint.Align.CENTER,
            if (selected) Color.rgb(248, 231, 187) else COLOR_TEXT
        )
        if (count > 1 || category == 3 || category == 4 || itemId == 48) {
            paint.color = COLOR_GOLD
            canvas.drawCircle(bounds.right - 12f, bounds.top + 16f, 11f, paint)
            drawText(canvas, count.toString(), bounds.right - 12f, bounds.top + 21f, 12f, true, Paint.Align.CENTER, COLOR_BACKGROUND_TOP)
        }
        if (equipped) {
            paint.color = COLOR_GREEN
            canvas.drawRoundRect(RectF(bounds.left + 6f, bounds.bottom - 25f, bounds.left + 30f, bounds.bottom - 7f), 3f, 3f, paint)
            drawText(canvas, "E", bounds.left + 18f, bounds.bottom - 11f, 12f, true, Paint.Align.CENTER, Color.WHITE)
        }
    }

    private fun drawPackedSlot(
        canvas: Canvas,
        bounds: RectF,
        packed: Int,
        selected: Boolean,
        compact: Boolean = false
    ) {
        val id = packedItemId(packed)
        val valid = id in 0 until ITEM_COUNT
        paint.color = when {
            selected -> COLOR_PRIMARY
            valid -> COLOR_SURFACE_RAISED
            else -> Color.rgb(193, 170, 119)
        }
        canvas.drawRoundRect(bounds, 5f, 5f, paint)
        linePaint.color = if (selected) COLOR_GOLD else COLOR_BORDER
        linePaint.strokeWidth = if (selected) 4f else 1.5f
        canvas.drawRoundRect(bounds, 5f, 5f, linePaint)
        if (valid) {
            val icon = assets.itemIcons[id]
            if (icon != null) {
                drawBitmapFit(
                    canvas,
                    icon,
                    if (compact) {
                        RectF(bounds.left + 9f, bounds.top + 6f, bounds.right - 9f, bounds.bottom - 16f)
                    } else {
                        RectF(bounds.left + 12f, bounds.top + 8f, bounds.right - 12f,
                            bounds.bottom - if (id in 0x74..0x7D) 34f else 25f)
                    }
                )
            }
            if (!compact) {
                drawTextFitted(
                    canvas,
                    itemName(id),
                    bounds.centerX(),
                    bounds.bottom - if (id in 0x74..0x7D) 16f else 7f,
                    bounds.width() - 10f,
                    12f,
                    true,
                    Paint.Align.CENTER,
                    if (selected) Color.rgb(248, 231, 187) else COLOR_TEXT
                )
            }
            if (id in 0x74..0x7D) {
                val durability = packed ushr 16 and 0xFF
                val maximum = SHIELD_MAX_DURABILITY[id - 0x74]
                val ratio = (durability.toFloat() / maximum.toFloat()).coerceIn(0f, 1f)
                val bar = RectF(bounds.left + 9f, bounds.bottom - 10f, bounds.right - 9f, bounds.bottom - 5f)
                paint.color = Color.rgb(135, 106, 66)
                canvas.drawRoundRect(bar, 3f, 3f, paint)
                if (ratio > 0f) {
                    paint.color = if (ratio < .25f) COLOR_RED else COLOR_GREEN
                    canvas.drawRoundRect(
                        RectF(bar.left, bar.top, bar.left + bar.width() * ratio, bar.bottom),
                        3f,
                        3f,
                        paint
                    )
                }
            }
        }
    }

    private fun drawPouchEquippedIndicators(canvas: Canvas, bounds: RectF, slot: Int) {
        if (slot == snapshot.selectedPouchSlot) {
            drawText(
                canvas,
                "SEL",
                bounds.left + 8f,
                bounds.top + 17f,
                10f,
                true,
                color = Color.rgb(235, 207, 107)
            )
        }
        if (slot == snapshot.shieldPouchSlot) {
            drawText(
                canvas,
                "S",
                bounds.right - 8f,
                bounds.top + 17f,
                13f,
                true,
                Paint.Align.RIGHT,
                COLOR_PRIMARY_LIGHT
            )
        }
    }

    private fun drawPagerButton(canvas: Canvas, bounds: RectF, label: String, enabled: Boolean) {
        paint.color = if (enabled) COLOR_PRIMARY else Color.rgb(179, 155, 107)
        canvas.drawRoundRect(bounds, 4f, 4f, paint)
        linePaint.color = if (enabled) COLOR_GOLD else COLOR_BORDER
        linePaint.strokeWidth = 1.5f
        canvas.drawRoundRect(bounds, 4f, 4f, linePaint)
        drawText(
            canvas,
            label,
            bounds.centerX(),
            bounds.centerY() + 6f,
            18f,
            true,
            Paint.Align.CENTER,
            if (enabled) Color.WHITE else COLOR_TEXT_MUTED
        )
    }

    private fun detailActionBounds(detailBounds: RectF): RectF = RectF(
        detailBounds.right - 222f,
        detailBounds.top + 78f,
        detailBounds.right - 18f,
        detailBounds.top + 110f
    )

    private fun drawUnavailable(canvas: Canvas) {
        paint.color = Color.argb(242, 61, 43, 24)
        canvas.drawRoundRect(RectF(300f, 382f, 1110f, 698f), 14f, 14f, paint)
        paint.color = COLOR_PRIMARY
        canvas.drawRoundRect(RectF(300f, 382f, 308f, 698f), 5f, 5f, paint)
        val title = when (snapshot.status) {
            1 -> "Waiting for Skyward Sword HD"
            2 -> "Unsupported Skyward Sword HD build"
            3 -> "Connecting to live game state"
            else -> "Waiting for the game to start"
        }
        drawText(canvas, "LIVE LINK", 344f, 430f, 16f, true, color = COLOR_PRIMARY_LIGHT)
        drawText(canvas, title, 344f, 486f, 32f, true, color = Color.WHITE)
        val checkpoint = (snapshot.capabilities ushr 24) and 0xFF
        val detail = when {
            snapshot.status == 2 ->
                "This integration supports the verified Skyward Sword HD 1.0.1 executable only."
            snapshot.status != 3 ->
                "Start or resume the game; the field kit will connect without opening the pause menu."
            checkpoint == 3 -> "The executable module is still initializing."
            checkpoint == 4 -> "Waiting for the game's file manager and Save A block to initialize."
            checkpoint == 5 -> "Waiting for Link and the active stage to enter a playable state."
            checkpoint == 6 -> "The Save A inventory block is not readable yet."
            else -> "Waiting for Save A and the live flag managers to initialize."
        }
        drawWrappedText(canvas, detail, RectF(344f, 520f, 1060f, 620f), 21f, 4, Color.rgb(224, 207, 171))
        if (snapshot.status == 3 && checkpoint != 0) {
            drawText(canvas, "TRACE CHECKPOINT $checkpoint", 344f, 662f, 15f, true, color = COLOR_GOLD)
        }
    }

    private fun sectionTitle(canvas: Canvas, title: String, subtitle: String, x: Float, y: Float) {
        drawText(canvas, title, x, y, 20f, true, color = COLOR_PRIMARY_DARK)
        val subtitleX = x + paint.measureText(title) + 22f
        drawText(canvas, subtitle, subtitleX, y, 15f, color = COLOR_TEXT_SECONDARY)
        linePaint.color = Color.argb(210, 151, 103, 39)
        linePaint.strokeWidth = 2f
        canvas.drawLine(x, y + 12f, x + 120f, y + 12f, linePaint)
    }

    private fun panel(canvas: Canvas, bounds: RectF, alpha: Int) {
        paint.color = Color.argb(85, 43, 28, 13)
        canvas.drawRoundRect(RectF(bounds.left + 3f, bounds.top + 4f, bounds.right + 3f, bounds.bottom + 4f), 7f, 7f, paint)
        paint.color = Color.argb((135 + alpha).coerceAtMost(247), 218, 197, 148)
        canvas.drawRoundRect(bounds, 7f, 7f, paint)
        linePaint.color = COLOR_BORDER
        linePaint.strokeWidth = 2f
        canvas.drawRoundRect(bounds, 7f, 7f, linePaint)
        linePaint.color = Color.argb(205, 151, 103, 39)
        linePaint.strokeWidth = 2f
        canvas.drawLine(bounds.left + 8f, bounds.top + 8f, bounds.left + 28f, bounds.top + 8f, linePaint)
        canvas.drawLine(bounds.left + 8f, bounds.top + 8f, bounds.left + 8f, bounds.top + 28f, linePaint)
        linePaint.color = Color.argb(215, 201, 151, 55)
        canvas.drawLine(bounds.right - 28f, bounds.bottom - 8f, bounds.right - 8f, bounds.bottom - 8f, linePaint)
        canvas.drawLine(bounds.right - 8f, bounds.bottom - 28f, bounds.right - 8f, bounds.bottom - 8f, linePaint)
    }

    private fun drawLabelValue(canvas: Canvas, label: String, value: String, x: Float, y: Float, width: Float) {
        drawText(canvas, label, x, y, 17f, color = COLOR_TEXT_SECONDARY)
        drawTextFitted(canvas, value, x + width, y, width * .60f, 19f, true, Paint.Align.RIGHT, COLOR_TEXT)
        linePaint.color = Color.argb(125, 112, 78, 38)
        linePaint.strokeWidth = 1f
        canvas.drawLine(x, y + 13f, x + width, y + 13f, linePaint)
    }

    private fun drawTextFitted(
        canvas: Canvas,
        value: String,
        x: Float,
        y: Float,
        maxWidth: Float,
        preferredSize: Float,
        strong: Boolean = false,
        align: Paint.Align = Paint.Align.LEFT,
        color: Int = COLOR_TEXT
    ) {
        paint.typeface = if (strong) bold else regular
        paint.textSize = preferredSize
        val measured = paint.measureText(value)
        val size = if (measured > maxWidth && measured > 0f) {
            (preferredSize * maxWidth / measured).coerceAtLeast(13f)
        } else {
            preferredSize
        }
        drawText(canvas, value, x, y, size, strong, align, color)
    }

    private fun drawText(
        canvas: Canvas,
        value: String,
        x: Float,
        y: Float,
        size: Float,
        strong: Boolean = false,
        align: Paint.Align = Paint.Align.LEFT,
        color: Int = COLOR_TEXT
    ) {
        paint.shader = null
        paint.alpha = 255
        paint.color = color
        paint.textSize = size
        paint.textAlign = align
        paint.typeface = if (strong) bold else regular
        canvas.drawText(value, x, y, paint)
    }

    private fun drawWrappedText(
        canvas: Canvas,
        value: String,
        bounds: RectF,
        size: Float,
        maxLines: Int,
        color: Int = COLOR_TEXT
    ) {
        paint.textSize = size
        paint.typeface = regular
        paint.textAlign = Paint.Align.LEFT
        paint.color = color
        paint.alpha = 255
        val lines = mutableListOf<String>()
        value.replace('\r', ' ').split('\n').forEach { paragraph ->
            var line = ""
            paragraph.trim().split(Regex("\\s+")).filter { it.isNotEmpty() }.forEach { word ->
                val candidate = if (line.isEmpty()) word else "$line $word"
                if (paint.measureText(candidate) <= bounds.width()) {
                    line = candidate
                } else {
                    if (line.isNotEmpty()) lines += line
                    line = word
                }
            }
            if (line.isNotEmpty()) lines += line
        }
        val visible = lines.take(maxLines).toMutableList()
        if (lines.size > maxLines && visible.isNotEmpty()) {
            visible[visible.lastIndex] = visible.last().trimEnd('.', ' ') + "…"
        }
        visible.forEachIndexed { index, line ->
            canvas.drawText(line, bounds.left, bounds.top + size + index * size * 1.26f, paint)
        }
    }

    private fun drawBitmapFit(canvas: Canvas, bitmap: Bitmap, bounds: RectF, alpha: Int = 255) {
        val scale = min(bounds.width() / bitmap.width, bounds.height() / bitmap.height)
        val width = bitmap.width * scale
        val height = bitmap.height * scale
        val destination = RectF(
            bounds.centerX() - width / 2f,
            bounds.centerY() - height / 2f,
            bounds.centerX() + width / 2f,
            bounds.centerY() + height / 2f
        )
        paint.alpha = alpha.coerceIn(0, 255)
        canvas.drawBitmap(bitmap, null, destination, paint)
        paint.alpha = 255
    }

    private fun inventoryItems(): List<Int> {
        if (category == 0) return snapshot.pouch.map(::packedItemId)
        return FLAG_CATEGORY_IDS[category - 1].filter {
            hasCapability(CAPABILITY_FLAG_STATE) && it in ITEM_ICON_ASSETS &&
                isItemOwned(snapshot, it)
        }
    }

    private fun isItemOwned(frame: Snapshot, itemId: Int): Boolean {
        if (itemId !in 0 until ITEM_COUNT) return false
        if (frame.capabilities and CAPABILITY_ITEM_OWNERSHIP_STATE == 0) {
            return frame.itemValues[itemId] > 0
        }
        val byte = frame.itemOwnedBits[itemId / 8].toInt() and 0xFF
        return byte and (1 shl (itemId and 7)) != 0
    }

    private fun itemDisplayValue(itemId: Int): Int = if (itemId == 48) {
        snapshot.gratitude
    } else {
        snapshot.itemValues.getOrElse(itemId) { 0 }
    }

    private fun itemLiveInfo(itemId: Int): String {
        if (category == 0) {
            val packed = snapshot.pouch.getOrNull(selectedIndex) ?: return "Pouch state unavailable"
            if (itemId in 0x74..0x7D) {
                val current = packed ushr 16 and 0xFF
                val maximum = SHIELD_MAX_DURABILITY[itemId - 0x74]
                return "Durability  $current / $maximum"
            }
            return "Pouch slot  ${selectedIndex + 1} / 8"
        }
        if (!hasCapability(CAPABILITY_FLAG_STATE)) return "Live item state unavailable"
        return when (itemId) {
            19, 90, 91 ->
                "Arrows  ${snapshot.arrows}    Upgrade  ${listOf(19, 90, 91).indexOf(itemId) + 1} / 3"
            92 -> "Bombs  ${snapshot.bombs}"
            52, 105 ->
                "Deku Seeds  ${snapshot.seeds}    Upgrade  ${listOf(52, 105).indexOf(itemId) + 1} / 2"
            53, 75, 76, 77 ->
                "Upgrade  ${listOf(53, 75, 76, 77).indexOf(itemId) + 1} / 4"
            71, 140 ->
                "Upgrade  ${listOf(71, 140).indexOf(itemId) + 1} / 2"
            48 -> "Quantity  ${snapshot.gratitude}"
            in 141..152, in 161..176 -> "Quantity  ${itemDisplayValue(itemId)}"
            in 116..125 -> pouchSlotForItem(itemId)?.let { slot ->
                val packed = snapshot.pouch[slot]
                val current = packed ushr 16 and 0xFF
                val maximum = SHIELD_MAX_DURABILITY[itemId - 0x74]
                "Durability  $current / $maximum"
            } ?: "Owned / stored"
            else -> "Obtained / synchronized"
        }
    }

    private fun hasCapability(capability: Int): Boolean = snapshot.capabilities and capability != 0

    private fun inventoryPageOffset(itemCount: Int): Int {
        if (itemCount <= 0) return 0
        val clamped = selectedIndex.coerceIn(0, itemCount - 1)
        return (clamped / INVENTORY_PAGE_SIZE) * INVENTORY_PAGE_SIZE
    }

    private fun itemName(itemId: Int): String {
        if (itemId !in 0 until ITEM_COUNT) return "None"
        return assets.itemText["NAME_ITEM_%03d".format(itemId)]
            ?.trim()
            ?.takeIf { it.isNotEmpty() }
            .orEmpty()
    }

    private fun itemDescription(itemId: Int): String =
        assets.itemText["CAPTION_ITEM_%03d".format(itemId)]?.trim().orEmpty()

    private fun fiMessage(prefix: String, id: Int, fallback: String): String {
        if (id < 0) return fallback
        val keys = if (prefix == "KEN2") {
            listOf("${prefix}_%03d".format(id), "${prefix}_%02d".format(id), "${prefix}_$id")
        } else {
            listOf("${prefix}_%02d".format(id), "${prefix}_%03d".format(id), "${prefix}_$id")
        }
        return keys.firstNotNullOfOrNull { key ->
            assets.fiText[key]?.trim()?.takeIf { it.isNotEmpty() }
        } ?: fallback
    }

    private fun packedItemId(packed: Int): Int = if (packed < 0) -1 else packed and 0xFF

    private fun equippedShieldId(): Int? = snapshot.shieldPouchSlot
        .takeIf { it in snapshot.pouch.indices }
        ?.let { packedItemId(snapshot.pouch[it]) }
        ?.takeIf { it in 0x74..0x7D }

    private fun pouchSlotForItem(itemId: Int): Int? = snapshot.pouch
        .indexOfFirst { packedItemId(it) == itemId }
        .takeIf { it >= 0 }

    private fun equippedBItemId(frame: Snapshot = snapshot): Int? = when (frame.equippedBItem) {
        0 -> 92 // Bomb Bag
        1 -> listOf(91, 90, 19).firstOrNull { isItemOwned(frame, it) } ?: 19
        2 -> 20 // Clawshots
        3 -> listOf(77, 76, 75, 53).firstOrNull { isItemOwned(frame, it) } ?: 53
        4 -> listOf(105, 52).firstOrNull { isItemOwned(frame, it) } ?: 52
        5 -> 49 // Gust Bellows
        6 -> 137 // Whip
        8 -> listOf(140, 71).firstOrNull { isItemOwned(frame, it) } ?: 71
        else -> null
    }

    private fun bWheelSlotForItem(itemId: Int): Int? = when (itemId) {
        19, 90, 91 -> 0
        92 -> 1
        53, 75, 76, 77 -> 2
        71, 140 -> 3
        52, 105 -> 4
        20 -> 5
        137 -> 6
        49 -> 7
        else -> null
    }

    private fun selectionActionLabel(itemId: Int): String = when (page) {
        Page.Inventory -> when {
            category == 0 && itemId in 0x74..0x7D -> "A  Equip shield"
            category == 0 && isPotion(itemId) -> "Use potion"
            category == 0 && isBottle(itemId) -> "Ready bottle"
            category == 0 -> "Select pouch slot"
            bWheelSlotForItem(itemId) != null -> "A  Equip B item"
            itemId in 0x74..0x7D && pouchSlotForItem(itemId) != null -> "A  Equip shield"
            else -> "Inspect only"
        }
        Page.Map, Page.Combat, Page.Progress -> "Inspect only"
    }

    private fun selectionHasNativeAction(itemId: Int): Boolean = when (page) {
        Page.Inventory -> if (category == 0) {
            selectedIndex in snapshot.pouch.indices && itemId in 0 until ITEM_COUNT
        } else {
            bWheelSlotForItem(itemId) != null ||
                (itemId in 0x74..0x7D && pouchSlotForItem(itemId) != null)
        }
        Page.Map, Page.Combat, Page.Progress -> false
    }

    private fun activateSelection(): Boolean {
        if (snapshot.status != ACTIVE_STATUS) return false
        if (itemInputPending) return showActionFeedback("An item action is already in progress")
        val request = when (page) {
            Page.Inventory -> {
                if (category == 0) {
                    if (selectedIndex !in snapshot.pouch.indices) return false
                    val item = packedItemId(snapshot.pouch[selectedIndex])
                    if (item !in 0 until ITEM_COUNT) {
                        return showActionFeedback("That pouch slot is empty")
                    }
                    if (isBottle(item)) return usePouchBottle(selectedIndex, item)
                    val pouchRequest = if (item in 0x74..0x7D) {
                        Triple(ACTION_EQUIP_SHIELD, selectedIndex, "Shield request applied: ${itemName(item)}")
                    } else {
                        Triple(ACTION_SELECT_POUCH, selectedIndex, "Pouch request applied: slot ${selectedIndex + 1}")
                    }
                    val applied = NativeLibrary.performSshdCompanionAction(
                        pouchRequest.first,
                        pouchRequest.second
                    )
                    return showActionFeedback(
                        if (applied) pouchRequest.third else "Action unavailable in the current game state"
                    )
                }
                if (!hasCapability(CAPABILITY_FLAG_STATE)) {
                    return showActionFeedback("Live item flags are unavailable")
                }
                val item = inventoryItems().getOrNull(selectedIndex) ?: return false
                val bWheelSlot = bWheelSlotForItem(item)
                val shieldSlot = pouchSlotForItem(item).takeIf { item in 0x74..0x7D }
                when {
                    bWheelSlot != null -> Triple(ACTION_EQUIP_B_WHEEL, bWheelSlot, "Equip request applied: ${itemName(item)}")
                    shieldSlot != null -> Triple(ACTION_EQUIP_SHIELD, shieldSlot, "Shield request applied: ${itemName(item)}")
                    else -> return showActionFeedback("This item has no field equip action")
                }
            }
            Page.Map, Page.Combat, Page.Progress ->
                return showActionFeedback("No item action is available on this page")
        }
        val applied = NativeLibrary.performSshdCompanionAction(request.first, request.second)
        return showActionFeedback(if (applied) request.third else "Action unavailable in the current game state")
    }

    private fun activateFocusedSelection(): Boolean {
        if (page == Page.Map) {
            resetMapView()
            return showActionFeedback("Map view reset")
        }
        if (page != Page.Inventory) {
            return if (activateSelection()) true else showActionFeedback("No item action is available")
        }
        return when (inventoryFocus) {
            InventoryFocus.Grid, InventoryFocus.Selected ->
                if (activateSelection()) true else showActionFeedback("No item action is available")
            InventoryFocus.Shield -> {
                val selectedItem = inventoryItems().getOrNull(selectedIndex)
                val shields = snapshot.pouch.indices.filter {
                    packedItemId(snapshot.pouch[it]) in 0x74..0x7D
                }
                val selectedSlot = selectedItem?.takeIf { it in 0x74..0x7D }?.let(::pouchSlotForItem)
                val slot = selectedSlot ?: shields.firstOrNull { it > snapshot.shieldPouchSlot }
                    ?: shields.firstOrNull()
                if (slot == null) {
                    showActionFeedback("No shield is in the pouch")
                } else {
                    val applied = NativeLibrary.performSshdCompanionAction(ACTION_EQUIP_SHIELD, slot)
                    showActionFeedback(if (applied) "Equipped ${itemName(packedItemId(snapshot.pouch[slot]))}" else "Shield action unavailable")
                }
            }
            InventoryFocus.BItem -> {
                val selectedSlot = inventoryItems().getOrNull(selectedIndex)?.let(::bWheelSlotForItem)
                val ownedSlots = (0 until ITEM_COUNT).filter { isItemOwned(snapshot, it) }
                    .mapNotNull(::bWheelSlotForItem).distinct().sorted()
                val currentSlot = equippedBItemId()?.let(::bWheelSlotForItem) ?: -1
                val slot = selectedSlot ?: ownedSlots.firstOrNull { it > currentSlot }
                    ?: ownedSlots.firstOrNull()
                if (slot == null) {
                    showActionFeedback("No B item is equipped")
                } else {
                    val applied = NativeLibrary.performSshdCompanionAction(ACTION_EQUIP_B_WHEEL, slot)
                    showActionFeedback(if (applied) "B-item request applied" else "B-item action unavailable")
                }
            }
            InventoryFocus.Dowsing -> {
                if (snapshot.status != ACTIVE_STATUS) {
                    showActionFeedback("Dowsing is unavailable outside a playable save")
                } else {
                    val nextSlot = nextDowsingSlot(snapshot.selectedDowsingSlot)
                    val applied = NativeLibrary.performSshdCompanionAction(
                        ACTION_SELECT_DOWSING,
                        nextSlot
                    )
                    showActionFeedback(
                        if (applied) "Dowsing selected: ${dowsingSlotName(nextSlot)}"
                        else "Dowsing selection is unavailable in the current game state"
                    )
                }
            }
            InventoryFocus.Status -> showActionFeedback("Vitals are display-only")
        }
    }

    private fun showActionFeedback(message: String): Boolean {
        actionFeedback = message
        actionFeedbackAt = SystemClock.uptimeMillis()
        performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK)
        invalidate()
        return true
    }

    private fun isPotion(item: Int): Boolean = item in setOf(
        65, 66, 78, 79, 80, 81, 83, 84, 85, 86, 87, 126, 127, 194
    )

    private fun isBottle(item: Int): Boolean = isPotion(item) || item in setOf(36, 54, 55, 74, 88, 153)

    private fun cancelItemInput() {
        itemInputGeneration++
        if (!itemInputPending) return
        itemInputPending = false
        setCombatButton(NativeButton.R, false)
        setCombatButton(NativeButton.A, false)
    }

    private fun usePouchBottle(slot: Int, item: Int): Boolean {
        if (itemInputPending) return showActionFeedback("An item action is already in progress")
        // Start with a stowed bottle so a previously held, different bottle cannot
        // accidentally be consumed after changing the pouch selection.
        if (NativeLibrary.getSshdBottleState(slot, item) != 0) {
            return showActionFeedback("Put away the held item before using this bottle")
        }
        if (!NativeLibrary.performSshdCompanionAction(ACTION_SELECT_POUCH, slot)) {
            return showActionFeedback("This bottle is unavailable in the current game state")
        }
        val stage = snapshot.stage
        val epoch = snapshot.lifecycleEpoch
        val generation = ++itemInputGeneration
        val deadline = SystemClock.uptimeMillis() + 5000L
        itemInputPending = true
        setCombatButton(NativeButton.R, true)
        showActionFeedback("Readying ${itemName(item)}")
        Log.info("[SSHD Item] Ready slot=$slot item=$item stage=$stage")
        postDelayed({
            if (generation == itemInputGeneration) setCombatButton(NativeButton.R, false)
        }, 160L)
        var lastState = -2
        var useSent = false
        val check = object : Runnable {
            override fun run() {
                if (generation != itemInputGeneration) return
                if (!isAttachedToWindow || page != Page.Inventory || snapshot.status != ACTIVE_STATUS ||
                    snapshot.currentHealth == 0 || snapshot.stage != stage || snapshot.lifecycleEpoch != epoch
                ) {
                    cancelItemInput()
                    return
                }
                val state = NativeLibrary.getSshdBottleState(slot, item)
                if (state != lastState) {
                    Log.info("[SSHD Item] slot=$slot item=$item state=$state useSent=$useSent")
                    lastState = state
                }
                if (state == 3 && useSent) {
                    cancelItemInput()
                    showActionFeedback("Using ${itemName(item)}")
                    return
                }
                if (state == 2 && !useSent) {
                    if (!isPotion(item)) {
                        cancelItemInput()
                        showActionFeedback("${itemName(item)} is ready")
                        return
                    }
                    // Only the game's stable BINHOLD pose permits the A press.
                    // The native game owns the animation, effects and quantity.
                    useSent = true
                    setCombatButton(NativeButton.A, true)
                    postDelayed({
                        if (generation == itemInputGeneration) setCombatButton(NativeButton.A, false)
                    }, 160L)
                }
                if (state < 0 || SystemClock.uptimeMillis() >= deadline) {
                    cancelItemInput()
                    showActionFeedback("The game could not use this item here")
                    return
                }
                postDelayed(this, 100L)
            }
        }
        postDelayed(check, 200L)
        return true
    }

    private fun Int.displayByte(): String = if (this == 0xFF) "—" else toString()

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val scale = min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT)
        if (scale <= 0f) return false
        val offsetX = (width - DESIGN_WIDTH * scale) * .5f
        val offsetY = (height - DESIGN_HEIGHT * scale) * .5f
        fun designX(index: Int): Float = (event.getX(index) - offsetX) / scale
        fun designY(index: Int): Float = (event.getY(index) - offsetY) / scale
        val x = designX(0)
        val y = designY(0)
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                downX = x
                downY = y
                if (page == Page.Progress && completedQuestBounds.contains(x, y)) {
                    completedQuestTouchCaptured = true
                    completedQuestLastY = y
                }
                if (page == Page.Map && mapContentBounds.contains(x, y)) {
                    mapTouchCaptured = true
                    mapTouchMoved = false
                    mapLastTouchX = x
                    mapLastTouchY = y
                    mapLastPinchDistance = 0f
                }
                if (page == Page.Combat && combatPadBounds.contains(x, y)) {
                    cancelSkywardHoldCandidate()
                    skywardTouchChargeHeld = false
                    combatTouchCaptured = true
                    combatPath.clear()
                    combatLatestX = x.coerceIn(combatPadBounds.left, combatPadBounds.right)
                    combatLatestY = y.coerceIn(combatPadBounds.top, combatPadBounds.bottom)
                    combatPath += CombatPoint(combatLatestX, combatLatestY, event.eventTime)
                    invalidate()
                }
                return true
            }
            MotionEvent.ACTION_POINTER_DOWN -> {
                if (mapTouchCaptured && event.pointerCount >= 2) {
                    mapLastPinchDistance = pointDistance(
                        designX(0), designY(0), designX(1), designY(1)
                    )
                    mapTouchMoved = true
                }
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                if (completedQuestTouchCaptured) {
                    completedQuestScroll = (completedQuestScroll + completedQuestLastY - y)
                        .coerceIn(0f, completedQuestScrollMax)
                    completedQuestLastY = y
                    invalidate()
                    return true
                }
                if (mapTouchCaptured) {
                    if (event.pointerCount >= 2) {
                        val distance = pointDistance(
                            designX(0), designY(0), designX(1), designY(1)
                        )
                        if (mapLastPinchDistance > 0f && distance > 0f) {
                            zoomMap(distance / mapLastPinchDistance)
                        }
                        mapLastPinchDistance = distance
                    } else {
                        val dx = x - mapLastTouchX
                        val dy = y - mapLastTouchY
                        if (abs(dx) + abs(dy) > 1f) {
                            mapViewOffsetX += dx
                            mapViewOffsetY += dy
                            constrainMapView()
                            mapTouchMoved = true
                        }
                        mapLastTouchX = x
                        mapLastTouchY = y
                    }
                    publishMapView()
                    invalidate()
                    return true
                }
                if (combatTouchCaptured) {
                    combatLatestX = x.coerceIn(combatPadBounds.left, combatPadBounds.right)
                    combatLatestY = y.coerceIn(combatPadBounds.top, combatPadBounds.bottom)
                    if (skywardTouchChargeHeld) {
                        val releaseDistance = pointDistance(
                            skywardHoldAnchorX,
                            skywardHoldAnchorY,
                            combatLatestX,
                            combatLatestY
                        )
                        if (releaseDistance >= 104f &&
                            SystemClock.uptimeMillis() - skywardTouchChargeStartedAt >= 70L
                        ) {
                            val direction = combatDirectionBetween(
                                skywardHoldAnchorX,
                                skywardHoldAnchorY,
                                combatLatestX,
                                combatLatestY
                            )
                            combatTouchCaptured = false
                            skywardTouchChargeHeld = false
                            cancelSkywardHoldCandidate()
                            combatPath.clear()
                            sendCombatStickSequence(
                                listOf(direction),
                                "Native charged ${direction.displayName} strike sent"
                            )
                            performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
                            invalidate()
                            return true
                        }
                    }
                    val previous = combatPath.lastOrNull()
                    if (previous == null || pointDistance(
                            previous.x,
                            previous.y,
                            combatLatestX,
                            combatLatestY
                        ) >= 8f
                    ) {
                        combatPath += CombatPoint(
                            combatLatestX,
                            combatLatestY,
                            event.eventTime
                        )
                    }
                    updateSkywardHoldCandidate()
                    invalidate()
                }
                return true
            }
            MotionEvent.ACTION_POINTER_UP -> {
                if (mapTouchCaptured) mapLastPinchDistance = 0f
                return true
            }
            MotionEvent.ACTION_CANCEL -> {
                completedQuestTouchCaptured = false
                mapTouchCaptured = false
                mapTouchMoved = false
                mapLastPinchDistance = 0f
                releaseCombatControls()
                combatPath.clear()
                combatTouchCaptured = false
                invalidate()
                return true
            }
            MotionEvent.ACTION_UP -> {
                performClick()
                if (completedQuestTouchCaptured) {
                    completedQuestTouchCaptured = false
                    return true
                }
                if (mapTouchCaptured) {
                    mapTouchCaptured = false
                    mapLastPinchDistance = 0f
                    if (mapTouchMoved) {
                        performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK)
                    }
                    mapTouchMoved = false
                    invalidate()
                    return true
                }
                if (combatTouchCaptured) {
                    cancelSkywardHoldCandidate()
                    if (skywardTouchChargeHeld) {
                        combatTouchCaptured = false
                        skywardTouchChargeHeld = false
                        combatPath.clear()
                        releaseCombatControls()
                        showActionFeedback("Skyward charge released")
                        invalidate()
                        return true
                    }
                    combatPath += CombatPoint(
                        x.coerceIn(combatPadBounds.left, combatPadBounds.right),
                        y.coerceIn(combatPadBounds.top, combatPadBounds.bottom),
                        event.eventTime
                    )
                    combatTouchCaptured = false
                    recognizeCombatGesture(combatPath.toList())
                    combatPath.clear()
                    invalidate()
                    return true
                }
                if (page != Page.Combat && abs(x - downX) > 90f && abs(y - downY) < 90f) {
                    cyclePage(if (x < downX) 1 else -1)
                    performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK)
                    invalidate()
                    return true
                }
                tabBounds.indexOfFirst { it.contains(x, y) }.takeIf { it >= 0 }?.let {
                    changePage(Page.entries[it])
                    invalidate()
                    return true
                }
                when (page) {
                    Page.Inventory -> handleInventoryTap(x, y)
                    Page.Map -> Unit
                    Page.Combat -> handleCombatTap(x, y)
                    Page.Progress -> Unit
                }
                invalidate()
                return true
            }
        }
        return true
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }

    private fun combatReady(requireSword: Boolean = true): Boolean {
        if (snapshot.status != ACTIVE_STATUS || snapshot.currentHealth <= 0 ||
            !hasCapability(CAPABILITY_PLAYER_STATE)
        ) {
            return false
        }
        if (!requireSword) return true
        if (!hasCapability(CAPABILITY_ITEM_OWNERSHIP_STATE)) return false
        return intArrayOf(9, 10, 11, 12, 13, 14).any { isItemOwned(snapshot, it) }
    }

    private fun pointDistance(x1: Float, y1: Float, x2: Float, y2: Float): Float =
        hypot((x2 - x1).toDouble(), (y2 - y1).toDouble()).toFloat()

    private fun setCombatStick(x: Float, y: Float) {
        runCatching { NativeInput.onOverlayJoystickEvent(0, NativeAnalog.RStick, x, y) }
            .onFailure { Log.error("[SSHD Companion] Native sword-stick input failed: ${it.message}") }
    }

    private fun setCombatMovementStick(x: Float, y: Float) {
        runCatching { NativeInput.onOverlayJoystickEvent(0, NativeAnalog.LStick, x, y) }
            .onFailure { Log.error("[SSHD Companion] Native movement-stick input failed: ${it.message}") }
    }

    private fun setCombatButton(button: NativeButton, pressed: Boolean) {
        runCatching { NativeInput.onOverlayButtonEvent(0, button, if (pressed) 1 else 0) }
            .onFailure { Log.error("[SSHD Companion] Native combat button input failed: ${it.message}") }
    }

    private fun cancelSkywardHoldCandidate() {
        skywardHoldGeneration++
        skywardHoldCandidate = false
    }

    private fun updateSkywardHoldCandidate() {
        if (!combatTouchCaptured || skywardTouchChargeHeld || combatPath.isEmpty()) return
        val start = combatPath.first()
        val upwardDistance = start.y - combatLatestY
        val sidewaysDistance = abs(combatLatestX - start.x)
        val isSwordUp = upwardDistance >= SKYWARD_HOLD_MIN_DISTANCE &&
            sidewaysDistance <= max(58f, upwardDistance * .72f)
        if (!isSwordUp) {
            cancelSkywardHoldCandidate()
            return
        }

        if (skywardHoldCandidate && pointDistance(
                skywardHoldAnchorX,
                skywardHoldAnchorY,
                combatLatestX,
                combatLatestY
            ) <= 44f
        ) {
            return
        }

        cancelSkywardHoldCandidate()
        skywardHoldCandidate = true
        skywardHoldAnchorX = combatLatestX
        skywardHoldAnchorY = combatLatestY
        val holdGeneration = skywardHoldGeneration
        postDelayed({
            if (!combatTouchCaptured || !skywardHoldCandidate ||
                skywardHoldGeneration != holdGeneration || page != Page.Combat
            ) {
                return@postDelayed
            }
            val currentStart = combatPath.firstOrNull() ?: return@postDelayed
            val stillUp = currentStart.y - combatLatestY >= SKYWARD_HOLD_MIN_DISTANCE &&
                abs(combatLatestX - currentStart.x) <=
                max(58f, (currentStart.y - combatLatestY) * .72f)
            val steady = pointDistance(
                skywardHoldAnchorX,
                skywardHoldAnchorY,
                combatLatestX,
                combatLatestY
            ) <= 44f
            if (stillUp && steady) beginTouchSkywardCharge() else cancelSkywardHoldCandidate()
        }, SKYWARD_HOLD_DELAY_MS)
    }

    private fun beginTouchSkywardCharge() {
        if (!combatReady() || !combatTouchCaptured) {
            cancelSkywardHoldCandidate()
            return
        }
        combatInputGeneration++
        setCombatStick(0f, 0f)
        setCombatMovementStick(0f, 0f)
        setCombatButton(NativeButton.RStick, false)
        setCombatButton(NativeButton.LStick, false)
        setCombatButton(NativeButton.ZL, false)
        setCombatButton(NativeButton.A, false)
        skywardChargeHeld = true
        skywardTouchChargeHeld = true
        skywardHoldCandidate = false
        skywardTouchChargeStartedAt = SystemClock.uptimeMillis()
        skywardHoldAnchorX = combatLatestX
        skywardHoldAnchorY = combatLatestY
        setCombatStick(CombatDirection.Up.x, CombatDirection.Up.y)
        showActionFeedback("Skyward Charge held — swipe again to strike")
        performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
        invalidate()
    }

    private fun releaseCombatControls() {
        combatInputGeneration++
        combatTouchCaptured = false
        skywardChargeHeld = false
        skywardTouchChargeHeld = false
        cancelSkywardHoldCandidate()
        setCombatStick(0f, 0f)
        setCombatMovementStick(0f, 0f)
        setCombatButton(NativeButton.RStick, false)
        setCombatButton(NativeButton.LStick, false)
        setCombatButton(NativeButton.ZL, false)
        setCombatButton(NativeButton.A, false)
    }

    private fun pulseCombatButton(
        button: NativeButton,
        label: String,
        requireSword: Boolean = true
    ) {
        if (!combatReady(requireSword)) {
            showActionFeedback("Combat input unavailable in the current game state")
            return
        }
        val releasingSkywardCharge = skywardChargeHeld
        releaseCombatControls()
        val generation = combatInputGeneration
        val pressDelay = if (releasingSkywardCharge) 24L else 0L
        postDelayed({
            if (combatInputGeneration == generation) setCombatButton(button, true)
        }, pressDelay)
        postDelayed({
            if (combatInputGeneration == generation) setCombatButton(button, false)
        }, pressDelay + 72L)
        showActionFeedback(label)
    }

    private fun sendJumpSlash() {
        if (!combatReady()) {
            showActionFeedback("Jump slash unavailable in the current game state")
            return
        }
        releaseCombatControls()
        val generation = combatInputGeneration
        setCombatButton(NativeButton.ZL, true)
        setCombatMovementStick(0f, 1f)
        postDelayed({
            if (combatInputGeneration == generation) setCombatButton(NativeButton.A, true)
        }, 48L)
        postDelayed({
            if (combatInputGeneration == generation) {
                setCombatButton(NativeButton.A, false)
                setCombatMovementStick(0f, 0f)
                setCombatButton(NativeButton.ZL, false)
            }
        }, 132L)
        showActionFeedback("Native targeted jump-slash input sent")
    }

    private fun sendCombatStickSequence(
        directions: List<CombatDirection>,
        label: String,
        holdTarget: Boolean = false
    ) {
        if (!combatReady()) {
            showActionFeedback("Combat input unavailable in the current game state")
            return
        }
        val releasingSkywardCharge = skywardChargeHeld
        releaseCombatControls()
        val generation = combatInputGeneration
        // A charged sword-up hold must cross a real neutral HID frame before
        // the release direction. The game remains the authority on whether
        // Link charged long enough to produce a Skyward Strike.
        var delay = if (releasingSkywardCharge) 24L else 0L
        if (holdTarget) {
            setCombatButton(NativeButton.ZL, true)
            delay = 34L
        }
        directions.forEach { direction ->
            postDelayed({
                if (combatInputGeneration == generation) {
                    setCombatStick(direction.x, direction.y)
                }
            }, delay)
            delay += 48L
            postDelayed({
                if (combatInputGeneration == generation) setCombatStick(0f, 0f)
            }, delay)
            delay += 24L
        }
        postDelayed({
            if (combatInputGeneration == generation) {
                setCombatStick(0f, 0f)
                if (holdTarget) setCombatButton(NativeButton.ZL, false)
            }
        }, delay)
        showActionFeedback(label)
    }

    private fun toggleSkywardCharge() {
        if (!combatReady()) {
            showActionFeedback("Combat input unavailable in the current game state")
            return
        }
        if (skywardChargeHeld) {
            releaseCombatControls()
            showActionFeedback("Skyward charge released")
            return
        }
        releaseCombatControls()
        skywardChargeHeld = true
        setCombatStick(CombatDirection.Up.x, CombatDirection.Up.y)
        showActionFeedback("Native sword-up input held; swipe to strike")
    }

    private fun combatDirectionBetween(
        startX: Float,
        startY: Float,
        endX: Float,
        endY: Float
    ): CombatDirection {
        val nativeAngle = Math.toDegrees(
            atan2((startY - endY).toDouble(), (endX - startX).toDouble())
        )
        val normalizedAngle = (nativeAngle + 360.0) % 360.0
        val sector = (((normalizedAngle + 22.5) / 45.0).toInt() and 7)
        return CombatDirection.entries[sector]
    }

    private fun recognizeCombatGesture(points: List<CombatPoint>) {
        if (points.isEmpty()) return
        val start = points.first()
        val end = points.last()
        val elapsed = (end.time - start.time).coerceAtLeast(0L)
        var pathLength = 0f
        for (index in 1 until points.size) {
            pathLength += pointDistance(
                points[index - 1].x,
                points[index - 1].y,
                points[index].x,
                points[index].y
            )
        }
        val displacement = pointDistance(start.x, start.y, end.x, end.y)
        if (elapsed <= 450L && displacement < 38f && pathLength < 80f) {
            pulseCombatButton(NativeButton.RStick, "Native thrust input sent")
            return
        }
        if (elapsed !in 35L..1_400L) {
            showActionFeedback("Gesture rejected: swipe more quickly")
            return
        }

        val cardinalTurns = mutableListOf<Char>()
        var anchor = start
        for (point in points.drop(1)) {
            val dx = point.x - anchor.x
            val dy = point.y - anchor.y
            if (hypot(dx.toDouble(), dy.toDouble()) < 42.0) continue
            val direction = when {
                abs(dx) >= abs(dy) * 1.35f -> if (dx > 0f) 'R' else 'L'
                abs(dy) >= abs(dx) * 1.35f -> if (dy > 0f) 'D' else 'U'
                else -> null
            }
            if (direction != null) {
                if (cardinalTurns.lastOrNull() != direction) cardinalTurns += direction
                anchor = point
            }
        }
        val finalTurns = cardinalTurns.takeLast(3).joinToString("")
        val finalPair = cardinalTurns.takeLast(2).joinToString("")
        val horizontalSpan = points.maxOf { it.x } - points.minOf { it.x }
        val verticalSpan = points.maxOf { it.y } - points.minOf { it.y }
        if ((finalPair == "LR" || finalPair == "RL") && pathLength >= 150f &&
            horizontalSpan >= 120f && verticalSpan <= horizontalSpan * .72f
        ) {
            val sequence = if (finalPair == "LR") {
                listOf(CombatDirection.Left, CombatDirection.Right, CombatDirection.Left)
            } else {
                listOf(CombatDirection.Right, CombatDirection.Left, CombatDirection.Right)
            }
            sendCombatStickSequence(sequence, "Native horizontal spin input sent")
            return
        }
        if ((finalPair == "UD" || finalPair == "DU") && pathLength >= 150f &&
            verticalSpan >= 120f && horizontalSpan <= verticalSpan * .72f
        ) {
            val sequence = if (finalPair == "UD") {
                listOf(CombatDirection.Up, CombatDirection.Down, CombatDirection.Up)
            } else {
                listOf(CombatDirection.Down, CombatDirection.Up, CombatDirection.Down)
            }
            sendCombatStickSequence(sequence, "Native vertical spin input sent")
            return
        }
        if (finalTurns == "LRL" || finalTurns == "RLR") {
            val sequence = if (finalTurns == "LRL") {
                listOf(CombatDirection.Left, CombatDirection.Right, CombatDirection.Left)
            } else {
                listOf(CombatDirection.Right, CombatDirection.Left, CombatDirection.Right)
            }
            sendCombatStickSequence(sequence, "Native horizontal spin input sent")
            return
        }
        if (finalTurns == "UDU" || finalTurns == "DUD") {
            val sequence = if (finalTurns == "UDU") {
                listOf(CombatDirection.Up, CombatDirection.Down, CombatDirection.Up)
            } else {
                listOf(CombatDirection.Down, CombatDirection.Up, CombatDirection.Down)
            }
            sendCombatStickSequence(sequence, "Native vertical spin input sent")
            return
        }

        if (displacement < 88f || displacement / pathLength.coerceAtLeast(1f) < .62f) {
            showActionFeedback("Gesture rejected: use a clean directional swipe")
            return
        }
        val direction = combatDirectionBetween(start.x, start.y, end.x, end.y)
        sendCombatStickSequence(
            listOf(direction),
            "Native ${direction.displayName} sword input sent"
        )
    }

    private fun handleCombatTap(x: Float, y: Float) {
        if (pointDistance(downX, downY, x, y) > 40f) return
        when {
            combatStabBounds.contains(downX, downY) && combatStabBounds.contains(x, y) ->
                pulseCombatButton(NativeButton.RStick, "Native thrust input sent")
            combatShieldBounds.contains(downX, downY) && combatShieldBounds.contains(x, y) ->
                pulseCombatButton(
                    NativeButton.LStick,
                    "Native shield-bash input sent",
                    requireSword = false
                )
            combatHorizontalSpinBounds.contains(downX, downY) && combatHorizontalSpinBounds.contains(x, y) ->
                sendCombatStickSequence(
                    listOf(CombatDirection.Left, CombatDirection.Right, CombatDirection.Left),
                    "Native horizontal spin input sent"
                )
            combatVerticalSpinBounds.contains(downX, downY) && combatVerticalSpinBounds.contains(x, y) ->
                sendCombatStickSequence(
                    listOf(CombatDirection.Up, CombatDirection.Down, CombatDirection.Up),
                    "Native vertical spin input sent"
                )
            combatFinalBlowBounds.contains(downX, downY) && combatFinalBlowBounds.contains(x, y) ->
                sendCombatStickSequence(
                    listOf(CombatDirection.Up, CombatDirection.Down, CombatDirection.Up),
                    "Native targeted final-blow input sent",
                    holdTarget = true
                )
            combatJumpSlashBounds.contains(downX, downY) && combatJumpSlashBounds.contains(x, y) ->
                sendJumpSlash()
            combatSkywardBounds.contains(downX, downY) && combatSkywardBounds.contains(x, y) ->
                toggleSkywardCharge()
        }
    }

    private fun handleInventoryTap(x: Float, y: Float) {
        categoryBounds.indexOfFirst { it.contains(x, y) }.takeIf { it >= 0 }?.let {
            category = it
            selectedIndex = 0
            inventoryFocus = InventoryFocus.Grid
            queueSnapshotIcons(snapshot)
            return
        }
        val focusTarget = when {
            inventoryStatusBounds.contains(x, y) -> InventoryFocus.Status
            shieldEquipmentBounds.contains(x, y) -> InventoryFocus.Shield
            bItemEquipmentBounds.contains(x, y) -> InventoryFocus.BItem
            selectedEquipmentBounds.contains(x, y) -> InventoryFocus.Selected
            dowsingEquipmentBounds.contains(x, y) -> InventoryFocus.Dowsing
            else -> null
        }
        if (focusTarget != null) {
            inventoryFocus = focusTarget
            if (itemInputPending) showActionFeedback("An item action is already in progress")
            else activateFocusedSelection()
            return
        }
        val items = inventoryItems()
        val pageOffset = inventoryPageOffset(items.size)
        val selectedItem = items.getOrNull(selectedIndex)
        if (selectedItem != null && selectionHasNativeAction(selectedItem) &&
            detailActionBounds(inventoryDetailBounds).contains(x, y)
        ) {
            inventoryFocus = InventoryFocus.Grid
            activateSelection()
            return
        }
        if (inventoryPreviousBounds.contains(x, y) && pageOffset > 0) {
            inventoryFocus = InventoryFocus.Grid
            selectedIndex = (pageOffset - INVENTORY_PAGE_SIZE).coerceAtLeast(0)
            return
        }
        if (inventoryNextBounds.contains(x, y) && pageOffset + INVENTORY_PAGE_SIZE < items.size) {
            inventoryFocus = InventoryFocus.Grid
            selectedIndex = pageOffset + INVENTORY_PAGE_SIZE
            return
        }
        if (x !in 32f..684f || y !in 178f..608f) return
        val column = ((x - 32f) / 132f).toInt().coerceIn(0, INVENTORY_GRID_COLUMNS - 1)
        val row = ((y - 178f) / 109.5f).toInt().coerceIn(0, INVENTORY_GRID_ROWS - 1)
        val index = pageOffset + row * INVENTORY_GRID_COLUMNS + column
        if (index < items.size) {
            inventoryFocus = InventoryFocus.Grid
            if (selectedIndex == index) activateSelection() else selectedIndex = index
        }
    }

    override fun onSshdControllerKey(event: KeyEvent): Boolean {
        if (event.keyCode == KeyEvent.KEYCODE_BUTTON_MODE) {
            if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
                controllerActive = !controllerActive
                invalidate()
            }
            return true
        }
        if (!controllerActive) return false
        if (event.action == KeyEvent.ACTION_UP) return isNavigationKey(event.keyCode)
        if (event.action != KeyEvent.ACTION_DOWN) return false
        val handled = when (event.keyCode) {
            KeyEvent.KEYCODE_BUTTON_L1 -> {
                cyclePage(-1)
                true
            }
            KeyEvent.KEYCODE_BUTTON_R1 -> {
                cyclePage(1)
                true
            }
            KeyEvent.KEYCODE_BUTTON_L2 -> cycleInventoryCategory(-1)
            KeyEvent.KEYCODE_BUTTON_R2 -> cycleInventoryCategory(1)
            KeyEvent.KEYCODE_DPAD_LEFT -> moveSelection(-1, 0)
            KeyEvent.KEYCODE_DPAD_RIGHT -> moveSelection(1, 0)
            KeyEvent.KEYCODE_DPAD_UP -> moveSelection(0, -1)
            KeyEvent.KEYCODE_DPAD_DOWN -> moveSelection(0, 1)
            KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BACK -> {
                controllerActive = false
                true
            }
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_ENTER,
            KeyEvent.KEYCODE_DPAD_CENTER ->
                if (event.repeatCount > 0) true else activateFocusedSelection()
            else -> false
        }
        if (handled) invalidate()
        return handled
    }

    override fun onSshdControllerMotion(event: MotionEvent): Boolean {
        if (!controllerActive || event.action != MotionEvent.ACTION_MOVE ||
            event.source and InputDevice.SOURCE_JOYSTICK != InputDevice.SOURCE_JOYSTICK
        ) {
            return false
        }
        val hatHorizontal = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hatVertical = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
        val horizontal = if (abs(hatHorizontal) >= .55f) hatHorizontal else event.getAxisValue(MotionEvent.AXIS_X)
        val vertical = if (abs(hatVertical) >= .55f) hatVertical else event.getAxisValue(MotionEvent.AXIS_Y)
        if (abs(horizontal) < .55f && abs(vertical) < .55f) return false
        val now = SystemClock.uptimeMillis()
        if (now - controllerMotionAt < 170L) return true
        controllerMotionAt = now
        if (abs(horizontal) >= abs(vertical)) {
            moveSelection(if (horizontal < 0f) -1 else 1, 0)
        } else {
            moveSelection(0, if (vertical < 0f) -1 else 1)
        }
        invalidate()
        return true
    }

    private fun moveSelection(dx: Int, dy: Int): Boolean {
        when (page) {
            Page.Inventory -> {
                val count = inventoryItems().size
                if (inventoryFocus != InventoryFocus.Grid) {
                    moveInventoryFocus(dx, dy)
                } else if (count > 0) {
                    selectedIndex = selectedIndex.coerceIn(0, count - 1)
                    val column = selectedIndex % INVENTORY_GRID_COLUMNS
                    when {
                        dx > 0 && column < INVENTORY_GRID_COLUMNS - 1 && selectedIndex + 1 < count -> selectedIndex++
                        dx > 0 -> {
                            val visibleRow = (selectedIndex % INVENTORY_PAGE_SIZE) / INVENTORY_GRID_COLUMNS
                            inventoryFocus = when (visibleRow) {
                                0 -> InventoryFocus.Status
                                1 -> InventoryFocus.Shield
                                2 -> InventoryFocus.BItem
                                else -> InventoryFocus.Selected
                            }
                        }
                        dx < 0 && column > 0 -> selectedIndex--
                        dy < 0 && selectedIndex - INVENTORY_GRID_COLUMNS >= 0 ->
                            selectedIndex -= INVENTORY_GRID_COLUMNS
                        dy > 0 && selectedIndex + INVENTORY_GRID_COLUMNS < count ->
                            selectedIndex += INVENTORY_GRID_COLUMNS
                    }
                } else if (dx > 0) {
                    inventoryFocus = InventoryFocus.BItem
                }
            }
            Page.Map -> {
                mapViewOffsetX -= dx * 42f
                mapViewOffsetY -= dy * 42f
                constrainMapView()
                return true
            }
            Page.Combat -> return true
            Page.Progress -> return true
        }
        return true
    }

    private fun moveInventoryFocus(dx: Int, dy: Int) {
        if (dx < 0 && inventoryFocus != InventoryFocus.Grid) {
            inventoryFocus = InventoryFocus.Grid
            return
        }
        inventoryFocus = when (inventoryFocus) {
            InventoryFocus.Grid -> InventoryFocus.Grid
            InventoryFocus.Status -> when {
                dy > 0 -> InventoryFocus.Shield
                else -> InventoryFocus.Status
            }
            InventoryFocus.Shield -> when {
                dy < 0 -> InventoryFocus.Status
                dy > 0 -> InventoryFocus.BItem
                else -> InventoryFocus.Shield
            }
            InventoryFocus.BItem -> when {
                dy < 0 -> InventoryFocus.Shield
                dy > 0 -> InventoryFocus.Selected
                else -> InventoryFocus.BItem
            }
            InventoryFocus.Selected -> when {
                dy < 0 -> InventoryFocus.BItem
                dy > 0 -> InventoryFocus.Dowsing
                else -> InventoryFocus.Selected
            }
            InventoryFocus.Dowsing -> when {
                dy < 0 -> InventoryFocus.Selected
                else -> InventoryFocus.Dowsing
            }
        }
    }

    private fun cycleInventoryCategory(delta: Int): Boolean {
        if (page != Page.Inventory) return true
        category = (category + delta + CATEGORY_TITLES.size) % CATEGORY_TITLES.size
        selectedIndex = 0
        inventoryFocus = InventoryFocus.Grid
        queueSnapshotIcons(snapshot)
        performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK)
        return true
    }

    private fun cyclePage(delta: Int) {
        changePage(Page.entries[(page.ordinal + delta + Page.entries.size) % Page.entries.size])
    }

    private fun changePage(next: Page) {
        if (page == next) return
        cancelItemInput()
        if (page == Page.Combat || next == Page.Combat) releaseCombatControls()
        page = next
        selectedIndex = 0
        inventoryFocus = InventoryFocus.Grid
        pageChangedAt = SystemClock.uptimeMillis()
        updatePlayerPresentation()
    }

    private fun updatePlayerPresentation() {
        val outfit = snapshot.linkOutfit.takeIf {
            snapshot.status == ACTIVE_STATUS &&
                hasCapability(CAPABILITY_LINK_APPEARANCE_STATE) && it in 0..1
        }
        if (outfit == null) {
            playerOutfit = -1
        } else if (playerOutfit != outfit) {
            playerOutfit = outfit
            onPlayerModelChanged(outfit)
        }
        updateMapPresentation()
        setPlayerVisible(page == Page.Inventory && outfit != null)
    }

    private fun updateMapPresentation() {
        onMapAspectChanged(mapDisplayAspect)
        onMapVisibilityChanged(page == Page.Map && snapshot.status == ACTIVE_STATUS && mapGpuAvailable)
    }

    private fun setPlayerVisible(visible: Boolean) {
        if (playerVisible == visible) return
        playerVisible = visible
        onPlayerVisibilityChanged(visible)
    }

    private fun pageTransitionProgress(): Float {
        if (pageChangedAt == 0L) return 1f
        val linear = ((SystemClock.uptimeMillis() - pageChangedAt) / 220f).coerceIn(0f, 1f)
        val inverse = 1f - linear
        return 1f - inverse * inverse * inverse
    }

    private fun isNavigationKey(keyCode: Int): Boolean = keyCode in setOf(
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
        KeyEvent.KEYCODE_BACK
    )

    private fun ByteBuffer.u8(offset: Int): Int = get(offset).toInt() and 0xFF

    private fun ByteBuffer.u16(offset: Int): Int = getShort(offset).toInt() and 0xFFFF

    private fun ByteBuffer.optionalU16(offset: Int): Int = u16(offset).let { if (it == 0xFFFF) -1 else it }

    private fun ByteBuffer.ascii(offset: Int, length: Int): String {
        val bytes = ByteArray(length)
        for (index in 0 until length) bytes[index] = get(offset + index)
        val end = bytes.indexOf(0).let { if (it < 0) bytes.size else it }
        return String(bytes, 0, end, Charsets.US_ASCII)
    }

    private fun parseMsbt(data: ByteArray): Map<String, String> {
        if (data.size < 0x20 || String(data, 0, 8, Charsets.US_ASCII) != "MsgStdBn") {
            return emptyMap()
        }
        val little = data[8] == 0xFF.toByte() && data[9] == 0xFE.toByte()
        val big = data[8] == 0xFE.toByte() && data[9] == 0xFF.toByte()
        if (!little && !big) return emptyMap()
        fun u16(offset: Int): Int {
            if (offset < 0 || offset + 2 > data.size) return -1
            val a = data[offset].toInt() and 0xFF
            val b = data[offset + 1].toInt() and 0xFF
            return if (little) a or (b shl 8) else (a shl 8) or b
        }
        fun u32(offset: Int): Int {
            if (offset < 0 || offset + 4 > data.size) return -1
            val a = data[offset].toInt() and 0xFF
            val b = data[offset + 1].toInt() and 0xFF
            val c = data[offset + 2].toInt() and 0xFF
            val d = data[offset + 3].toInt() and 0xFF
            return if (little) a or (b shl 8) or (c shl 16) or (d shl 24) else
                (a shl 24) or (b shl 16) or (c shl 8) or d
        }
        val sections = mutableMapOf<String, Pair<Int, Int>>()
        var sectionOffset = 0x20
        for (section in 0 until u16(0x0E).coerceIn(0, 32)) {
            if (sectionOffset + 0x10 > data.size) break
            val name = String(data, sectionOffset, 4, Charsets.US_ASCII)
            val size = u32(sectionOffset + 4)
            val start = sectionOffset + 0x10
            if (size < 0 || start + size > data.size) break
            sections[name] = start to size
            sectionOffset = (start + size + 0x0F) and 0x0F.inv()
        }
        val labelSection = sections["LBL1"] ?: return emptyMap()
        val textSection = sections["TXT2"] ?: return emptyMap()
        val labels = mutableMapOf<Int, String>()
        val groupCount = u32(labelSection.first)
        if (groupCount !in 0..65536) return emptyMap()
        for (group in 0 until groupCount) {
            val table = labelSection.first + 4 + group * 8
            val count = u32(table)
            var entry = labelSection.first + u32(table + 4)
            if (count !in 0..65536) continue
            for (labelIndex in 0 until count) {
                if (entry !in data.indices) break
                val length = data[entry].toInt() and 0xFF
                entry++
                if (entry + length + 4 > data.size) break
                val label = String(data, entry, length, Charsets.UTF_8)
                entry += length
                labels[u32(entry)] = label
                entry += 4
            }
        }
        val textCount = u32(textSection.first)
        if (textCount !in 0..65536) return emptyMap()
        val offsets = IntArray(textCount) { u32(textSection.first + 4 + it * 4) }
        val encoding: Charset = Charset.forName(if (little) "UTF-16LE" else "UTF-16BE")
        return labels.mapNotNull { (index, label) ->
            if (index !in offsets.indices) return@mapNotNull null
            var cursor = textSection.first + offsets[index]
            val end = textSection.first + if (index + 1 < offsets.size) offsets[index + 1] else textSection.second
            if (cursor !in 0..data.size || end !in cursor..data.size) return@mapNotNull null
            val clean = ByteArrayOutputStream()
            while (cursor + 2 <= end) {
                val code = u16(cursor)
                if (code == 0) break
                if (code == 0x000E && cursor + 8 <= end) {
                    val payload = u16(cursor + 6)
                    cursor += 8 + payload
                } else if (code == 0x000F && cursor + 6 <= end) {
                    cursor += 6
                } else {
                    clean.write(data, cursor, 2)
                    cursor += 2
                }
            }
            label to String(clean.toByteArray(), encoding).replace("\r", "")
        }.toMap()
    }
}
