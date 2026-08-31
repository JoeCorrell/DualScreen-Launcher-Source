package com.wemu.session.secondscreen

import androidx.compose.runtime.Immutable

@Immutable
data class StardewState(
    val connected: Boolean = false,
    val saveName: String = "",

    val season: String = "",
    val day: Int = 0,
    val year: Int = 1,
    val weekday: String = "",
    val timeOfDay: Int = 600,
    val weatherToday: String = "",
    val weatherTomorrow: String = "",
    val location: String = "",
    val luck: Int = 0,
    val mapX: Int = -1,
    val mapY: Int = -1,

    val gold: Int = 0,
    val energy: Int = 0,
    val maxEnergy: Int = 270,
    val health: Int = 0,
    val maxHealth: Int = 100,
    val playerName: String = "",
    val farmName: String = "",
    val playerLevel: Int = 0,
    val totalEarnings: Long = 0,

    val items: List<StardewItem> = emptyList(),
    val selectedSlot: Int = 0,
    val capacity: Int = 12,

    val sprites: Map<String, UiSprite> = emptyMap(),
    val quests: List<StardewQuest> = emptyList(),
    val villagers: List<StardewVillager> = emptyList(),
    val crops: List<StardewCrop> = emptyList(),
    val machines: List<StardewMachine> = emptyList(),
    val bundles: List<StardewBundle> = emptyList(),
    val animals: List<StardewAnimal> = emptyList(),

    val grounds: List<String> = emptyList(),

    val config: StardewConfigMessage = StardewConfigMessage(),

    val gifts: List<StardewGiftVillager> = emptyList(),
    val recipes: List<StardewRecipe> = emptyList(),
    val calendar: StardewCalendarMessage = StardewCalendarMessage(),
    val cartToday: Boolean = false,
    val orders: List<StardewOrder> = emptyList(),
    val cooking: List<StardewRecipe> = emptyList(),
    val shipping: StardewShippingMessage = StardewShippingMessage(),
    val trees: List<StardewTree> = emptyList(),
    val pet: StardewPetMessage = StardewPetMessage(),
    val mines: StardewMinesMessage = StardewMinesMessage(),
    val collections: StardewCollectionsMessage = StardewCollectionsMessage(),

    val farming: Int = 0,
    val mining: Int = 0,
    val foraging: Int = 0,
    val fishing: Int = 0,
    val combat: Int = 0,
    val farmingNext: Int = -1,
    val miningNext: Int = -1,
    val foragingNext: Int = -1,
    val fishingNext: Int = -1,
    val combatNext: Int = -1,
) {

    fun frame(id: String): UiSprite? =
        if (StardewPrefs.state.value.gameFrames) sprites[id] else null

    val backdrop: UiSprite?
        get() {
            val prefs = StardewPrefs.state.value
            if (!prefs.showGround || prefs.groundChoice == StardewPrefs.COLOUR) return null
            val chosen = prefs.groundChoice
            return if (chosen.isEmpty()) sprites[StardewUi.GROUND]
            else sprites[chosen] ?: sprites[StardewUi.GROUND]
        }

    val hasWorld: Boolean get() = season.isNotEmpty()

    val clock: String
        get() {
            val hours24 = (timeOfDay / 100) % 24
            val minutes = timeOfDay % 100
            val suffix = if (hours24 < 12) "am" else "pm"
            val hours12 = when {
                hours24 % 12 == 0 -> 12
                else -> hours24 % 12
            }
            return "%d:%02d%s".format(hours12, minutes, suffix)
        }

    val dayProgress: Float
        get() = ((timeOfDay - 600).coerceAtLeast(0) / 2000f).coerceIn(0f, 1f)

    val onMap: Boolean get() = mapX >= 0 && mapY >= 0

    val mapAcross: Float get() = mapX / MAP_FRACTION

    val mapDown: Float get() = mapY / MAP_FRACTION

    val lateInDay: Boolean get() = timeOfDay >= 2400

    val timeLeft: String
        get() {
            val minutes = (2600 - timeOfDay).let { (it / 100) * 60 + it % 100 }
            if (minutes <= 0) return "none"
            return if (minutes >= 60) "${minutes / 60}h ${minutes % 60}m" else "${minutes}m"
        }

    val earnedToday: Int
        get() {
            val opening = StardewHistory.days.value.lastOrNull() ?: return 0
            return gold - opening.gold
        }

    val luckLabel: String
        get() = when {
            luck >= 75 -> "Very lucky"
            luck >= 25 -> "Lucky"
            luck > -25 -> "Neutral"
            luck > -75 -> "Unlucky"
            else -> "Very unlucky"
        }

    val date: String
        get() = if (season.isEmpty()) {
            ""
        } else {
            listOf(weekday, "$season $day, Year $year").filter { it.isNotEmpty() }.joinToString(" · ")
        }

    val birthdayToday: StardewVillager?
        get() = villagers.firstOrNull { it.birthdayIn == 0 }

    val birthdaysSoon: List<StardewVillager>
        get() = villagers
            .filter { it.birthdayIn in 0..7 }
            .sortedBy { it.birthdayIn }

    val cropsReady: List<StardewCrop> get() = crops.filter { it.ready }
    val cropsThirsty: List<StardewCrop> get() = crops.filter { it.needsWater && !it.ready }

    val activeQuests: List<StardewQuest> get() = quests.filter { !it.complete }

    val machinesByUrgency: List<StardewMachine>
        get() = machines.sortedWith(
            compareByDescending<StardewMachine> { it.ready }.thenBy { it.minutes },
        )

    val machinesReady: Int get() = machines.count { it.ready }

    val bundlesByProgress: List<StardewBundle>
        get() = bundles.sortedBy { (it.required - it.have).coerceAtLeast(0) }

    val bundleItemsCarried: List<Pair<StardewBundle, StardewBundleItem>>
        get() = bundles.flatMap { bundle ->
            bundle.missing.filter { it.carried && it.id.isNotEmpty() }.map { bundle to it }
        }

    val animalsUnpetted: List<StardewAnimal> get() = animals.filter { !it.petted }

    val animalsWithProduce: List<StardewAnimal>
        get() = animals.filter { it.produce.isNotEmpty() }

    val giftableNow: List<StardewGiftVillager> get() = gifts.filter { it.carrying }

    /** Every unlocked slot, including placeholders if an older sender omitted empties. */
    val inventoryCapacity: Int
        get() = maxOf(
            HOTBAR,
            capacity,
            items.maxOfOrNull { it.slot + 1 } ?: 0,
        )

    val inventorySlots: List<StardewItem>
        get() {
            val bySlot = items.associateBy { it.slot }
            return (0 until inventoryCapacity).map { slot ->
                bySlot[slot] ?: StardewItem(slot = slot, id = "")
            }
        }

    val hotbarSlots: List<StardewItem> get() = inventorySlots.take(HOTBAR)

    val backpackSlots: List<StardewItem> get() = inventorySlots.drop(HOTBAR)

    // Kept as aliases for callers outside the inventory page.
    val hotbar: List<StardewItem> get() = hotbarSlots
    val backpack: List<StardewItem> get() = backpackSlots

    fun itemAt(slot: Int): StardewItem =
        inventorySlots.getOrNull(slot) ?: StardewItem(slot = slot, id = "")

    companion object {
        const val HOTBAR = 12

        const val MAX_INVENTORY = HOTBAR * 3

        private const val MAP_FRACTION = 1000f
    }
}

val StardewVillager.onMap: Boolean
    get() = mapX in 0..1000 && mapY in 0..1000

val StardewVillager.mapAcross: Float
    get() = mapX / 1000f

val StardewVillager.mapDown: Float
    get() = mapY / 1000f
