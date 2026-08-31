package com.wemu.session.secondscreen

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class StardewReferenceInsightsTest {
    @Test
    fun `progress overview uses live mine museum and economy values`() {
        val overview = stardewProgressOverview(
            StardewState(
                gold = 12_345,
                totalEarnings = 3_000_000_000L,
                mines = StardewMinesMessage(deepest = 43, skull = 17),
                collections = StardewCollectionsMessage(donated = 31, total = 95),
                shipping = StardewShippingMessage(total = 850),
            ),
        )

        assertEquals(43, overview.deepestMine)
        assertEquals(45, overview.nextMineMilestone)
        assertEquals(17, overview.skullFloor)
        assertEquals(64, overview.museumRemaining)
        assertEquals(32, overview.museumPercent)
        assertEquals(850, overview.shippingValue)
        assertEquals(12_345, overview.walletGold)
        assertEquals(3_000_000_000L, overview.lifetimeEarnings)
    }

    @Test
    fun `mine elevator milestone advances and stops at the bottom`() {
        assertEquals(5, nextMineElevatorFloor(0))
        assertEquals(5, nextMineElevatorFloor(4))
        assertEquals(10, nextMineElevatorFloor(5))
        assertEquals(120, nextMineElevatorFloor(119))
        assertNull(nextMineElevatorFloor(120))
    }

    @Test
    fun `shipping rows merge duplicate live bin stacks and sort by value`() {
        val rows = aggregateProgressShipments(
            listOf(
                StardewShipped("(O)24", "Parsnip", count = 2, worth = 70),
                StardewShipped("(O)188", "Green Bean", count = 1, worth = 40),
                StardewShipped("(O)24", "Parsnip", count = 3, worth = 105),
            ),
        )

        assertEquals(listOf("Parsnip", "Green Bean"), rows.map { it.name })
        assertEquals(5, rows.first().count)
        assertEquals(175, rows.first().worth)
    }

    @Test
    fun `recent history keeps seven days and labels wallet movement as net change`() {
        val records = (1..9).map { day ->
            StardewDayRecord(
                label = "Sp $day",
                gold = 100 * day,
                shipped = 75 * day,
            )
        }

        val recent = stardewRecentHistory(records, currentWallet = 1_250)

        assertEquals((3..9).map { "Sp $it" }, recent.days.map { it.label })
        assertEquals(950L, recent.walletNetChange)
        assertEquals(675, recent.peakBinValue)
    }

    @Test
    fun `recent history handles no records without inventing a change`() {
        val recent = stardewRecentHistory(emptyList(), currentWallet = 45_000)

        assertEquals(emptyList<StardewDayRecord>(), recent.days)
        assertEquals(0L, recent.walletNetChange)
        assertEquals(0, recent.peakBinValue)
    }

    @Test
    fun `compact progress money preserves sign and readable units`() {
        assertEquals("0g", compactProgressGold(0))
        assertEquals("+950g", compactProgressGold(950, signed = true))
        assertEquals("−12.3k", compactProgressGold(-12_345, signed = true))
        assertEquals("2.5m", compactProgressGold(2_500_000))
    }

    @Test
    fun `progress marks a sender capped shipping breakdown as partial`() {
        val state = StardewState(
            shipping = StardewShippingMessage(
                total = 1_200,
                items = (1..12).map { index ->
                    StardewShipped("item-$index", "Item $index", count = 1, worth = 100)
                },
            ),
        )

        assertEquals(true, stardewProgressOverview(state).shippingMayBePartial)
    }

    @Test
    fun `gift guide joins relationships case insensitively and prioritizes carried matches`() {
        val abigail = StardewVillager(name = "Abigail", npc = "Abigail", hearts = 6)
        val entries = stardewGiftGuideEntries(
            gifts = listOf(
                StardewGiftVillager(
                    name = "Sebastian",
                    loves = listOf(StardewBundleItem("(O)80", "Quartz")),
                ),
                StardewGiftVillager(
                    name = "abigail",
                    carrying = true,
                    loves = listOf(
                        StardewBundleItem("(O)66", "Amethyst", carried = true),
                        StardewBundleItem("(O)66", "Amethyst", carried = true),
                    ),
                ),
            ),
            villagers = listOf(abigail),
        )

        assertEquals(listOf("abigail", "Sebastian"), entries.map { it.name })
        assertEquals(1, entries.first().readyCount)
        assertEquals(1, entries.first().gifts.size)
        assertEquals(abigail, entries.first().villager)
    }

    @Test
    fun `weekly gift cue reports only received relationship data`() {
        assertEquals("—", giftWeeklyDetail(null))
        assertEquals("0/2", giftWeeklyDetail(StardewVillager(name = "Leah")))
        assertEquals(
            "2/2",
            giftWeeklyDetail(StardewVillager(name = "Leah", giftsThisWeek = 2)),
        )
    }
}
