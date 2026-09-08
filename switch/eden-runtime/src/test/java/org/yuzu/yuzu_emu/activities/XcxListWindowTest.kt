package org.yuzu.yuzu_emu.activities

import org.junit.Assert.*
import org.junit.Test

class XcxListWindowTest {
    @Test fun everyMissionIsReachableAndControllerSelectionRemainsVisible() {
        val window = XcxListWindow(7)
        for (selected in 0 until 40) {
            window.reconcile(40, selected)
            assertTrue(selected in window.first until window.first + 7)
            assertEquals(selected, window.index(selected - window.first, 40))
        }
        window.reconcile(40, 0)
        assertEquals(0, window.first)
    }

    @Test fun completedMissionsClampTheWindowAndEmptyRowsNeverResolve() {
        val window = XcxListWindow(7)
        window.reconcile(40, 39)
        window.reconcile(3, 1)
        assertEquals(0, window.first)
        assertNull(window.index(3, 3))
        assertNull(window.index(-1, 3))
        assertNull(window.index(7, 40))
        window.reconcile(0, -1)
        assertNull(window.index(0, 0))
        assertEquals(-1, window.movePage(0, 1))
    }

    @Test fun pageButtonsReachLastPartialPageAndDoNotWrapUnexpectedly() {
        val window = XcxListWindow(7)
        assertEquals(7, window.movePage(17, 1))
        assertEquals(10, window.movePage(17, 1))
        assertEquals(10, window.movePage(17, 1))
        assertEquals(16, window.index(6, 17))
        assertEquals(3, window.movePage(17, -1))
        assertEquals(0, window.movePage(17, -1))
        assertEquals(0, window.movePage(17, -1))
    }
}
