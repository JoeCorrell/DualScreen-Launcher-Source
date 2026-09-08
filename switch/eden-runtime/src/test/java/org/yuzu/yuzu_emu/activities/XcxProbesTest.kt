package org.yuzu.yuzu_emu.activities

import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.*
import org.junit.Test

class XcxProbesTest {
    @Test fun installAvailabilityFollowsLiveStockBalanceAndPendingState() {
        val item = XcxProbes.Item(2, 1000, "Mining Probe G1", "")
        val stock = mapOf(2 to XcxProbes.Stock(3, 2))
        assertEquals(XcxProbes.Availability.Ready, XcxProbes.availability(1, item, stock, 1000, false))
        assertEquals(XcxProbes.Availability.InsufficientCredits, XcxProbes.availability(1, item, stock, 999, false))
        assertEquals(XcxProbes.Availability.BalanceUnavailable, XcxProbes.availability(1, item, stock, null, false))
        assertEquals(XcxProbes.Availability.NoStock,
            XcxProbes.availability(1, item, mapOf(2 to XcxProbes.Stock(2, 2)), 1000, false))
        assertEquals(XcxProbes.Availability.InventoryUnavailable, XcxProbes.availability(1, item, null, 1000, false))
        assertEquals(XcxProbes.Availability.Updating, XcxProbes.availability(1, item, stock, 1000, true))
        assertEquals(XcxProbes.Availability.Installed, XcxProbes.availability(2, item, stock, 1000, false))
        assertEquals(XcxProbes.Availability.NotDeployed, XcxProbes.availability(0, item, stock, 1000, false))
        assertEquals(XcxProbes.Availability.NoSelection, XcxProbes.availability(1, null, stock, 1000, false))
        val basic = XcxProbes.Item(1, 0, "Basic Probe", "")
        assertEquals(XcxProbes.Availability.Ready, XcxProbes.availability(2, basic, null, null, false))
    }
    private fun packet(): ByteArray {
        val name = "Mining Probe G1".toByteArray()
        val caption = "Increases mining output.".toByteArray()
        return ByteBuffer.allocate(28 + name.size + caption.size).order(ByteOrder.LITTLE_ENDIAN)
            .putInt(0x52504358).putInt(1).putInt(1).putInt(2).putInt(1000)
            .putInt(name.size).put(name).putInt(caption.size).put(caption).array()
    }
    @Test fun nativeItemAndPriceSurviveMetadataDecode() {
        val item = XcxProbes.parse(packet())!!.items.single()
        assertEquals(2, item.id); assertEquals(1000L, item.cost)
        assertEquals("Mining Probe G1", item.name)
        assertEquals("Increases mining output.", item.description)
    }
    @Test fun rejectsTruncationDuplicateItemsAndInvalidIds() {
        val bytes = packet()
        for (size in 0 until bytes.size) assertNull(XcxProbes.parse(bytes.copyOf(size)))
        assertNull(XcxProbes.parse(bytes + byteArrayOf(0)))
        val duplicate = bytes + bytes.copyOfRange(12, bytes.size)
        ByteBuffer.wrap(duplicate).order(ByteOrder.LITTLE_ENDIAN).putInt(8, 2)
        assertNull(XcxProbes.parse(duplicate))
        ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).putInt(12, 40)
        assertNull(XcxProbes.parse(bytes))
    }
    @Test fun actionUsesSiteAndItemIdsWithNoTruncation() {
        assertEquals(0x27008f, XcxProbes.argument(143, 39))
        assertEquals(0x10001, XcxProbes.argument(1, 1))
        for (site in listOf(-1, 0, 144, 65537)) assertNull(XcxProbes.argument(site, 1))
        for (item in listOf(-1, 0, 40, 65537)) assertNull(XcxProbes.argument(1, item))
    }
    @Test fun availableStockSubtractsAllInstalledProbesAndRejectsCorruption() {
        val bytes = ByteArray(39 * 8)
        val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        b.putInt(8, 5).putInt(12, 3)
        assertEquals(2, XcxProbes.inventory(bytes)!![2]!!.available)
        b.putInt(12, 6)
        assertEquals(0, XcxProbes.inventory(bytes)!![2]!!.available)
        b.putInt(12, 144)
        assertNull(XcxProbes.inventory(bytes))
        assertNull(XcxProbes.inventory(bytes.copyOf(bytes.size - 1)))
    }
}
