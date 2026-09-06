package org.yuzu.yuzu_emu.activities

import java.nio.ByteBuffer
import java.nio.ByteOrder

data class SmoMarker(val id: Int, val kind: Int, val position: List<Float>, val stage: String, val label: String) {
    val fallbackName: String get() = when (kind) {
        0 -> "Checkpoint"
        1 -> "Odyssey"
        2 -> "Crazy Cap"
        else -> "Minigame"
    }
    val messageKey: String get() = if (kind == 0) "Checkpoint_$label" else label
}

data class SmoMarkers(val generation: Long, val world: Int, val scenario: Int, val entries: List<SmoMarker>) {
    companion object {
        const val BYTES = 4896
        fun decode(source: ByteBuffer, bytes: Int): SmoMarkers? {
            if (bytes != BYTES || source.capacity() < BYTES) return null
            val b = source.duplicate().order(ByteOrder.LITTLE_ENDIAN).apply { clear() }
            if (b.getInt(0) != 0x4b4f4d53 || b.getInt(4) != 1 || b.getInt(8) != BYTES) return null
            val count = b.getInt(12)
            val world = b.getInt(24)
            val scenario = b.getInt(28)
            if (count !in 0..32 || world !in 0..16 || scenario < 0) return null
            fun text(offset: Int): String? {
                val raw = ByteArray(64) { b.get(offset + it) }
                val end = raw.indexOf(0.toByte())
                if (end < 0 || raw.take(end).any { it.toInt() !in 32..126 }) return null
                return String(raw, 0, end, Charsets.US_ASCII)
            }
            val entries = ArrayList<SmoMarker>(count)
            val ids = HashSet<Int>()
            repeat(count) { i ->
                val at = 32 + i * 152
                val id = b.getInt(at)
                val kind = b.getInt(at + 4)
                val position = List(3) { b.getFloat(at + 8 + it * 4) }
                val stage = text(at + 24) ?: return null
                val label = text(at + 88) ?: return null
                if (kind !in 0..3 || id < 0 || !ids.add(id) || position.any { !it.isFinite() } ||
                    !stage.matches(Regex("[A-Za-z0-9_]{1,63}")) || b.getInt(at + 20) != 0 ||
                    (kind == 0 && !label.matches(Regex("[A-Za-z0-9_]{1,63}")))) return null
                entries.add(SmoMarker(id, kind, position, stage, label))
            }
            return SmoMarkers(b.getLong(16), world, scenario, entries)
        }
    }
}
