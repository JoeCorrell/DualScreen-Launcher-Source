package com.wemu.session.secondscreen

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class StardewReferenceFieldsTest {
    @Test
    fun `empty crops produce an honest empty summary`() {
        val summary = stardewFieldsSummary(emptyList())

        assertEquals(0, summary.total)
        assertEquals(0, summary.ready)
        assertEquals(0, summary.thirsty)
        assertEquals(0, summary.watered)
        assertTrue(summary.locations.isEmpty())
    }

    @Test
    fun `summary groups crop tiles by location status and identity`() {
        val summary = stardewFieldsSummary(
            listOf(
                StardewCrop("Parsnip", "(O)24", "", daysLeft = 2, needsWater = true),
                StardewCrop("Parsnip", "(O)24", "Farm", daysLeft = 4, needsWater = true),
                StardewCrop("Blueberry", "(O)258", "Farm", ready = true, needsWater = true),
                StardewCrop("Coffee Bean", "(O)433", "Greenhouse", daysLeft = 1),
            ),
        )

        assertEquals(4, summary.total)
        assertEquals(1, summary.ready)
        assertEquals(2, summary.thirsty)
        assertEquals(1, summary.watered)
        assertEquals(listOf("Farm", "Greenhouse"), summary.locations.map { it.name })

        val farm = summary.locations.first()
        assertEquals(3, farm.total)
        assertEquals(1, farm.ready)
        assertEquals(2, farm.thirsty)
        assertEquals(
            listOf(StardewFieldStatus.THIRSTY, StardewFieldStatus.READY),
            farm.groups.map { it.status },
        )

        val parsnips = farm.groups.first().rows.single()
        assertEquals(2, parsnips.count)
        assertEquals(2, parsnips.minimumDays)
        assertEquals(4, parsnips.maximumDays)
    }

    @Test
    fun `ready status takes precedence over dry soil`() {
        val crop = StardewCrop("Melon", ready = true, needsWater = true)

        assertEquals(StardewFieldStatus.READY, stardewFieldStatus(crop))
    }

    @Test
    fun `day labels cover exact ranges and harvest readiness`() {
        fun row(status: StardewFieldStatus, low: Int, high: Int) = StardewFieldCropRow(
            id = "crop",
            name = "Crop",
            status = status,
            count = 1,
            minimumDays = low,
            maximumDays = high,
        )

        assertEquals("Ready to harvest", fieldsCropDays(row(StardewFieldStatus.READY, 0, 0)))
        assertEquals("1 day remaining", fieldsCropDays(row(StardewFieldStatus.WATERED, 1, 1)))
        assertEquals("2–4 days remaining", fieldsCropDays(row(StardewFieldStatus.THIRSTY, 2, 4)))
        assertEquals(
            "Up to 3 days remaining",
            fieldsCropDays(row(StardewFieldStatus.WATERED, 0, 3)),
        )
    }
}
