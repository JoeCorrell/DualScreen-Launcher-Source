package org.yuzu.yuzu_emu.activities

import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.max
import kotlin.math.min

/** Immutable copy of the exact-base native contract. Never retains the JNI buffer. */
data class SmoSnapshot(
    val status: Int,
    val generation: Long,
    val world: Int = -1,
    val scenario: Int = -1,
    val coins: Int = 0,
    val regionalCoins: Int = 0,
    val health: Int = 0,
    val maxHealth: Int = 0,
    val outfit: String = "",
    val hat: String = "",
    val stage: String = "",
    val map: SmoMapState? = null,
    val worldMoons: Int = -1,
    val totalMoons: Int = -1
) {
    val active: Boolean get() = status == 4

    companion object {
        const val BYTES = 416
        fun decode(source: ByteBuffer, bytes: Int): SmoSnapshot? {
            if (bytes != BYTES || source.capacity() < BYTES) return null
            val b = source.duplicate().order(ByteOrder.LITTLE_ENDIAN)
            b.clear()
            if (b.getInt(0) != 0x534f4d53 || b.getInt(4) != 1 || b.getInt(8) != BYTES) return null
            val status = b.getInt(12)
            if (status !in 0..5) return null
            val generation = b.getLong(16)
            if (status != 4) return SmoSnapshot(status, generation)
            val world = b.getInt(24)
            val scenario = b.getInt(28)
            val coins = b.getInt(32)
            val regional = b.getInt(36)
            val health = b.getInt(40)
            val maxHealth = b.getInt(44)
            val worldMoons = b.getInt(60)
            val totalMoons = b.getInt(412)
            if (world !in 0..16 || scenario < 0 || coins < 0 || regional < 0 ||
                health !in 0..9 || maxHealth !in setOf(3, 6, 9) ||
                worldMoons !in -1..9999 || totalMoons !in -1..199980) return null
            fun text(offset: Int): String? {
                val raw = ByteArray(64) { b.get(offset + it) }
                val end = raw.indexOf(0.toByte())
                if (end < 0 || raw.take(end).any { it.toInt() !in 32..126 }) return null
                return String(raw, 0, end, Charsets.US_ASCII)
            }
            val outfit = text(64) ?: return null
            val hat = text(128) ?: return null
            val stage = text(192) ?: return null
            val map = if (b.getInt(256) == 1) {
                val texture = text(272) ?: return null
                val w = b.getInt(264)
                val h = b.getInt(268)
                val matrix = List(16) { b.getFloat(336 + it * 4) }
                val position = List(3) { b.getFloat(400 + it * 4) }
                if (!texture.matches(Regex("[A-Za-z0-9_]{1,63}")) || w !in 1..2048 ||
                    h !in 1..2048 || (matrix + position).any { !it.isFinite() } ||
                    matrix[12] != 0f || matrix[13] != 0f || matrix[14] != 0f || matrix[15] != 1f
                ) return null
                SmoMapState(texture, b.getInt(260), w, h, matrix, position)
            } else null
            return SmoSnapshot(status, generation, world, scenario, coins, regional, health,
                maxHealth, outfit, hat, stage, map, worldMoons, totalMoons)
        }
    }
}

data class SmoMapState(
    val texture: String,
    val scenario: Int,
    val width: Int,
    val height: Int,
    val viewProjection: List<Float>,
    val position: List<Float>
) {
    /** MapLayout::updatePlayerPosLayout: row-major affine XY, 1024 half-texture extent.
     * Android canvas Y is down; native layout Y is up. No guessed kingdom bounds. */
    fun playerUv(): Pair<Float, Float>? = project(position)

    fun project(position: List<Float>): Pair<Float, Float>? {
        if (viewProjection.size != 16 || position.size != 3) return null
        fun row(r: Int): Double = viewProjection[r * 4 + 3].toDouble() +
            (0..2).sumOf { viewProjection[r * 4 + it].toDouble() * position[it] }
        val u = ((row(0) + 1.0) * .5).toFloat()
        val v = ((1.0 - row(1)) * .5).toFloat()
        return if (u.isFinite() && v.isFinite()) u to v else null
    }
}

/** Pure map transform shared by drawing and hit testing. Coordinates are viewport-local. */
class SmoMapViewport {
    var width = 1f; private set
    var height = 1f; private set
    var zoom = 1f; private set
    var panX = 0f; private set
    var panY = 0f; private set
    val extent: Float get() = min(width, height) * zoom
    val left: Float get() = (width - extent) * .5f + panX
    val top: Float get() = (height - extent) * .5f + panY

    fun resize(w: Float, h: Float) {
        width = max(1f, w); height = max(1f, h); clamp()
    }
    fun reset() { zoom = 1f; panX = 0f; panY = 0f }
    fun drag(dx: Float, dy: Float) {
        if (!dx.isFinite() || !dy.isFinite()) return
        panX += dx; panY += dy; clamp()
    }
    fun scale(factor: Float, focusX: Float, focusY: Float) {
        if (!factor.isFinite() || factor <= 0f || !focusX.isFinite() || !focusY.isFinite()) return
        val u = (focusX - left) / extent
        val v = (focusY - top) / extent
        zoom = (zoom * factor).coerceIn(1f, 5f)
        panX = focusX - u * extent - (width - extent) * .5f
        panY = focusY - v * extent - (height - extent) * .5f
        clamp()
    }
    fun recenter(u: Float, v: Float) {
        if (!u.isFinite() || !v.isFinite()) return
        panX = (.5f - u) * extent; panY = (.5f - v) * extent; clamp()
    }
    fun point(u: Float, v: Float) = left + u * extent to top + v * extent
    private fun clamp() {
        val x = max(0f, (extent - width) * .5f)
        val y = max(0f, (extent - height) * .5f)
        panX = panX.coerceIn(-x, x); panY = panY.coerceIn(-y, y)
    }
}
