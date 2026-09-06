package org.yuzu.yuzu_emu.activities

import java.nio.ByteBuffer
import java.nio.ByteOrder

data class SmoMoon(val id: Int, val number: Int, val collected: Boolean, val known: Boolean,
    val moonRock: Boolean, val hint: Boolean, val stage: String, val placement: String) {
    val messageKey: String get() = "ScenarioName_$placement"
}

data class SmoMoons(val generation: Long, val world: Int, val scenario: Int, val entries: List<SmoMoon>) {
    companion object {
        const val BYTES = 36896
        fun decode(source: ByteBuffer, bytes: Int): SmoMoons? {
            if (bytes != BYTES || source.capacity() < BYTES) return null
            val b = source.duplicate().order(ByteOrder.LITTLE_ENDIAN).apply { clear() }
            if (b.getInt(0) != 0x4d4f4d53 || b.getInt(4) != 1 || b.getInt(8) != BYTES) return null
            val count = b.getInt(12)
            val world = b.getInt(24)
            val scenario = b.getInt(28)
            if (count !in 0..256 || world !in 0..16 || scenario < 0) return null
            val entries = ArrayList<SmoMoon>(count)
            val seen = HashSet<Int>()
            repeat(count) { i ->
                val at = 32 + i * 144
                val id = b.getInt(at)
                val number = b.getInt(at + 4)
                val flags = b.getInt(at + 8)
                if (id !in 0..1023 || !seen.add(id) || number != i + 1 || flags !in 0..15) return null
                fun text(offset: Int): String? {
                    val raw = ByteArray(64) { b.get(offset + it) }
                    val end = raw.indexOf(0.toByte())
                    if (end < 0) return null
                    val value = String(raw, 0, end, Charsets.US_ASCII)
                    return value.takeIf { it.matches(Regex("[A-Za-z0-9_]*")) }
                }
                val stage = text(at + 16) ?: return null
                val placement = text(at + 80) ?: return null
                val known = flags and 2 != 0
                if (!known && (stage.isNotEmpty() || placement.isNotEmpty())) return null
                entries += SmoMoon(id, number, flags and 1 != 0, known, flags and 4 != 0,
                    flags and 8 != 0, stage, placement)
            }
            return SmoMoons(b.getLong(16), world, scenario, entries.toList())
        }
    }
}
