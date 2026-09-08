package org.yuzu.yuzu_emu.activities

import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.*
import org.junit.Test

class XcxEquipmentTest {
    private fun inventory(): ByteArray = ByteBuffer.allocate(48).order(ByteOrder.LITTLE_ENDIAN)
        .putInt(0x49455858).putInt(1).putInt(6).putInt(2)
        .putInt(51).putInt(0xF1234506.toInt()).putInt(51 or (6 shl 13)).putInt(0x03C48D14)
        .putInt(51).putInt(0xF1234546.toInt()).putInt(51 or (6 shl 13)).putInt(0x03C48D15)
        .array()

    @Test fun inventoryKeepsDistinctInstancesOfTheSameItem() {
        val value = XcxEquipment.inventory(inventory(), 6)!!
        assertEquals(listOf(51, 51), value.items.map { it.id })
        assertEquals(listOf(0xF1234506L, 0xF1234546L), value.items.map { it.instance })
        assertNull(XcxEquipment.inventory(inventory(), 7))
    }

    @Test fun inventoryRejectsDuplicateKeysAndMismatchedIdentity() {
        val bytes = inventory()
        for (size in 0 until bytes.size) assertNull(XcxEquipment.inventory(bytes.copyOf(size), 6))
        assertNull(XcxEquipment.inventory(bytes + byteArrayOf(0), 6))
        for ((offset, value) in listOf(12 to 4097, 16 to 52, 20 to 0xF1234507.toInt(),
            28 to 0, 36 to 0xF1234506.toInt())) {
            val bad = bytes.copyOf()
            ByteBuffer.wrap(bad).order(ByteOrder.LITTLE_ENDIAN).putInt(offset, value)
            assertNull("offset $offset", XcxEquipment.inventory(bad, 6))
        }
    }
    private fun frame(): ByteArray {
        val b = ByteBuffer.allocate(208).order(ByteOrder.LITTLE_ENDIAN)
            .putInt(0x51455858).putInt(1).putInt(11).putInt(12)
        repeat(12) { slot ->
            b.putInt(slot).putInt(XcxEquipment.categories[slot]).putInt(if (slot == 0) 51 else 0)
                .putInt(if (slot == 0) 0xF1234506.toInt() else 0)
        }
        return b.array()
    }

    @Test fun preservesInstanceIdentityAndLiveEquipmentChanges() {
        val bytes = frame()
        val before = XcxEquipment.frame(bytes, 11)!!
        assertEquals(0xF1234506L, before.items[0].instance)
        assertEquals(51, before.items[0].id)
        ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).putInt(24, 52)
        assertEquals(52, XcxEquipment.frame(bytes, 11)!!.items[0].id)
        assertEquals(51, before.items[0].id)
        assertNull(XcxEquipment.frame(bytes, 12))
    }

    @Test fun rejectsTruncatedAndMalformedNativeFrames() {
        val bytes = frame()
        for (size in 0 until bytes.size) assertNull(XcxEquipment.frame(bytes.copyOf(size), 11))
        assertNull(XcxEquipment.frame(bytes + byteArrayOf(0), 11))
        for ((offset, value) in listOf(0 to 0, 4 to 2, 12 to 13, 16 to 1, 20 to 7, 24 to 8192, 24 to -1, 24 to 0)) {
            val bad = bytes.copyOf()
            ByteBuffer.wrap(bad).order(ByteOrder.LITTLE_ENDIAN).putInt(offset, value)
            assertNull("offset $offset value $value", XcxEquipment.frame(bad, 11))
        }
    }

    @Test fun catalogSeparatesWeaponAndArmorIds() {
        val b = ByteBuffer.allocate(1024).order(ByteOrder.LITTLE_ENDIAN)
            .putInt(0x4E514558).putInt(1).putInt(12)
        fun text(value: String) { val bytes = value.toByteArray(); b.putInt(bytes.size).put(bytes) }
        repeat(12) { text("Part $it") }
        b.putInt(1).putInt(51); text("Weapon")
        b.putInt(1).putInt(51); text("Armor")
        val bytes = b.array().copyOf(b.position())
        val catalog = XcxEquipment.catalog(bytes)!!
        assertEquals("Weapon", catalog.name(XcxEquipment.Item(0, 6, 51, 0)))
        assertEquals("Armor", catalog.name(XcxEquipment.Item(2, 1, 51, 0)))
        assertEquals("Empty", catalog.name(XcxEquipment.Item(2, 1, 0, 0)))
        for (size in 0 until bytes.size) assertNull(XcxEquipment.catalog(bytes.copyOf(size)))
        assertNull(XcxEquipment.catalog(bytes + byteArrayOf(0)))
    }
}
