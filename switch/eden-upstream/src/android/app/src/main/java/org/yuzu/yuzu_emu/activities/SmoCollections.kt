package org.yuzu.yuzu_emu.activities

import java.nio.ByteBuffer
import java.nio.ByteOrder

data class SmoCollectionItem(val id: Int, val owned: Boolean, val name: String, val variant: String) {
    val messageKey: String get() = if (variant.isEmpty()) name else "$name@$variant"
}

data class SmoCollection(val generation: Long, val world: Int, val kind: Int, val entries: List<SmoCollectionItem>) {
    companion object {
        const val BYTES = 34848
        fun decode(source: ByteBuffer, bytes: Int): SmoCollection? {
            if (bytes != BYTES || source.capacity() < BYTES) return null
            val b = source.duplicate().order(ByteOrder.LITTLE_ENDIAN).apply { clear() }
            if (b.getInt(0) != 0x434f4d53 || b.getInt(4) != 1 || b.getInt(8) != BYTES) return null
            val count = b.getInt(12); val world = b.getInt(24); val kind = b.getInt(28)
            if (count !in 0..256 || world !in 0..16 || kind !in 0..5) return null
            fun text(at: Int): String? {
                val raw = ByteArray(64) { b.get(at + it) }; val end = raw.indexOf(0)
                if (end < 0) return null
                val value = String(raw, 0, end, Charsets.US_ASCII)
                return value.takeIf { it.matches(Regex("[A-Za-z0-9_]*")) }
            }
            val entries = ArrayList<SmoCollectionItem>(count)
            repeat(count) { i ->
                val at = 32 + i * 136
                val id = b.getInt(at); val owned = b.getInt(at + 4)
                val name = text(at + 8) ?: return null; val variant = text(at + 72) ?: return null
                if (id != i || owned !in 0..1 || (owned == 1 && name.isEmpty()) ||
                    (owned == 0 && (name.isNotEmpty() || variant.isNotEmpty()))) return null
                entries.add(SmoCollectionItem(id, owned == 1, name, variant))
            }
            return SmoCollection(b.getLong(16), world, kind, entries)
        }
    }
}

/** Shared draw/hit-test geometry: 3 columns, gaps are not touch targets. */
object SmoCollectionGrid {
    const val LEFT = 62f; const val TOP = 358f; const val RIGHT = 780f; const val BOTTOM = 932f
    const val PITCH_X = 242f; const val PITCH_Y = 298f
    const val WIDTH = 228f; const val HEIGHT = 282f
    fun maxScroll(count: Int): Float = (((count + 2) / 3) * PITCH_Y - (BOTTOM - TOP)).coerceAtLeast(0f)
    fun itemAt(x: Float, y: Float, scroll: Float, count: Int): Int? {
        if (!x.isFinite() || !y.isFinite() || !scroll.isFinite() || scroll < 0f) return null
        if (x < LEFT || x >= RIGHT || y < TOP || y >= BOTTOM) return null
        val xx = x - LEFT; val yy = y - TOP + scroll
        val col = (xx / PITCH_X).toInt(); val row = (yy / PITCH_Y).toInt()
        if (col !in 0..2 || xx % PITCH_X >= WIDTH || yy % PITCH_Y >= HEIGHT) return null
        return (row * 3 + col).takeIf { it in 0 until count }
    }
}
