package org.yuzu.yuzu_emu.activities

import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Reads literal labels only. Tagged/substituted strings are deliberately not objectives. */
object SmoMessages {
    fun decode(data: ByteArray): Map<String, String> = runCatching {
        require(data.size in 32..524288 && String(data, 0, 8, Charsets.US_ASCII) == "MsgStdBn")
        val order = when {
            data[8] == 0xff.toByte() && data[9] == 0xfe.toByte() -> ByteOrder.LITTLE_ENDIAN
            data[8] == 0xfe.toByte() && data[9] == 0xff.toByte() -> ByteOrder.BIG_ENDIAN
            else -> error("Invalid byte order")
        }
        val b = ByteBuffer.wrap(data).order(order)
        val encoding = data[12].toInt()
        require(encoding in 0..1 && b.getInt(18) == data.size)
        val width = encoding + 1
        val charset = if (width == 1) Charsets.UTF_8 else if (order == ByteOrder.LITTLE_ENDIAN)
            Charsets.UTF_16LE else Charsets.UTF_16BE
        val sections = mutableMapOf<String, ByteBuffer>()
        var cursor = 32
        val count = b.getShort(14).toInt() and 65535
        require(count <= 32)
        repeat(count) {
            require(cursor <= data.size - 16)
            val name = String(data, cursor, 4, Charsets.US_ASCII)
            val size = b.getInt(cursor + 4)
            require(size >= 0 && size <= data.size - cursor - 16 && name !in sections)
            sections[name] = ByteBuffer.wrap(data.copyOfRange(cursor + 16, cursor + 16 + size)).order(order)
            cursor = (cursor + 16 + size + 15) and -16
        }
        val labels = sections["LBL1"] ?: error("Missing labels")
        val texts = sections["TXT2"] ?: error("Missing texts")
        val textCount = texts.getInt(0)
        require(textCount in 0..10000 && textCount <= (texts.capacity() - 4) / 4)
        val offsets = List(textCount) { texts.getInt(4 + it * 4) }
        require(offsets.all { it >= 4 + textCount * 4 && it < texts.capacity() } &&
            offsets.zipWithNext().all { it.first <= it.second })
        val groups = labels.getInt(0)
        require(groups in 0..10000 && groups <= (labels.capacity() - 4) / 8)
        val result = mutableMapOf<String, String>()
        var totalLabels = 0
        repeat(groups) { group ->
            val entries = labels.getInt(4 + group * 8)
            var pos = labels.getInt(8 + group * 8)
            require(entries >= 0 && entries <= 10000 - totalLabels)
            totalLabels += entries
            repeat(entries) {
                val length = labels.get(pos).toInt() and 255
                require(length > 0 && pos + length + 5 <= labels.capacity())
                val name = ByteArray(length) { labels.get(pos + 1 + it) }.toString(Charsets.UTF_8)
                val index = labels.getInt(pos + 1 + length)
                pos += length + 5
                require(index in offsets.indices && name !in result)
                val start = offsets[index]
                val end = offsets.getOrNull(index + 1) ?: texts.capacity()
                var unitPos = start
                var literal = true
                var terminated = false
                while (unitPos + width <= end) {
                    val unit = if (width == 1) texts.get(unitPos).toInt() and 255
                        else texts.getShort(unitPos).toInt() and 65535
                    if (unit == 0) { terminated = true; break }
                    if (unit == 14 || unit == 15) { literal = false; break }
                    unitPos += width
                }
                if (literal && terminated) result[name] = ByteArray(unitPos - start) {
                    texts.get(start + it)
                }.toString(charset).replace("\r", "")
            }
        }
        result.toMap()
    }.getOrDefault(emptyMap())
}
