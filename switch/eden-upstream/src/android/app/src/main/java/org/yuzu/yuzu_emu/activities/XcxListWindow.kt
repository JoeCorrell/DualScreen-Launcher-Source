package org.yuzu.yuzu_emu.activities

/** Keeps a selected live record visible without confusing a row index with its identity. */
internal class XcxListWindow(private val capacity: Int) {
    init { require(capacity > 0) }
    var first = 0
        private set

    fun reconcile(size: Int, selectedIndex: Int) {
        first = first.coerceIn(0, (size - capacity).coerceAtLeast(0))
        if (selectedIndex !in 0 until size) return
        if (selectedIndex < first) first = selectedIndex
        if (selectedIndex >= first + capacity) first = selectedIndex - capacity + 1
    }

    fun movePage(size: Int, direction: Int): Int {
        first = (first + direction.coerceIn(-1, 1) * capacity)
            .coerceIn(0, (size - capacity).coerceAtLeast(0))
        return if (size == 0) -1 else first
    }

    fun index(row: Int, size: Int): Int? = (first + row)
        .takeIf { row in 0 until capacity && it in 0 until size }
}
