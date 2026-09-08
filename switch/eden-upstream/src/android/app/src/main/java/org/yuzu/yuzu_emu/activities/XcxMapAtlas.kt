package org.yuzu.yuzu_emu.activities

import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Native map coordinates and sparse texture planes. No Android or guest-memory dependencies. */
internal data class XcxMapAtlas(
    val layer: Int,
    val worldWidth: Float, val worldHeight: Float,
    val pixelWidth: Float, val pixelHeight: Float,
    val offsetX: Float, val offsetZ: Float,
    val tileWidth: Int, val tileHeight: Int, val columns: Int, val rows: Int,
    val width: Int, val height: Int,
    val variants: List<Variant>, val regions: List<Region>
) {
    data class Variant(val flag: Int, val name: String, val coverage: ByteArray)
    data class Region(val zone: Long, val left: Float, val top: Float,
                      val right: Float, val bottom: Float, val name: String)

    fun position(x: Float, z: Float): Pair<Float, Float> =
        Pair((x - offsetX) * pixelWidth / worldWidth + pixelWidth * .5f,
             (z - offsetZ) * pixelHeight / worldHeight + pixelHeight * .5f)

    fun tileName(x: Int, y: Int, flags: Set<Int>): String? {
        if (x !in 0 until columns || y !in 0 until rows) return null
        val active = variants.filterIndexed { index, variant ->
            (index == 0 || variant.flag in flags) && variant.coverage[y * columns + x] != 0.toByte()
        }
        if (active.isEmpty()) return null
        // 0x560C10 prefers enabled extension planes over base, and combines
        // suffixes in table order when several extension planes cover a tile.
        val extensions = active.filter { it !== variants.first() }
        val name = if (extensions.isEmpty()) variants.first().name else {
            val base = variants.first().name
            if (extensions.any { !it.name.startsWith(base) }) return null
            base + extensions.joinToString("") { it.name.removePrefix(base) }
        }
        return "%s_%02d%02d".format(java.util.Locale.ROOT, name, x, y)
    }

    companion object {
        fun parse(bytes: ByteArray): XcxMapAtlas? = runCatching {
            val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            require(b.int == 0x534C5458 && b.int == 1)
            val layer = b.int
            require(layer in 1..2)
            val transform = List(6) { b.float }
            require(transform.all { it.isFinite() } && transform.take(4).all { it > 0f })
            fun string(): String {
                val size = b.int
                require(size in 0..4096 && size <= b.remaining())
                return String(ByteArray(size).also(b::get), Charsets.UTF_8)
            }
            val definitions = List(5) { Pair(b.int, string()) }
            require(definitions.first().second == if (layer == 1) "map01" else "map02")
            val segSize = b.int
            require(segSize in 32..65536 && segSize <= b.remaining())
            val seg = ByteArray(segSize).also(b::get)
            val s = ByteBuffer.wrap(seg).order(ByteOrder.LITTLE_ENDIAN)
            val tw = s.int; val th = s.int; val cols = s.int; val rows = s.int
            require(tw in 1..1024 && th in 1..1024 && cols in 1..64 && rows in 1..64)
            val trimX = s.short.toInt() and 65535; val trimY = s.short.toInt() and 65535
            require(trimX < tw && trimY < th)
            val stride = ((cols * rows + 3) and -4) + 32
            require(segSize % stride == 0 && segSize / stride in 1..5)
            val variants = List(segSize / stride) { index ->
                val (flag, name) = definitions[index]
                require(name.matches(Regex("map0[12](_ex[0-9]{2})*")))
                Variant(flag, name, seg.copyOfRange(index * stride + 32,
                    index * stride + 32 + cols * rows))
            }
            val count = b.int
            require(count in 1..16)
            val regions = List(count) {
                val zone = b.int.toLong() and 0xffffffffL
                val v = List(6) { b.float }
                require(v.all { it.isFinite() } && v.drop(2).all { it > 0f })
                val ratio = v[5] / v[4]
                Region(zone, v[0] * ratio, v[1] * ratio,
                    (v[0] + v[2]) * ratio, (v[1] + v[3]) * ratio, string())
            }
            require(!b.hasRemaining())
            XcxMapAtlas(layer, transform[0], transform[1], transform[2], transform[3],
                transform[4], transform[5], tw, th, cols, rows,
                cols * tw - trimX, rows * th - trimY, variants, regions)
        }.getOrNull()
    }
}
