package org.yuzu.yuzu_emu.activities

import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.*
import org.junit.Test

class XcxMapAtlasTest {
    private fun fixture(): ByteArray {
        val b = ByteBuffer.allocate(4096).order(ByteOrder.LITTLE_ENDIAN)
        b.putInt(0x534C5458).putInt(1).putInt(2)
        // Native NLA constructor 0x89277C, sourced from UI.bin.
        for (v in listOf(20000f, 20000f, 37200f, 37200f, 6540f, 16206f)) b.putFloat(v)
        fun string(value: String) { val bytes = value.toByteArray(); b.putInt(bytes.size).put(bytes) }
        for ((flag, name) in listOf(0 to "map02", 30 to "map02_ex01",
                8396 to "map02_ex02", 0 to "", 0 to "")) {
            b.putInt(flag); string(name)
        }
        b.putInt(108) // Three 2x2 sparse planes, each with its own 32-byte header.
        repeat(3) { plane ->
            b.putInt(256).putInt(256).putInt(2).putInt(2)
            b.putShort(152).putShort(152)
            repeat(3) { b.putInt(0) }
            b.put(byteArrayOf(1, if (plane == 0) 1 else 0, 0, 0))
        }
        b.putInt(1).putInt(1401)
        for (v in listOf(-2835f, 9466f, 1564f, 2396f, 28422f, 20000f)) b.putFloat(v)
        string("Administrative District")
        return b.array().copyOf(b.position())
    }

    @Test fun tileCoordinatesAreNotZoneIdsAndStoryPlanesFollowNativePrecedence() {
        val atlas = XcxMapAtlas.parse(fixture())!!
        assertEquals(360, atlas.width)
        assertEquals("map02_0100", atlas.tileName(1, 0, emptySet()))
        assertEquals("map02_ex01_0000", atlas.tileName(0, 0, setOf(30)))
        assertEquals("map02_ex01_ex02_0000", atlas.tileName(0, 0, setOf(30, 8396)))
        assertNull(atlas.tileName(0, 1, emptySet()))
        assertNull(atlas.tileName(-1, 0, emptySet()))
        assertNull(atlas.tileName(2, 0, emptySet()))
    }

    @Test fun realNlaLocationUsesNativeOffsetAndScale() {
        val p = XcxMapAtlas.parse(fixture())!!.position(-1436.5600586f, 7315.1621094f)
        assertEquals(3763.5983f, p.first, .005f)
        assertEquals(2063.0415f, p.second, .005f)
    }

    @Test fun malformedDefinitionsCannotAllocateOrIndexAnUnboundedTileGrid() {
        val good = fixture()
        for (size in 0 until good.size) assertNull(XcxMapAtlas.parse(good.copyOf(size)))
        assertNull(XcxMapAtlas.parse(good + 0))
        val invalid = good.copyOf()
        ByteBuffer.wrap(invalid).order(ByteOrder.LITTLE_ENDIAN).putFloat(12, Float.NaN)
        assertNull(XcxMapAtlas.parse(invalid))
    }
}
