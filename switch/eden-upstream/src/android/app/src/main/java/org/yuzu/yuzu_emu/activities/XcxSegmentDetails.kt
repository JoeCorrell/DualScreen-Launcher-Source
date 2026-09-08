package org.yuzu.yuzu_emu.activities

import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.view.MotionEvent
import kotlin.math.abs

/** A discovered segment's actual SEG_NormalList / SEG_Segment_ms description. */
internal class XcxSegmentDetails {
    var cell: XcxMapSegments.Cell? = null
        private set
    private var detail = XcxMapSegments.Detail("", "")
    private var firstLine = 0
    private var lineCount = 0
    private var pressed = 0
    private var downX = 0f
    private var downY = 0f
    private var lastY = 0f
    private var dragged = false
    private var scrollRemainder = 0f
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val close = RectF(1010f, 202f, 1150f, 260f)
    private val previous = RectF(90f, 824f, 280f, 884f)
    private val next = RectF(310f, 824f, 500f, 884f)
    private val body = RectF(90f, 336f, 1150f, 798f)
    private val pageLines = 14

    fun open(value: XcxMapSegments.Cell, text: XcxMapSegments.Detail) {
        cell = value; detail = text; firstLine = 0; lineCount = 0
    }
    fun dismiss() { cell = null; pressed = 0 }
    fun refresh(cells: List<XcxMapSegments.Cell>) {
        val selected = cell ?: return
        cell = cells.firstOrNull { it.field.id == selected.field.id && it.id == selected.id &&
            it.column == selected.column && it.row == selected.row && it.kind == selected.kind && it.open }
    }
    fun scroll(lines: Int) { firstLine = (firstLine + lines).coerceIn(0, (lineCount - pageLines).coerceAtLeast(0)) }

    fun touch(event: MotionEvent, x: Float, y: Float) {
        fun buttonAt(): Int = when {
            close.contains(x, y) -> 1
            previous.contains(x, y) -> 2
            next.contains(x, y) -> 3
            else -> 0
        }
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                pressed = buttonAt(); downX = x; downY = y; lastY = y
                dragged = false; scrollRemainder = 0f
            }
            MotionEvent.ACTION_MOVE -> {
                if (abs(x - downX) + abs(y - downY) > 10f) dragged = true
                if (dragged && body.contains(downX, downY)) {
                    scrollRemainder += lastY - y
                    val lines = (scrollRemainder / 30f).toInt()
                    scroll(lines); scrollRemainder -= lines * 30f
                }
                lastY = y
            }
            MotionEvent.ACTION_POINTER_DOWN, MotionEvent.ACTION_CANCEL -> { dragged = true; pressed = 0 }
            MotionEvent.ACTION_UP -> {
                if (!dragged && pressed == buttonAt()) when (pressed) {
                    1 -> dismiss()
                    2 -> scroll(-pageLines)
                    3 -> scroll(pageLines)
                }
                pressed = 0
            }
        }
    }

    fun draw(canvas: Canvas) {
        val selected = cell ?: return
        paint.color = Color.argb(252, 7, 22, 32)
        canvas.drawRect(64f, 190f, 1176f, 902f, paint)
        fun text(value: String, x: Float, y: Float, size: Float = 22f) {
            paint.color = Color.WHITE; paint.textSize = size
            canvas.drawText(value, x, y, paint)
        }
        fun button(rect: RectF, value: String) {
            paint.color = Color.rgb(24, 67, 82); canvas.drawRect(rect, paint)
            text(value, rect.left + 14f, rect.centerY() + 8f)
        }
        val clean: (String) -> String = { it.replace(Regex("<[^>]*>|\\[[^]]*]"), "").replace("\\n", "\n") }
        val title = clean(detail.title).ifBlank { "Segment" }
        paint.textSize = 28f
        while (paint.textSize > 16f && paint.measureText(title) > 900f) paint.textSize -= 1f
        canvas.save(); canvas.clipRect(90f, 198f, 995f, 264f)
        text(title, 90f, 242f, paint.textSize); canvas.restore()
        button(close, "CLOSE")
        text(if (selected.complete) "COMPLETED" else "INCOMPLETE", 90f, 302f)
        paint.textSize = 22f
        val lines = mutableListOf<String>()
        for (paragraph in clean(detail.description).split('\n')) {
            var line = ""
            for (word in paragraph.trim().split(Regex("\\s+"))) {
                val candidate = if (line.isEmpty()) word else "$line $word"
                if (line.isNotEmpty() && paint.measureText(candidate) > body.width()) {
                    lines += line; line = word
                } else line = candidate
            }
            lines += line
        }
        lineCount = lines.size; scroll(0)
        canvas.save(); canvas.clipRect(body)
        lines.drop(firstLine).take(pageLines).forEachIndexed { index, line ->
            text(line, body.left, body.top + 24f + index * 30f)
        }
        canvas.restore()
        if (firstLine > 0) button(previous, "PREVIOUS")
        if (firstLine + pageLines < lineCount) button(next, "NEXT")
    }
}
