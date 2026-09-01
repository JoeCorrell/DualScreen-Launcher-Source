


package org.yuzu.yuzu_emu.activities

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Typeface
import android.os.Process
import android.os.SystemClock
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import android.util.LruCache
import java.io.File
import java.io.FileOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.TimeUnit
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sqrt
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.utils.Log





internal class Mk8dCompanionView(context: Context) : View(context) {
    private companion object {
        private const val DESIGN_WIDTH = 1240f
        private const val DESIGN_HEIGHT = 1080f
        private const val SNAPSHOT_SIZE = 312
        private const val SNAPSHOT_MAGIC = 0x44384B4D



        private const val SNAPSHOT_INTERVAL_MS = 67L
        private const val ASSET_RETRY_BASE_MS = 2_000L
        private const val ASSET_RETRY_MAX_MS = 30_000L
        private const val RACER_COUNT = 12
        private const val COIN_ITEM_ID = 15
        private const val THEME_PREFERENCES = "mk8d_companion_theme"
        private const val DARK_MODE_KEY = "dark_mode"
        private const val MAX_RUNTIME_ASSET_BYTES = 8 * 1024 * 1024
        private const val LEFT_WIDTH = 402f
        private const val ROW_TOP = 12f
        private const val ROW_HEIGHT = 88f
        private val MAP_BOUNDS = RectF(430f, 20f, 1220f, 852f)
        private val ITEM_SLOT_0 = RectF(862f, 878f, 1028f, 1052f)
        private val ITEM_SLOT_1 = RectF(1046f, 878f, 1212f, 1052f)
        private val ITEM_ICON_0 = RectF(893f, 895f, 997f, 1005f)
        private val ITEM_ICON_1 = RectF(1077f, 895f, 1181f, 1005f)
        private val COIN_SLOT = RectF(678f, 878f, 844f, 1052f)
        private val COIN_ICON_BOUNDS = RectF(709f, 895f, 813f, 1005f)



        private val DRIVER_INTERNAL: Array<String?> = arrayOf(
            "Mario", "Luigi", "Peach", "Daisy", "Yoshi", "Kinopio", "Kinopico",
            "Nokonoko", "Koopa", "DK", "Wario", "Waluigi", "Rosetta", "MetalMario",
            "PGoldPeach", "Jugemu", "Heyho", "BbMario", "BbLuigi", "BbPeach", "BbDaisy",
            "BbRosetta", "Larry", "Lemmy", "Wendy", "Ludwig", "Iggy", "Roy", "Morton",
            null, "TanukiMario", "Link", "AnimalBoyA", "Shizue", "CatPeach", "HoneKoopa",
            "AnimalGirlA", "GoldMario", "Karon", "KoopaJr", "KingTeresa", "SplatoonGirl",
            "SplatoonBoy", "LinkBotw", "Catherine", "Kameck", "BossPakkun", "Hanachan",
            "DiddyKong", "FK", "Kinopeach", "Pauline", null
        )

        private val DRIVER_NAMES = arrayOf(
            "Mario", "Luigi", "Peach", "Daisy", "Yoshi", "Toad", "Toadette",
            "Koopa Troopa", "Bowser", "Donkey Kong", "Wario", "Waluigi", "Rosalina",
            "Mario", "Peach", "Lakitu", "Shy Guy", "Baby Mario",
            "Baby Luigi", "Baby Peach", "Baby Daisy", "Baby Rosalina", "Larry", "Lemmy",
            "Wendy", "Ludwig", "Iggy", "Roy", "Morton", "Mii", "Mario", "Link",
            "Villager", "Isabelle", "Peach", "Bowser", "Villager", "Mario",
            "Dry Bones", "Bowser Jr.", "King Boo", "Inkling Girl", "Inkling Boy",
            "Link", "Birdo", "Kamek", "Petey Piranha", "Wiggler", "Diddy Kong",
            "Funky Kong", "Peachette", "Pauline", "Mii"
        )




        private val COURSE_INTERNAL = arrayOfNulls<String>(0x7C).apply {
            arrayOf(
                "Gu_MarioCircuit", "Gu_DossunIseki", "Gu_City", "Gu_Cake",
                "Gu_HorrorHouse", "Gu_Expert", "Gu_Desert", "Gu_Cloud",
                "Gu_SnowMountain", "Gu_Techno", "Gu_Airport", "Gu_FirstCircuit",
                "Gu_WaterPark", "Gu_Ocean", "Gu_BowserCastle", "Gu_RainbowRoad",
                "G3ds_DKJungle", "Gwii_MooMooMeadows", "G64_PeachCircuit",
                "G64_KinopioHighway", "Gds_PukupukuBeach", "Ggc_SherbetLand",
                "Gagb_MarioCircuit", "G3ds_MusicPark", "Gwii_GrumbleVolcano",
                "Gsfc_DonutsPlain3", "Ggc_DryDryDesert", "G3ds_PackunSlider",
                "Gds_TickTockClock", "G64_YoshiValley", "Gds_WarioStadium",
                "G64_RainbowRoad"
            ).forEachIndexed { index, name -> this[0x11 + index] = name }
            arrayOf(
                "Du_Metro", "Du_MuteCity", "Du_DragonRoad", "Du_Hyrule",
                "Du_Animal_Summer", "Du_ExciteBike", "Du_Woods", "Du_IcePark",
                "Dgc_YoshiCircuit", "Dwii_WariosMine", "Dsfc_RainbowRoad",
                "Dagb_RibbonRoad", "D3ds_NeoBowserCity", "Dgc_BabyPark",
                "Dagb_CheeseLand", "Du_BigBlue", "Du_Animal_Spring", "Du_Animal_Autumn",
                "Du_Animal_Winter", "B3ds_WuhuTown", "Bgc_LuigiMansion", "Bsfc_Battle1",
                "Bu_DekaLine", "Bu_Moon", "Bu_BattleStadium", "Bu_Dojo", "Bu_Sweets"
            ).forEachIndexed { index, name -> this[0x31 + index] = name }
            arrayOf(
                "Cnsw_11", "Cnsw_12", "Cnsw_13", "Cnsw_14", "Cnsw_15", "Cnsw_16",
                "Cnsw_17", "Cnsw_18", "Cnsw_21", "Cnsw_22", "Cnsw_23", "Cnsw_24",
                "Cnsw_25", "Cnsw_26", "Cnsw_27", "Cnsw_28", "Cnsw_31", "Cnsw_33",
                "Cnsw_34", "Cnsw_62", "Cnsw_35", "Cnsw_32", "Cnsw_37", "Cnsw_38",
                "Cnsw_41", "Cnsw_47", "Cnsw_42", "Cnsw_44", "Cnsw_55", "Cnsw_43",
                "Cnsw_36", "Cnsw_45", "Cnsw_65", "Cnsw_46", "Cnsw_63", "Cnsw_58",
                "Cnsw_48", "Cnsw_53", "Cnsw_52", "Cnsw_61", "Cnsw_54", "Cnsw_56",
                "Cnsw_66", "Cnsw_64", "Cnsw_51", "Cnsw_67", "Cnsw_57", "Cnsw_68"
            ).forEachIndexed { index, name -> this[0x4C + index] = name }
        }


        private val CNSW_COURSE_NAMES = mapOf(
            "Cnsw_11" to "Paris Promenade",
            "Cnsw_12" to "Toad Circuit",
            "Cnsw_13" to "Choco Mountain",
            "Cnsw_14" to "Coconut Mall",
            "Cnsw_15" to "Tokyo Blur",
            "Cnsw_16" to "Shroom Ridge",
            "Cnsw_17" to "Sky Garden",
            "Cnsw_18" to "Ninja Hideaway",
            "Cnsw_21" to "New York Minute",
            "Cnsw_22" to "Mario Circuit 3",
            "Cnsw_23" to "Kalimari Desert",
            "Cnsw_24" to "Waluigi Pinball",
            "Cnsw_25" to "Sydney Sprint",
            "Cnsw_26" to "Snow Land",
            "Cnsw_27" to "Mushroom Gorge",
            "Cnsw_28" to "Sky-High Sundae",
            "Cnsw_31" to "London Loop",
            "Cnsw_32" to "Boo Lake",
            "Cnsw_33" to "Rock Rock Mountain",
            "Cnsw_34" to "Maple Treeway",
            "Cnsw_35" to "Berlin Byways",
            "Cnsw_36" to "Peach Gardens",
            "Cnsw_37" to "Merry Mountain",
            "Cnsw_38" to "Rainbow Road",
            "Cnsw_41" to "Amsterdam Drift",
            "Cnsw_42" to "Riverside Park",
            "Cnsw_43" to "DK Summit",
            "Cnsw_44" to "Yoshi's Island",
            "Cnsw_45" to "Bangkok Rush",
            "Cnsw_46" to "Mario Circuit",
            "Cnsw_47" to "Waluigi Stadium",
            "Cnsw_48" to "Singapore Speedway",
            "Cnsw_51" to "Athens Dash",
            "Cnsw_52" to "Daisy Cruiser",
            "Cnsw_53" to "Moonview Highway",
            "Cnsw_54" to "Squeaky Clean Sprint",
            "Cnsw_55" to "Los Angeles Laps",
            "Cnsw_56" to "Sunset Wilds",
            "Cnsw_57" to "Koopa Cape",
            "Cnsw_58" to "Vancouver Velocity",
            "Cnsw_61" to "Rome Avanti",
            "Cnsw_62" to "DK Mountain",
            "Cnsw_63" to "Daisy Circuit",
            "Cnsw_64" to "Piranha Plant Cove",
            "Cnsw_65" to "Madrid Drive",
            "Cnsw_66" to "Rosalina's Ice World",
            "Cnsw_67" to "Bowser Castle 3",
            "Cnsw_68" to "Rainbow Road"
        )

        private val ITEM_NAMES = arrayOf(
            "Banana", "Green Shell", "Red Shell", "Mushroom", "Bob-omb", "Blooper",
            "Spiny Shell", "Triple Mushrooms", "Star", "Bullet Bill", "Lightning",
            "Golden Mushroom", "Fire Flower", "Piranha Plant", "Boomerang Flower", "Coin",
            "Super Horn", "Triple Bananas", "Triple Green Shells", "Triple Red Shells",
            "Crazy Eight", "Feather", "Boo"
        )

        private val DRIVER_COLORS = IntArray(DRIVER_INTERNAL.size) { id ->
            Color.HSVToColor(floatArrayOf(((id * 47) % 360).toFloat(), .68f, .86f))
        }
    }

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 3f
    }
    private val path = Path()
    private val typeface = Typeface.create("sans-serif", Typeface.NORMAL)
    private val boldTypeface = Typeface.create("sans-serif", Typeface.BOLD)
    private val snapshotBuffer = ByteBuffer.allocateDirect(SNAPSHOT_SIZE).order(ByteOrder.LITTLE_ENDIAN)
    private val frameLock = Any()
    private val themePreferences =
        context.getSharedPreferences(THEME_PREFERENCES, Context.MODE_PRIVATE)
    private val touchSlop = ViewConfiguration.get(context).scaledTouchSlop.toFloat()
    private var poller: ScheduledExecutorService? = null
    private var assetExecutor: ExecutorService? = null
    private var assetGeneration = 0
    @Volatile private var assetRevision: String? = null
    private var background: Bitmap? = null
    private var darkMode = themePreferences.getBoolean(DARK_MODE_KEY, false)
    private val bitmapCache = object : LruCache<String, Bitmap>(20 * 1024) {
        override fun sizeOf(key: String, value: Bitmap): Int = max(1, value.byteCount / 1024)
    }
    private val missingAssets = mutableSetOf<String>()
    private val runtimeAssetRequests = ConcurrentHashMap.newKeySet<String>()
    private val runtimeAssetMissing = ConcurrentHashMap.newKeySet<String>()
    private val runtimeAssetRetryAfter = ConcurrentHashMap<String, Long>()
    private val runtimeAssetFailures = ConcurrentHashMap<String, Int>()
    private val runtimeBytes = ConcurrentHashMap<String, ByteArray>()
    private val racerPortraitBounds = Array(RACER_COUNT) { row ->
        val top = ROW_TOP + row * ROW_HEIGHT
        RectF(70f, top + 5f, 144f, top + ROW_HEIGHT - 10f)
    }
    private val racerItemBounds = Array(RACER_COUNT) { row ->
        val top = ROW_TOP + row * ROW_HEIGHT
        RectF(148f, top + 15f, 198f, top + ROW_HEIGHT - 20f)
    }
    private val markerBounds = Array(RACER_COUNT) { RectF() }
    private val lapFlagBounds = RectF()
    private val driverPortraits = arrayOfNulls<Bitmap>(RACER_COUNT)
    private val loadedDriverId = IntArray(RACER_COUNT) { Int.MIN_VALUE }
    private val loadedDriverVariant = IntArray(RACER_COUNT) { Int.MIN_VALUE }
    private val itemBitmaps = arrayOfNulls<Bitmap>(ITEM_NAMES.size)

    @Volatile private var status = 0
    private var sequence = 0
    private var courseId = -1
    private var playerCount = 0
    private var localIndex = -1
    private var slotRollingMask = 0
    private var lapTotal = 0
    private val active = BooleanArray(RACER_COUNT)
    private val local = BooleanArray(RACER_COUNT)
    private val rank = IntArray(RACER_COUNT)
    private val lap = IntArray(RACER_COUNT)
    private val driverId = IntArray(RACER_COUNT) { -1 }
    private val driverVariant = IntArray(RACER_COUNT)
    private val item0 = IntArray(RACER_COUNT) { -1 }
    private val item1 = IntArray(RACER_COUNT) { -1 }
        private val item0Count = IntArray(RACER_COUNT)
        private val item1Count = IntArray(RACER_COUNT)
        private val coins = IntArray(RACER_COUNT)
    private val worldX = FloatArray(RACER_COUNT)
    private val worldY = FloatArray(RACER_COUNT)
    private val worldZ = FloatArray(RACER_COUNT)
    private val mapPointX = FloatArray(RACER_COUNT)
    private val mapPointY = FloatArray(RACER_COUNT)
    private val mapPointValid = BooleanArray(RACER_COUNT)
    private val displayOrder = IntArray(RACER_COUNT) { it }
    private var pressedSlot = -1
    private var rejectedSlot = -1
    private var gestureActive = false
    private var longPressTriggered = false
    private var touchDownX = 0f
    private var touchDownY = 0f
    private val toggleDarkMode = Runnable {
        if (!gestureActive) return@Runnable
        darkMode = !darkMode
        themePreferences.edit().putBoolean(DARK_MODE_KEY, darkMode).apply()
        background?.recycle()
        background = createBackground()
        pressedSlot = -1
        longPressTriggered = true
        performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
        invalidate()
    }
    private val clearRejectedSlot = Runnable {
        rejectedSlot = -1
        invalidate()
    }
    private var lastCourseBitmapId = Int.MIN_VALUE
    private var courseBitmap: Bitmap? = null
    private var lastMapCameraId = Int.MIN_VALUE
    private var mapCamera: MapCamera? = null
    private var lapFlagBitmap: Bitmap? = null
    private var courseLabel = "Course"

    private data class Vec3(val x: Float, val y: Float, val z: Float)

    private class MapCamera(
        private val lookAt: Vec3,
        private val rightX: Float,
        private val rightY: Float,
        private val rightZ: Float,
        private val upX: Float,
        private val upY: Float,
        private val upZ: Float,
        private val width: Float,
        private val height: Float
    ) {
        fun project(
            x: Float,
            y: Float,
            z: Float,
            bounds: RectF,
            outputX: FloatArray,
            outputY: FloatArray,
            index: Int
        ): Boolean {
            val dx = x - lookAt.x
            val dy = y - lookAt.y
            val dz = z - lookAt.z
            val viewX = dx * rightX + dy * rightY + dz * rightZ
            val viewY = dx * upX + dy * upY + dz * upZ
            if (!viewX.isFinite() || !viewY.isFinite() || width <= 0f || height <= 0f) return false
            val u = .5f + viewX / width
            val v = .5f - viewY / height
            outputX[index] = bounds.left + u * bounds.width()
            outputY[index] = bounds.top + v * bounds.height()
            return true
        }

        companion object {
            fun create(position: Vec3, lookAt: Vec3, up: Vec3, width: Float, height: Float): MapCamera? {
                val forwardLength = sqrt(
                    (lookAt.x - position.x) * (lookAt.x - position.x) +
                        (lookAt.y - position.y) * (lookAt.y - position.y) +
                        (lookAt.z - position.z) * (lookAt.z - position.z)
                )
                if (!forwardLength.isFinite() || forwardLength < .0001f) return null
                val forwardX = (lookAt.x - position.x) / forwardLength
                val forwardY = (lookAt.y - position.y) / forwardLength
                val forwardZ = (lookAt.z - position.z) / forwardLength
                var rightX = forwardY * up.z - forwardZ * up.y
                var rightY = forwardZ * up.x - forwardX * up.z
                var rightZ = forwardX * up.y - forwardY * up.x
                val rightLength = sqrt(rightX * rightX + rightY * rightY + rightZ * rightZ)
                if (!rightLength.isFinite() || rightLength < .0001f) return null
                rightX /= rightLength
                rightY /= rightLength
                rightZ /= rightLength
                val realUpX = rightY * forwardZ - rightZ * forwardY
                val realUpY = rightZ * forwardX - rightX * forwardZ
                val realUpZ = rightX * forwardY - rightY * forwardX
                return MapCamera(
                    lookAt,
                    rightX,
                    rightY,
                    rightZ,
                    realUpX,
                    realUpY,
                    realUpZ,
                    width,
                    height
                )
            }
        }
    }

    init {
        isClickable = true
        isLongClickable = true
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        if (poller != null) return
        assetGeneration++
        assetRevision = null
        runtimeAssetRequests.clear()
        runtimeAssetMissing.clear()
        runtimeAssetRetryAfter.clear()
        runtimeAssetFailures.clear()
        runtimeBytes.clear()
        assetExecutor = Executors.newSingleThreadExecutor { runnable ->
            Thread(
                {
                    Process.setThreadPriority(Process.THREAD_PRIORITY_LOWEST)
                    runnable.run()
                },
                "MK8D-RomFS-Assets"
            ).apply {
                isDaemon = true
                priority = Thread.MIN_PRIORITY
            }
        }
        poller = Executors.newSingleThreadScheduledExecutor { runnable ->
            Thread(
                {
                    Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND)
                    runnable.run()
                },
                "MK8D-Companion-Telemetry"
            ).apply {
                isDaemon = true
                priority = Thread.NORM_PRIORITY - 1
            }
        }.also { executor ->
            executor.scheduleWithFixedDelay(
                {
                    try {
                        pollSnapshot()
                    } catch (exception: Throwable) {
                        Log.error("[MK8D Companion] Snapshot poll failed: ${exception.message}")
                    }
                },
                0,
                SNAPSHOT_INTERVAL_MS,
                TimeUnit.MILLISECONDS
            )
        }
    }

    override fun onDetachedFromWindow() {
        removeCallbacks(clearRejectedSlot)
        removeCallbacks(toggleDarkMode)
        gestureActive = false
        rejectedSlot = -1
        poller?.shutdownNow()
        poller = null
        assetGeneration++
        assetExecutor?.shutdownNow()
        assetExecutor = null
        runtimeAssetRequests.clear()
        runtimeAssetMissing.clear()
        runtimeAssetRetryAfter.clear()
        runtimeAssetFailures.clear()
        runtimeBytes.clear()
        super.onDetachedFromWindow()
    }

    private fun pollSnapshot() {
        snapshotBuffer.clear()
        if (NativeLibrary.fillMk8dCompanionSnapshot(snapshotBuffer) != SNAPSHOT_SIZE) return
        if (snapshotBuffer.getInt(0) != SNAPSHOT_MAGIC || snapshotBuffer.getShort(4).toInt() != 1) return

        var changed: Boolean
        var assetsChanged: Boolean
        var animateRoulette: Boolean
        synchronized(frameLock) {
            val nextSequence = snapshotBuffer.getInt(8)
            val nextStatus = snapshotBuffer.getInt(12)
            val nextCourse = snapshotBuffer.getInt(16)
            val nextCount = snapshotBuffer.get(20).toInt() and 0xFF
            val nextLocal = snapshotBuffer.get(21).toInt()
            val nextRollingMask = snapshotBuffer.get(22).toInt() and 0x03
            val nextLapTotal = snapshotBuffer.get(23).toInt() and 0xFF
            changed = nextStatus != status || nextCourse != courseId || nextCount != playerCount ||
                nextLocal != localIndex || nextRollingMask != slotRollingMask ||
                nextLapTotal != lapTotal
            assetsChanged = (nextStatus == 4 && nextStatus != status) ||
                nextCourse != courseId || (nextRollingMask != 0 && nextRollingMask != slotRollingMask)
            sequence = nextSequence
            status = nextStatus
            courseId = nextCourse
            playerCount = nextCount.coerceIn(0, RACER_COUNT)
            localIndex = nextLocal
            slotRollingMask = nextRollingMask
            lapTotal = nextLapTotal
            for (index in 0 until RACER_COUNT) {
                val offset = 24 + index * 24
                val nextActive = snapshotBuffer.get(offset).toInt() != 0
                val nextIsLocal = snapshotBuffer.get(offset + 1).toInt() != 0
                val nextRank = snapshotBuffer.get(offset + 2).toInt() and 0xFF
                val nextLap = snapshotBuffer.get(offset + 3).toInt() and 0xFF
                val nextDriver = snapshotBuffer.getShort(offset + 4).toInt()
                val nextItem0 = snapshotBuffer.get(offset + 6).toInt()
                val nextItem1 = snapshotBuffer.get(offset + 7).toInt()
                val nextItem0Count = snapshotBuffer.get(offset + 8).toInt() and 0xFF
                val nextItem1Count = snapshotBuffer.get(offset + 9).toInt() and 0xFF
                val nextDriverVariant = snapshotBuffer.get(offset + 10).toInt() and 0xFF
                val nextCoins = snapshotBuffer.get(offset + 11).toInt() and 0xFF
                val nextX = snapshotBuffer.getFloat(offset + 12)
                val nextY = snapshotBuffer.getFloat(offset + 16)
                val nextZ = snapshotBuffer.getFloat(offset + 20)
                assetsChanged = assetsChanged || driverId[index] != nextDriver ||
                    driverVariant[index] != nextDriverVariant || item0[index] != nextItem0 ||
                    item1[index] != nextItem1
                changed = changed || active[index] != nextActive || local[index] != nextIsLocal ||
                    rank[index] != nextRank || lap[index] != nextLap ||
                    driverId[index] != nextDriver || driverVariant[index] != nextDriverVariant ||
                    item0[index] != nextItem0 ||
                    item1[index] != nextItem1 || item0Count[index] != nextItem0Count ||
                    item1Count[index] != nextItem1Count || coins[index] != nextCoins ||
                    worldX[index] != nextX ||
                    worldY[index] != nextY || worldZ[index] != nextZ
                active[index] = nextActive
                local[index] = nextIsLocal
                rank[index] = nextRank
                lap[index] = nextLap
                driverId[index] = nextDriver
                driverVariant[index] = nextDriverVariant
                item0[index] = nextItem0
                item1[index] = nextItem1
                item0Count[index] = nextItem0Count
                item1Count[index] = nextItem1Count
                coins[index] = nextCoins
                worldX[index] = nextX
                worldY[index] = nextY
                worldZ[index] = nextZ
            }
            rebuildDisplayOrder()
            animateRoulette = slotRollingMask != 0
        }
        if (assetsChanged) prefetchLiveAssets()
        if (changed || animateRoulette) postInvalidate()
    }

    private fun rebuildDisplayOrder() {
        for (i in 0 until RACER_COUNT) displayOrder[i] = i
        for (i in 1 until playerCount) {
            val value = displayOrder[i]
            var cursor = i - 1
            while (cursor >= 0 && displayRank(displayOrder[cursor]) > displayRank(value)) {
                displayOrder[cursor + 1] = displayOrder[cursor]
                cursor--
            }
            displayOrder[cursor + 1] = value
        }
    }

    private fun displayRank(index: Int): Int = rank[index].takeIf { it in 1..RACER_COUNT } ?: (index + 1)

    private fun prefetchLiveAssets() {
        if (status != 4) return
        prefetchItem(COIN_ITEM_ID)
        if (lapFlagBitmap == null) {
            val nextLapFlag = loadBitmap("mk8d/ui/lap_flag.png")
            synchronized(frameLock) {
                if (lapFlagBitmap == null) lapFlagBitmap = nextLapFlag
            }
        }
        for (i in 0 until playerCount) {
            val nextDriver = driverId[i]
            val nextVariant = driverVariant[i]
            if (loadedDriverId[i] != nextDriver || loadedDriverVariant[i] != nextVariant ||
                driverPortraits[i] == null
            ) {
                val nextPortrait = driverAssetPath(nextDriver, nextVariant)?.let(::loadBitmap)
                synchronized(frameLock) {
                    if (driverId[i] == nextDriver && driverVariant[i] == nextVariant) {
                        driverPortraits[i] = nextPortrait
                        loadedDriverId[i] = nextDriver
                        loadedDriverVariant[i] = nextVariant
                    }
                }
            }
            prefetchItem(item0[i])
            prefetchItem(item1[i])
        }
        val nextCourseId = courseId
        if (lastCourseBitmapId != nextCourseId || lastMapCameraId != nextCourseId ||
            courseBitmap == null || mapCamera == null
        ) {
            val internal = courseInternal(nextCourseId)

            val nextBitmap = internal?.let { loadBitmap("mk8d/maps/$it.png") }
            val nextCamera = internal?.let(::loadMapCamera)
            synchronized(frameLock) {
                if (courseId == nextCourseId) {
                    lastCourseBitmapId = nextCourseId
                    lastMapCameraId = nextCourseId
                    courseBitmap = nextBitmap
                    mapCamera = nextCamera
                    courseLabel = courseDisplayName(nextCourseId, internal)
                }
            }
        }
    }

    private fun prefetchItem(item: Int) {
        if (item !in ITEM_NAMES.indices || itemBitmaps[item] != null) return
        val bitmap = loadBitmap("mk8d/items/item_$item.png")
        synchronized(frameLock) {
            if (itemBitmaps[item] == null) itemBitmaps[item] = bitmap
        }
    }

    override fun onSizeChanged(width: Int, height: Int, oldWidth: Int, oldHeight: Int) {
        super.onSizeChanged(width, height, oldWidth, oldHeight)
        if (background == null) background = createBackground()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val scale = min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT)
        val offsetX = (width - DESIGN_WIDTH * scale) * .5f
        val offsetY = (height - DESIGN_HEIGHT * scale) * .5f
        canvas.save()
        canvas.translate(offsetX, offsetY)
        canvas.scale(scale, scale)
        background?.let { canvas.drawBitmap(it, 0f, 0f, paint) }

        synchronized(frameLock) {
            when (status) {
                2 -> drawWaiting(canvas, "Unsupported Mario Kart 8 Deluxe build", "This companion targets update 3.0.5.")
                4 -> drawRace(canvas)
                else -> drawWaiting(canvas, "Waiting for Mario Kart 8 Deluxe", "The race interface will appear after a course has loaded.")
            }
        }
        canvas.restore()
    }

    private fun drawWaiting(canvas: Canvas, title: String, detail: String) {
        paint.color = if (darkMode) Color.rgb(12, 20, 25) else Color.rgb(246, 249, 251)
        canvas.drawRect(0f, 0f, DESIGN_WIDTH, DESIGN_HEIGHT, paint)
        paint.color = Color.rgb(39, 177, 233)
        canvas.drawCircle(DESIGN_WIDTH * .5f, 430f, 62f, paint)
        stroke.color = Color.WHITE
        stroke.strokeWidth = 11f
        canvas.drawCircle(DESIGN_WIDTH * .5f, 430f, 32f, stroke)
        text(canvas, title, DESIGN_WIDTH * .5f, 555f, 42f, primaryTextColor(), Paint.Align.CENTER, true)
        text(canvas, detail, DESIGN_WIDTH * .5f, 612f, 25f, secondaryTextColor(), Paint.Align.CENTER)
    }

    private fun drawRace(canvas: Canvas) {
        drawRacerList(canvas)
        drawCourseMap(canvas)
        drawRaceFooter(canvas)
    }

    private fun drawRacerList(canvas: Canvas) {
        for (row in 0 until RACER_COUNT) {
            val top = ROW_TOP + row * ROW_HEIGHT
            val bottom = top + ROW_HEIGHT - 5f
            val racer = if (row < playerCount) displayOrder[row] else -1
            val isLocal = racer >= 0 && local[racer]
            paint.color = when {
                isLocal -> Color.rgb(255, 226, 0)
                darkMode && row % 2 == 0 -> Color.rgb(30, 43, 51)
                darkMode -> Color.rgb(23, 34, 41)
                row % 2 == 0 -> Color.rgb(251, 253, 254)
                else -> Color.rgb(239, 247, 251)
            }
            canvas.drawRoundRect(7f, top, LEFT_WIDTH - 8f, bottom, 8f, 8f, paint)
            paint.color = when {
                isLocal -> Color.rgb(238, 205, 0)
                darkMode -> Color.rgb(36, 54, 63)
                else -> Color.rgb(213, 239, 250)
            }
            canvas.drawRoundRect(7f, top, 62f, bottom, 8f, 8f, paint)
            if (racer < 0) continue

            val shownRank = displayRank(racer)
            val rowTextColor = if (darkMode && !isLocal) primaryTextColor() else Color.rgb(56, 61, 65)
            text(canvas, shownRank.toString(), 35f, top + 59f, 39f, rowTextColor, Paint.Align.CENTER, true)
            drawDriverPortrait(canvas, racer, racerPortraitBounds[row], false)
            val item = if (item0[racer] >= 0) item0[racer] else item1[racer]
            drawItemIcon(canvas, item, racerItemBounds[row], false)
            fittedText(
                canvas,
                driverName(driverId[racer]),
                207f,
                top + 57f,
                22f,
                94f,
                if (darkMode && !isLocal) primaryTextColor() else Color.rgb(48, 53, 57),
                Paint.Align.LEFT,
                isLocal
            )
            drawLapIndicator(canvas, racer, top)
        }
        paint.color = if (darkMode) Color.rgb(36, 153, 199) else Color.rgb(55, 191, 238)
        canvas.drawRect(LEFT_WIDTH - 5f, 0f, LEFT_WIDTH, DESIGN_HEIGHT, paint)
    }

    private fun drawLapIndicator(canvas: Canvas, racer: Int, rowTop: Float) {
        val currentLap = lap[racer]
        val totalLaps = lapTotal
        if (currentLap !in 1..9 || totalLaps !in 1..9) return




        lapFlagBounds.set(304f, rowTop + 14f, 356f, rowTop + 66f)
        lapFlagBitmap?.let { canvas.drawBitmap(it, null, lapFlagBounds, paint) }
        text(
            canvas,
            "$currentLap/$totalLaps",
            358f,
            rowTop + 57f,
            22f,
            if (darkMode && !local[racer]) primaryTextColor() else Color.rgb(48, 53, 57),
            Paint.Align.LEFT,
            true
        )
    }

    private fun drawCourseMap(canvas: Canvas) {
        projectMapPoints()
        canvas.save()
        canvas.clipRect(MAP_BOUNDS)
        courseBitmap?.let { bitmap ->
            paint.alpha = 255
            canvas.drawBitmap(bitmap, null, MAP_BOUNDS, paint)
        } ?: drawLiveTrackTrace(canvas)
        if (darkMode) {
            paint.alpha = 255
            paint.color = Color.argb(132, 3, 13, 20)
            canvas.drawRect(MAP_BOUNDS, paint)
            paint.alpha = 255
        }

        for (i in 0 until playerCount) {
            if (!mapPointValid[i] || !MAP_BOUNDS.contains(mapPointX[i], mapPointY[i])) continue
            val radius = if (local[i]) 33f else 25f
            markerBounds[i].set(
                mapPointX[i] - radius,
                mapPointY[i] - radius,
                mapPointX[i] + radius,
                mapPointY[i] + radius
            )
            drawDriverPortrait(canvas, i, markerBounds[i], true)
            if (local[i]) {
                stroke.color = Color.rgb(255, 218, 0)
                stroke.strokeWidth = 6f
                canvas.drawCircle(mapPointX[i], mapPointY[i], radius + 5f, stroke)
            }
        }
        canvas.restore()

    }

    private fun drawLiveTrackTrace(canvas: Canvas) {
        stroke.strokeWidth = 18f
        stroke.color = Color.rgb(32, 160, 231)
        stroke.strokeCap = Paint.Cap.ROUND
        path.reset()
        var started = false
        for (row in 0 until playerCount) {
            val index = displayOrder[row]
            if (!mapPointValid[index]) continue
            if (!started) {
                path.moveTo(mapPointX[index], mapPointY[index])
                started = true
            } else {
                path.lineTo(mapPointX[index], mapPointY[index])
            }
        }
        if (started) canvas.drawPath(path, stroke)
        stroke.strokeCap = Paint.Cap.BUTT
    }

    private fun projectMapPoints() {
        mapPointValid.fill(false)
        val camera = mapCamera
        if (camera != null) {
            for (i in 0 until playerCount) {
                mapPointValid[i] = camera.project(
                    worldX[i],
                    worldY[i],
                    worldZ[i],
                    MAP_BOUNDS,
                    mapPointX,
                    mapPointY,
                    i
                )
            }
        } else {
            projectWithLiveBounds()
        }
    }

    private fun projectWithLiveBounds() {
        if (playerCount <= 0) return
        var minX = Float.POSITIVE_INFINITY
        var maxX = Float.NEGATIVE_INFINITY
        var minZ = Float.POSITIVE_INFINITY
        var maxZ = Float.NEGATIVE_INFINITY
        for (i in 0 until playerCount) {
            if (!worldX[i].isFinite() || !worldZ[i].isFinite()) continue
            minX = min(minX, worldX[i])
            maxX = max(maxX, worldX[i])
            minZ = min(minZ, worldZ[i])
            maxZ = max(maxZ, worldZ[i])
        }
        if (!minX.isFinite()) return
        val rangeX = max(120f, maxX - minX)
        val rangeZ = max(120f, maxZ - minZ)
        val padding = 74f
        for (i in 0 until playerCount) {
            val u = .5f + (worldX[i] - (minX + maxX) * .5f) / (rangeX * 1.25f)
            val v = .5f + (worldZ[i] - (minZ + maxZ) * .5f) / (rangeZ * 1.25f)
            mapPointX[i] = MAP_BOUNDS.left + padding + u * (MAP_BOUNDS.width() - padding * 2f)
            mapPointY[i] = MAP_BOUNDS.top + padding + v * (MAP_BOUNDS.height() - padding * 2f)
            mapPointValid[i] = true
        }
    }

    private fun drawRaceFooter(canvas: Canvas) {
        val localRacer = localIndex.takeIf { it in 0 until playerCount }
        drawCoinSlot(canvas, localRacer?.let { coins[it] } ?: 0)
        drawTouchItemSlot(canvas, 0, ITEM_SLOT_0, ITEM_ICON_0, localRacer?.let { item0[it] } ?: -1, localRacer?.let { item0Count[it] } ?: 0)
        drawTouchItemSlot(canvas, 1, ITEM_SLOT_1, ITEM_ICON_1, localRacer?.let { item1[it] } ?: -1, localRacer?.let { item1Count[it] } ?: 0)
    }

    private fun drawCoinSlot(canvas: Canvas, count: Int) {
        paint.color = cardBackgroundColor()
        canvas.drawRoundRect(COIN_SLOT, 14f, 14f, paint)
        stroke.color = cardBorderColor()
        stroke.strokeWidth = 4f
        canvas.drawRoundRect(COIN_SLOT, 14f, 14f, stroke)
        paint.alpha = 255
        drawItemIcon(canvas, COIN_ITEM_ID, COIN_ICON_BOUNDS, true)
        text(
            canvas,
            "x${count.coerceIn(0, 99)}",
            COIN_SLOT.centerX(),
            COIN_SLOT.bottom - 17f,
            30f,
            primaryTextColor(),
            Paint.Align.CENTER,
            true
        )
    }

    private fun drawTouchItemSlot(
        canvas: Canvas,
        slot: Int,
        bounds: RectF,
        iconBounds: RectF,
        item: Int,
        count: Int
    ) {
        val pressed = pressedSlot == slot
        val rolling = slotRollingMask and (1 shl slot) != 0
        val displayedItem = if (rolling) {
            (((SystemClock.uptimeMillis() / SNAPSHOT_INTERVAL_MS) + slot * 11L) %
                ITEM_NAMES.size).toInt()
        } else {
            item
        }
        paint.color = if (pressed) Color.rgb(255, 226, 0) else cardBackgroundColor()
        canvas.drawRoundRect(bounds, 14f, 14f, paint)
        stroke.color = if (pressed) Color.rgb(232, 188, 0) else cardBorderColor()
        stroke.strokeWidth = if (pressed) 6f else 4f
        canvas.drawRoundRect(bounds, 14f, 14f, stroke)
        paint.alpha = 255
        drawItemIcon(canvas, displayedItem, iconBounds, true)
        val label = when {
            rolling -> "ROULETTE"
            item in ITEM_NAMES.indices -> ITEM_NAMES[item]
            else -> "EMPTY"
        }
        val cardTextColor = if (pressed) Color.rgb(52, 61, 67) else primaryTextColor()
        text(canvas, label, bounds.centerX(), bounds.bottom - 18f, 18f, cardTextColor, Paint.Align.CENTER, true)
        if (!rolling && count > 1) {
            text(canvas, "x$count", bounds.right - 17f, bounds.top + 31f, 21f, cardTextColor, Paint.Align.RIGHT, true)
        }
        if (rejectedSlot == slot) {
            text(canvas, "WAIT", bounds.centerX(), bounds.centerY(), 22f, Color.rgb(198, 33, 39), Paint.Align.CENTER, true)
        }
    }

    private fun drawDriverPortrait(canvas: Canvas, racer: Int, bounds: RectF, circular: Boolean) {
        val bitmap = driverPortraits[racer]
        if (bitmap != null) {
            canvas.drawBitmap(bitmap, null, bounds, paint)
            return
        }
        if (driverId[racer] == 29 || driverId[racer] == 52) {
            drawMiiPortrait(canvas, bounds)
            return
        }
        paint.color = driverColor(driverId[racer])
        if (circular) canvas.drawCircle(bounds.centerX(), bounds.centerY(), bounds.width() * .5f, paint)
        else canvas.drawRoundRect(bounds, 9f, 9f, paint)
        val initial = driverName(driverId[racer]).firstOrNull()?.uppercaseChar()?.toString() ?: "?"
        text(canvas, initial, bounds.centerX(), bounds.centerY() + bounds.height() * .23f, bounds.height() * .62f, Color.WHITE, Paint.Align.CENTER, true)
    }







    private fun drawMiiPortrait(canvas: Canvas, bounds: RectF) {
        val width = bounds.width()
        val height = bounds.height()
        val cx = bounds.centerX()
        val cy = bounds.centerY()
        val faceLeft = bounds.left + width * .18f
        val faceRight = bounds.right - width * .18f
        val faceTop = bounds.top + height * .12f
        val faceBottom = bounds.bottom - height * .08f

        paint.color = Color.rgb(237, 177, 126)
        canvas.drawOval(
            bounds.left + width * .10f,
            cy - height * .12f,
            bounds.left + width * .28f,
            cy + height * .14f,
            paint
        )
        canvas.drawOval(
            bounds.right - width * .28f,
            cy - height * .12f,
            bounds.right - width * .10f,
            cy + height * .14f,
            paint
        )
        paint.color = Color.rgb(255, 211, 164)
        canvas.drawOval(faceLeft, faceTop, faceRight, faceBottom, paint)

        path.reset()
        path.moveTo(faceLeft + width * .01f, faceTop + height * .25f)
        path.cubicTo(
            faceLeft + width * .05f,
            faceTop - height * .10f,
            faceRight - width * .06f,
            faceTop - height * .08f,
            faceRight - width * .01f,
            faceTop + height * .24f
        )
        path.cubicTo(
            cx + width * .19f,
            faceTop + height * .10f,
            cx - width * .18f,
            faceTop + height * .18f,
            faceLeft + width * .01f,
            faceTop + height * .25f
        )
        path.close()
        paint.color = Color.rgb(92, 59, 39)
        canvas.drawPath(path, paint)

        paint.color = Color.rgb(56, 45, 42)
        canvas.drawOval(
            cx - width * .17f,
            cy - height * .12f,
            cx - width * .10f,
            cy + height * .02f,
            paint
        )
        canvas.drawOval(
            cx + width * .10f,
            cy - height * .12f,
            cx + width * .17f,
            cy + height * .02f,
            paint
        )
        paint.color = Color.rgb(218, 147, 105)
        canvas.drawOval(
            cx - width * .035f,
            cy - height * .01f,
            cx + width * .035f,
            cy + height * .12f,
            paint
        )
        stroke.color = Color.rgb(156, 68, 59)
        stroke.strokeWidth = max(1.5f, width * .035f)
        path.reset()
        path.moveTo(cx - width * .12f, cy + height * .21f)
        path.quadTo(cx, cy + height * .31f, cx + width * .12f, cy + height * .21f)
        canvas.drawPath(path, stroke)
    }

    private fun drawItemIcon(canvas: Canvas, item: Int, bounds: RectF, large: Boolean) {
        if (item < 0) {
            paint.color = if (darkMode) Color.rgb(58, 73, 82) else Color.rgb(225, 229, 231)
            canvas.drawCircle(bounds.centerX(), bounds.centerY(), min(bounds.width(), bounds.height()) * .42f, paint)
            return
        }
        val runtimePath = "mk8d/items/item_$item.png"
        val bitmap = itemBitmaps.getOrNull(item) ?: bitmapCache.get(runtimePath)
        if (bitmap != null) {
            canvas.drawBitmap(bitmap, null, bounds, paint)
            return
        }
        if (item in ITEM_NAMES.indices) {
            requestRuntimeAsset(runtimePath)
        }
        paint.color = itemColor(item)
        canvas.drawCircle(bounds.centerX(), bounds.centerY(), min(bounds.width(), bounds.height()) * .46f, paint)
        val short = when (item) {
            0, 17 -> "B"
            1, 18 -> "G"
            2, 19 -> "R"
            3, 7, 11 -> "M"
            4 -> "💣"
            8 -> "★"
            10 -> "⚡"
            else -> (item + 1).toString()
        }
        text(canvas, short, bounds.centerX(), bounds.centerY() + bounds.height() * .2f, if (large) 43f else 24f, Color.WHITE, Paint.Align.CENTER, true)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val scale = min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT)
        if (scale <= 0f) return false
        val x = (event.x - (width - DESIGN_WIDTH * scale) * .5f) / scale
        val y = (event.y - (height - DESIGN_HEIGHT * scale) * .5f) / scale
        val hit = when {
            ITEM_SLOT_0.contains(x, y) -> 0
            ITEM_SLOT_1.contains(x, y) -> 1
            else -> -1
        }
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                gestureActive = true
                longPressTriggered = false
                touchDownX = event.x
                touchDownY = event.y
                pressedSlot = if (status == 4) hit else -1
                if (rejectedSlot == hit) {
                    removeCallbacks(clearRejectedSlot)
                    rejectedSlot = -1
                }
                removeCallbacks(toggleDarkMode)
                postDelayed(toggleDarkMode, ViewConfiguration.getLongPressTimeout().toLong())
                invalidate()
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                if (!gestureActive) return false
                val dx = event.x - touchDownX
                val dy = event.y - touchDownY
                if (dx * dx + dy * dy > touchSlop * touchSlop) {
                    removeCallbacks(toggleDarkMode)
                }
                if (pressedSlot >= 0 && pressedSlot != hit) {
                    pressedSlot = -1
                    invalidate()
                }
                return true
            }
            MotionEvent.ACTION_UP -> {
                val consumed = gestureActive
                gestureActive = false
                removeCallbacks(toggleDarkMode)
                val selected = pressedSlot
                pressedSlot = -1
                if (longPressTriggered) {
                    longPressTriggered = false
                    invalidate()
                    return true
                }
                if (selected >= 0 && selected == hit && status == 4) {
                    submitItemAction(selected)
                    performClick()
                } else {
                    invalidate()
                }
                return consumed
            }
            MotionEvent.ACTION_CANCEL -> {
                val consumed = gestureActive
                gestureActive = false
                longPressTriggered = false
                removeCallbacks(toggleDarkMode)
                pressedSlot = -1
                invalidate()
                return consumed
            }
        }
        return gestureActive
    }

    private fun submitItemAction(slot: Int) {
        val executor = poller
        if (executor == null || executor.isShutdown) {
            showItemActionRejected(slot)
            return
        }
        try {


            executor.execute {
                val accepted = runCatching {
                    NativeLibrary.activateMk8dItemSlot(slot)
                }.getOrDefault(false)
                if (!accepted) post { showItemActionRejected(slot) }
            }
        } catch (_: RejectedExecutionException) {
            showItemActionRejected(slot)
        }
    }

    private fun showItemActionRejected(slot: Int) {
        rejectedSlot = slot
        removeCallbacks(clearRejectedSlot)
        postDelayed(clearRejectedSlot, 900L)
        invalidate()
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }

    private fun createBackground(): Bitmap {
        val bitmap = Bitmap.createBitmap(DESIGN_WIDTH.toInt(), DESIGN_HEIGHT.toInt(), Bitmap.Config.RGB_565)
        val canvas = Canvas(bitmap)
        paint.color = if (darkMode) Color.rgb(11, 18, 23) else Color.rgb(247, 250, 252)
        canvas.drawRect(0f, 0f, DESIGN_WIDTH, DESIGN_HEIGHT, paint)
        val checker = 46f
        var row = 0
        var y = 0f
        while (y < DESIGN_HEIGHT) {
            var column = 0
            var x = LEFT_WIDTH
            while (x < DESIGN_WIDTH) {
                paint.color = if (darkMode) {
                    if ((row + column) and 1 == 0) Color.rgb(20, 31, 37)
                    else Color.rgb(25, 38, 45)
                } else if ((row + column) and 1 == 0) {
                    Color.rgb(239, 244, 247)
                } else {
                    Color.rgb(225, 232, 237)
                }
                canvas.drawRect(x, y, min(DESIGN_WIDTH, x + checker), min(DESIGN_HEIGHT, y + checker), paint)
                x += checker
                column++
            }
            y += checker
            row++
        }
        paint.color = if (darkMode) Color.rgb(15, 27, 34) else Color.rgb(213, 240, 251)
        canvas.drawRect(0f, 0f, LEFT_WIDTH, DESIGN_HEIGHT, paint)
        return bitmap
    }

    private fun loadBitmap(path: String): Bitmap? {
        requestRuntimeAsset(path)
        bitmapCache.get(path)?.let { return it }
        synchronized(missingAssets) {
            if (path in missingAssets) return null
        }
        return try {
            context.assets.open(path).use { stream ->
                BitmapFactory.decodeStream(stream)?.also { bitmapCache.put(path, it) }
            } ?: run {
                synchronized(missingAssets) { missingAssets += path }
                null
            }
        } catch (_: Exception) {
            synchronized(missingAssets) { missingAssets += path }
            null
        }
    }

    private fun loadMapCamera(internal: String): MapCamera? {
        val path = "mk8d/maps/$internal.mapcamera"
        requestRuntimeAsset(path)
        runtimeBytes[path]?.let { return decodeMapCamera(it) }
        return try {
            val bytes = context.assets.open(path).use { it.readBytes() }
            decodeMapCamera(bytes)
        } catch (_: Exception) {
            null
        }
    }

    private fun decodeMapCamera(bytes: ByteArray): MapCamera? {
        if (bytes.size < 45) return null
        val data = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        val position = Vec3(data.float, data.float, data.float)
        val lookAt = Vec3(data.float, data.float, data.float)
        val up = Vec3(data.float, data.float, data.float)
        val mapWidth = data.float
        val mapHeight = data.float
        return if (!mapWidth.isFinite() || !mapHeight.isFinite() || mapWidth <= 0f || mapHeight <= 0f) {
            null
        } else {
            MapCamera.create(position, lookAt, up, mapWidth, mapHeight)
        }
    }

    private fun requestRuntimeAsset(path: String) {
        val now = SystemClock.uptimeMillis()
        if (!path.startsWith("mk8d/") || !isAttachedToWindow ||
            path in runtimeAssetMissing || now < (runtimeAssetRetryAfter[path] ?: 0L) ||
            !runtimeAssetRequests.add(path)
        ) {
            return
        }
        val executor = assetExecutor ?: run {
            runtimeAssetRequests.remove(path)
            return
        }
        val generation = assetGeneration
        try {
            executor.execute {
                val bytes = loadRuntimeAsset(path)
                if (generation != assetGeneration || !isAttachedToWindow) {
                    runtimeAssetRequests.remove(path)
                    return@execute
                }
                if (bytes == null) {
                    // A null result commonly means the companion view was created before the
                    // game's patched RomFS was mounted. That is transient, not proof that the
                    // requested asset does not exist.
                    val failures = runtimeAssetFailures.merge(path, 1, Int::plus) ?: 1
                    val multiplier = 1L shl (failures - 1).coerceIn(0, 4)
                    val retryDelay = (ASSET_RETRY_BASE_MS * multiplier)
                        .coerceAtMost(ASSET_RETRY_MAX_MS)
                    runtimeAssetRetryAfter[path] = SystemClock.uptimeMillis() + retryDelay
                    try {
                        poller?.schedule(
                            {
                                runtimeAssetRetryAfter.remove(path)
                                if (status == 4) {
                                    prefetchLiveAssets()
                                    postInvalidate()
                                }
                            },
                            retryDelay,
                            TimeUnit.MILLISECONDS
                        )
                    } catch (_: RejectedExecutionException) {
                    }
                } else if (path.endsWith(".png")) {
                    val bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
                    if (bitmap == null) {
                        runtimeAssetMissing += path
                    } else {
                        runtimeAssetRetryAfter.remove(path)
                        runtimeAssetFailures.remove(path)
                        bitmapCache.put(path, bitmap)
                        synchronized(missingAssets) { missingAssets.remove(path) }
                    }
                } else {
                    runtimeAssetRetryAfter.remove(path)
                    runtimeAssetFailures.remove(path)
                    runtimeBytes[path] = bytes
                }
                runtimeAssetRequests.remove(path)
                try {
                    poller?.execute {
                        if (status == 4) prefetchLiveAssets()
                        postInvalidate()
                    }
                } catch (_: RejectedExecutionException) {
                }
            }
        } catch (_: RejectedExecutionException) {
            runtimeAssetRequests.remove(path)
        }
    }

    private fun loadRuntimeAsset(path: String): ByteArray? {
        val cacheFile = runtimeAssetCacheFile(path)
        if (cacheFile?.isFile == true &&
            cacheFile.length() in 1..MAX_RUNTIME_ASSET_BYTES.toLong()
        ) {
            runCatching { cacheFile.readBytes() }.getOrNull()?.let { return it }
        }
        val bytes = try {
            NativeLibrary.getDualScreenRomfsAsset(path)
        } catch (_: Throwable) {
            null
        } ?: return null
        if (bytes.isEmpty() || bytes.size > MAX_RUNTIME_ASSET_BYTES) return null
        if (cacheFile != null) {
            try {
                val parent = cacheFile.parentFile
                if (parent != null) {
                    parent.mkdirs()
                    val temporary = File(parent, "${cacheFile.name}.tmp")
                    FileOutputStream(temporary).use { output -> output.write(bytes) }
                    if (!temporary.renameTo(cacheFile)) temporary.delete()
                }
            } catch (_: Exception) {
            }
        }
        return bytes
    }

    private fun runtimeAssetCacheFile(path: String): File? {
        var revision = assetRevision
        if (revision == null) {
            revision = try {
                NativeLibrary.getDualScreenRomfsRevision()
            } catch (_: Throwable) {
                ""
            }.ifBlank { "base" }.replace(Regex("[^A-Za-z0-9_.-]"), "_")
            assetRevision = revision
        }
        val digest = MessageDigest.getInstance("SHA-256").digest(path.toByteArray(Charsets.UTF_8))
        val name = buildString(digest.size * 2 + 6) {
            digest.forEach { byte -> append("%02x".format(byte.toInt() and 0xFF)) }
            append(if (path.endsWith(".png")) ".png" else ".bin")
        }
        return File(context.cacheDir, "mk8d-romfs-assets/v1-$revision/$name")
    }

    private fun driverAssetPath(id: Int, variant: Int): String? {
        val internal = DRIVER_INTERNAL.getOrNull(id) ?: return null
        val variantMax = when (id) {
            4, 16, 44 -> 8
            41, 42 -> 2
            else -> -1
        }
        val alias = if (variantMax >= 0) {
            "$internal${variant.coerceIn(0, variantMax).toString().padStart(2, '0')}"
        } else {
            internal
        }
        return "mk8d/characters/tc_MapChara_$alias.png"
    }

    private fun courseInternal(id: Int): String? = COURSE_INTERNAL.getOrNull(id)
    private fun courseDisplayName(id: Int, internal: String?): String = when (internal) {
        "Gu_MarioCircuit" -> "Mario Circuit"
        "Gu_DossunIseki" -> "Thwomp Ruins"
        "Gu_City" -> "Toad Harbor"
        "Gu_Cake" -> "Sweet Sweet Canyon"
        "Gu_HorrorHouse" -> "Twisted Mansion"
        "Gu_Expert" -> "Shy Guy Falls"
        "Gu_Desert" -> "Bone-Dry Dunes"
        "Gu_Cloud" -> "Cloudtop Cruise"
        "Gu_SnowMountain" -> "Mount Wario"
        "Gu_Techno" -> "Electrodrome"
        "Gu_Airport" -> "Sunshine Airport"
        "Gu_FirstCircuit" -> "Mario Kart Stadium"
        "Gu_WaterPark" -> "Water Park"
        "Gu_Ocean" -> "Dolphin Shoals"
        "Gu_BowserCastle" -> "Bowser's Castle"
        "Gu_RainbowRoad" -> "Rainbow Road"
        "G3ds_DKJungle" -> "3DS DK Jungle"
        "Gwii_MooMooMeadows" -> "Wii Moo Moo Meadows"
        "G64_PeachCircuit" -> "N64 Royal Raceway"
        "G64_KinopioHighway" -> "N64 Toad's Turnpike"
        "Gds_PukupukuBeach" -> "DS Cheep Cheep Beach"
        "Ggc_SherbetLand" -> "GCN Sherbet Land"
        "Gagb_MarioCircuit" -> "GBA Mario Circuit"
        "G3ds_MusicPark" -> "3DS Music Park"
        "Gwii_GrumbleVolcano" -> "Wii Grumble Volcano"
        "Gsfc_DonutsPlain3" -> "SNES Donut Plains 3"
        "Ggc_DryDryDesert" -> "GCN Dry Dry Desert"
        "G3ds_PackunSlider" -> "3DS Piranha Plant Slide"
        "Gds_TickTockClock" -> "DS Tick-Tock Clock"
        "G64_YoshiValley" -> "N64 Yoshi Valley"
        "Gds_WarioStadium" -> "DS Wario Stadium"
        "G64_RainbowRoad" -> "N64 Rainbow Road"
        "Du_Metro" -> "Super Bell Subway"
        "Du_MuteCity" -> "Mute City"
        "Du_DragonRoad" -> "Dragon Driftway"
        "Du_Hyrule" -> "Hyrule Circuit"
        "Du_Animal_Summer", "Du_Animal_Spring", "Du_Animal_Autumn", "Du_Animal_Winter" ->
            "Animal Crossing"
        "Du_ExciteBike" -> "Excitebike Arena"
        "Du_Woods" -> "Wild Woods"
        "Du_IcePark" -> "Ice Ice Outpost"
        "Dgc_YoshiCircuit" -> "GCN Yoshi Circuit"
        "Dwii_WariosMine" -> "Wii Wario's Gold Mine"
        "Dsfc_RainbowRoad" -> "SNES Rainbow Road"
        "Dagb_RibbonRoad" -> "GBA Ribbon Road"
        "D3ds_NeoBowserCity" -> "3DS Neo Bowser City"
        "Dgc_BabyPark" -> "GCN Baby Park"
        "Dagb_CheeseLand" -> "GBA Cheese Land"
        "Du_BigBlue" -> "Big Blue"
        "B3ds_WuhuTown" -> "3DS Wuhu Town"
        "Bgc_LuigiMansion" -> "GCN Luigi's Mansion"
        "Bsfc_Battle1" -> "SNES Battle Course 1"
        "Bu_DekaLine" -> "Urchin Underpass"
        "Bu_Moon" -> "Lunar Colony"
        "Bu_BattleStadium" -> "Battle Stadium"
        "Bu_Dojo" -> "Dragon Palace"
        "Bu_Sweets" -> "Sweet Sweet Kingdom"
        null -> "Course 0x${id.toString(16).uppercase()}"
        else -> CNSW_COURSE_NAMES[internal] ?: internal.replace('_', ' ')
    }

    private fun driverName(id: Int): String = DRIVER_NAMES.getOrNull(id) ?: "Driver ${id.coerceAtLeast(0) + 1}"
    private fun primaryTextColor(): Int =
        if (darkMode) Color.rgb(239, 245, 248) else Color.rgb(48, 53, 57)

    private fun secondaryTextColor(): Int =
        if (darkMode) Color.rgb(155, 171, 180) else Color.rgb(103, 113, 120)

    private fun cardBackgroundColor(): Int =
        if (darkMode) Color.argb(244, 24, 34, 40) else Color.argb(238, 255, 255, 255)

    private fun cardBorderColor(): Int =
        if (darkMode) Color.rgb(52, 181, 228) else Color.rgb(76, 191, 234)

    private fun driverColor(id: Int): Int = DRIVER_COLORS.getOrElse(id) { Color.rgb(58, 175, 226) }
    private fun itemColor(item: Int): Int = when (item) {
        0, 17 -> Color.rgb(244, 202, 24)
        1, 18 -> Color.rgb(45, 184, 74)
        2, 19 -> Color.rgb(224, 53, 48)
        3, 7, 11 -> Color.rgb(222, 49, 48)
        8 -> Color.rgb(247, 204, 28)
        10 -> Color.rgb(55, 91, 194)
        else -> Color.rgb(58, 175, 226)
    }

    private fun ordinal(value: Int): String = when {
        value % 100 in 11..13 -> "TH"
        value % 10 == 1 -> "ST"
        value % 10 == 2 -> "ND"
        value % 10 == 3 -> "RD"
        else -> "TH"
    }

    private fun text(
        canvas: Canvas,
        value: String,
        x: Float,
        y: Float,
        size: Float,
        color: Int,
        align: Paint.Align,
        bold: Boolean = false
    ) {
        paint.typeface = if (bold) boldTypeface else typeface
        paint.textSize = size
        paint.textAlign = align
        paint.color = color
        paint.style = Paint.Style.FILL
        canvas.drawText(value, x, y, paint)
    }

    private fun fittedText(
        canvas: Canvas,
        value: String,
        x: Float,
        y: Float,
        size: Float,
        maxWidth: Float,
        color: Int,
        align: Paint.Align,
        bold: Boolean = false
    ) {
        paint.typeface = if (bold) boldTypeface else typeface
        paint.textSize = size
        paint.textAlign = align
        paint.color = color
        paint.style = Paint.Style.FILL
        val measured = paint.measureText(value)
        if (measured > maxWidth && measured > 0f) {
            paint.textSize = max(17f, size * maxWidth / measured)
        }
        canvas.drawText(value, x, y, paint)
    }
}
