package org.yuzu.yuzu_emu.activities

internal object XcxPartyOrder {
    fun argument(from: Int, to: Int, validMask: Int, characters: IntArray): Int? {
        if (from !in 1..3 || to !in 1..3 || from == to || characters.size != 4 ||
            validMask and 1 == 0 || validMask and (1 shl from) == 0 ||
            validMask and (1 shl to) == 0) return null
        val source = characters[from]; val target = characters[to]
        if (source !in 0..23 || target !in 0..23 || source == target) return null
        return from or (to shl 8) or (source shl 16) or (target shl 24)
    }
}
