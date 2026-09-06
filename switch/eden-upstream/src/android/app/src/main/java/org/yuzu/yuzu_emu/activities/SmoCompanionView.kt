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
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.os.SystemClock
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.View
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.utils.Log
import java.nio.ByteBuffer
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference
import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.min

/** Odyssey-only presentation. Game reads and RomFS decoding never run in onDraw/onTouch. */
class SmoCompanionView(context: Context,
    private val onAppearance: (String, String, Boolean) -> Unit = { _, _, _ -> }
) : View(context) {
    private val red = Color.rgb(224, 24, 44)
    private val ink = Color.rgb(30, 29, 27)
    private val paper = Color.rgb(248, 245, 231)
    private val muted = Color.rgb(102, 98, 89)
    private val gold = Color.rgb(139, 117, 48)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
    private val bold = Typeface.create("sans-serif-condensed", Typeface.BOLD)
    private val regular = Typeface.create("sans-serif", Typeface.NORMAL)
    private val pages = listOf("MAP", "POWER MOONS", "COLLECTIONS", "OUTFITS")
    private val mapBounds = RectF(36f, 180f, 812f, 940f)
    private val recenterBounds = RectF(622f, 844f, 796f, 924f)
    private val viewport = SmoMapViewport().apply { resize(mapBounds.width(), mapBounds.height()) }
    private val ui = Handler(Looper.getMainLooper())
    private var readerThread: HandlerThread? = null
    private var assetThread: HandlerThread? = null
    private var assetHandler: Handler? = null
    @Volatile private var epoch = 0
    private var state = SmoSnapshot(0, 0)
    private var names: Map<String, String> = emptyMap()
    private var outfitNames: Map<String, String> = emptyMap()
    private var hatNames: Map<String, String> = emptyMap()
    private var bitmap: Bitmap? = null
    private var bitmapKey: String? = null
    private var requestedKey: String? = null
    private var assetBusy = false
    private var retryAt = 0L
    private var namesBusy = false
    private var namesRetryAt = 0L
    @Volatile private var page = 0
    private var moons: SmoMoons? = null
    private var markers: SmoMarkers? = null
    private var selectedMarker: Int? = null
    @Volatile private var collectionKind = 2
    private var wardrobeKind = 0
    private var collectionTab = 0
    private var collection: SmoCollection? = null
    private var collectionScroll = 0f
    private var selectedCollection: Int? = null
    private data class OutfitRequest(val kind: Int, val name: String, val generation: Long)
    private val outfitRequests = AtomicReference<OutfitRequest?>()
    private var outfitBusy = false
    private var actionMessage: String? = null
    private val clearActionMessage = Runnable { actionMessage = null; postInvalidateOnAnimation() }
    private val equipBounds = RectF(SmoWardrobe.LEFT, SmoWardrobe.TOP, SmoWardrobe.RIGHT, SmoWardrobe.BOTTOM)
    private val collectionNames = HashMap<Int, Map<String, String>>()
    private val collectionIcons = LinkedHashMap<String, Bitmap>()
    private val iconRequests = HashSet<String>()
    private val iconRetry = HashMap<String, Long>()
    private var moonScroll = 0f
    private var selectedMoon: Int? = null
    private val moonBounds = RectF(48f, 304f, 800f, 938f)
    private val moonRowHeight = 112f
    private val stageMessages = LinkedHashMap<String, Map<String, String>>()
    private val stageRequests = HashSet<String>()
    private val stageRetry = HashMap<String, Long>()
    private var pageChangedAt = 0L
    private var scale = 1f
    private var originX = 0f
    private var originY = 0f
    private var downX = 0f
    private var downY = 0f
    private var lastX = 0f
    private var lastY = 0f
    private var dragged = false
    private var multiTouch = false
    private var mapGesture = false
    private var lastTapAt = 0L
    private var lastTapX = 0f
    private var lastTapY = 0f

    private val pinch = ScaleGestureDetector(context,
        object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
            override fun onScaleBegin(detector: ScaleGestureDetector): Boolean {
                multiTouch = true
                return mapGesture && state.map != null
            }
            override fun onScale(detector: ScaleGestureDetector): Boolean {
                viewport.scale(detector.scaleFactor,
                    (detector.focusX - originX) / scale - mapBounds.left,
                    (detector.focusY - originY) / scale - mapBounds.top)
                postInvalidateOnAnimation()
                return true
            }
        })

    init { isClickable = true; setBackgroundColor(paper); contentDescription = "Odyssey second screen" }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        val token = ++epoch
        val worker = HandlerThread("SMO-state", android.os.Process.THREAD_PRIORITY_BACKGROUND)
        worker.start(); readerThread = worker
        val pollHandler = Handler(worker.looper)
        val assets = HandlerThread("SMO-assets", android.os.Process.THREAD_PRIORITY_BACKGROUND)
        assets.start(); assetThread = assets; assetHandler = Handler(assets.looper)
        val buffer = ByteBuffer.allocateDirect(SmoSnapshot.BYTES)
        val moonBuffer = ByteBuffer.allocateDirect(SmoMoons.BYTES)
        val markerBuffer = ByteBuffer.allocateDirect(SmoMarkers.BYTES)
        val collectionBuffer = ByteBuffer.allocateDirect(SmoCollection.BYTES)
        val pending = AtomicReference<SmoSnapshot?>()
        val scheduled = AtomicBoolean(false)
        val deliver = Runnable {
            scheduled.set(false)
            val frame = pending.getAndSet(null)
            if (epoch == token && frame != null) accept(frame)
        }
        pollHandler.post(object : Runnable {
            var reportedError = false
            var moonReadAt = 0L
            var markerReadAt = 0L
            var collectionReadAt = 0L
            var lastCollectionKind = -1
            var outfitTicket = 0
            override fun run() {
                if (epoch != token) return
                outfitRequests.getAndSet(null)?.let { request ->
                    Log.info("[SMO] Wardrobe submit kind=${request.kind} name=${request.name} generation=${request.generation}")
                    outfitTicket = runCatching {
                        NativeLibrary.queueSmoOutfit(request.kind, request.name, request.generation)
                    }.onFailure {
                        Log.warning("[SMO] Wardrobe queue failed: ${it.javaClass.simpleName}: ${it.message}")
                    }.getOrDefault(-1)
                    Log.info("[SMO] Wardrobe ticket=$outfitTicket")
                    if (outfitTicket <= 0) {
                        val failure = outfitTicket
                        ui.post { if (epoch == token) finishOutfit(failure) }
                        outfitTicket = 0
                    }
                }
                if (outfitTicket > 0) {
                    val result = runCatching { NativeLibrary.pollSmoOutfit(outfitTicket) }.getOrDefault(-1)
                    if (result != 0) {
                        outfitTicket = 0
                        ui.post { if (epoch == token) finishOutfit(result) }
                    }
                }
                val frame = try {
                    val bytes = NativeLibrary.fillSmoCompanionSnapshotV1(buffer)
                    SmoSnapshot.decode(buffer, bytes) ?: SmoSnapshot(3, 0)
                } catch (e: Exception) {
                    if (!reportedError) Log.warning("[SMO] Snapshot failed: ${e.javaClass.simpleName}")
                    reportedError = true
                    SmoSnapshot(3, 0)
                } catch (e: UnsatisfiedLinkError) {
                    if (!reportedError) Log.warning("[SMO] Runtime lacks Odyssey snapshot ABI")
                    reportedError = true
                    SmoSnapshot(2, 0)
                }
                pending.set(frame)
                if (scheduled.compareAndSet(false, true)) ui.post(deliver)
                if (frame.active && page == 1 && SystemClock.uptimeMillis() >= moonReadAt) {
                    moonReadAt = SystemClock.uptimeMillis() + 250
                    val collection = runCatching {
                        SmoMoons.decode(moonBuffer, NativeLibrary.fillSmoCompanionMoonsV1(moonBuffer))
                    }.getOrNull()
                    ui.post {
                        if (epoch == token && state.active) {
                            moons = collection?.takeIf {
                                it.generation == state.generation && it.world == state.world
                            }
                            clampMoonScroll()
                            if (selectedMoon != null && moons?.entries?.any { it.id == selectedMoon && it.known } != true)
                                selectedMoon = null
                            requestMoonNames(); postInvalidateOnAnimation()
                        }
                    }
                }
                if (frame.active && page == 0 && SystemClock.uptimeMillis() >= markerReadAt) {
                    markerReadAt = SystemClock.uptimeMillis() + 250
                    val packet = runCatching {
                        SmoMarkers.decode(markerBuffer, NativeLibrary.fillSmoCompanionMarkersV1(markerBuffer))
                    }.getOrNull()
                    ui.post {
                        if (epoch == token) {
                            val next = packet?.takeIf { state.active && it.generation == state.generation &&
                                it.world == state.world && it.scenario == state.scenario }
                            if (next != markers) {
                                markers = next
                                if (next?.entries?.any { it.id == selectedMarker } != true) selectedMarker = null
                                Log.info("[SMO] Map markers: ${next?.entries?.size ?: 0}")
                                requestMoonNames(); postInvalidateOnAnimation()
                            }
                        }
                    }
                }
                if (frame.active && page >= 2 && (collectionKind != lastCollectionKind ||
                        SystemClock.uptimeMillis() >= collectionReadAt)) {
                    val kind = collectionKind
                    lastCollectionKind = kind; collectionReadAt = SystemClock.uptimeMillis() + 500
                    val packet = runCatching {
                        SmoCollection.decode(collectionBuffer,
                            NativeLibrary.fillSmoCompanionCollectionV1(collectionBuffer, kind))
                    }.getOrNull()
                    ui.post {
                        if (epoch == token && collectionKind == kind) {
                            val next = packet?.takeIf { state.active && it.kind == kind &&
                                it.generation == state.generation && it.world == state.world }
                            if (next != collection) {
                                collection = next
                                collectionScroll = collectionScroll.coerceIn(0f, SmoCollectionGrid.maxScroll(next?.entries?.size ?: 0))
                                if (next?.entries?.any { it.id == selectedCollection && it.owned } != true) selectedCollection = null
                                Log.info("[SMO] Collection kind=$kind owned=${next?.entries?.count { it.owned }} total=${next?.entries?.size}")
                                requestCollectionIcons(); postInvalidateOnAnimation()
                            }
                        }
                    }
                }
                // Bounded typed reads, not memory scanning. UI animations are independent.
                pollHandler.postDelayed(this, if (frame.active) 33L else 250L)
            }
        })
        requestNames()
    }

    private fun requestNames() {
        if (names.isNotEmpty() || namesBusy || SystemClock.uptimeMillis() < namesRetryAt) return
        val handler = assetHandler ?: return
        val token = epoch
        namesBusy = true
        handler.post {
            fun read(name: String) = runCatching {
                NativeLibrary.getDualScreenRomfsAsset("smo/messages/$name.msbt")
                    ?.let(SmoMessages::decode).orEmpty()
            }.getOrDefault(emptyMap())
            val messages = read("StageName")
            val outfits = read("ItemCloth")
            val hats = read("ItemCap")
            val extra = mapOf(2 to read("ItemGift"), 3 to read("ItemSticker"),
                4 to read("HackList"), 5 to read("CollectBgmList"))
            ui.post namesResult@{
                if (epoch != token) return@namesResult
                namesBusy = false
                namesRetryAt = SystemClock.uptimeMillis() + 5000
                names = messages
                outfitNames = outfits; hatNames = hats
                collectionNames.putAll(extra); collectionNames[0] = outfits; collectionNames[1] = hats
                invalidate()
            }
        }
    }

    override fun onDetachedFromWindow() {
        ++epoch
        outfitRequests.set(null); outfitBusy = false; actionMessage = null
        readerThread?.quitSafely(); readerThread = null
        assetThread?.quitSafely(); assetThread = null; assetHandler = null
        ui.removeCallbacksAndMessages(null)
        bitmap = null; bitmapKey = null; requestedKey = null; assetBusy = false
        namesBusy = false; namesRetryAt = 0
        stageRequests.clear(); stageRetry.clear(); stageMessages.clear()
        moons = null; selectedMoon = null
        markers = null; selectedMarker = null
        collection = null; selectedCollection = null
        collectionIcons.clear(); iconRequests.clear(); iconRetry.clear()
        state = SmoSnapshot(0, 0)
        onAppearance("", "", false)
        super.onDetachedFromWindow()
    }

    private fun accept(next: SmoSnapshot) {
        val ownerChanged = state.generation != next.generation || state.world != next.world
        val mapChanged = state.map?.texture != next.map?.texture
        if (mapChanged || state.scenario != next.scenario) {
            markers = null; selectedMarker = null
        }
        if (ownerChanged || !next.active) {
            moons = null; moonScroll = 0f; selectedMoon = null
            markers = null; selectedMarker = null
            collection = null; selectedCollection = null; collectionScroll = 0f
        }
        if (ownerChanged || mapChanged || !next.active) {
            // Retain no bitmap across a scene/save identity boundary. Late work is key-checked.
            bitmap = null; bitmapKey = null; requestedKey = null; retryAt = 0
            viewport.reset()
        }
        val changed = next != state
        state = next
        onAppearance(next.outfit, next.hat, next.active)
        if (next.active) requestNames()
        requestMap()
        if (changed) postInvalidateOnAnimation()
    }

    private fun requestMap() {
        val map = state.map ?: return
        if (!state.active || assetBusy || page != 0 || SystemClock.uptimeMillis() < retryAt) return
        val key = "${state.generation}:${map.texture}"
        if (bitmapKey == key) return
        val handler = assetHandler ?: return
        val token = epoch
        assetBusy = true; requestedKey = key
        handler.post {
            val started = SystemClock.uptimeMillis()
            val decoded = runCatching {
                NativeLibrary.getDualScreenRomfsAsset("smo/maps/${map.texture}.png")?.let {
                    BitmapFactory.decodeByteArray(it, 0, it.size)
                }
            }.getOrNull()
            ui.post mapResult@{
                if (epoch != token) return@mapResult
                assetBusy = false
                val currentKey = state.map?.let { "${state.generation}:${it.texture}" }
                if (key == currentKey && requestedKey == key) {
                    if (decoded != null && decoded.width == map.width && decoded.height == map.height) {
                        bitmap = decoded; bitmapKey = key
                        Log.info("[SMO] Map ready ${map.texture} in ${SystemClock.uptimeMillis() - started}ms")
                    } else {
                        retryAt = SystemClock.uptimeMillis() + 5000
                        Log.warning("[SMO] Map resource unavailable: ${map.texture}")
                    }
                    postInvalidateOnAnimation()
                }
                // At most one decode in flight; a kingdom change replaces the desired key.
                if (currentKey != key) requestMap()
            }
        }
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        scale = min(w / 1240f, h / 1080f).coerceAtLeast(.001f)
        originX = (w - 1240f * scale) * .5f
        originY = (h - 1080f * scale) * .5f
    }

    private fun text(c: Canvas, value: String, x: Float, y: Float, size: Float,
                     color: Int = ink, heavy: Boolean = false, center: Boolean = false) {
        paint.color = color; paint.style = Paint.Style.FILL; paint.textSize = size
        paint.typeface = if (heavy) bold else regular
        paint.textAlign = if (center) Paint.Align.CENTER else Paint.Align.LEFT
        c.drawText(value, x, y, paint)
    }
    private fun rect(c: Canvas, l: Float, t: Float, r: Float, b: Float, color: Int) {
        paint.color = color; paint.style = Paint.Style.FILL; c.drawRect(l, t, r, b, paint)
    }
    private fun worldKey(): String = state.map?.texture?.substringBefore("World")
        ?: state.stage.substringBefore("World").takeIf { "World" in state.stage }.orEmpty()
    private fun kingdom(): String = names["WorldName_${worldKey()}"] ?: "Current kingdom"

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        // One opaque redraw and one bitmap transform; no persistent map layers/trails.
        canvas.drawColor(red)
        canvas.save(); canvas.translate(originX, originY); canvas.scale(scale, scale)
        panel(canvas, RectF(20f, 20f, 830f, 958f))
        panel(canvas, RectF(850f, 20f, 1220f, 958f))
        // Never substitute the reference image's objective for unconnected story data.
        text(canvas, "CURRENT KINGDOM", 56f, 56f, 21f, gold, true)
        rect(canvas, 56f, 66f, 718f, 68f, gold)
        fitted(canvas, if (state.active) kingdom() else "Waiting for Mario", 56f, 106f, 668f, 35f)
        text(canvas, if (state.active) names["RegionName_${worldKey()}"].orEmpty() else when (state.status) {
            2 -> "Odyssey 1.0.0 is required"
            5 -> "Changing scene…"
            else -> "Waiting for gameplay"
        }, 56f, 143f, 23f, muted)
        drawPlayerCard(canvas)
        paint.style = Paint.Style.STROKE; paint.strokeWidth = 6f; paint.color = gold
        canvas.drawRect(33f, 177f, 815f, 943f, paint)
        paint.style = Paint.Style.FILL
        val elapsed = SystemClock.uptimeMillis() - pageChangedAt
        canvas.save(); canvas.clipRect(36f, 180f, 812f, 940f)
        if (elapsed in 0..160) canvas.translate(0f, 12f * (1f - elapsed / 160f))
        when (page) {
            0 -> drawMap(canvas)
            1 -> drawMoons(canvas)
            2 -> drawCollections(canvas)
            3 -> drawAppearance(canvas)
        }
        canvas.restore()
        pages.forEachIndexed { index, name ->
            val left = 24f + index * 304f
            rect(canvas, left + 3f, 990f, left + 287f, 1064f, Color.rgb(145, 20, 27))
            rect(canvas, left, 986f, left + 284f, 1060f, if (index == page) red else paper)
            paint.style = Paint.Style.STROKE; paint.strokeWidth = 3f
            paint.color = if (index == page) Color.rgb(136, 26, 28) else gold
            canvas.drawRect(left, 986f, left + 284f, 1060f, paint); paint.style = Paint.Style.FILL
            text(canvas, name, left + 142f, 1035f, 28f,
                if (index == page) Color.WHITE else gold, true, true)
        }
        actionMessage?.let { message ->
            // Draw inside our existing Presentation. Android Toast windows are
            // a different type and crash when given this WindowContext.
            rect(canvas, 48f, 866f, 800f, 928f, ink)
            fitted(canvas, message, 64f, 906f, 720f, 24f, Color.WHITE)
        }
        canvas.restore()
        if (elapsed in 0..160) postInvalidateOnAnimation()
    }

    private fun panel(c: Canvas, bounds: RectF) {
        paint.color = Color.argb(65, 60, 12, 6)
        c.drawRoundRect(RectF(bounds.left + 3, bounds.top + 5, bounds.right + 3, bounds.bottom + 5), 16f, 16f, paint)
        paint.color = paper; c.drawRoundRect(bounds, 14f, 14f, paint)
    }

    private fun drawPlayerCard(c: Canvas) {
        paint.style = Paint.Style.STROKE; paint.strokeWidth = 2f; paint.color = gold
        c.drawRect(864f, 36f, 1206f, 944f, paint); paint.style = Paint.Style.FILL
        rect(c, 868f, 40f, 1202f, 550f, Color.rgb(247, 231, 191))
        text(c, if (state.active) "LIFE  ${state.health} / ${state.maxHealth}" else "NO ACTIVE SAVE",
            1035f, 577f, 21f, red, true, true)
        fun entry(label: String, value: String, y: Float) {
            text(c, label, 882f, y, 20f, gold, true)
            fitted(c, value, 882f, y + 35f, 310f, 31f)
            rect(c, 882f, y + 45f, 1192f, y + 46f, Color.rgb(213, 207, 184))
        }
        entry("CURRENT OUTFIT", outfitNames[state.outfit] ?: state.outfit.ifEmpty { "—" }, 611f)
        entry("CURRENT HAT", hatNames[state.hat] ?: state.hat.ifEmpty { "—" }, 686f)
        entry("GOLD COINS", if (state.active) "%,d".format(state.coins) else "—", 761f)
        entry("REGIONAL COINS", if (state.active) state.regionalCoins.toString() else "—", 836f)
        fitted(c, kingdom(), 882f, 926f, 310f, 27f, gold)
    }

    private fun drawCollections(c: Canvas) {
        drawCollectionGrid(c, false)
    }

    private fun drawMap(c: Canvas) {
        rect(c, mapBounds.left, mapBounds.top, mapBounds.right, mapBounds.bottom, Color.rgb(230, 229, 215))
        c.save(); c.clipRect(mapBounds); c.translate(mapBounds.left, mapBounds.top)
        val image = bitmap
        if (image != null && state.map != null && state.active) {
            paint.color = Color.WHITE; paint.alpha = 255
            c.drawBitmap(image, null, RectF(viewport.left, viewport.top,
                viewport.left + viewport.extent, viewport.top + viewport.extent), paint)
            markers?.entries?.forEach { marker ->
                state.map?.project(marker.position)?.let { (u, v) ->
                    if (u in 0f..1f && v in 0f..1f) {
                        val (x, y) = viewport.point(u, v)
                        paint.color = Color.WHITE; c.drawCircle(x, y, 20f, paint)
                        paint.color = if (marker.id == selectedMarker) red else ink
                        c.drawCircle(x, y, 16f, paint)
                        // Readable map symbols; selected marker uses the game's real name.
                        text(c, when (marker.kind) { 0 -> "⚑"; 1 -> "O"; 2 -> "C"; else -> "★" },
                            x, y + 8f, 23f, Color.WHITE, true, true)
                    }
                }
            }
            state.map?.playerUv()?.let { (u, v) ->
                if (u in 0f..1f && v in 0f..1f) {
                    val (x, y) = viewport.point(u, v)
                    paint.color = Color.WHITE; c.drawCircle(x, y, 23f, paint)
                    paint.color = red; c.drawCircle(x, y, 18f, paint)
                    text(c, "M", x, y + 10f, 27f, Color.WHITE, true, true)
                }
            }
        } else {
            text(c, if (state.map != null) "Loading kingdom map…" else "Map unavailable in this scene",
                mapBounds.width() / 2, 345f, 32f, muted, true, true)
            text(c, "No undiscovered markers are displayed.", mapBounds.width() / 2,
                389f, 23f, muted, center = true)
        }
        c.restore()
        markers?.entries?.firstOrNull { it.id == selectedMarker }?.let { marker ->
            rect(c, mapBounds.left + 16, mapBounds.top + 16, mapBounds.right - 16, mapBounds.top + 105, Color.WHITE)
            fitted(c, stageMessages[marker.stage]?.get(marker.messageKey) ?: marker.fallbackName,
                mapBounds.left + 38, mapBounds.top + 56, mapBounds.width() - 76, 29f)
            text(c, "${marker.fallbackName.uppercase()}  ·  TAP MAP TO CLOSE", mapBounds.left + 38,
                mapBounds.top + 87, 18f, muted, true)
        }
        if (state.map != null) {
            rect(c, recenterBounds.left, recenterBounds.top, recenterBounds.right, recenterBounds.bottom, Color.WHITE)
            text(c, "RECENTER", recenterBounds.centerX(), recenterBounds.centerY() + 10, 25f, gold, true, true)
        }
    }

    private fun drawKingdom(c: Canvas) {
        text(c, "A JOURNEY THROUGH", 60f, 279f, 23f, red, true)
        text(c, kingdom(), 60f, 350f, 58f, ink, true)
        rect(c, 60f, 382f, 1180f, 386f, ink)
        text(c, names["RegionName_${worldKey()}"].orEmpty(), 60f, 448f, 35f, ink, true)
        text(c, "Coins on hand", 60f, 542f, 29f)
        text(c, if (state.active) state.coins.toString() else "—", 890f, 542f, 38f, red, true)
        text(c, "Regional coins on hand", 60f, 612f, 29f)
        text(c, if (state.active) state.regionalCoins.toString() else "—", 890f, 612f, 38f, red, true)
        text(c, "Kingdom Power Moons", 60f, 682f, 29f)
        text(c, state.worldMoons.takeIf { state.active && it >= 0 }?.toString() ?: "—",
            890f, 682f, 38f, red, true)
        text(c, "Story objectives and completion are still being connected.", 60f, 820f, 26f, muted)
    }

    private fun clampMoonScroll() {
        moonScroll = moonScroll.coerceIn(0f,
            ((moons?.entries?.size ?: 0) * moonRowHeight - moonBounds.height()).coerceAtLeast(0f))
    }

    private fun requestMoonNames() {
        val first = (moonScroll / moonRowHeight).toInt()
        val stages = when (page) {
            0 -> markers?.entries.orEmpty().map { it.stage }
            1 -> moons?.entries.orEmpty().drop(first).take(8).filter { it.known }.map { it.stage }
            else -> emptyList()
        }.filter { it.isNotEmpty() }.distinct()
        val token = epoch
        for (stage in stages) {
            if (stage in stageMessages || stage in stageRequests ||
                SystemClock.uptimeMillis() < (stageRetry[stage] ?: 0)) continue
            val handler = assetHandler ?: return
            stageRequests.add(stage)
            handler.post {
                val parsed = runCatching {
                    NativeLibrary.getDualScreenRomfsAsset("smo/stage/$stage.msbt")
                        ?.let(SmoMessages::decode).orEmpty()
                }.getOrDefault(emptyMap())
                ui.post stageResult@{
                    if (epoch != token) return@stageResult
                    stageRequests.remove(stage)
                    if (parsed.isNotEmpty()) {
                        if (stageMessages.size >= 32) stageMessages.remove(stageMessages.keys.first())
                        stageMessages[stage] = parsed
                    } else stageRetry[stage] = SystemClock.uptimeMillis() + 10000
                    postInvalidateOnAnimation()
                }
            }
        }
    }

    private fun moonName(moon: SmoMoon): String = if (!moon.known) "???"
        else stageMessages[moon.stage]?.get(moon.messageKey) ?: "Power Moon ${moon.number}"

    private fun fitted(c: Canvas, value: String, x: Float, y: Float, maxWidth: Float,
                       size: Float, color: Int = ink) {
        paint.typeface = bold; paint.textSize = size
        var label = value.replace('\n', ' ')
        if (paint.measureText(label) > maxWidth) {
            while (label.isNotEmpty() && paint.measureText("$label…") > maxWidth) label = label.dropLast(1)
            label += "…"
        }
        text(c, label, x, y, size, color, true)
    }

    private fun drawMoons(c: Canvas) {
        rect(c, 36f, 180f, 812f, 300f, Color.rgb(233, 222, 184))
        text(c, "POWER MOONS", 62f, 223f, 32f, ink, true)
        val collection = moons
        if (collection == null) {
            text(c, "Reading game-owned Moon data…", 424f, 575f, 28f, muted, center = true)
            return
        }
        val collected = collection.entries.count { it.collected }
        text(c, "$collected / ${collection.entries.size} Collected", 650f, 223f, 23f, ink, false, true)
        val track = RectF(64f, 247f, 782f, 273f)
        paint.color = Color.rgb(147, 146, 132); c.drawRoundRect(track, 13f, 13f, paint)
        val fraction = collected.toFloat() / collection.entries.size.coerceAtLeast(1)
        if (fraction > 0f) {
            paint.color = Color.rgb(249, 210, 32)
            c.drawRoundRect(RectF(track.left, track.top, track.left + track.width() * fraction, track.bottom), 13f, 13f, paint)
        }
        c.save(); c.clipRect(moonBounds)
        val first = (moonScroll / moonRowHeight).toInt()
        for (index in first until min(first + 8, collection.entries.size)) {
            val moon = collection.entries[index]
            val top = moonBounds.top + index * moonRowHeight - moonScroll
            paint.style = Paint.Style.STROKE; paint.strokeWidth = 3f
            paint.color = if (moon.collected) gold else Color.rgb(190, 185, 169)
            c.drawRoundRect(RectF(67f, top + 38f, 99f, top + 70f), 3f, 3f, paint)
            if (moon.collected) {
                paint.strokeWidth = 5f
                val check = Path().apply { moveTo(72f, top + 48f); lineTo(83f, top + 60f); lineTo(104f, top + 32f) }
                c.drawPath(check, paint)
            }
            paint.style = Paint.Style.FILL
            drawMoonIcon(c, 144f, top + 54f, moon.collected)
            fitted(c, "%02d: %s".format(moon.number, moonName(moon)), 194f, top + 48f, 569f, 31f)
            text(c, if (moon.collected) "Collected" else if (moon.known) "Discovered" else "Not discovered",
                194f, top + 83f, 24f, muted)
            rect(c, 64f, top + 110f, 772f, top + 111f, Color.rgb(207, 204, 188))
        }
        c.restore()
        val totalHeight = collection.entries.size * moonRowHeight
        if (totalHeight > moonBounds.height()) {
            val thumb = (moonBounds.height() * moonBounds.height() / totalHeight).coerceAtLeast(40f)
            val y = moonBounds.top + moonScroll / (totalHeight - moonBounds.height()) * (moonBounds.height() - thumb)
            paint.color = gold; c.drawRoundRect(RectF(784f, y, 795f, y + thumb), 5f, 5f, paint)
        }
        val selected = collection.entries.firstOrNull { it.id == selectedMoon && it.known }
        if (selected != null) {
            rect(c, 36f, 180f, 812f, 940f, Color.argb(170, 20, 18, 15))
            rect(c, 70f, 352f, 778f, 798f, paper)
            rect(c, 70f, 352f, 778f, 363f, gold)
            text(c, "POWER MOON ${selected.number}", 114f, 420f, 26f, red, true)
            fitted(c, moonName(selected), 114f, 491f, 620f, 34f)
            text(c, if (selected.collected) "Collected" else "Name discovered", 114f, 565f, 28f, ink, true)
            if (selected.moonRock) text(c, "Moon Rock", 114f, 622f, 25f, muted)
            text(c, "TAP TO CLOSE", 424f, 754f, 25f, gold, true, true)
        }
    }

    private fun drawMoonIcon(c: Canvas, x: Float, y: Float, collected: Boolean) {
        val shape = Path().apply {
            moveTo(x - 14, y - 31)
            cubicTo(x + 32, y - 44, x + 46, y + 24, x + 6, y + 32)
            cubicTo(x - 15, y + 40, x - 33, y + 14, x - 19, y + 11)
            cubicTo(x + 4, y + 34, x + 18, y - 13, x - 9, y - 18)
            cubicTo(x - 19, y - 15, x - 22, y - 27, x - 14, y - 31)
            close()
        }
        paint.color = if (collected) Color.rgb(252, 216, 44) else Color.rgb(170, 172, 167)
        c.drawPath(shape, paint)
        paint.style = Paint.Style.STROKE; paint.strokeWidth = 2f; paint.color = ink
        c.drawPath(shape, paint); paint.style = Paint.Style.FILL
        c.drawOval(RectF(x + 12, y - 10, x + 16, y + 1), paint)
        c.drawOval(RectF(x + 21, y - 9, x + 25, y + 2), paint)
    }

    private fun drawAppearance(c: Canvas) {
        drawCollectionGrid(c, true)
    }

    private fun collectionName(item: SmoCollectionItem): String = if (!item.owned) "????"
        else collectionNames[collectionKind]?.get(item.messageKey)
            ?: "${listOf("Outfit", "Hat", "Souvenir", "Sticker", "Capture", "Music")[collectionKind]} ${item.id + 1}"

    private fun iconPath(item: SmoCollectionItem): String? {
        if (!item.owned) return null
        return when (collectionKind) {
            0 -> "smo/icons/clothes/${item.name}.png"
            1 -> "smo/icons/clothes/${item.name}Cap.png"
            2 -> "smo/icons/souvenirs/${item.name}.png"
            3 -> "smo/icons/stickers/${item.name}.png"
            4 -> "smo/icons/captures/${item.name}.png"
            else -> null
        }
    }

    private fun requestCollectionIcons() {
        if (page < 2) return
        val handler = assetHandler ?: return
        val first = (collectionScroll / SmoCollectionGrid.PITCH_Y).toInt() * 3
        val visible = collection?.entries.orEmpty().drop(first).take(9)
        val token = epoch
        for (item in visible) {
            val path = iconPath(item) ?: continue
            if (path in collectionIcons || path in iconRequests || SystemClock.uptimeMillis() < (iconRetry[path] ?: 0)) continue
            // Bound queued work while fast scrolling; never decode in onDraw.
            if (iconRequests.size >= 12) break
            iconRequests.add(path)
            handler.post {
                val image = runCatching {
                    NativeLibrary.getDualScreenRomfsAsset(path)?.let { bytes ->
                        BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
                    }
                }.getOrNull()
                ui.post {
                    if (epoch == token) {
                        iconRequests.remove(path)
                        if (image != null) {
                            if (collectionIcons.size >= 48) collectionIcons.remove(collectionIcons.keys.first())
                            collectionIcons[path] = image
                        } else {
                            iconRetry[path] = SystemClock.uptimeMillis() + 15000
                            Log.warning("[SMO] Collection icon unavailable: $path")
                        }
                        requestCollectionIcons(); postInvalidateOnAnimation()
                    }
                }
            }
        }
    }

    private fun drawCollectionGrid(c: Canvas, wardrobe: Boolean) {
        val packet = collection
        val entries = packet?.entries.orEmpty()
        val owned = entries.count { it.owned }
        rect(c, 36f, 180f, 812f, 296f, Color.rgb(233, 222, 184))
        text(c, if (wardrobe) "OUTFITS & HATS" else "COLLECTIONS", 62f, 222f, 32f, ink, true)
        text(c, if (packet == null) "—" else "$owned / ${entries.size} Collected", 649f, 222f, 22f, ink, center = true)
        paint.color = Color.rgb(147, 146, 132)
        c.drawRoundRect(RectF(64f, 246f, 782f, 272f), 13f, 13f, paint)
        if (owned > 0) {
            paint.color = Color.rgb(249, 210, 32)
            c.drawRoundRect(RectF(64f, 246f, 64f + 718f * owned / entries.size, 272f), 13f, 13f, paint)
        }
        val tabs = if (wardrobe) listOf("OUTFITS", "HATS") else listOf("SOUVENIRS", "CAPTURES", "MUSIC", "STICKERS")
        val selected = if (wardrobe) wardrobeKind else collectionTab
        tabs.forEachIndexed { i, label ->
            val left = 48f + i * 752f / tabs.size
            text(c, label, left + 376f / tabs.size, 330f, 22f, if (i == selected) ink else gold, true, true)
            if (i == selected) rect(c, left + 12, 341f, left + 752f / tabs.size - 12, 345f, gold)
        }
        if (packet == null) {
            text(c, "Reading your collection…", 424f, 563f, 27f, muted, center = true); return
        }
        c.save(); c.clipRect(48f, SmoCollectionGrid.TOP, 800f, SmoCollectionGrid.BOTTOM)
        val first = (collectionScroll / SmoCollectionGrid.PITCH_Y).toInt() * 3
        for (index in first until min(first + 9, entries.size)) {
            val item = entries[index]
            val left = SmoCollectionGrid.LEFT + index % 3 * SmoCollectionGrid.PITCH_X
            val top = SmoCollectionGrid.TOP + index / 3 * SmoCollectionGrid.PITCH_Y - collectionScroll
            val bounds = RectF(left, top, left + SmoCollectionGrid.WIDTH, top + SmoCollectionGrid.HEIGHT)
            paint.color = Color.argb(38, 60, 49, 27); c.drawRoundRect(RectF(bounds).apply { offset(0f, 4f) }, 15f, 15f, paint)
            paint.color = Color.rgb(251, 238, 202); c.drawRoundRect(bounds, 15f, 15f, paint)
            val equipped = item.owned && ((collectionKind == 0 && item.name == state.outfit) ||
                (collectionKind == 1 && item.name == state.hat))
            if (equipped) {
                paint.color = gold; paint.style = Paint.Style.STROKE; paint.strokeWidth = 4f
                c.drawRoundRect(bounds, 15f, 15f, paint); paint.style = Paint.Style.FILL
            }
            val image = iconPath(item)?.let { collectionIcons[it] }
            if (image != null) {
                val extent = min(186f / image.width, 172f / image.height)
                val w = image.width * extent; val h = image.height * extent
                paint.color = Color.WHITE
                c.drawBitmap(image, null, RectF(left + 114f - w/2, top + 102f - h/2,
                    left + 114f + w/2, top + 102f + h/2), paint)
            } else text(c, if (!item.owned) "?" else if (collectionKind == 5) "♪" else "…",
                left + 114f, top + 124f, 72f, if (item.owned) gold else ink, true, true)
            paint.color = if (item.owned) gold else Color.rgb(178, 168, 139)
            paint.style = Paint.Style.STROKE; paint.strokeWidth = 3f
            c.drawRoundRect(RectF(left+13, top+13, left+44, top+44), 3f, 3f, paint)
            if (item.owned) c.drawPath(Path().apply {
                moveTo(left+18, top+27); lineTo(left+27, top+37); lineTo(left+50, top+10)
            }, paint)
            paint.style = Paint.Style.FILL
            rect(c, left + 2, top + 195, left + 226, top + 197, Color.rgb(217, 204, 167))
            // Wrap the real localized name into two lines; never expose locked names/artwork.
            val words = collectionName(item).replace('\n', ' ').split(' ')
            paint.typeface = bold; paint.textSize = 26f
            var line = ""; val lines = ArrayList<String>()
            for (word in words) {
                if (line.isNotEmpty() && paint.measureText("$line $word") > 208f) { lines.add(line); line = word }
                else line = if (line.isEmpty()) word else "$line $word"
            }
            if (line.isNotEmpty()) lines.add(line)
            lines.take(2).forEachIndexed { i, value -> fitted(c, value, left+10, top+226+i*29, 208f, 26f) }
            if (equipped) text(c, "EQUIPPED", left+114, top+277, 16f, gold, true, true)
        }
        c.restore()
        val max = SmoCollectionGrid.maxScroll(entries.size)
        if (max > 0) {
            val height = SmoCollectionGrid.BOTTOM - SmoCollectionGrid.TOP
            val thumb = (height * height / (height + max)).coerceAtLeast(36f)
            val y = SmoCollectionGrid.TOP + collectionScroll / max * (height - thumb)
            paint.color = gold; c.drawRoundRect(RectF(787f, y, 797f, y+thumb), 5f, 5f, paint)
        }
        entries.firstOrNull { it.id == selectedCollection && it.owned }?.let { item ->
            rect(c, 36f, 180f, 812f, 940f, Color.argb(170, 20, 18, 15))
            paint.color = paper; c.drawRoundRect(RectF(80f, 352f, 770f, 790f), 18f, 18f, paint)
            fitted(c, collectionName(item), 110f, 420f, 630f, 32f)
            iconPath(item)?.let { collectionIcons[it] }?.let { image ->
                val s = min(240f/image.width, 210f/image.height)
                paint.color = Color.WHITE
                c.drawBitmap(image, null, RectF(425f-image.width*s/2, 545f-image.height*s/2,
                    425f+image.width*s/2, 545f+image.height*s/2), paint)
            }
            if (wardrobe) {
                val equipped = item.name == if (collectionKind == 0) state.outfit else state.hat
                text(c, "Stand still · Brief reload keeps your position", 425f, 674f, 23f, muted, center = true)
                rect(c, equipBounds.left, equipBounds.top, equipBounds.right, equipBounds.bottom,
                    if (canEquip(item)) red else muted)
                text(c, if (outfitBusy) "CHANGING…" else if (equipped) "EQUIPPED" else "EQUIP",
                    425f, 752f, 27f, Color.WHITE, true, true)
            } else {
                text(c, "Collected in your current save", 425f, 695f, 25f, muted, center = true)
                text(c, "TAP TO CLOSE", 425f, 753f, 24f, gold, true, true)
            }
        }
    }

    private fun canEquip(item: SmoCollectionItem): Boolean =
        SmoWardrobe.canEquip(state, collectionKind, collection?.generation, item, outfitBusy)

    private fun finishOutfit(result: Int) {
        outfitBusy = false
        if (result == 1) selectedCollection = null
        val message = when (result) {
            1 -> "Outfit changed"
            -2, -12 -> "Wait for gameplay, stand still, then try again"
            -15 -> "Mario's pose is unavailable; try again when standing safely"
            -18 -> "Appearance changed, but reload verification failed; check logs"
            -3 -> "An outfit change is already in progress"
            -11 -> "This item is not owned"
            -14 -> "Stand still and wait for Mario to finish his current action"
            else -> "Outfit change unavailable; wait for gameplay and try again"
        }
        Log.info("[SMO] Wardrobe result=$result")
        actionMessage = message
        ui.removeCallbacks(clearActionMessage)
        ui.postDelayed(clearActionMessage, 5000)
        postInvalidateOnAnimation()
    }

    private fun drawPending(c: Canvas) {
        text(c, pages[page], 60f, 290f, 44f, ink, true)
        rect(c, 60f, 322f, 1180f, 326f, red)
        val moons = if (page == 1) state.worldMoons else state.totalMoons
        text(c, "${moons.takeIf { state.active && it >= 0 }?.toString() ?: "—"} POWER MOONS",
            60f, 600f, 49f, red, true)
        text(c, "Native ${if (page == 1) "Moon visibility" else "travel"} integration is in progress.",
            60f, 420f, 30f)
        text(c, "Hidden progress is not exposed and game state is not modified.", 60f, 478f, 25f, muted)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val x = (event.x - originX) / scale
        val y = (event.y - originY) / scale
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                downX = x; downY = y; lastX = x; lastY = y
                dragged = false; multiTouch = false
                mapGesture = page == 0 && mapBounds.contains(x, y) && !recenterBounds.contains(x, y)
                parent?.requestDisallowInterceptTouchEvent(true)
            }
            MotionEvent.ACTION_POINTER_DOWN -> multiTouch = true
        }
        pinch.onTouchEvent(event)
        when (event.actionMasked) {
            MotionEvent.ACTION_MOVE -> {
                if (hypot(x - downX, y - downY) > 12f) dragged = true
                if (mapGesture && dragged && !multiTouch && !pinch.isInProgress) {
                    viewport.drag(x - lastX, y - lastY); postInvalidateOnAnimation()
                }
                if (page == 1 && moonBounds.contains(downX, downY) && dragged && !multiTouch && selectedMoon == null) {
                    moonScroll -= y - lastY; clampMoonScroll(); requestMoonNames()
                    postInvalidateOnAnimation()
                }
                if (page >= 2 && downX in 48f..800f && downY in SmoCollectionGrid.TOP..SmoCollectionGrid.BOTTOM &&
                    dragged && !multiTouch && selectedCollection == null) {
                    collectionScroll = (collectionScroll - y + lastY).coerceIn(0f,
                        SmoCollectionGrid.maxScroll(collection?.entries?.size ?: 0))
                    requestCollectionIcons(); postInvalidateOnAnimation()
                }
                lastX = x; lastY = y
            }
            MotionEvent.ACTION_UP -> {
                if (!dragged && !multiTouch && abs(x - downX) < 12 && abs(y - downY) < 12) {
                    val tab = SmoUiLayout.tabAt(x, y)
                    if (tab != null && tab == SmoUiLayout.tabAt(downX, downY)) {
                        val selected = tab
                        if (page != selected) {
                            page = selected; pageChangedAt = SystemClock.uptimeMillis()
                            selectedMoon = null
                            selectedMarker = null
                            collection = null; selectedCollection = null; collectionScroll = 0f
                            collectionKind = if (page == 3) wardrobeKind else listOf(2, 4, 5, 3)[collectionTab]
                            requestMoonNames()
                            requestMap(); postInvalidateOnAnimation()
                            Log.info("[SMO] Page ${pages[page]}")
                        }
                    } else if (page >= 2) {
                        if (selectedCollection != null) {
                            val item = collection?.entries?.firstOrNull { it.id == selectedCollection }
                            if (page == 3 && item != null && SmoWardrobe.equipTap(downX, downY, x, y)) {
                                if (canEquip(item)) {
                                    outfitBusy = true
                                    outfitRequests.set(OutfitRequest(collectionKind, item.name, state.generation))
                                }
                            } else selectedCollection = null
                        }
                        else if (x in 48f..800f && y in 296f..348f && downY in 296f..348f) {
                            val count = if (page == 3) 2 else 4
                            val index = ((x - 48f) * count / 752f).toInt().coerceIn(0, count-1)
                            if (page == 3) wardrobeKind = index else collectionTab = index
                            val nextKind = if (page == 3) index else listOf(2,4,5,3)[index]
                            if (nextKind != collectionKind) {
                                collectionKind = nextKind; collection = null; collectionScroll = 0f
                            }
                        } else {
                            val items = collection?.entries.orEmpty()
                            val index = SmoCollectionGrid.itemAt(x, y, collectionScroll, items.size)
                            if (index != null && index == SmoCollectionGrid.itemAt(downX, downY, collectionScroll, items.size))
                                selectedCollection = items[index].takeIf { it.owned }?.id
                        }
                        requestCollectionIcons(); postInvalidateOnAnimation()
                    } else if (page == 1) {
                        if (selectedMoon != null) selectedMoon = null
                        else if (moonBounds.contains(x, y) && moonBounds.contains(downX, downY)) {
                            val index = ((y - moonBounds.top + moonScroll) / moonRowHeight).toInt()
                            selectedMoon = moons?.entries?.getOrNull(index)?.takeIf { it.known }?.id
                        }
                        postInvalidateOnAnimation()
                    } else if (page == 0) {
                        val now = SystemClock.uptimeMillis()
                        val doubleTap = mapGesture && now - lastTapAt < 320 &&
                            hypot(x - lastTapX, y - lastTapY) < 48
                        if (doubleTap || (recenterBounds.contains(x, y) && recenterBounds.contains(downX, downY))) {
                            selectedMarker = null
                            state.map?.playerUv()?.let { viewport.recenter(it.first, it.second) }
                            postInvalidateOnAnimation(); lastTapAt = 0
                        } else {
                            if (mapGesture) {
                                selectedMarker = markers?.entries?.mapNotNull { marker ->
                                    state.map?.project(marker.position)?.let { (u, v) ->
                                        if (u !in 0f..1f || v !in 0f..1f) return@let null
                                        val (px, py) = viewport.point(u, v)
                                        marker.id to hypot(x - mapBounds.left - px, y - mapBounds.top - py)
                                    }
                                }?.minByOrNull { it.second }?.takeIf { it.second <= 32f }?.first
                                postInvalidateOnAnimation()
                            }
                            lastTapAt = now; lastTapX = x; lastTapY = y
                        }
                    }
                    performClick()
                }
                mapGesture = false
                parent?.requestDisallowInterceptTouchEvent(false)
            }
            MotionEvent.ACTION_CANCEL -> {
                mapGesture = false; multiTouch = true; lastTapAt = 0
                parent?.requestDisallowInterceptTouchEvent(false)
            }
        }
        return true
    }
    override fun performClick(): Boolean { super.performClick(); return true }
}
