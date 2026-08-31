package com.wemu.session.secondscreen

import androidx.compose.foundation.layout.Box
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import kotlin.math.roundToInt

data class UiSprite(
    val image: ImageBitmap,
    val left: Int = 0,
    val top: Int = 0,
    val right: Int = 0,
    val bottom: Int = 0,
) {
    val isNineSlice: Boolean get() = left > 0 || top > 0 || right > 0 || bottom > 0
}

fun DrawScope.drawNineSlice(sprite: UiSprite, size: Size) {
    val image = sprite.image
    val sw = image.width
    val sh = image.height
    if (sw <= 0 || sh <= 0) return

    val dw = size.width.roundToInt()
    val dh = size.height.roundToInt()
    if (dw <= 0 || dh <= 0) return

    if (!sprite.isNineSlice) {
        drawImage(
            image = image,
            srcOffset = IntOffset.Zero,
            srcSize = IntSize(sw, sh),
            dstOffset = IntOffset.Zero,
            dstSize = IntSize(dw, dh),
            filterQuality = FilterQuality.None,
        )
        return
    }

    val sl = sprite.left.coerceIn(0, sw / 2)
    val sr = sprite.right.coerceIn(0, sw / 2)
    val st = sprite.top.coerceIn(0, sh / 2)
    val sb = sprite.bottom.coerceIn(0, sh / 2)

    val zoom = spriteZoom(density)
    val l = (sl * zoom).coerceAtMost(dw / 2)
    val r = (sr * zoom).coerceAtMost(dw / 2)
    val t = (st * zoom).coerceAtMost(dh / 2)
    val b = (sb * zoom).coerceAtMost(dh / 2)

    val srcMidW = (sw - sl - sr).coerceAtLeast(0)
    val srcMidH = (sh - st - sb).coerceAtLeast(0)
    val dstMidW = (dw - l - r).coerceAtLeast(0)
    val dstMidH = (dh - t - b).coerceAtLeast(0)

    val cols = listOf(0 to sl, sl to srcMidW, sw - sr to sr)
    val rows = listOf(0 to st, st to srcMidH, sh - sb to sb)
    val dstCols = listOf(0 to l, l to dstMidW, dw - r to r)
    val dstRows = listOf(0 to t, t to dstMidH, dh - b to b)

    for (row in 0..2) {
        val (sy, sHeight) = rows[row]
        val (dy, dHeight) = dstRows[row]
        if (sHeight <= 0 || dHeight <= 0) continue
        for (col in 0..2) {
            val (sx, sWidth) = cols[col]
            val (dx, dWidth) = dstCols[col]
            if (sWidth <= 0 || dWidth <= 0) continue
            drawImage(
                image = image,
                srcOffset = IntOffset(sx, sy),
                srcSize = IntSize(sWidth, sHeight),
                dstOffset = IntOffset(dx, dy),
                dstSize = IntSize(dWidth, dHeight),
                filterQuality = FilterQuality.None,
            )
        }
    }
}

fun spriteZoom(density: Float): Int = density.roundToInt().coerceIn(1, 6)

@Composable
fun SpriteBox(
    sprite: UiSprite?,
    modifier: Modifier = Modifier,
    fallback: Modifier = Modifier,
    content: @Composable () -> Unit,
) {
    val painted = if (sprite != null) {
        modifier.drawBehind { drawNineSlice(sprite, size) }
    } else {
        modifier.then(fallback)
    }
    Box(painted) { content() }
}

fun DrawScope.drawTiled(sprite: UiSprite, magnify: Int = 1) {
    val image = sprite.image
    if (image.width <= 0 || image.height <= 0) return

    val zoom = spriteZoom(density) * magnify.coerceIn(1, 8)
    val tileWidth = image.width * zoom
    val tileHeight = image.height * zoom
    if (tileWidth <= 0 || tileHeight <= 0) return

    var y = 0
    while (y < size.height) {
        var x = 0
        while (x < size.width) {
            drawImage(
                image = image,
                srcOffset = IntOffset.Zero,
                srcSize = IntSize(image.width, image.height),
                dstOffset = IntOffset(x, y),
                dstSize = IntSize(tileWidth, tileHeight),
                filterQuality = FilterQuality.None,
            )
            x += tileWidth
        }
        y += tileHeight
    }
}

object StardewUi {
    const val PANEL = "ui:panel"
    const val SLOT = "ui:slot"
    const val SLOT_SELECTED = "ui:slot_selected"
    const val HEART = "ui:heart"
    const val HEART_EMPTY = "ui:heart_empty"
    const val COIN = "ui:coin"
    const val MAP = "ui:map"
    const val PLAYER = "ui:player"
    const val FARMER = "ui:farmer"
    const val PET = "ui:pet"
    const val ARROW_LEFT = "ui:arrow_left"
    const val ARROW_RIGHT = "ui:arrow_right"
    const val GROUND = "ui:ground"
    const val SCROLL = "ui:scroll"
    const val CHECK_ON = "ui:check_on"
    const val CHECK_OFF = "ui:check_off"
    const val BUTTON = "ui:button"
    const val WATER_GAUGE = "ui:water_gauge"
    const val SORT = "ui:sort"
    const val SEASON = "ui:season"
    const val TIME = "ui:time"
    const val TAB_BACKPACK = "ui:tab_backpack"
    const val TAB_SKILLS = "ui:tab_skills"
    const val TAB_RELATIONSHIPS = "ui:tab_relationships"
    const val TAB_MAP = "ui:tab_map"
    const val TAB_CRAFTING = "ui:tab_crafting"
    const val TAB_JOURNAL = "ui:tab_journal"
    const val TAB_SETTINGS = "ui:tab_settings"
}
