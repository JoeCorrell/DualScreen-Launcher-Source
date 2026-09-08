package org.yuzu.yuzu_emu.activities

import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF

internal class XcxProbePicker(private val install: (Int) -> Unit) {
    var site = 0
        private set
    var items: List<XcxProbes.Item> = emptyList()
    var current = 0
    var stock: Map<Int, XcxProbes.Stock>? = null
    var credits: Long? = null
    var updating = false
    private var selected = 0
    private var page = 0
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val close = RectF(1010f, 200f, 1150f, 254f)
    private val previous = RectF(90f, 824f, 230f, 884f)
    private val next = RectF(250f, 824f, 390f, 884f)
    private val apply = RectF(820f, 824f, 1150f, 884f)

    fun open(cell: XcxMapSegments.Cell) {
        site = cell.probeId; current = cell.probeItem; selected = current
        stock = null
        page = items.indexOfFirst { it.id == current }.coerceAtLeast(0) / 6
    }
    fun dismiss() { site = 0 }
    fun move(delta: Int) {
        if (items.isEmpty()) return
        val index = (items.indexOfFirst { it.id == selected }.coerceAtLeast(0) + delta)
            .coerceIn(0, items.lastIndex)
        selected = items[index].id; page = index / 6
    }
    fun activate() { if (canInstall()) XcxProbes.argument(site, selected)?.let(install) }
    fun tap(x: Float, y: Float) {
        when {
            close.contains(x, y) -> dismiss()
            previous.contains(x, y) -> page = (page - 1).coerceAtLeast(0)
            next.contains(x, y) -> page = (page + 1).coerceAtMost((items.size - 1).coerceAtLeast(0) / 6)
            apply.contains(x, y) && canInstall() ->
                XcxProbes.argument(site, selected)?.let(install)
            x in 90f..1150f && y in 276f..<672f ->
                items.getOrNull(page * 6 + ((y - 276f) / 66f).toInt())?.let { selected = it.id }
        }
    }
    private fun availability() = XcxProbes.availability(current,
        items.firstOrNull { it.id == selected }, stock, credits, updating)
    private fun canInstall() = availability() == XcxProbes.Availability.Ready
    fun draw(canvas: Canvas) {
        paint.color = Color.argb(252, 7, 22, 32)
        canvas.drawRect(64f, 190f, 1176f, 902f, paint)
        fun text(value: String, x: Float, y: Float, size: Float = 22f) {
            paint.color = Color.WHITE; paint.textSize = size
            canvas.drawText(value, x, y, paint)
        }
        fun button(rect: RectF, value: String) {
            paint.color = Color.rgb(24, 67, 82); canvas.drawRect(rect, paint)
            text(value, rect.left + 12f, rect.centerY() + 8f)
        }
        text("PROBE SITE $site", 90f, 239f, 28f)
        text(credits?.let { "%,d credits".format(it) } ?: "Balance unavailable", 620f, 239f, 20f)
        button(close, "CLOSE")
        if (items.isEmpty()) text("Loading probe definitions...", 90f, 330f)
        items.drop(page * 6).take(6).forEachIndexed { index, item ->
            val top = 276f + index * 66f
            paint.color = if (item.id == selected) Color.rgb(30, 90, 106) else Color.rgb(12, 39, 51)
            canvas.drawRect(90f, top, 1150f, top + 60f, paint)
            val name = item.name.ifBlank { "Probe ${item.id}" }
            text(name.take(48), 106f, top + 27f)
            text(if (current == item.id) "INSTALLED" else "${item.cost} credits", 890f, top + 27f)
            val count = stock?.get(item.id)
            text(if (item.id == 1) "Basic probe" else count?.let {
                "${it.available} available · ${it.installed} installed · ${it.owned} owned"
            } ?: "Inventory unavailable", 106f, top + 51f, 18f)
        }
        val item = items.firstOrNull { it.id == selected }
        val description = item?.description.orEmpty().replace(Regex("<[^>]*>|\\[[^]]*]"), "")
            .replace(Regex("\\s+"), " ")
        var line = ""; var y = 710f
        for (word in description.split(' ')) {
            if (paint.measureText("$line $word") > 1020f && line.isNotEmpty()) {
                text(line, 90f, y); y += 28f; line = ""
                if (y > 790f) break
            }
            line = if (line.isEmpty()) word else "$line $word"
        }
        if (y <= 790f) text(line, 90f, y)
        button(previous, "PREVIOUS"); button(next, "NEXT")
        text("${page + 1} / ${((items.size - 1).coerceAtLeast(0) / 6) + 1}", 440f, 862f)
        when (availability()) {
            XcxProbes.Availability.Ready -> button(apply, "INSTALL · ${item!!.cost} credits")
            XcxProbes.Availability.Updating -> text("Updating probe...", 820f, 862f)
            XcxProbes.Availability.NotDeployed -> text("Visit this site to deploy its first probe.", 630f, 862f)
            XcxProbes.Availability.NoSelection -> text("Select a probe", 820f, 862f)
            XcxProbes.Availability.Installed -> text("Already installed", 820f, 862f)
            XcxProbes.Availability.InventoryUnavailable -> text("Inventory unavailable", 820f, 862f)
            XcxProbes.Availability.NoStock -> text("No available probe", 820f, 862f)
            XcxProbes.Availability.BalanceUnavailable -> text("Balance unavailable", 820f, 862f)
            XcxProbes.Availability.InsufficientCredits -> text("Not enough credits", 820f, 862f)
        }
    }
}
