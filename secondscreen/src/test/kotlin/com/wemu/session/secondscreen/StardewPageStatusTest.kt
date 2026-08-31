package com.wemu.session.secondscreen

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class StardewPageStatusTest {
    @Test
    fun pageArrowNavigationWrapsAtBothEnds() {
        assertEquals(11, wrappedPageIndex(page = 0, delta = -1, pageCount = 12))
        assertEquals(0, wrappedPageIndex(page = 11, delta = 1, pageCount = 12))
        assertEquals(3, wrappedPageIndex(page = 2, delta = 1, pageCount = 12))
    }

    @Test
    fun pageRailKeepsAFullThreeButtonWindowAtBothEnds() {
        assertEquals(0, pageWindowStart(page = 0, pageCount = 12))
        assertEquals(0, pageWindowStart(page = 1, pageCount = 12))
        assertEquals(4, pageWindowStart(page = 5, pageCount = 12))
        assertEquals(9, pageWindowStart(page = 11, pageCount = 12))
    }

    @Test
    fun todayDoesNotShowDefaultVitalsAsUrgentWhileWaiting() {
        val status = pageStatus(StardewPage.TODAY, StardewState())

        assertEquals("Waiting", status.detail)
        assertFalse(status.urgent)
    }

    @Test
    fun todayHighlightsLowVitalsInAWorld() {
        val status = pageStatus(
            StardewPage.TODAY,
            StardewState(
                season = "Spring",
                energy = 25,
                maxEnergy = 100,
                health = 80,
                maxHealth = 100,
            ),
        )

        assertEquals("6:00am", status.detail)
        assertTrue(status.urgent)
    }

    @Test
    fun machineStatusOnlyCombinesMachinesTreesAndAnimals() {
        val state = StardewState(
            crops = listOf(StardewCrop("Parsnip", needsWater = true)),
            machines = listOf(StardewMachine("keg", "Keg", ready = true)),
            animals = listOf(
                StardewAnimal("Bessie", petted = false),
                StardewAnimal("Clucky", petted = true, produce = "Egg"),
            ),
            trees = listOf(StardewTree(id = "apple", location = "Farm", count = 2)),
        )

        val status = pageStatus(StardewPage.MACHINES, state)

        assertEquals("5 ready", status.detail)
        assertFalse(status.urgent)
        assertTrue(status.ready)
    }

    @Test
    fun thirstyCropsDoNotMakeTheMachinesTabUrgent() {
        val status = pageStatus(
            StardewPage.MACHINES,
            StardewState(crops = listOf(StardewCrop("Parsnip", needsWater = true))),
        )

        assertEquals("All clear", status.detail)
        assertFalse(status.urgent)
        assertFalse(status.ready)
    }

    @Test
    fun bundlesPreferItemsReadyToDonate() {
        val state = StardewState(
            bundles = listOf(
                StardewBundle(
                    room = "Pantry",
                    name = "Spring Crops",
                    required = 4,
                    missing = listOf(
                        StardewBundleItem("24", "Parsnip", carried = true),
                    ),
                ),
            ),
        )

        val status = pageStatus(StardewPage.BUNDLES, state)

        assertEquals("1 to donate", status.detail)
        assertFalse(status.urgent)
        assertTrue(status.ready)
    }

    @Test
    fun calendarCallsOutTodaysBirthday() {
        val state = StardewState(
            villagers = listOf(StardewVillager(name = "Abigail", birthdayIn = 0)),
        )

        val status = pageStatus(StardewPage.CALENDAR, state)

        assertEquals("Abigail's birthday", status.detail)
        assertTrue(status.urgent)
    }

    @Test
    fun progressPrefersMuseumCompletionThenMineDepth() {
        val museum = pageStatus(
            StardewPage.PROGRESS,
            StardewState(collections = StardewCollectionsMessage(donated = 63, total = 95)),
        )
        val mine = pageStatus(
            StardewPage.PROGRESS,
            StardewState(mines = StardewMinesMessage(deepest = 85)),
        )

        assertEquals("63/95 museum", museum.detail)
        assertEquals("Mine level 85", mine.detail)
    }

    @Test
    fun giftGuideHighlightsLovedItemsAlreadyCarried() {
        val status = pageStatus(
            StardewPage.GIFT_GUIDE,
            StardewState(
                gifts = listOf(
                    StardewGiftVillager(
                        name = "Leah",
                        carrying = true,
                        loves = listOf(StardewBundleItem("426", "Goat Cheese")),
                    ),
                ),
            ),
        )

        assertEquals("1 gift in bag", status.detail)
        assertFalse(status.urgent)
        assertTrue(status.ready)
    }

    @Test
    fun fieldsCallsOutWateringBeforeReadyHarvests() {
        val status = pageStatus(
            StardewPage.FIELDS,
            StardewState(
                crops = listOf(
                    StardewCrop("Blueberry", ready = true),
                    StardewCrop("Melon", needsWater = true),
                ),
            ),
        )

        assertEquals("1 need water", status.detail)
        assertFalse(status.urgent)
        assertTrue(status.ready)
    }
}
