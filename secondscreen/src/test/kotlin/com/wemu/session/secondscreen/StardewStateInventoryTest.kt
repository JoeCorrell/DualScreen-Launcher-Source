package com.wemu.session.secondscreen

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class StardewStateInventoryTest {
    @Test
    fun `sparse inventory still exposes every unlocked slot`() {
        val state = StardewState(
            capacity = 36,
            items = listOf(
                StardewItem(
                    slot = 0,
                    id = "tool",
                    name = "Watering Can",
                    water = 27,
                    waterMax = 40,
                ),
                StardewItem(slot = 35, id = "food", name = "Cheese"),
            ),
        )

        assertEquals(36, state.inventorySlots.size)
        assertEquals(12, state.hotbarSlots.size)
        assertEquals(24, state.backpackSlots.size)
        assertEquals("tool", state.itemAt(0).id)
        assertTrue(state.itemAt(0).wateringCan)
        assertEquals(27, state.itemAt(0).water)
        assertEquals("food", state.itemAt(35).id)
        assertTrue(state.itemAt(20).empty)
    }

    @Test
    fun `modded backpack capacities are not capped at vanilla size`() {
        val state = StardewState(capacity = 48)

        assertEquals(48, state.inventorySlots.size)
        assertEquals(36, state.backpackSlots.size)
        assertEquals(47, state.backpackSlots.last().slot)
    }

    @Test
    fun `reported item rows can extend a stale capacity value`() {
        val state = StardewState(
            capacity = 36,
            items = listOf(StardewItem(slot = 47, id = "modded-slot")),
        )

        assertEquals(48, state.inventoryCapacity)
        assertEquals(36, state.backpackSlots.size)
        assertEquals("modded-slot", state.itemAt(47).id)
    }

    @Test
    fun `live inventory details keep water and weapon cooldown`() {
        val message = parseGameMessage(
            """{"type":"sdv_inventory","selected":2,"capacity":36,"items":[""" +
                """{"slot":0,"id":"can","water":23,"waterMax":40,"bottomless":"0"},""" +
                """{"slot":2,"id":"sword","cooldownMs":1250,"cooldownMax":1500}]}""",
        ) as StardewInventoryMessage

        assertEquals(23, message.items[0].water)
        assertTrue(message.items[0].wateringCan)
        assertEquals(1250, message.items[1].cooldownMs)
        assertTrue(message.items[1].weapon)
    }

    @Test
    fun `weekday is part of the displayed date`() {
        val state = StardewState(season = "Spring", day = 8, year = 2, weekday = "Monday")

        assertEquals("Monday · Spring 8, Year 2", state.date)
    }

    @Test
    fun `toolbar row commands preserve direction`() {
        assertEquals("{\"type\":\"shift_toolbar\",\"value\":0}", Commands.shiftToolbar(false))
        assertEquals("{\"type\":\"shift_toolbar\",\"value\":1}", Commands.shiftToolbar(true))
    }

    @Test
    fun `reference backpack never draws more than three rows`() {
        assertEquals(1, referenceInventoryRows(12))
        assertEquals(2, referenceInventoryRows(24))
        assertEquals(3, referenceInventoryRows(36))
        assertEquals(3, referenceInventoryRows(72))
    }

    @Test
    fun `hotbar labels match Stardew keyboard bindings`() {
        assertEquals("1", referenceHotbarLabel(0))
        assertEquals("9", referenceHotbarLabel(8))
        assertEquals("0", referenceHotbarLabel(9))
        assertEquals("-", referenceHotbarLabel(10))
        assertEquals("=", referenceHotbarLabel(11))
    }

    @Test
    fun `weapon cooldown gauge grows toward ready`() {
        assertEquals(0f, weaponCooldownReadyFraction(1500, 1500), 0f)
        assertEquals(0.5f, weaponCooldownReadyFraction(750, 1500), 0f)
        assertEquals(1f, weaponCooldownReadyFraction(0, 1500), 0f)
        assertEquals(0f, weaponCooldownReadyFraction(500, 0), 0f)
    }

    @Test
    fun `day message carries the farmer sidebar identity`() {
        val message = parseGameMessage(
            """{"type":"sdv_day","playerName":"Correll","farmName":"Correll","playerLevel":12}""",
        ) as StardewDayMessage

        assertEquals("Correll", message.playerName)
        assertEquals("Correll", message.farmName)
        assertEquals(12, message.playerLevel)
    }
}
