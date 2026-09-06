package com.dualscreen.launcher

import org.junit.Assert.*
import org.junit.Test
import org.yuzu.yuzu_emu.activities.*

class SmoWardrobeTest {
    private val state = SmoSnapshot(4, 7, health = 3, outfit = "Mario", hat = "MarioCaptain",
        stage = "HomeShipInsideStage")
    private val item = SmoCollectionItem(2, true, "MarioTailCoat", "")

    @Test fun onlyFreshOwnedDifferentWardrobeItemsCanBeQueued() {
        assertTrue(SmoWardrobe.canEquip(state, 0, 7, item, false))
        assertTrue(SmoWardrobe.canEquip(state, 1, 7, item, false))
        assertFalse(SmoWardrobe.canEquip(state, 0, 7, item, true))
        assertFalse(SmoWardrobe.canEquip(state, 0, 6, item, false))
        assertFalse(SmoWardrobe.canEquip(state, 0, null, item, false))
        assertFalse(SmoWardrobe.canEquip(state, 2, 7, item, false))
        assertFalse(SmoWardrobe.canEquip(state, 0, 7, item.copy(owned = false), false))
        assertFalse(SmoWardrobe.canEquip(state, 0, 7, item.copy(name = "Mario"), false))
        assertFalse(SmoWardrobe.canEquip(state, 1, 7, item.copy(name = "MarioCaptain"), false))
    }
    @Test fun transitionDeathAndUnavailableStagesAreDisabled() {
        assertFalse(SmoWardrobe.canEquip(state.copy(status = 5), 0, 7, item, false))
        assertFalse(SmoWardrobe.canEquip(state.copy(health = 0), 0, 7, item, false))
        assertFalse(SmoWardrobe.canEquip(state.copy(stage = ""), 0, 7, item, false))
    }
    @Test fun wardrobeIsNotRestrictedToTheOdysseyOrToMainKingdomMaps() {
        for (stage in listOf("SandWorldHomeStage", "ForestWorldHomeStage", "SandWorldShopStage", "TestSubStage")) {
            assertTrue(SmoWardrobe.canEquip(state.copy(stage = stage), 0, 7, item, false))
            assertTrue(SmoWardrobe.canEquip(state.copy(stage = stage), 1, 7, item, false))
        }
    }
    @Test fun touchMustBeginAndEndInsideVisibleEquipButton() {
        assertTrue(SmoWardrobe.equipTap(425f, 740f, 430f, 745f))
        assertFalse(SmoWardrobe.equipTap(425f, 700f, 425f, 740f))
        assertFalse(SmoWardrobe.equipTap(425f, 740f, 425f, 780f))
        assertFalse(SmoWardrobe.equipTap(740f, 740f, 740f, 740f))
    }
}
