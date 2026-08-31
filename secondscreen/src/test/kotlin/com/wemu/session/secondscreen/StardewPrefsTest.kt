package com.wemu.session.secondscreen

import org.junit.Assert.assertEquals
import org.junit.Test

class StardewPrefsTest {
    @Test
    fun `new background tiles default to the smallest scale`() {
        assertEquals(1, StardewPrefsState().groundZoom)
        assertEquals(1, normalizeGroundZoom(-10))
        assertEquals(4, normalizeGroundZoom(20))
    }

    @Test
    fun `legacy background scale is reduced without becoming invalid`() {
        assertEquals(1, migrateLegacyGroundZoom(1))
        assertEquals(1, migrateLegacyGroundZoom(3))
        assertEquals(2, migrateLegacyGroundZoom(4))
        assertEquals(3, migrateLegacyGroundZoom(8))
    }

    @Test
    fun `ground choices are grouped in useful game order`() {
        val groups = groundChoiceGroups(
            listOf(
                "ui:ground:winter:0:0",
                "ui:ground:summer:0:0",
                "ui:ground:walls_and_floors:16:0",
                "ui:ground:spring:0:0",
                "ui:ground:fall:0:0",
                "ui:ground:summer:0:0",
                "ui:ground:custom_sheet:0:0",
            ),
        )

        assertEquals(
            listOf(
                "Interior floors",
                "Spring ground",
                "Summer ground",
                "Fall ground",
                "Winter ground",
                "Other game tiles",
            ),
            groups.map { it.title },
        )
        assertEquals(1, groups.single { it.title == "Summer ground" }.ids.size)
    }

    @Test
    fun `late alert time is constrained to Stardew hour steps`() {
        assertEquals(2200, normalizeLateNightAlertTime(1800))
        assertEquals(2300, normalizeLateNightAlertTime(2399))
        assertEquals(2500, normalizeLateNightAlertTime(2700))
        assertEquals("10:00 pm", formatStardewTime(2200))
        assertEquals("12:00 am", formatStardewTime(2400))
        assertEquals("1:00 am", formatStardewTime(2500))
    }
}
