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
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.TimeUnit
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.utils.Log

internal class SshdCompanionView(
    context: Context,
    private val onPlayerVisibilityChanged: (Boolean) -> Unit = {}
) : View(context), SshdControllerTarget {
    private companion object {
        private const val DESIGN_WIDTH = 1240f
        private const val DESIGN_HEIGHT = 1080f
        private const val SNAPSHOT_SIZE = 816
        private const val SNAPSHOT_MAGIC = 0x44485353
        private const val SNAPSHOT_INTERVAL_MS = 250L
        private const val ACTIVE_STATUS = 4
        private const val ITEM_COUNT = 216

        // Skyward Sword's pause UI is warm parchment framed with carved brown-gold
        // ornament and a saturated blue selection state. These are intentionally drawn
        // rather than stretching tr_kami: the source texture contains transparent bands
        // intended for the original BRLYT panes and produced the broken background seen
        // in the first companion build.
        private val COLOR_BACKGROUND_TOP = Color.rgb(79, 54, 28)
        private val COLOR_BACKGROUND_BOTTOM = Color.rgb(125, 94, 48)
        private val COLOR_SURFACE = Color.rgb(218, 197, 148)
        private val COLOR_SURFACE_RAISED = Color.rgb(235, 219, 176)
        private val COLOR_BORDER = Color.rgb(112, 78, 38)
        private val COLOR_BLUE = Color.rgb(34, 112, 151)
        private val COLOR_BLUE_DARK = Color.rgb(23, 72, 105)
        private val COLOR_CYAN = COLOR_BLUE
        private val COLOR_GOLD = Color.rgb(201, 151, 55)
        private val COLOR_TEXT = Color.rgb(55, 37, 20)
        private val COLOR_TEXT_SECONDARY = Color.rgb(99, 72, 40)
        private val COLOR_TEXT_MUTED = Color.rgb(145, 112, 68)
        private val COLOR_GREEN = Color.rgb(91, 142, 51)
        private val COLOR_RED = Color.rgb(184, 61, 43)

        private val PAGE_TITLES = arrayOf("Inventory", "Pouch", "Atlas", "Fi")
        private val PAGE_SUBTITLES = arrayOf(
            "Gear, upgrades and key items",
            "Adventure pouch and item check",
            "The chart for Link's current region",
            "Live guidance and progression"
        )
        private val CATEGORY_TITLES = arrayOf("Equipment", "Upgrades", "Collection", "Quest Items")
        private val CATEGORY_IDS = arrayOf(
            intArrayOf(
                9, 10, 11, 12, 13, 14, 15, 16, 19, 20, 49, 52, 53, 56, 68, 71, 75,
                76, 77, 89, 90, 91, 92, 99, 105, 116, 117, 118, 119, 120, 121, 122,
                123, 124, 125, 137, 138, 140
            ),
            intArrayOf(
                70, 100, 101, 102, 103, 104, 108, 109, 110, 111, 112, 113, 114,
                126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136
            ),
            IntArray(36) { 141 + it },
            IntArray(24) { 177 + it }
        )

        private val MAP_ASSETS = arrayOf(
            "sshd/map/world.png",
            "sshd/map/faron.png",
            "sshd/map/eldin.png",
            "sshd/map/lanayru.png"
        )
        private val MAP_TITLES = arrayOf("World", "Faron", "Eldin", "Lanayru")

        // These IDs are the game's ITEMFLAGS enum, not an invented companion index. Every
        // mapping points to the matching named texture in Layout/MenuPause.arc.
        private val ITEM_ICON_ASSETS = mapOf(
            10 to "sshd/item/practice_sword.png",
            14 to "sshd/item/true_master_sword.png",
            15 to "sshd/item/sailcloth.png",
            16 to "sshd/item/harp.png",
            56 to "sshd/item/digging_mitts.png",
            68 to "sshd/item/water_dragon_scale.png",
            94 to "sshd/item/heart_piece.png",
            95 to "sshd/item/triforce.png",
            96 to "sshd/item/triforce.png",
            97 to "sshd/item/triforce.png",
            99 to "sshd/item/mogma_mitts.png",
            108 to "sshd/item/wallet_b.png",
            109 to "sshd/item/wallet_c.png",
            110 to "sshd/item/wallet_d.png",
            111 to "sshd/item/wallet_e.png",
            138 to "sshd/item/fireshield_earrings.png",
            158 to "sshd/item/cawlin_letter.png",
            159 to "sshd/item/insect_cage.png",
            160 to "sshd/item/rattle.png",
            177 to "sshd/item/emerald_tablet.png",
            178 to "sshd/item/ruby_tablet.png",
            179 to "sshd/item/amber_tablet.png",
            197 to "sshd/item/life_tree_seedling.png",
            198 to "sshd/item/life_tree_fruit.png",
            199 to "sshd/item/wallet_a.png"
        )
    }

    private enum class Page {
        Inventory,
        Pouch,
        Map,
        Progress
    }

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
        val itemValues: IntArray = IntArray(ITEM_COUNT)
    )

    private data class Assets(
        val itemCell: Bitmap? = null,
        val selectedCell: Bitmap? = null,
        val heart: Bitmap? = null,
        val fiIcon: Bitmap? = null,
        val purse: Bitmap? = null,
        val linkPortrait: Bitmap? = null,
        val itemIcons: Map<Int, Bitmap?> = emptyMap(),
        val maps: List<Bitmap?> = List(4) { null },
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
    private val tabBounds = Array(4) { index ->
        RectF(194f + index * 256f, 20f, 444f + index * 256f, 102f)
    }
    private val categoryBounds = Array(4) { index ->
        RectF(32f + index * 163f, 174f, 187f + index * 163f, 222f)
    }
    private var poller: ScheduledExecutorService? = null
    private var assetExecutor: ExecutorService? = null
    @Volatile private var snapshot = Snapshot()
    @Volatile private var assets = Assets()
    private var stateHash = 0
    private var page = Page.Inventory
    private var category = 0
    private var selectedIndex = 0
    private var playerVisible = false
    private var controllerActive = false
    private var controllerMotionAt = 0L
    private var downX = 0f
    private var downY = 0f
    private var pageChangedAt = 0L

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
        setPlayerVisible(false)
        poller?.shutdownNow()
        poller = null
        assetExecutor?.shutdownNow()
        assetExecutor = null
        val bitmaps = buildList {
            addAll(assets.maps)
            add(assets.itemCell)
            add(assets.selectedCell)
            add(assets.heart)
            add(assets.fiIcon)
            add(assets.purse)
            add(assets.linkPortrait)
            addAll(assets.itemIcons.values)
        }
        bitmaps.filterNotNull().distinct().forEach { it.recycle() }
        assets = Assets()
        Log.info("[SSHD Companion] Bottom-screen view detached")
        super.onDetachedFromWindow()
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
        val itemIcons = ITEM_ICON_ASSETS.mapValues { (_, path) -> icon(path) }

        assets = Assets(
            itemCell = bitmap("sshd/ui/item_cell.png"),
            selectedCell = bitmap("sshd/ui/item_cell_selected.png"),
            heart = bitmap("sshd/ui/heart.png"),
            fiIcon = icon("sshd/ui/fi.png"),
            purse = icon("sshd/ui/rupee_purse.png"),
            linkPortrait = bitmap("sshd/ui/link_portrait.png"),
            itemIcons = itemIcons,
            maps = MAP_ASSETS.map(::bitmap),
            itemText = messages("sshd/raw/items.msbt"),
            fiText = messages("sshd/raw/fi.msbt")
        )
        postInvalidateOnAnimation()
        Log.info(
            "[SSHD Companion] Loaded original assets: " +
                "items=${assets.itemText.size} fi=${assets.fiText.size} " +
                "icons=${assets.itemIcons.count { it.value != null }} " +
                "maps=${assets.maps.count { it != null }}"
        )
    }

    private fun pollSnapshot() {
        snapshotBuffer.clear()
        if (NativeLibrary.fillSshdCompanionSnapshot(snapshotBuffer) != SNAPSHOT_SIZE) return
        if (snapshotBuffer.getInt(0) != SNAPSHOT_MAGIC ||
            snapshotBuffer.getShort(4).toInt() != 1 ||
            snapshotBuffer.getShort(6).toInt() != SNAPSHOT_SIZE
        ) {
            return
        }
        var nextHash = 17
        for (offset in 12 until SNAPSHOT_SIZE) {
            nextHash = 31 * nextHash + snapshotBuffer.get(offset)
        }
        if (nextHash == stateHash) return
        stateHash = nextHash
        val next = Snapshot(
            status = snapshotBuffer.getInt(12),
            capabilities = snapshotBuffer.getInt(16),
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
            itemValues = IntArray(ITEM_COUNT) { snapshotBuffer.u16(384 + it * 2) }
        )
        snapshot = next
        selectedIndex = selectedIndex.coerceAtLeast(0)
        postInvalidateOnAnimation()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val scale = min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT)
        val offsetX = (width - DESIGN_WIDTH * scale) * .5f
        val offsetY = (height - DESIGN_HEIGHT * scale) * .5f
        canvas.save()
        canvas.translate(offsetX, offsetY)
        canvas.scale(scale, scale)
        drawBackground(canvas)
        drawHeader(canvas)
        drawTabs(canvas)
        setPlayerVisible(page == Page.Inventory && snapshot.status == ACTIVE_STATUS)
        val transition = pageTransitionProgress()
        canvas.save()
        canvas.translate((1f - transition) * 34f, 0f)
        when (page) {
            Page.Inventory -> drawInventory(canvas)
            Page.Pouch -> drawPouch(canvas)
            Page.Map -> drawMap(canvas)
            Page.Progress -> drawProgress(canvas)
        }
        canvas.restore()
        drawFooter(canvas)
        if (snapshot.status != ACTIVE_STATUS) drawUnavailable(canvas)
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

    private fun drawHeader(canvas: Canvas) {
        paint.color = Color.rgb(83, 57, 29)
        canvas.drawRoundRect(RectF(16f, 16f, 1224f, 108f), 6f, 6f, paint)
        linePaint.color = Color.rgb(48, 31, 15)
        linePaint.strokeWidth = 3f
        canvas.drawRoundRect(RectF(18f, 18f, 1222f, 106f), 5f, 5f, linePaint)

        paint.color = Color.rgb(134, 98, 47)
        canvas.drawRoundRect(RectF(24f, 22f, 184f, 102f), 4f, 4f, paint)
        linePaint.color = COLOR_GOLD
        linePaint.strokeWidth = 2f
        canvas.drawRoundRect(RectF(24f, 22f, 184f, 102f), 4f, 4f, linePaint)
        drawText(canvas, "SKYWARD", 104f, 53f, 22f, true, Paint.Align.CENTER, Color.rgb(247, 224, 166))
        drawText(canvas, "COMPANION", 104f, 80f, 17f, true, Paint.Align.CENTER, Color.rgb(226, 193, 119))
        val connected = snapshot.status == ACTIVE_STATUS
        paint.color = if (connected) COLOR_GREEN else COLOR_GOLD
        canvas.drawCircle(40f, 91f, 4f, paint)
        drawText(canvas, if (connected) "LIVE" else "SYNC", 51f, 96f, 11f, true, color = Color.rgb(247, 224, 166))
    }

    private fun drawHeaderHearts(canvas: Canvas, x: Float, y: Float) {
        val hearts = ((snapshot.healthCapacity + 3) / 4).coerceIn(0, 10)
        val filled = snapshot.currentHealth / 4f
        for (index in 0 until hearts) {
            val left = x + (index % 5) * 34f
            val top = y + (index / 5) * 32f
            paint.alpha = if (index < filled) 255 else 60
            val heart = assets.heart
            if (heart != null) {
                canvas.drawBitmap(heart, null, RectF(left, top, left + 27f, top + 27f), paint)
            } else {
                paint.color = if (index < filled) COLOR_RED else COLOR_TEXT_MUTED
                canvas.drawCircle(left + 13f, top + 13f, 11f, paint)
            }
        }
        paint.alpha = 255
    }

    private fun drawTabs(canvas: Canvas) {
        tabBounds.forEachIndexed { index, bounds ->
            val active = page.ordinal == index
            paint.color = if (active) COLOR_BLUE else COLOR_SURFACE_RAISED
            canvas.drawRoundRect(bounds, 4f, 4f, paint)
            linePaint.color = if (active) COLOR_GOLD else Color.rgb(128, 93, 48)
            linePaint.strokeWidth = if (active) 3f else 1f
            canvas.drawRoundRect(bounds, 4f, 4f, linePaint)
            drawTabGlyph(canvas, index, bounds, active)
            drawText(
                canvas,
                PAGE_TITLES[index],
                bounds.left + 82f,
                bounds.centerY() + 10f,
                28f,
                true,
                color = if (active) Color.rgb(248, 231, 187) else COLOR_TEXT
            )
        }
    }

    private fun drawTabGlyph(canvas: Canvas, index: Int, bounds: RectF, active: Boolean) {
        val left = bounds.left + 30f
        val top = bounds.top + 25f
        linePaint.color = if (active) Color.rgb(248, 231, 187) else COLOR_BORDER
        linePaint.strokeWidth = if (active) 2.5f else 2f
        when (index) {
            0 -> {
                for (row in 0..1) {
                    for (column in 0..1) {
                        val x = left + column * 13f
                        val y = top + row * 13f
                        canvas.drawRoundRect(RectF(x, y, x + 9f, y + 9f), 2f, 2f, linePaint)
                    }
                }
            }
            1 -> {
                canvas.drawRoundRect(RectF(left, top + 6f, left + 27f, top + 27f), 5f, 5f, linePaint)
                canvas.drawArc(RectF(left + 7f, top, left + 20f, top + 14f), 190f, 160f, false, linePaint)
            }
            2 -> {
                canvas.drawLine(left, top + 6f, left + 8f, top + 1f, linePaint)
                canvas.drawLine(left + 8f, top + 1f, left + 18f, top + 7f, linePaint)
                canvas.drawLine(left + 18f, top + 7f, left + 28f, top + 2f, linePaint)
                canvas.drawLine(left, top + 6f, left, top + 28f, linePaint)
                canvas.drawLine(left + 9f, top + 3f, left + 9f, top + 25f, linePaint)
                canvas.drawLine(left + 19f, top + 8f, left + 19f, top + 28f, linePaint)
                canvas.drawLine(left + 28f, top + 2f, left + 28f, top + 24f, linePaint)
            }
            else -> {
                val icon = assets.fiIcon
                if (icon != null) {
                    drawBitmapFit(
                        canvas,
                        icon,
                        RectF(left - 1f, top - 3f, left + 31f, top + 29f),
                        if (active) 255 else 120
                    )
                } else {
                    canvas.drawCircle(left + 14f, top + 14f, 12f, linePaint)
                    canvas.drawLine(left + 14f, top + 3f, left + 14f, top + 25f, linePaint)
                }
            }
        }
    }

    private fun drawInventory(canvas: Canvas) {
        panel(canvas, RectF(12f, 120f, 714f, 1008f), 112)
        panel(canvas, RectF(722f, 120f, 1228f, 1008f), 112)
        sectionTitle(canvas, "INVENTORY", "Gear and items obtained in the current save", 32f, 156f)
        categoryBounds.forEachIndexed { index, bounds ->
            paint.color = if (category == index) COLOR_BLUE else COLOR_SURFACE_RAISED
            canvas.drawRoundRect(bounds, 6f, 6f, paint)
            linePaint.color = if (category == index) COLOR_GOLD else COLOR_BORDER
            linePaint.strokeWidth = if (category == index) 2f else 1f
            canvas.drawRoundRect(bounds, 6f, 6f, linePaint)
            drawText(
                canvas,
                CATEGORY_TITLES[index],
                bounds.centerX(),
                bounds.centerY() + 7f,
                17f,
                true,
                Paint.Align.CENTER,
                if (category == index) Color.rgb(248, 231, 187) else COLOR_TEXT_SECONDARY
            )
        }
        val items = inventoryItems()
        val visible = items.take(30)
        visible.forEachIndexed { index, id ->
            val column = index % 6
            val row = index / 6
            val bounds = RectF(
                32f + column * 109f,
                238f + row * 102f,
                133f + column * 109f,
                332f + row * 102f
            )
            drawItemSlot(canvas, bounds, id, snapshot.itemValues[id], index == selectedIndex)
        }
        if (visible.isEmpty()) {
            drawText(
                canvas,
                "No obtained items in this category",
                363f,
                490f,
                25f,
                false,
                Paint.Align.CENTER,
                COLOR_TEXT_SECONDARY
            )
        }
        val selected = visible.getOrNull(selectedIndex)
        drawDetail(canvas, RectF(32f, 756f, 694f, 990f), selected)
        drawStatus(canvas, RectF(742f, 138f, 1208f, 326f), compact = true)
        drawPlayerCard(canvas, selected)
    }

    private fun drawPlayerCard(canvas: Canvas, selected: Int?) {
        drawText(canvas, "LINK", 742f, 365f, 20f, true, color = COLOR_BLUE_DARK)
        drawText(
            canvas,
            "Drag the 3D model to rotate",
            1208f,
            365f,
            15f,
            false,
            Paint.Align.RIGHT,
            COLOR_TEXT_SECONDARY
        )
        panel(canvas, RectF(824f, 378f, 1090f, 850f), 30)
        assets.linkPortrait?.let { portrait ->
            drawBitmapFit(canvas, portrait, RectF(834f, 388f, 1080f, 840f), 215)
        }

        val shield = snapshot.shieldPouchSlot.takeIf { it in snapshot.pouch.indices }
            ?.let { packedItemId(snapshot.pouch[it]) }
        drawEquipmentSlot(
            canvas,
            RectF(742f, 398f, 816f, 512f),
            "SHIELD",
            shield
        )
        drawEquipmentSlot(
            canvas,
            RectF(1098f, 398f, 1208f, 530f),
            "B ITEM",
            snapshot.equippedBItem.takeIf { it in 0 until ITEM_COUNT }
        )
        drawEquipmentSlot(
            canvas,
            RectF(1098f, 552f, 1208f, 684f),
            "SELECTED",
            selected
        )

        panel(canvas, RectF(742f, 868f, 1208f, 990f), 45)
        drawText(canvas, "CURRENT LOADOUT", 758f, 900f, 17f, true, color = COLOR_BLUE_DARK)
        drawLabelValue(
            canvas,
            "B-wheel",
            itemName(snapshot.equippedBItem).take(22),
            758f,
            934f,
            204f
        )
        drawLabelValue(
            canvas,
            "Shield",
            shield?.let(::itemName)?.take(22) ?: "None",
            980f,
            934f,
            210f
        )
        drawText(
            canvas,
            listOf(snapshot.area, snapshot.stage).filter(String::isNotBlank).joinToString("  /  ")
                .ifBlank { "Waiting for current location" },
            758f,
            974f,
            15f,
            color = COLOR_TEXT_SECONDARY
        )
    }

    private fun drawEquipmentSlot(canvas: Canvas, bounds: RectF, label: String, itemId: Int?) {
        paint.color = Color.argb(205, 229, 210, 164)
        canvas.drawRoundRect(bounds, 5f, 5f, paint)
        linePaint.color = COLOR_BORDER
        linePaint.strokeWidth = 2f
        canvas.drawRoundRect(bounds, 5f, 5f, linePaint)
        drawText(canvas, label, bounds.centerX(), bounds.top + 20f, 12f, true, Paint.Align.CENTER, COLOR_BLUE_DARK)
        val icon = itemId?.let { assets.itemIcons[it] }
        if (icon != null) {
            drawBitmapFit(canvas, icon, RectF(bounds.left + 7f, bounds.top + 25f, bounds.right - 7f, bounds.bottom - 23f))
        } else {
            drawText(
                canvas,
                itemId?.let(::itemInitials) ?: "--",
                bounds.centerX(),
                bounds.centerY() + 10f,
                21f,
                true,
                Paint.Align.CENTER,
                COLOR_TEXT_MUTED
            )
        }
        drawTextFitted(
            canvas,
            itemId?.let(::itemName) ?: "None",
            bounds.centerX(),
            bounds.bottom - 7f,
            bounds.width() - 8f,
            11f,
            true,
            Paint.Align.CENTER,
            COLOR_TEXT
        )
    }

    private fun drawPouch(canvas: Canvas) {
        panel(canvas, RectF(12f, 120f, 894f, 1008f), 112)
        panel(canvas, RectF(902f, 120f, 1228f, 1008f), 112)
        sectionTitle(canvas, "ADVENTURE POUCH", "Active slots and item-check storage", 32f, 164f)
        snapshot.pouch.forEachIndexed { index, packed ->
            val bounds = RectF(32f + index * 103f, 198f, 125f + index * 103f, 300f)
            drawPackedSlot(canvas, bounds, packed, index == selectedIndex)
            if (index == snapshot.selectedPouchSlot) {
                drawText(
                    canvas,
                    "B",
                    bounds.left + 8f,
                    bounds.top + 17f,
                    13f,
                    true,
                    color = Color.rgb(235, 207, 107)
                )
            }
            if (index == snapshot.shieldPouchSlot) {
                drawText(
                    canvas,
                    "S",
                    bounds.right - 8f,
                    bounds.top + 17f,
                    13f,
                    true,
                    Paint.Align.RIGHT,
                    Color.rgb(133, 230, 239)
                )
            }
        }
        drawText(canvas, "ITEM CHECK STORAGE", 32f, 348f, 18f, true, color = COLOR_BLUE_DARK)
        snapshot.itemCheck.forEachIndexed { index, packed ->
            val column = index % 10
            val row = index / 10
            val bounds = RectF(
                32f + column * 82f,
                370f + row * 67f,
                106f + column * 82f,
                430f + row * 67f
            )
            drawPackedSlot(canvas, bounds, packed, selectedIndex == index + 8, compact = true)
        }
        val selectedPacked = if (selectedIndex < 8) {
            snapshot.pouch.getOrNull(selectedIndex)
        } else {
            snapshot.itemCheck.getOrNull(selectedIndex - 8)
        }
        drawDetail(
            canvas,
            RectF(32f, 794f, 874f, 990f),
            selectedPacked?.let(::packedItemId)?.takeIf { it in 0 until ITEM_COUNT }
        )
        drawStatus(canvas, RectF(918f, 144f, 1212f, 370f), compact = true)
        drawText(canvas, "LIVE POUCH STATE", 918f, 416f, 18f, true, color = COLOR_BLUE_DARK)
        val labels = listOf(
            "B-wheel item" to itemName(snapshot.equippedBItem),
            "Selected slot" to slotLabel(snapshot.selectedPouchSlot, 8),
            "Shield slot" to slotLabel(snapshot.shieldPouchSlot, 8),
            "Dowsing slot" to snapshot.selectedDowsingSlot.toString(),
            "Stored items" to snapshot.itemCheck.count { packedItemId(it) in 0 until ITEM_COUNT }.toString()
        )
        labels.forEachIndexed { index, pair ->
            drawLabelValue(canvas, pair.first, pair.second, 918f, 462f + index * 54f, 278f)
        }
        panel(canvas, RectF(918f, 758f, 1212f, 990f), 62)
        drawText(canvas, "POTION TIMERS", 934f, 794f, 18f, true, color = COLOR_BLUE_DARK)
        val timers = snapshot.potionTimers
        drawLabelValue(canvas, "Air", "${timers[0]} / ${timers[1]}", 934f, 838f, 260f)
        drawLabelValue(canvas, "Stamina", "${timers[2]} / ${timers[3]}", 934f, 892f, 260f)
        drawLabelValue(canvas, "Guardian", "${timers[4]} / ${timers[5]}", 934f, 946f, 260f)
    }

    private fun drawMap(canvas: Canvas) {
        panel(canvas, RectF(12f, 120f, 906f, 1008f), 112)
        panel(canvas, RectF(914f, 120f, 1228f, 1008f), 112)
        val mapIndex = currentMapIndex(snapshot.stage)
        val mapTitle = MAP_TITLES[mapIndex]
        sectionTitle(canvas, "$mapTitle MAP", "Automatically matched to the live stage", 32f, 164f)
        val map = assets.maps.getOrNull(mapIndex)
        val mapRect = RectF(32f, 184f, 886f, 990f)
        paint.color = Color.rgb(67, 49, 28)
        canvas.drawRoundRect(mapRect, 8f, 8f, paint)
        if (map != null) {
            drawBitmapFit(canvas, map, RectF(mapRect.left + 8f, mapRect.top + 8f, mapRect.right - 8f, mapRect.bottom - 8f))
        } else {
            drawText(canvas, "Loading original map art...", mapRect.centerX(), mapRect.centerY(), 28f, false, Paint.Align.CENTER, Color.rgb(239, 221, 178))
        }
        linePaint.color = COLOR_GOLD
        linePaint.strokeWidth = 3f
        canvas.drawRoundRect(mapRect, 8f, 8f, linePaint)

        paint.color = COLOR_BLUE
        canvas.drawRoundRect(RectF(930f, 144f, 1212f, 204f), 5f, 5f, paint)
        linePaint.color = COLOR_GOLD
        linePaint.strokeWidth = 2f
        canvas.drawRoundRect(RectF(930f, 144f, 1212f, 204f), 5f, 5f, linePaint)
        drawText(canvas, "LIVE REGION", 950f, 168f, 13f, true, color = Color.rgb(241, 218, 164))
        drawText(canvas, mapTitle, 1192f, 188f, 28f, true, Paint.Align.RIGHT, Color.WHITE)

        drawText(canvas, "CURRENT LOCATION", 930f, 250f, 18f, true, color = COLOR_BLUE_DARK)
        drawLabelValue(canvas, "Area", snapshot.area.ifBlank { "--" }, 930f, 300f, 266f)
        drawLabelValue(canvas, "Stage", snapshot.stage.ifBlank { "--" }, 930f, 358f, 266f)
        drawLabelValue(canvas, "Room", snapshot.room.displayByte(), 930f, 416f, 266f)
        drawLabelValue(canvas, "Layer", snapshot.layer.displayByte(), 930f, 474f, 266f)
        drawLabelValue(canvas, "Entrance", snapshot.entrance.displayByte(), 930f, 532f, 266f)
        drawLabelValue(canvas, "Time", if (snapshot.night) "Night" else "Day", 930f, 590f, 266f)
        drawLabelValue(canvas, "Trial", snapshot.trial.displayByte(), 930f, 648f, 266f)
        drawText(canvas, "GAME ASSET", 930f, 736f, 18f, true, color = COLOR_BLUE_DARK)
        drawWrappedText(
            canvas,
            "This chart is decoded from Skyward Sword HD's map archive and selected from the live stage ID. It cannot be changed to an unrelated region.",
            RectF(930f, 760f, 1198f, 896f),
            19f,
            6,
            COLOR_TEXT_SECONDARY
        )
        drawText(canvas, "Stage-driven map selection", 930f, 956f, 16f, true, color = COLOR_GREEN)
    }

    private fun currentMapIndex(stage: String): Int {
        val id = stage.trim().uppercase()
        if (id.startsWith("F4")) return 1 // Sealed Grounds / Temple of Hylia
        if (id.length < 2 || id[0] !in "FDBS") return 0
        return when (id[1]) {
            '1' -> 1
            '2' -> 2
            '3' -> 3
            else -> 0
        }
    }

    private fun drawProgress(canvas: Canvas) {
        panel(canvas, RectF(12f, 120f, 872f, 1008f), 112)
        panel(canvas, RectF(880f, 120f, 1228f, 1008f), 112)
        panel(canvas, RectF(32f, 144f, 852f, 528f), 66)
        sectionTitle(canvas, "FI SUMMARY", "Live guidance selected by story flags", 52f, 184f)
        assets.fiIcon?.let {
            drawBitmapFit(canvas, it, RectF(752f, 166f, 826f, 240f), 150)
        }
        drawWrappedText(
            canvas,
            fiMessage("KEN0", snapshot.summaryId, "No progress summary is available yet."),
            RectF(52f, 224f, 824f, 498f),
            24f,
            9
        )
        panel(canvas, RectF(32f, 546f, 852f, 990f), 66)
        sectionTitle(canvas, "CURRENT OBJECTIVE", "Resolved from the native navigation table", 52f, 588f)
        drawWrappedText(
            canvas,
            fiMessage("KEN2", snapshot.objectiveId, "No objective is available yet."),
            RectF(52f, 614f, 824f, 958f),
            24f,
            11
        )
        drawStatus(canvas, RectF(896f, 144f, 1212f, 370f), compact = true)
        drawText(canvas, "PROGRESSION", 896f, 416f, 18f, true, color = COLOR_BLUE_DARK)
        val values = listOf(
            "Gratitude Crystals" to snapshot.gratitude.toString(),
            "Heart Pieces" to snapshot.heartPieces.toString(),
            "Key Pieces" to snapshot.keyPieces.toString(),
            "Pouch Expansions" to snapshot.pouchExpansions.toString(),
            "Extra Wallets" to snapshot.extraWallets.toString(),
            "Save state" to if (snapshot.newFile) "Unsaved new file" else "Established"
        )
        values.forEachIndexed { index, pair ->
            drawLabelValue(canvas, pair.first, pair.second, 896f, 466f + index * 58f, 300f)
        }
        panel(canvas, RectF(896f, 848f, 1212f, 990f), 62)
        drawText(canvas, "SOURCE", 912f, 884f, 17f, true, color = COLOR_BLUE_DARK)
        drawText(canvas, "Live story and item flags", 912f, 922f, 19f, true)
        drawText(canvas, "Fi entries ${snapshot.summaryId} / ${snapshot.objectiveId}", 912f, 958f, 17f)
    }

    private fun drawStatus(canvas: Canvas, bounds: RectF, compact: Boolean = false) {
        panel(canvas, bounds, 92)
        drawText(
            canvas,
            "VITALS",
            bounds.left + 16f,
            bounds.top + 32f,
            17f,
            true,
            color = COLOR_BLUE_DARK
        )
        val heartSize = if (compact) 19f else 22f
        val hearts = max(1, (snapshot.healthCapacity + 3) / 4).coerceAtMost(20)
        val filled = snapshot.currentHealth / 4f
        val columns = if (compact) 8 else 10
        for (index in 0 until hearts) {
            val column = index % columns
            val row = index / columns
            val left = bounds.left + 16f + column * (heartSize + 2f)
            val top = bounds.top + 48f + row * (heartSize + 2f)
            val heart = assets.heart
            paint.alpha = if (index < filled) 255 else 60
            if (heart != null) {
                canvas.drawBitmap(heart, null, RectF(left, top, left + heartSize, top + heartSize), paint)
            } else {
                paint.color = if (index < filled) COLOR_RED else COLOR_TEXT_MUTED
                canvas.drawCircle(left + heartSize / 2f, top + heartSize / 2f, heartSize * .42f, paint)
            }
        }
        paint.alpha = 255
        drawText(
            canvas,
            "${snapshot.currentHealth} / ${snapshot.healthCapacity}",
            bounds.right - 16f,
            bounds.top + 32f,
            17f,
            true,
            Paint.Align.RIGHT,
            COLOR_RED
        )
        val rows = (hearts + columns - 1) / columns
        val gaugeTop = bounds.top + 58f + rows * (heartSize + 2f)
        drawText(canvas, "ST", bounds.left + 16f, gaugeTop + 17f, 14f, true)
        val gauge = RectF(bounds.left + 46f, gaugeTop + 4f, bounds.right - 16f, gaugeTop + 20f)
        paint.color = Color.rgb(91, 69, 38)
        canvas.drawRoundRect(gauge, 7f, 7f, paint)
        val ratio = (snapshot.stamina / 1000f).coerceIn(0f, 1f)
        paint.color = COLOR_GREEN
        canvas.drawRoundRect(
            RectF(gauge.left, gauge.top, gauge.left + gauge.width() * ratio, gauge.bottom),
            7f,
            7f,
            paint
        )
        val resourceY = bounds.bottom - 18f
        drawText(canvas, "R ${snapshot.rupees}", bounds.left + 16f, resourceY, 17f, true)
        drawText(
            canvas,
            "S ${snapshot.seeds}   A ${snapshot.arrows}   B ${snapshot.bombs}",
            bounds.right - 16f,
            resourceY,
            15f,
            false,
            Paint.Align.RIGHT
        )
    }

    private fun drawLoadout(canvas: Canvas, bounds: RectF, selected: Int?) {
        panel(canvas, bounds, 62)
        drawText(
            canvas,
            "LIVE LOADOUT",
            bounds.left + 16f,
            bounds.top + 36f,
            18f,
            true,
            color = COLOR_CYAN
        )
        drawLabelValue(
            canvas,
            "B-wheel",
            itemName(snapshot.equippedBItem).take(18),
            bounds.left + 16f,
            bounds.top + 82f,
            bounds.width() - 32f
        )
        val shield = snapshot.shieldPouchSlot.takeIf { it in snapshot.pouch.indices }
            ?.let { packedItemId(snapshot.pouch[it]) }
        drawLabelValue(
            canvas,
            "Shield",
            shield?.let(::itemName)?.take(18) ?: "None",
            bounds.left + 16f,
            bounds.top + 138f,
            bounds.width() - 32f
        )
        drawLabelValue(
            canvas,
            "Dowsing slot",
            snapshot.selectedDowsingSlot.displayByte(),
            bounds.left + 16f,
            bounds.top + 194f,
            bounds.width() - 32f
        )
        linePaint.color = COLOR_BORDER
        canvas.drawLine(bounds.left + 16f, bounds.top + 226f, bounds.right - 16f, bounds.top + 226f, linePaint)
        drawText(canvas, "SELECTED ITEM", bounds.left + 16f, bounds.top + 266f, 18f, true, color = COLOR_CYAN)
        val selectedIcon = selected?.let { assets.itemIcons[it] }
        if (selectedIcon != null) {
            drawBitmapFit(
                canvas,
                selectedIcon,
                RectF(bounds.left + 16f, bounds.top + 282f, bounds.left + 94f, bounds.top + 360f)
            )
        }
        drawText(
            canvas,
            selected?.let(::itemName) ?: "No item selected",
            bounds.left + if (selectedIcon != null) 106f else 16f,
            bounds.top + 312f,
            if (selectedIcon != null) 21f else 24f,
            true
        )
        val descriptionTop = bounds.top + if (selectedIcon != null) 374f else 330f
        drawWrappedText(
            canvas,
            selected?.let(::itemDescription).orEmpty().ifBlank { "Select an obtained item for its original description." },
            RectF(bounds.left + 16f, descriptionTop, bounds.right - 16f, bounds.bottom - 18f),
            19f,
            if (selectedIcon != null) 7 else 9
        )
    }

    private fun drawDetail(canvas: Canvas, bounds: RectF, itemId: Int?) {
        panel(canvas, bounds, 75)
        drawText(canvas, "INSPECT", bounds.left + 18f, bounds.top + 32f, 16f, true, color = COLOR_BLUE_DARK)
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
        drawWrappedText(
            canvas,
            itemDescription(itemId),
            RectF(textLeft, bounds.top + 88f, bounds.right - 18f, bounds.bottom - 12f),
            19f,
            5
        )
    }

    private fun drawItemSlot(canvas: Canvas, bounds: RectF, itemId: Int, count: Int, selected: Boolean) {
        paint.color = if (selected) COLOR_BLUE else COLOR_SURFACE_RAISED
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
        } else {
            drawText(
                canvas,
                itemInitials(itemId),
                bounds.centerX(),
                bounds.top + 48f,
                27f,
                true,
                Paint.Align.CENTER,
                if (selected) Color.rgb(248, 231, 187) else COLOR_TEXT_MUTED
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
        if (count > 1) {
            paint.color = COLOR_GOLD
            canvas.drawCircle(bounds.right - 12f, bounds.top + 16f, 11f, paint)
            drawText(canvas, count.toString(), bounds.right - 12f, bounds.top + 21f, 12f, true, Paint.Align.CENTER, COLOR_BACKGROUND_TOP)
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
            selected -> COLOR_BLUE
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
                        RectF(bounds.left + 12f, bounds.top + 8f, bounds.right - 12f, bounds.bottom - 25f)
                    }
                )
            } else {
                drawText(
                    canvas,
                    if (compact) id.toString() else itemInitials(id),
                    bounds.centerX(),
                    bounds.centerY() + if (compact) 6f else 1f,
                    if (compact) 15f else 22f,
                    true,
                    Paint.Align.CENTER,
                    if (selected) Color.rgb(248, 231, 187) else COLOR_TEXT_MUTED
                )
            }
            if (!compact) {
                drawTextFitted(
                    canvas,
                    itemName(id),
                    bounds.centerX(),
                    bounds.bottom - 7f,
                    bounds.width() - 10f,
                    12f,
                    true,
                    Paint.Align.CENTER,
                    if (selected) Color.rgb(248, 231, 187) else COLOR_TEXT
                )
                val upper = (packed ushr 16) and 0xFFFF
                if (upper != 0) {
                    paint.color = COLOR_GOLD
                    canvas.drawCircle(bounds.right - 9f, bounds.top + 12f, 9f, paint)
                    drawText(canvas, upper.toString(), bounds.right - 9f, bounds.top + 16f, 10f, true, Paint.Align.CENTER, COLOR_BACKGROUND_TOP)
                }
            }
        }
    }

    private fun drawFooter(canvas: Canvas) {
        paint.color = Color.rgb(82, 56, 28)
        canvas.drawRoundRect(RectF(12f, 1018f, 1228f, 1068f), 7f, 7f, paint)
        linePaint.color = COLOR_GOLD
        linePaint.strokeWidth = 2f
        canvas.drawRoundRect(RectF(12f, 1018f, 1228f, 1068f), 7f, 7f, linePaint)
        val mode = if (controllerActive) "BOTTOM-SCREEN CONTROL ACTIVE" else "GAME CONTROLS PASS THROUGH"
        drawText(canvas, mode, 32f, 1050f, 17f, true, color = Color.rgb(241, 214, 142))
        drawText(
            canvas,
            "Touch tabs or swipe   |   MODE toggles navigation   |   L/R page   D-pad move",
            1205f,
            1050f,
            16f,
            false,
            Paint.Align.RIGHT,
            Color.rgb(230, 205, 151)
        )
    }

    private fun drawUnavailable(canvas: Canvas) {
        paint.color = Color.argb(242, 61, 43, 24)
        canvas.drawRoundRect(RectF(300f, 382f, 1110f, 698f), 14f, 14f, paint)
        paint.color = COLOR_BLUE
        canvas.drawRoundRect(RectF(300f, 382f, 308f, 698f), 5f, 5f, paint)
        val title = when (snapshot.status) {
            1 -> "Waiting for Skyward Sword HD"
            2 -> "Unsupported Skyward Sword HD build"
            3 -> "Connecting to live game state"
            else -> "Waiting for the game to start"
        }
        drawText(canvas, "LIVE LINK", 344f, 430f, 16f, true, color = Color.rgb(112, 198, 221))
        drawText(canvas, title, 344f, 486f, 32f, true, color = Color.WHITE)
        val checkpoint = (snapshot.capabilities ushr 24) and 0xFF
        val detail = when {
            snapshot.status == 2 ->
                "This integration supports the verified Skyward Sword HD 1.0.1 executable only."
            snapshot.status != 3 ->
                "Start or resume the game; the field kit will connect without opening the pause menu."
            checkpoint == 3 -> "The executable module is still initializing."
            checkpoint == 4 -> "Waiting for the game-thread telemetry payload to publish live state."
            checkpoint == 5 -> "The payload's Save A block has not passed live-state validation yet."
            checkpoint == 6 -> "The Save A inventory block is not readable yet."
            else -> "Waiting for Save A and the live flag managers to initialize."
        }
        drawWrappedText(canvas, detail, RectF(344f, 520f, 1060f, 620f), 21f, 4, Color.rgb(224, 207, 171))
        if (snapshot.status == 3 && checkpoint != 0) {
            drawText(canvas, "TRACE CHECKPOINT $checkpoint", 344f, 662f, 15f, true, color = COLOR_GOLD)
        }
    }

    private fun sectionTitle(canvas: Canvas, title: String, subtitle: String, x: Float, y: Float) {
        drawText(canvas, title, x, y, 20f, true, color = COLOR_BLUE_DARK)
        val subtitleX = x + paint.measureText(title) + 22f
        drawText(canvas, subtitle, subtitleX, y, 15f, color = COLOR_TEXT_SECONDARY)
        linePaint.color = Color.argb(210, 34, 112, 151)
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
        linePaint.color = Color.argb(205, 38, 114, 151)
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

    private fun inventoryItems(): List<Int> = CATEGORY_IDS[category]
        .filter { it in snapshot.itemValues.indices && snapshot.itemValues[it] > 0 }

    private fun itemName(itemId: Int): String {
        if (itemId !in 0 until ITEM_COUNT) return "None"
        return assets.itemText["NAME_ITEM_%03d".format(itemId)]
            ?.trim()
            ?.takeIf { it.isNotEmpty() }
            ?: "Item $itemId"
    }

    private fun itemDescription(itemId: Int): String =
        assets.itemText["CAPTION_ITEM_%03d".format(itemId)]?.trim().orEmpty()

    private fun itemInitials(itemId: Int): String {
        val words = itemName(itemId)
            .replace(Regex("[^A-Za-z0-9 ]"), " ")
            .split(Regex("\\s+"))
            .filter { it.isNotBlank() }
        return when {
            words.isEmpty() -> "?"
            words.size == 1 -> words.first().take(2).uppercase()
            else -> "${words[0].first()}${words[1].first()}".uppercase()
        }
    }

    private fun fiMessage(prefix: String, id: Int, fallback: String): String {
        if (id < 0) return fallback
        return assets.fiText["${prefix}_%02d".format(id)]?.trim()?.takeIf { it.isNotEmpty() }
            ?: fallback
    }

    private fun packedItemId(packed: Int): Int = if (packed < 0) -1 else packed and 0xFF

    private fun slotLabel(value: Int, count: Int): String =
        if (value in 0 until count) (value + 1).toString() else "None"

    private fun Int.displayByte(): String = if (this == 0xFF) "—" else toString()

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val scale = min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT)
        if (scale <= 0f) return false
        val x = (event.x - (width - DESIGN_WIDTH * scale) * .5f) / scale
        val y = (event.y - (height - DESIGN_HEIGHT * scale) * .5f) / scale
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                downX = x
                downY = y
                return true
            }
            MotionEvent.ACTION_UP -> {
                performClick()
                if (abs(x - downX) > 90f && abs(y - downY) < 90f) {
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
                    Page.Pouch -> handlePouchTap(x, y)
                    Page.Map -> Unit
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

    private fun handleInventoryTap(x: Float, y: Float) {
        categoryBounds.indexOfFirst { it.contains(x, y) }.takeIf { it >= 0 }?.let {
            category = it
            selectedIndex = 0
            return
        }
        if (x !in 32f..678f || y !in 238f..740f) return
        val column = ((x - 32f) / 109f).toInt().coerceIn(0, 5)
        val row = ((y - 238f) / 102f).toInt().coerceIn(0, 4)
        val index = row * 6 + column
        if (index < inventoryItems().take(30).size) selectedIndex = index
    }

    private fun handlePouchTap(x: Float, y: Float) {
        if (y in 198f..300f && x in 32f..846f) {
            selectedIndex = ((x - 32f) / 103f).toInt().coerceIn(0, 7)
            return
        }
        if (x !in 32f..844f || y !in 370f..765f) return
        val column = ((x - 32f) / 82f).toInt().coerceIn(0, 9)
        val row = ((y - 370f) / 67f).toInt().coerceIn(0, 5)
        selectedIndex = 8 + row * 10 + column
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
            KeyEvent.KEYCODE_DPAD_LEFT -> moveSelection(-1, 0)
            KeyEvent.KEYCODE_DPAD_RIGHT -> moveSelection(1, 0)
            KeyEvent.KEYCODE_DPAD_UP -> moveSelection(0, -1)
            KeyEvent.KEYCODE_DPAD_DOWN -> moveSelection(0, 1)
            KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BACK -> {
                controllerActive = false
                true
            }
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_ENTER,
            KeyEvent.KEYCODE_DPAD_CENTER -> true
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
        val horizontal = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val vertical = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
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
                val count = inventoryItems().take(30).size
                if (count > 0) {
                    selectedIndex = (selectedIndex + dx + dy * 6).coerceIn(0, count - 1)
                }
            }
            Page.Pouch -> selectedIndex = (selectedIndex + dx + dy * 10).coerceIn(0, 67)
            Page.Map -> return true
            Page.Progress -> return true
        }
        return true
    }

    private fun cyclePage(delta: Int) {
        changePage(Page.entries[(page.ordinal + delta + Page.entries.size) % Page.entries.size])
    }

    private fun changePage(next: Page) {
        if (page == next) return
        page = next
        selectedIndex = 0
        pageChangedAt = SystemClock.uptimeMillis()
        setPlayerVisible(page == Page.Inventory && snapshot.status == ACTIVE_STATUS)
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
