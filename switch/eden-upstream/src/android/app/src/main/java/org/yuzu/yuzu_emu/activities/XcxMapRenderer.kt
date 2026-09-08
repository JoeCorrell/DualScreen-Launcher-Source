package org.yuzu.yuzu_emu.activities

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Rect
import android.util.LruCache
import android.view.MotionEvent
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.*
import org.yuzu.yuzu_emu.NativeLibrary

/** Draws the game's sparse tile atlas without allocating a 6K full-map bitmap. */
internal class XcxMapRenderer(private val invalidate: () -> Unit,
                              private val dispatch: (() -> Unit) -> Unit,
                              private val onRegion: (Long) -> Unit,
                              private val onTravel: (Int) -> Unit,
                              onProbe: (Int) -> Unit,
                              private val onMission: (Int) -> Unit) {
    private val probePicker = XcxProbePicker(onProbe)
    private val probeHits = ArrayList<Pair<RectF, XcxMapSegments.Cell>>()
    private val missionHits = ArrayList<Pair<RectF, Int>>()
    private val detailHits = ArrayList<Pair<RectF, XcxMapSegments.Cell>>()
    private val segmentDetails = XcxSegmentDetails()
    data class Site(val id: Int, val name: String, val x: Float, val z: Float,
                    val formation: Int, val probe: Boolean)
    var sites: List<Site> = emptyList()
    var sitesZone: Long = 0
    private var selectedSite: Site? = null
    private val siteHits = ArrayList<Pair<RectF, Site>>()
    private val travelButton = RectF(958f, 840f, 1174f, 900f)
    private data class Tile(val name: String, val sample: Int)
    private val worker = Executors.newSingleThreadExecutor { r ->
        Thread(r, "XCX-Map-Assets").apply { isDaemon = true }
    }
    private val tiles = object : LruCache<Tile, Bitmap>(24 * 1024 * 1024) {
        override fun sizeOf(key: Tile, value: Bitmap) = value.allocationByteCount
    }
    private val loading = AtomicBoolean(false)
    @Volatile private var wanted = emptyList<Tile>()
    @Volatile private var closed = false
    private val unavailable = HashMap<Tile, Long>() // Worker only; failed reads retry.
    private var generation = 0
    private var loadingLayer = 0
    private var retryAt = 0L
    private var atlas: XcxMapAtlas? = null
    private var segmentCatalog: XcxMapSegments? = null
    private var segmentSheet: Bitmap? = null
    private var cells = emptyList<XcxMapSegments.Cell>()
    private var cellLayer = 0
    private var initialCenterDone = false
    private var pendingSelf = false
    private var pendingRegionCenter = 0L
    private var pendingProbe = 0
    private var pendingProbeZone = 0L
    private var segmentPollAt = 0L
    private var segmentPollRunning = false
    private var flags = emptySet<Int>()
    private var flagPollAt = 0L
    private var flagPollRunning = false
    private var centerX = 0f
    private var centerY = 0f
    private var zoom = 1f // Screen pixels per native atlas pixel.
    private var selectedRegion = 0L
    private var pendingZone = 0L
    private var playerX = 0f
    private var playerZ = 0f
    private var playerYaw = 0f
    private var playerValid = false
    private var zone = 0L
    private var pointerX = 0f
    private var pointerY = 0f
    private var downX = 0f
    private var downY = 0f
    private var distance = 0f
    private var dragged = false
    private val viewport = RectF(48f, 112f, 1192f, 908f)
    private val worldButton = RectF(64f, 128f, 218f, 184f)
    private val selfButton = RectF(754f, 128f, 958f, 184f)
    private val minusButton = RectF(978f, 128f, 1066f, 184f)
    private val plusButton = RectF(1080f, 128f, 1174f, 184f)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val regionHits = ArrayList<Pair<RectF, XcxMapAtlas.Region>>()

    fun close() {
        closed = true
        worker.shutdownNow()
        tiles.evictAll() // Never recycle bitmaps held by RenderThread display lists.
    }

    fun update(zone: Long, x: Float, z: Float, yaw: Float, valid: Boolean,
               credits: Long?, actionPending: Boolean) {
        probePicker.credits = credits
        probePicker.updating = actionPending
        this.zone = zone
        playerX = x; playerZ = z; playerYaw = yaw; playerValid = valid
        if (atlas == null && loadingLayer == 0 && android.os.SystemClock.uptimeMillis() >= retryAt) {
            requestLayer(1)
        }
        val current = atlas ?: return
        if (!initialCenterDone && valid && zone != 0L) {
            initialCenterDone = true
            findSelf()
        }
        val now = android.os.SystemClock.uptimeMillis()
        pollSegments(now)
        if (!flagPollRunning && now >= flagPollAt) {
            val ids = current.variants.map { it.flag }.filter { it != 0 }.distinct()
            flagPollAt = now + 1000
            if (ids.isNotEmpty()) {
                flagPollRunning = true
                worker.execute {
                    val values = runCatching {
                        NativeLibrary.readXcxGameFlags(1, ids.toIntArray())
                    }.getOrNull()
                    dispatch {
                        flagPollRunning = false
                        if (!closed && atlas === current && values?.size == ids.size) {
                            flags = ids.filterIndexed { i, _ -> values[i].toInt() != 0 }.toSet()
                            invalidate()
                        }
                    }
                }
            }
        }
    }

    private fun pollSegments(now: Long) {
        val requestedMap = atlas ?: return
        if (closed || segmentPollRunning || now < segmentPollAt) return
        val existingCatalog = segmentCatalog
        val existingSheet = segmentSheet
        val needsProbes = probePicker.items.isEmpty()
        val needsInventory = probePicker.site != 0
        segmentPollRunning = true
        segmentPollAt = now + 500
        worker.execute {
            var catalog = existingCatalog
            var sheet = existingSheet
            val probeDefinitions = if (needsProbes) runCatching {
                NativeLibrary.getDualScreenRomfsAsset("xcx/probes/data.bin")?.let(XcxProbes::parse)
            }.getOrNull() else null
            val probeInventory = if (needsInventory) runCatching {
                NativeLibrary.readXcxProbeInventory()?.let(XcxProbes::inventory)
            }.getOrNull() else null
            val result = runCatching {
                if (catalog == null) catalog = NativeLibrary.getDualScreenRomfsAsset(
                    "xcx/maps/segments.bin")?.let(XcxMapSegments::parse)
                if (sheet == null) sheet = NativeLibrary.getDualScreenRomfsAsset(
                    "xcx/maps/icon_segment.png")?.let { BitmapFactory.decodeByteArray(it, 0, it.size) }
                val definition = catalog ?: return@runCatching null
                val zones = requestedMap.regions.map { it.zone }.toSet()
                val records = definition.fields.filter { it.zone in zones && it.id in 1..19 }
                    .flatMap { field ->
                        NativeLibrary.readXcxMapSegments(field.id)?.let {
                            definition.cells(field, it)
                        }.orEmpty()
                    }
                if (records.isEmpty()) return@runCatching records
                val openIds = records.map { it.openFlag }.distinct()
                val stateIds = records.map { it.stateFlag }.distinct()
                val open = NativeLibrary.readXcxGameFlags(1, openIds.toIntArray())
                    ?.takeIf { it.size == openIds.size } ?: return@runCatching null
                val states = NativeLibrary.readXcxGameFlags(2, stateIds.toIntArray())
                    ?.takeIf { it.size == stateIds.size } ?: return@runCatching null
                val openValues = openIds.zip(open.toList()).toMap()
                val stateValues = stateIds.zip(states.toList()).toMap()
                records.map { it.copy(open = openValues[it.openFlag]?.toInt() != 0,
                    complete = it.kind == 2 && stateValues[it.stateFlag]?.toInt() == 3) }
            }.getOrNull()
            dispatch {
                segmentPollRunning = false
                if (!closed) {
                    probeDefinitions?.let { probePicker.items = it.items }
                    probePicker.stock = probeInventory
                    segmentCatalog = catalog
                    segmentSheet = sheet
                    if (atlas === requestedMap) {
                        cells = result.orEmpty()
                        segmentDetails.refresh(cells)
                        if (probePicker.site != 0) {
                            val site = cells.firstOrNull { it.kind == 1 && it.probeId == probePicker.site }
                            if (site == null || !site.open || !site.probeVisible) probePicker.dismiss()
                            else probePicker.current = site.probeItem
                        }
                        cellLayer = requestedMap.layer
                        if (pendingRegionCenter != 0L) centerRegion(pendingRegionCenter)
                        openPendingProbe()
                        invalidate()
                    }
                }
            }
        }
    }

    private fun drawSegments(canvas: Canvas, map: XcxMapAtlas) {
        probeHits.clear()
        missionHits.clear()
        detailHits.clear()
        if (cellLayer != map.layer) return
        // 0x7B9250 transforms the field anchor, then adds 0x5632A4's 105px grid.
        // Applying the world-spacing conversion again introduces cumulative rounding drift.
        val step = 105f * zoom
        for (cell in cells) {
            val anchor = map.position(cell.field.x, cell.field.z)
            val p = screen(anchor.first + cell.column * 78.75f,
                anchor.second + (cell.row + if (cell.column and 1 != 0) .5f else 0f) * 105f)
            if (!RectF.intersects(viewport, RectF(p.first - step, p.second - step,
                    p.first + step, p.second + step))) continue
            if (cell.kind == 1 && cell.open && cell.probeVisible && cell.probeId in 1..143 && zoom >= .3f) {
                val hit = max(20f, step * .35f)
                probeHits += RectF(p.first - hit, p.second - hit, p.first + hit, p.second + hit) to cell
            }
            if (cell.kind == 2 && cell.open && cell.questId != 0 && zoom >= .3f) {
                val hit = max(20f, step * .35f)
                missionHits += RectF(p.first - hit, p.second - hit,
                    p.first + hit, p.second + hit) to cell.questId
            }
            if (cell.kind == 2 && cell.open && cell.questId == 0 && zoom >= .3f &&
                segmentCatalog?.details?.containsKey(cell.id) == true) {
                val hit = max(20f, step * .35f)
                detailHits += RectF(p.first - hit, p.second - hit,
                    p.first + hit, p.second + hit) to cell
            }
            val outline = Path().apply {
                moveTo(p.first - step * .5f, p.second)
                lineTo(p.first - step * .25f, p.second - step * .5f)
                lineTo(p.first + step * .25f, p.second - step * .5f)
                lineTo(p.first + step * .5f, p.second)
                lineTo(p.first + step * .25f, p.second + step * .5f)
                lineTo(p.first - step * .25f, p.second + step * .5f)
                close()
            }
            paint.style = Paint.Style.STROKE
            paint.strokeWidth = max(1f, zoom)
            paint.color = Color.argb(180, 226, 239, 241)
            canvas.drawPath(outline, paint)
            paint.style = Paint.Style.FILL
            // These are literal sheet cells, independent of the layout's animation frame IDs.
            val sheet = segmentSheet
            fun sprite(index: Int, size: Float, offsetX: Float = 0f, offsetY: Float = 0f) {
                if (sheet == null || sheet.width != 480 || sheet.height != 288 || index !in 0..14) return
                val side = size * zoom
                paint.color = Color.WHITE
                canvas.drawBitmap(sheet, Rect(index % 5 * 96, index / 5 * 96,
                    (index % 5 + 1) * 96, (index / 5 + 1) * 96),
                    RectF(p.first + offsetX * zoom - side / 2, p.second + offsetY * zoom - side / 2,
                        p.first + offsetX * zoom + side / 2, p.second + offsetY * zoom + side / 2), paint)
            }
            if (cell.complete || cell.kind == 1 && cell.probeVisible && cell.probeInstalled) sprite(11, 96f)
            if (!cell.open) sprite(10, 48f)
            else when (cell.kind) {
                1 -> if (cell.probeVisible) sprite(if (cell.probeInstalled) 8 else 9, 48f)
                2 -> segmentCatalog?.icons?.get(cell.id)?.let { sprite(it, 48f) }
                3 -> sprite(7, 48f)
            }
            if (cell.relatedVisible) sprite(13, 32f, 18f, 24f)
        }
    }

    private fun requestLayer(layer: Int) {
        if (closed || loadingLayer == layer) return
        loadingLayer = layer
        val request = ++generation
        worker.execute {
            val definition = runCatching {
                NativeLibrary.getDualScreenRomfsAsset("xcx/maps/atlas_$layer.bin")
                    ?.let(XcxMapAtlas::parse)
            }.getOrNull()
            dispatch {
                if (!closed && request == generation) {
                    loadingLayer = 0
                    if (definition != null) {
                        atlas = definition
                        flags = emptySet()
                        flagPollAt = 0
                        segmentPollAt = 0
                        selectedRegion = 0
                        fitAtlas(definition)
                        definition.regions.firstOrNull { it.zone == pendingZone }?.let(::focus)
                        pendingZone = 0
                        if (pendingSelf) {
                            pendingSelf = false
                            findSelf()
                        }
                    } else retryAt = android.os.SystemClock.uptimeMillis() + 5000
                    invalidate()
                }
            }
        }
    }

    private fun fitAtlas(map: XcxMapAtlas) {
        centerX = map.width * .5f; centerY = map.height * .5f
        zoom = min(viewport.width() / map.width, viewport.height() / map.height)
    }

    private fun focus(region: XcxMapAtlas.Region) {
        val map = atlas ?: return
        if (pendingProbeZone != region.zone) pendingProbe = 0
        zoom = 1f
        selectedRegion = region.zone
        pendingRegionCenter = region.zone
        centerRegion(region.zone)
        selectedSite = null
        onRegion(region.zone)
        invalidate()
    }

    private fun centerRegion(region: Long) {
        val map = atlas ?: return
        if (cellLayer != map.layer) return
        val positions = cells.filter { it.field.zone == region }.map { cell ->
            val anchor = map.position(cell.field.x, cell.field.z)
            Pair(anchor.first + cell.column * 78.75f,
                anchor.second + (cell.row + if (cell.column and 1 != 0) .5f else 0f) * 105f)
        }
        if (positions.isEmpty()) return
        centerX = (positions.minOf { it.first } + positions.maxOf { it.first }) / 2
        centerY = (positions.minOf { it.second } + positions.maxOf { it.second }) / 2
        pendingRegionCenter = 0
    }

    fun world(): Boolean {
        val current = atlas ?: return false
        pendingZone = 0
        pendingSelf = false
        pendingRegionCenter = 0
        pendingProbe = 0
        selectedSite = null
        if (current.layer == 2 && selectedRegion != 0L) {
            selectedRegion = 0; fitAtlas(current)
        } else if (current.layer == 2) requestLayer(1)
        else { selectedRegion = 0; fitAtlas(current) }
        invalidate()
        return true
    }

    fun back(): Boolean {
        if (segmentDetails.cell != null) { segmentDetails.dismiss(); invalidate(); return true }
        if (probePicker.site != 0) { probePicker.dismiss(); invalidate(); return true }
        val map = atlas ?: return false
        if (selectedRegion == 0L && map.layer == 1) return false
        return world()
    }

    fun activateCenter() {
        val x = viewport.centerX(); val y = viewport.centerY()
        if (segmentDetails.cell != null) { segmentDetails.dismiss(); invalidate(); return }
        if (probePicker.site != 0) { probePicker.activate(); return }
        probeHits.firstOrNull { it.first.contains(x, y) }?.second?.let {
            probePicker.open(it); invalidate(); return
        }
        missionHits.firstOrNull { it.first.contains(x, y) }?.second?.let { onMission(it); return }
        detailHits.firstOrNull { it.first.contains(x, y) }?.second?.let { openSegmentDetails(it); return }
        val site = siteHits.filter { it.first.contains(x, y) }.minByOrNull {
            hypot(it.first.centerX() - x, it.first.centerY() - y)
        }?.second
        if (site != null) {
            // A second confirmation travels only to the pin still under the crosshair.
            // Panning away must not send the player to a previously selected destination.
            if (selectedSite?.id == site.id && site.formation > 0) onTravel(site.formation)
            else { selectedSite = site; invalidate() }
            return
        }
        regionHits.firstOrNull { it.first.contains(x, y) }?.second?.let {
            if (it.zone == 1000L) requestLayer(2) else focus(it)
        }
    }

    fun findSelf() {
        pendingProbe = 0
        if (!playerValid) return
        val layer = if (zone in 1001L..1401L) 2 else 1
        val map = atlas
        if (map?.layer != layer) {
            pendingZone = zone
            pendingSelf = true
            requestLayer(layer)
            return
        }
        map.regions.firstOrNull { it.zone == zone }?.let(::focus)
        pendingRegionCenter = 0
        val p = map.position(playerX, playerZ)
        centerX = p.first; centerY = p.second
        zoom = 1f
        invalidate()
    }

    fun adjustZoom(factor: Float) {
        if (segmentDetails.cell != null) {
            segmentDetails.scroll(if (factor > 1f) 14 else -14); invalidate(); return
        }
        if (probePicker.site != 0) {
            probePicker.move(if (factor > 1f) 6 else -6); invalidate(); return
        }
        val map = atlas ?: return
        pendingProbe = 0
        val minimum = min(viewport.width() / map.width, viewport.height() / map.height)
        zoom = (zoom * factor).coerceIn(minimum, 3f)
        clamp(map)
        invalidate()
    }

    fun pan(dx: Float, dy: Float) {
        if (segmentDetails.cell != null) {
            segmentDetails.scroll(if (abs(dy) >= abs(dx)) if (dy > 0) 1 else -1
                else if (dx > 0) 14 else -14)
            invalidate(); return
        }
        if (probePicker.site != 0) {
            probePicker.move(if (abs(dy) >= abs(dx)) if (dy > 0) 1 else -1
                else if (dx > 0) 6 else -6)
            invalidate(); return
        }
        val map = atlas ?: return
        pendingProbe = 0
        pendingRegionCenter = 0
        centerX += dx / zoom; centerY += dy / zoom
        clamp(map)
        invalidate()
    }

    private fun clamp(map: XcxMapAtlas) {
        centerX = centerX.coerceIn(0f, map.width.toFloat())
        centerY = centerY.coerceIn(0f, map.height.toFloat())
    }

    private fun screen(x: Float, y: Float) = Pair(
        viewport.centerX() + (x - centerX) * zoom,
        viewport.centerY() + (y - centerY) * zoom)

    fun draw(canvas: Canvas) {
        paint.style = Paint.Style.FILL
        paint.color = Color.rgb(8, 20, 30)
        canvas.drawRect(viewport, paint)
        val map = atlas
        if (map == null) {
            label(canvas, "LOADING NATIVE MAP", viewport.centerX(), viewport.centerY(), 24f)
            return
        }
        canvas.save()
        canvas.clipRect(viewport)
        val requests = ArrayList<Tile>()
        val x0 = floor((centerX - viewport.width() / (2 * zoom)) / map.tileWidth).toInt().coerceAtLeast(0)
        val y0 = floor((centerY - viewport.height() / (2 * zoom)) / map.tileHeight).toInt().coerceAtLeast(0)
        val x1 = floor((centerX + viewport.width() / (2 * zoom)) / map.tileWidth).toInt().coerceAtMost(map.columns - 1)
        val y1 = floor((centerY + viewport.height() / (2 * zoom)) / map.tileHeight).toInt().coerceAtMost(map.rows - 1)
        val sample = when { zoom <= .25f -> 4; zoom <= .5f -> 2; else -> 1 }
        paint.isFilterBitmap = true
        paint.color = Color.WHITE
        for (y in y0..y1) for (x in x0..x1) {
            val name = map.tileName(x, y, flags) ?: continue
            val tile = Tile(name, sample)
            requests += tile
            val bitmap = tiles.get(tile) ?: continue
            val a = screen(x.toFloat() * map.tileWidth, y.toFloat() * map.tileHeight)
            val b = screen((x + 1f) * map.tileWidth, (y + 1f) * map.tileHeight)
            canvas.drawBitmap(bitmap, null, RectF(a.first, a.second, b.first, b.second), paint)
        }
        wanted = requests
        loadTiles()
        drawSegments(canvas, map)
        regionHits.clear()
        // Region bounds and labels come from SegmentMapInfo / FLD_FieldNameList.
        if (selectedRegion == 0L) for (region in map.regions) {
            val a = map.position(region.left, region.top).let { screen(it.first, it.second) }
            val b = map.position(region.right, region.bottom).let { screen(it.first, it.second) }
            val r = RectF(a.first, a.second, b.first, b.second)
            if (!RectF.intersects(r, viewport)) continue
            paint.style = Paint.Style.STROKE
            paint.strokeWidth = 1.5f
            paint.color = Color.argb(150, 196, 219, 228)
            canvas.drawRect(r, paint)
            paint.style = Paint.Style.FILL
            val width = max(100f, min(290f, region.name.length * 12f))
            val hit = RectF(r.centerX() - width / 2, r.centerY() - 24f,
                r.centerX() + width / 2, r.centerY() + 24f)
            paint.color = Color.argb(220, 11, 30, 40)
            canvas.drawRect(hit, paint)
            label(canvas, region.name, hit.centerX(), hit.centerY() + 7f, 20f)
            regionHits += Pair(RectF(r).apply { union(hit) }, region)
        }
        if (playerValid && (map.layer == 1 || zone in 1001L..1401L)) {
            val p = map.position(playerX, playerZ).let { screen(it.first, it.second) }
            val arrow = Path().apply {
                moveTo(0f, -14f); lineTo(-9f, 12f); lineTo(0f, 6f); lineTo(9f, 12f); close()
            }
            canvas.save()
            canvas.translate(p.first, p.second)
            paint.color = Color.CYAN
            if (playerYaw.isFinite()) {
                canvas.rotate(playerYaw)
                canvas.drawPath(arrow, paint)
            } else canvas.drawCircle(0f, 0f, 8f, paint)
            canvas.restore()
        }
        siteHits.clear()
        if (selectedRegion != 0L && sitesZone == selectedRegion) for (site in sites) {
            val p = map.position(site.x, site.z).let { screen(it.first, it.second) }
            if (!viewport.contains(p.first, p.second)) continue
            paint.color = if (site.id == selectedSite?.id) Color.WHITE else Color.rgb(170, 215, 230)
            paint.style = Paint.Style.STROKE
            paint.strokeWidth = 2f
            val diamond = Path().apply {
                moveTo(p.first, p.second - 8f); lineTo(p.first + 7f, p.second)
                lineTo(p.first, p.second + 8f); lineTo(p.first - 7f, p.second); close()
            }
            canvas.drawPath(diamond, paint)
            paint.style = Paint.Style.FILL
            siteHits += Pair(RectF(p.first - 20f, p.second - 20f, p.first + 20f, p.second + 20f), site)
        }
        canvas.restore()
        button(canvas, worldButton, if (map.layer == 2 && selectedRegion == 0L) "MIRA" else "WORLD")
        button(canvas, selfButton, "FIND SELF")
        button(canvas, minusButton, "−")
        button(canvas, plusButton, "+")
        val regionName = map.regions.firstOrNull { it.zone == selectedRegion }?.name
            ?: if (map.layer == 1) "Mira" else "New Los Angeles"
        paint.color = Color.argb(230, 8, 20, 30)
        canvas.drawRect(48f, 856f, 1192f, 908f, paint)
        label(canvas, selectedSite?.name ?: regionName, viewport.centerX(), 890f, 22f)
        if ((selectedSite?.formation ?: 0) > 0) button(canvas, travelButton, "FAST TRAVEL")
        if (probePicker.site != 0) probePicker.draw(canvas)
        segmentDetails.draw(canvas)
    }

    private fun loadTiles() {
        if (closed || !loading.compareAndSet(false, true)) return
        worker.execute {
            var changed = false
            try {
                // Batches yield to map/flag requests and re-read the latest viewport.
                val now = android.os.SystemClock.uptimeMillis()
                val batch = wanted.filter { tiles.get(it) == null &&
                    now >= (unavailable[it] ?: 0L) }.take(16)
                for (tile in batch) {
                    if (closed || Thread.currentThread().isInterrupted) break
                    val bitmap = runCatching {
                        NativeLibrary.getDualScreenRomfsAsset("xcx/maps/${tile.name}.png")?.let {
                            BitmapFactory.decodeByteArray(it, 0, it.size,
                                BitmapFactory.Options().apply { inSampleSize = tile.sample })
                        }
                    }.getOrNull()
                    if (bitmap != null) tiles.put(tile, bitmap)
                    else unavailable[tile] = now + 5000
                }
                changed = batch.isNotEmpty()
            } finally {
                loading.set(false)
                if (changed) dispatch { if (!closed) invalidate() }
            }
        }
    }

    private fun label(canvas: Canvas, text: String, x: Float, y: Float, size: Float) {
        paint.style = Paint.Style.FILL
        paint.color = Color.WHITE; paint.textSize = size; paint.textAlign = Paint.Align.CENTER
        canvas.drawText(text, x, y, paint)
    }

    private fun button(canvas: Canvas, r: RectF, text: String) {
        paint.color = Color.argb(235, 12, 38, 50)
        canvas.drawRect(r, paint)
        label(canvas, text, r.centerX(), r.centerY() + 8f, 22f)
    }

    fun touch(event: MotionEvent, scaleX: Float, scaleY: Float,
              offsetX: Float, offsetY: Float): Boolean {
        val x = (event.x - offsetX) * scaleX; val y = (event.y - offsetY) * scaleY
        if (event.actionMasked == MotionEvent.ACTION_DOWN && !viewport.contains(x, y)) return false
        if (segmentDetails.cell != null) {
            segmentDetails.touch(event, x, y); invalidate(); return true
        }
        if (probePicker.site != 0) {
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> { downX = x; downY = y; dragged = false }
                MotionEvent.ACTION_MOVE -> if (hypot(x - downX, y - downY) > 10f) dragged = true
                MotionEvent.ACTION_CANCEL -> dragged = true
                MotionEvent.ACTION_UP -> if (!dragged) { probePicker.tap(x, y); invalidate() }
            }
            return true
        }
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                downX = x; downY = y; pointerX = x; pointerY = y; dragged = false; distance = 0f
            }
            MotionEvent.ACTION_POINTER_DOWN -> { dragged = true; distance = 0f }
            MotionEvent.ACTION_MOVE -> {
                if (event.pointerCount >= 2) {
                    val next = hypot((event.getX(0) - event.getX(1)) * scaleX,
                        (event.getY(0) - event.getY(1)) * scaleY)
                    if (distance > 0 && next > 0) adjustZoom(next / distance)
                    distance = next; dragged = true
                } else {
                    if (hypot(x - downX, y - downY) > 10f) dragged = true
                    if (dragged) pan(pointerX - x, pointerY - y)
                }
                pointerX = x; pointerY = y
            }
            MotionEvent.ACTION_POINTER_UP -> { distance = 0f; dragged = true }
            MotionEvent.ACTION_UP -> if (!dragged) {
                when {
                    worldButton.contains(x, y) -> world()
                    selfButton.contains(x, y) -> findSelf()
                    minusButton.contains(x, y) -> adjustZoom(1 / 1.35f)
                    plusButton.contains(x, y) -> adjustZoom(1.35f)
                    travelButton.contains(x, y) && (selectedSite?.formation ?: 0) > 0 ->
                        selectedSite?.let { onTravel(it.formation) }
                    else -> {
                        val probe = probeHits.filter { it.first.contains(x, y) }.minByOrNull {
                            hypot(it.first.centerX() - x, it.first.centerY() - y)
                        }?.second
                        if (probe != null) { probePicker.open(probe); invalidate(); return true }
                        val mission = missionHits.filter { it.first.contains(x, y) }.minByOrNull {
                            hypot(it.first.centerX() - x, it.first.centerY() - y)
                        }?.second
                        if (mission != null) { onMission(mission); return true }
                        val detail = detailHits.filter { it.first.contains(x, y) }.minByOrNull {
                            hypot(it.first.centerX() - x, it.first.centerY() - y)
                        }?.second
                        if (detail != null) { openSegmentDetails(detail); return true }
                        val site = siteHits.minByOrNull {
                            hypot(it.first.centerX() - x, it.first.centerY() - y)
                        }?.takeIf { it.first.contains(x, y) }?.second
                        if (site != null) { selectedSite = site; invalidate() }
                        else regionHits.firstOrNull { it.first.contains(x, y) }?.second?.let {
                            if (it.zone == 1000L) requestLayer(2) else focus(it)
                        }
                    }
                }
            }
        }
        return true
    }

    private fun openSegmentDetails(cell: XcxMapSegments.Cell) {
        pendingProbe = 0
        segmentCatalog?.details?.get(cell.id)?.let { segmentDetails.open(cell, it); invalidate() }
    }

    fun showRegion(region: Long, probe: Int = 0) {
        segmentDetails.dismiss(); probePicker.dismiss()
        // An explicit destination takes precedence over first-load player auto-centering.
        initialCenterDone = true
        pendingSelf = false
        pendingProbe = probe; pendingProbeZone = region
        val layer = if (region in 1001L..1401L) 2 else 1
        val map = atlas
        if (map == null || map.layer != layer || loadingLayer != 0) {
            pendingZone = region
            requestLayer(layer)
        } else {
            map.regions.firstOrNull { it.zone == region }?.let(::focus)
            openPendingProbe()
        }
        invalidate()
    }

    private fun openPendingProbe() {
        if (pendingProbe == 0) return
        val map = atlas ?: return
        if (cellLayer != map.layer) return
        val cell = cells.firstOrNull { it.kind == 1 && it.probeId == pendingProbe &&
            it.field.zone == pendingProbeZone } ?: return
        pendingProbe = 0; pendingRegionCenter = 0
        val anchor = map.position(cell.field.x, cell.field.z)
        centerX = anchor.first + cell.column * 78.75f
        centerY = anchor.second + (cell.row + if (cell.column and 1 != 0) .5f else 0f) * 105f
        zoom = 1f
        if (cell.open && cell.probeVisible) probePicker.open(cell)
    }
}
