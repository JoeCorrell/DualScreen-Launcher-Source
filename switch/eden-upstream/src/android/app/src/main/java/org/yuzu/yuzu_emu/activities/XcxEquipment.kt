package org.yuzu.yuzu_emu.activities

import java.nio.ByteBuffer
import java.nio.ByteOrder

internal object XcxEquipment {
    val categories = intArrayOf(6, 7, 1, 2, 4, 3, 5, 1, 2, 4, 3, 5)
    // 0x405D28 categories; DEF_PcList DefWpnFar/Near and DefAmr1..5 establish labels.
    val slotLabels = listOf("Ranged weapon", "Melee weapon", "Head", "Torso", "Left arm",
        "Right arm", "Legs", "Fashion head", "Fashion torso", "Fashion left arm",
        "Fashion right arm", "Fashion legs")
    data class Item(val slot: Int, val category: Int, val id: Int, val instance: Long)
    data class Frame(val character: Int, val items: List<Item>)
    data class OwnedItem(val id: Int, val instance: Long, val flags: Long, val identity: Long)
    data class Inventory(val category: Int, val items: List<OwnedItem>)

    fun inventory(bytes: ByteArray, expectedCategory: Int): Inventory? = runCatching {
        require(expectedCategory in 1..7 && bytes.size in 16..(16 + 4096 * 16))
        val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        require(b.int == 0x49455858 && b.int == 1 && b.int == expectedCategory)
        val count = b.int
        require(count in 0..4096 && b.remaining() == count * 16)
        val seen = HashSet<Long>()
        val items = List(count) {
            val id = b.int
            val key = b.int.toLong() and 0xffffffffL
            val flags = b.int.toLong() and 0xffffffffL
            val identity = b.int.toLong() and 0xffffffffL
            require(id in 1..8191 && (flags and 8191L) == id.toLong())
            require((key and 63L) == expectedCategory.toLong() && seen.add(key))
            require((key ushr 6) == (identity and 0x3ffffffL))
            OwnedItem(id, key, flags, identity)
        }
        Inventory(expectedCategory, items)
    }.getOrNull()
    data class Catalog(val slots: List<String>, val weapons: Map<Int, String>, val armor: Map<Int, String>) {
        fun name(item: Item): String = if (item.id == 0) "Empty" else
            (if (item.category >= 6) weapons else armor)[item.id]?.takeIf { it.isNotBlank() }
                ?: "Item ${item.id}"
    }
    fun frame(bytes: ByteArray, expectedCharacter: Int): Frame? = runCatching {
        require(bytes.size == 208 && expectedCharacter in 0..22)
        val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        require(b.int == 0x51455858 && b.int == 1 && b.int == expectedCharacter && b.int == 12)
        val items = List(12) { slot ->
            require(b.int == slot)
            val category = b.int; val id = b.int; val instance = b.int.toLong() and 0xffffffffL
            require(category == categories[slot] && id in 0..8191 && (id != 0 || instance == 0L))
            Item(slot, category, id, instance)
        }
        Frame(expectedCharacter, items)
    }.getOrNull()

    fun catalog(bytes: ByteArray): Catalog? = runCatching {
        val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        fun text(): String {
            val size = b.int
            require(size in 0..4096 && size <= b.remaining())
            return ByteArray(size).also { b.get(it) }.toString(Charsets.UTF_8)
        }
        require(b.int == 0x4E514558 && b.int == 1 && b.int == 12)
        val slots = List(12) { text() }
        fun names(): Map<Int, String> {
            val count = b.int
            require(count in 0..8192)
            val result = HashMap<Int, String>(count)
            repeat(count) {
                val id = b.int
                require(id in 0..8191 && id !in result)
                result[id] = text()
            }
            return result
        }
        val catalog = Catalog(slots, names(), names())
        require(!b.hasRemaining())
        catalog
    }.getOrNull()
}
