package org.yuzu.yuzu_emu.activities

import java.nio.ByteBuffer
import java.nio.ByteOrder

/** ITM_BeaconList item IDs, not the probe category/grade shown in the UI. */
internal data class XcxProbes(val items: List<Item>) {
    data class Item(val id: Int, val cost: Long, val name: String, val description: String)
    data class Stock(val owned: Int, val installed: Int) {
        val available: Int get() = (owned - installed).coerceAtLeast(0)
    }
    enum class Availability {
        Ready, Updating, NotDeployed, NoSelection, Installed, InventoryUnavailable,
        NoStock, BalanceUnavailable, InsufficientCredits
    }

    companion object {
        fun availability(current: Int, item: Item?, stock: Map<Int, Stock>?,
                         credits: Long?, updating: Boolean): Availability = when {
            updating -> Availability.Updating
            current !in 1..39 -> Availability.NotDeployed
            item == null || item.id !in 1..39 -> Availability.NoSelection
            item.id == current -> Availability.Installed
            item.id != 1 && stock == null -> Availability.InventoryUnavailable
            item.id != 1 && (stock?.get(item.id)?.available ?: 0) <= 0 -> Availability.NoStock
            item.cost > 0 && (credits == null || credits < 0) -> Availability.BalanceUnavailable
            item.cost > (credits ?: 0) -> Availability.InsufficientCredits
            else -> Availability.Ready
        }
        fun inventory(bytes: ByteArray): Map<Int, Stock>? = runCatching {
            require(bytes.size == 39 * 8)
            val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            (1..39).associateWith {
                val owned = b.int; val installed = b.int
                require(owned in 0..255 && installed in 0..143)
                Stock(owned, installed)
            }
        }.getOrNull()
        fun argument(site: Int, item: Int): Int? =
            if (site in 1..143 && item in 1..39) (item shl 16) or site else null

        fun parse(bytes: ByteArray): XcxProbes? = runCatching {
            val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            require(b.int == 0x52504358 && b.int == 1)
            val count = b.int
            require(count in 1..39)
            fun string(): String {
                val size = b.int
                require(size in 0..16384 && size <= b.remaining())
                return ByteArray(size).also { b.get(it) }.toString(Charsets.UTF_8)
            }
            val items = List(count) {
                val id = b.int
                val cost = b.int.toLong() and 0xffffffffL
                require(id in 1..39)
                Item(id, cost, string(), string())
            }
            require(items.map { it.id }.distinct().size == count && !b.hasRemaining())
            XcxProbes(items)
        }.getOrNull()
    }
}
