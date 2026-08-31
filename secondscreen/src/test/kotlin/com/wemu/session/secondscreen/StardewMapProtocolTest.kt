package com.wemu.session.secondscreen

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class StardewMapProtocolTest {
    @Test
    fun `day message preserves lifetime earnings above int range`() {
        val message = parseGameMessage(
            """{"type":"sdv_day","totalEarnings":3000000000}""",
        ) as StardewDayMessage

        assertEquals(3_000_000_000L, message.totalEarnings)
    }

    @Test
    fun `missing lifetime earnings remains backwards compatible`() {
        val message = parseGameMessage("""{"type":"sdv_day"}""") as StardewDayMessage

        assertEquals(0L, message.totalEarnings)
    }

    @Test
    fun `villager message carries live map location`() {
        val message = parseGameMessage(
            """{"type":"sdv_villagers","villagers":[{"name":"Abigail","npc":"Abigail","location":"Pierre's General Store","mapX":412,"mapY":536}]}""",
        ) as StardewVillagersMessage
        val abigail = message.villagers.single()

        assertEquals("Pierre's General Store", abigail.location)
        assertTrue(abigail.onMap)
        assertEquals(0.412f, abigail.mapAcross, 0.0001f)
        assertEquals(0.536f, abigail.mapDown, 0.0001f)
    }

    @Test
    fun `missing villager position stays off the map`() {
        val message = parseGameMessage(
            """{"type":"sdv_villagers","villagers":[{"name":"Krobus"}]}""",
        ) as StardewVillagersMessage
        val krobus = message.villagers.single()

        assertEquals(-1, krobus.mapX)
        assertEquals(-1, krobus.mapY)
        assertFalse(krobus.onMap)
    }

    @Test
    fun `map edges are valid villager positions`() {
        assertTrue(StardewVillager(name = "Northwest", mapX = 0, mapY = 0).onMap)
        assertTrue(StardewVillager(name = "Southeast", mapX = 1000, mapY = 1000).onMap)
        assertFalse(StardewVillager(name = "Outside", mapX = 1001, mapY = 500).onMap)
    }

    @Test
    fun `map zoom and panning stay inside the viewport`() {
        assertEquals(1f, clampedMapZoom(0.2f), 0f)
        assertEquals(4f, clampedMapZoom(7f), 0f)
        assertEquals(0f, clampedMapTranslation(400f, 1f, 1000f), 0f)
        assertEquals(500f, clampedMapTranslation(900f, 2f, 1000f), 0f)
        assertEquals(-500f, clampedMapTranslation(-900f, 2f, 1000f), 0f)
    }

    @Test
    fun `villager focus and marker screen position use the same transform`() {
        val translation = focusedMapTranslation(fraction = 0.8f, zoom = 2f, extent = 1000f)

        assertEquals(-500f, translation, 0.001f)
        // The map cannot pan beyond its right edge, so an edge-adjacent marker stops
        // slightly off center while remaining aligned with the transformed map image.
        assertEquals(
            600f,
            mapMarkerScreenCoordinate(0.8f, 1000f, 2f, translation),
            0.001f,
        )
    }

    @Test
    fun `crafting message keeps actionable recipe details`() {
        val message = parseGameMessage(
            """{"type":"sdv_crafting","recipes":[{"id":"(BC)130","name":"Chest","key":"Chest","description":"Stores items.","ready":"0","crafted":4,"outputCount":1,"ingredients":[{"id":"(O)388","name":"Wood","have":23,"required":50}]}]}""",
        ) as StardewCraftingMessage
        val recipe = message.recipes.single()

        assertEquals("Chest", recipe.key)
        assertEquals("Stores items.", recipe.description)
        assertFalse(recipe.ready)
        assertEquals(4, recipe.crafted)
        assertEquals("Wood", recipe.ingredients.single().name)
        assertEquals(50, recipe.ingredients.single().required)
        assertEquals(
            "{\"type\":\"craft_recipe\",\"key\":\"Chest\"}",
            Commands.craftRecipe("Chest"),
        )
    }

    @Test
    fun `crafting command escapes recipe keys as JSON`() {
        assertEquals(
            "{\"type\":\"craft_recipe\",\"key\":\"Chest \\\"Deluxe\\\"\\\\Back\\nTab\\tEnd\"}",
            Commands.craftRecipe("Chest \"Deluxe\"\\Back\nTab\tEnd"),
        )
    }

    @Test
    fun `crafting send option follows the mod config contract`() {
        val disabled = parseGameMessage(
            """{"type":"sdv_config","sendCrafting":"0"}""",
        ) as StardewConfigMessage
        val booleanDisabled = parseGameMessage(
            """{"type":"sdv_config","sendCrafting":false}""",
        ) as StardewConfigMessage
        val legacy = parseGameMessage("""{"type":"sdv_config"}""") as StardewConfigMessage

        assertFalse(disabled.sendCrafting)
        assertFalse(booleanDisabled.sendCrafting)
        assertTrue(legacy.sendCrafting)
    }

    @Test
    fun `farm dashboard receives real game sprite identifiers`() {
        val crop = parseGameMessage(
            """{"type":"sdv_crops","crops":[{"id":"(O)24","name":"Parsnip"}]}""",
        ) as StardewCropsMessage
        val animals = parseGameMessage(
            """{"type":"sdv_animals","animals":[{"name":"Hennifer","type":"Chicken","spriteId":"animal:White Chicken"}]}""",
        ) as StardewAnimalsMessage

        assertEquals("(O)24", crop.crops.single().id)
        assertEquals("animal:White Chicken", animals.animals.single().spriteId)
    }

    @Test
    fun `fruit tree message includes the live fruit name`() {
        val message = parseGameMessage(
            """{"type":"sdv_trees","trees":[{"id":"(O)613","name":"Apple","location":"Farm","count":3}]}""",
        ) as StardewTreesMessage
        val tree = message.trees.single()

        assertEquals("Apple", tree.name)
        assertEquals("Farm", tree.location)
        assertEquals(3, tree.count)
    }

    @Test
    fun `bundle rooms use game order and aggregate progress`() {
        val rooms = referenceBundleRooms(
            listOf(
                StardewBundle("Vault", "25,000g", have = 0, required = 1),
                StardewBundle("Pantry", "Spring Crops", have = 2, required = 4),
                StardewBundle("Pantry", "Quality Crops", have = 1, required = 3),
                StardewBundle("Crafts Room", "Spring Foraging", have = 3, required = 4),
            ),
        )

        assertEquals(listOf("Crafts Room", "Pantry", "Vault"), rooms.map { it.name })
        assertEquals(3, rooms[1].have)
        assertEquals(7, rooms[1].required)
    }
}
