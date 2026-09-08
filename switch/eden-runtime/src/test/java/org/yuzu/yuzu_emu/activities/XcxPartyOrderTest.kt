package org.yuzu.yuzu_emu.activities

import org.junit.Assert.*
import org.junit.Test

class XcxPartyOrderTest {
    private val party = intArrayOf(0, 11, 12, 13)

    @Test fun capturesBothOccupantsForNativeStaleSelectionCheck() {
        assertEquals(0x0c0b0201, XcxPartyOrder.argument(1, 2, 15, party))
        assertEquals(0x0b0c0102, XcxPartyOrder.argument(2, 1, 15, party))
        val changed = party.copyOf().also { it[2] = 14 }
        assertNotEquals(XcxPartyOrder.argument(1, 2, 15, party),
            XcxPartyOrder.argument(1, 2, 15, changed))
    }

    @Test fun excludesLeaderEmptySlotsAndInvalidRecords() {
        for (slot in listOf(-1, 0, 4, 256)) {
            assertNull(XcxPartyOrder.argument(slot, 2, 15, party))
            assertNull(XcxPartyOrder.argument(1, slot, 15, party))
        }
        assertNull(XcxPartyOrder.argument(1, 1, 15, party))
        for (mask in listOf(0, 14, 13, 11)) assertNull(XcxPartyOrder.argument(1, 2, mask, party))
        assertNull(XcxPartyOrder.argument(1, 2, 15, intArrayOf(0, 11, 11, 13)))
        assertNull(XcxPartyOrder.argument(1, 2, 15, intArrayOf(0, 11, 24, 13)))
        assertNull(XcxPartyOrder.argument(1, 2, 15, party.copyOf(3)))
    }
}
