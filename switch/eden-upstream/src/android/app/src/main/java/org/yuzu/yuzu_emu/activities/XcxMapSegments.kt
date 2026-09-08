package org.yuzu.yuzu_emu.activities

import java.nio.ByteBuffer
import java.nio.ByteOrder

internal data class XcxMapSegments(val fields: List<Field>, val icons: Map<Int, Int>,
                                  val questParents: Map<Int, Int> = emptyMap(),
                                  val details: Map<Int, Detail> = emptyMap()) {
    data class Detail(val title: String, val description: String)
    data class Field(val id: Int, val zone: Long, val x: Float, val z: Float, val spacing: Float) {
        fun position(column: Int, row: Int) = Pair(
            x + column * spacing * .75f,
            z + (row + if (column and 1 != 0) .5f else 0f) * spacing)
    }
    data class Cell(val field: Field, val column: Int, val row: Int, val kind: Int,
                    val id: Int, val openFlag: Int, val stateFlag: Int, val edges: Int,
                    val open: Boolean = false, val complete: Boolean = false,
                    val probeInstalled: Boolean = false, val probeId: Int = 0,
                    val probeItem: Int = 0, val questId: Int = 0,
                    val probeVisible: Boolean = true, val relatedVisible: Boolean = false,
                    val relatedId: Int = 0)

    fun cells(field: Field, bytes: ByteArray): List<Cell>? = runCatching {
        require(bytes.size % 48 == 0 && bytes.size <= 128 * 48)
        val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        (0 until bytes.size step 48).mapNotNull { at ->
            val column = b[at].toInt(); val row = b[at + 1].toInt()
            val kind = b[at + 10].toInt() and 255
            val installed = b.getInt(at + 36)
            val quest = b.getInt(at + 40)
            val visibility = b.getInt(at + 44)
            require(visibility in 0..7)
            require(quest in 0..2558 && (quest == 0 || kind == 2))
            require(installed in 0..39)
            require(b.getInt(at + 4) == field.id && column in -64..63 && row in -64..63)
            if (kind == 4) null else {
                require(kind in 1..3)
                Cell(field, column, row, kind, b.getShort(at + 12).toInt() and 65535,
                    b.getShort(at + 8).toInt() and 65535,
                    b.getShort(at + 18).toInt() and 65535, b[at + 33].toInt() and 63,
                    probeInstalled = installed != 0,
                    probeId = b.getShort(at + 16).toInt() and 65535,
                    probeItem = installed, questId = questParents[quest]?.takeIf { it != 0 } ?: quest,
                    probeVisible = (visibility and 3) == 1,
                    relatedVisible = (visibility and 4) != 0,
                    relatedId = b.getShort(at + 28).toInt() and 65535)
            }
        }
    }.getOrNull()

    companion object {
        fun parse(bytes: ByteArray): XcxMapSegments? = runCatching {
            val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            require(b.int == 0x4C475358 && b.int == 3)
            val count = b.int
            require(count in 1..64)
            val fields = List(count) {
                val id = b.int; val zone = b.int.toLong() and 0xffffffffL
                val x = b.float; val z = b.float; val spacing = b.float
                require(id in 1..64 && x.isFinite() && z.isFinite() && spacing.isFinite() && spacing > 0)
                Field(id, zone, x, z, spacing)
            }
            val iconCount = b.int
            require(iconCount in 0..8192)
            val icons = HashMap<Int, Int>(iconCount)
            repeat(iconCount) { val id = b.int; val icon = b.int; require(icon in 0..14); icons[id] = icon }
            val questCount = b.int
            require(questCount in 0..4096)
            val parents = HashMap<Int, Int>(questCount)
            repeat(questCount) {
                val id = b.int; val parent = b.int
                require(id in 1..2558 && parent in 0..2558 && id !in parents)
                parents[id] = parent
            }
            val detailCount = b.int
            require(detailCount in 0..8192)
            fun text(): String {
                val size = b.int
                require(size in 0..16384 && size <= b.remaining())
                return ByteArray(size).also { b.get(it) }.toString(Charsets.UTF_8)
            }
            val details = HashMap<Int, Detail>(detailCount)
            repeat(detailCount) {
                val id = b.int
                require(id in 1..65535 && id !in details && id in icons)
                details[id] = Detail(text(), text())
            }
            require(!b.hasRemaining())
            XcxMapSegments(fields, icons, parents, details)
        }.getOrNull()
    }
}
