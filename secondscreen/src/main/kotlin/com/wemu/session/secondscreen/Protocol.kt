package com.wemu.session.secondscreen

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.floatOrNull
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.longOrNull

sealed class GameMessage

data class GameInfoMessage(
    val gameId: String,
    val saveName: String = "",
) : GameMessage()

data class NoticeMessage(val text: String) : GameMessage()

data class StardewDayMessage(
    val season: String = "",
    val day: Int = 0,
    val year: Int = 1,
    val weekday: String = "",

    val timeOfDay: Int = 600,
    val weatherToday: String = "",
    val weatherTomorrow: String = "",
    val gold: Int = 0,
    val energy: Int = 0,
    val maxEnergy: Int = 270,
    val health: Int = 0,
    val maxHealth: Int = 100,
    val playerName: String = "",
    val farmName: String = "",
    val playerLevel: Int = 0,
    val totalEarnings: Long = 0,
    val location: String = "",

    val luck: Int = 0,

    val mapX: Int = -1,
    val mapY: Int = -1,
) : GameMessage()

data class StardewItem(
    val slot: Int,
    val id: String,
    val name: String = "",
    val count: Int = 0,
    val quality: Int = 0,
    val category: String = "",
    val water: Int = -1,
    val waterMax: Int = 0,
    val bottomless: Boolean = false,
    val cooldownMs: Int = -1,
    val cooldownMax: Int = 0,
) {
    val empty: Boolean get() = id.isEmpty()
    val wateringCan: Boolean get() = water >= 0 && waterMax > 0
    val weapon: Boolean get() = cooldownMs >= 0 && cooldownMax > 0
}

data class StardewInventoryMessage(
    val items: List<StardewItem>,
    val selected: Int = 0,
    val capacity: Int = 12,
) : GameMessage()

data class StardewSpriteMessage(
    val id: String,
    val png: String,

    val inset: Int = 0,

    val insetX: Int = inset,
    val insetY: Int = inset,
) : GameMessage()

data class StardewQuest(
    val index: Int = -1,
    val title: String,
    val detail: String = "",
    val objective: String = "",
    val daysLeft: Int = -1,
    val reward: Int = 0,
    val daily: Boolean = false,
    val cancellable: Boolean = false,
    val complete: Boolean = false,
)

data class StardewQuestsMessage(val quests: List<StardewQuest>) : GameMessage()

data class StardewVillager(
    val name: String,
    val npc: String = "",
    val location: String = "",
    val mapX: Int = -1,
    val mapY: Int = -1,
    val hearts: Int = 0,
    val maxHearts: Int = 10,
    val birthdaySeason: String = "",
    val birthdayDay: Int = 0,
    val birthdayIn: Int = -1,
    val giftsThisWeek: Int = 0,
    val talkedToday: Boolean = false,
)

data class StardewVillagersMessage(val villagers: List<StardewVillager>) : GameMessage()

data class StardewCrop(
    val name: String,
    val id: String = "",
    val location: String = "",
    val daysLeft: Int = 0,
    val needsWater: Boolean = false,
    val ready: Boolean = false,
)

data class StardewCropsMessage(val crops: List<StardewCrop>) : GameMessage()

data class StardewMachine(
    val id: String,
    val name: String,
    val outputId: String = "",
    val output: String = "",
    val count: Int = 1,
    val location: String = "",
    val minutes: Int = 0,
    val ready: Boolean = false,
)

data class StardewMachinesMessage(val machines: List<StardewMachine>) : GameMessage()

data class StardewBundleItem(
    val id: String,
    val name: String,
    val count: Int = 1,
    val quality: Int = 0,
    val carried: Boolean = false,
)

data class StardewBundle(
    val room: String,
    val name: String,
    val have: Int = 0,
    val required: Int = 0,
    val missing: List<StardewBundleItem> = emptyList(),
)

data class StardewBundlesMessage(val bundles: List<StardewBundle>) : GameMessage()

data class StardewAnimal(
    val name: String,
    val type: String = "",
    val spriteId: String = "",
    val hearts: Int = 0,
    val petted: Boolean = false,
    val baby: Boolean = false,
    val produceId: String = "",
    val produce: String = "",
)

data class StardewAnimalsMessage(val animals: List<StardewAnimal>) : GameMessage()

data class StardewConfigMessage(
    val hideGameHud: Boolean = true,
    val allowInventoryEdits: Boolean = true,
    val allowQuestCancel: Boolean = true,
    val farmerMarker: Boolean = true,
    val sendCrops: Boolean = true,
    val sendMachines: Boolean = true,
    val sendAnimals: Boolean = true,
    val sendBundles: Boolean = true,
    val sendVillagers: Boolean = true,
    val sendMap: Boolean = true,
    val sendCrafting: Boolean = true,
) : GameMessage()

data class StardewGroundsMessage(val grounds: List<String>) : GameMessage()

data class StardewSkillsMessage(
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
) : GameMessage()

data class StardewGiftVillager(
    val name: String,
    val carrying: Boolean = false,
    val loves: List<StardewBundleItem> = emptyList(),
)

data class StardewGiftsMessage(val villagers: List<StardewGiftVillager>) : GameMessage()

data class StardewIngredient(
    val id: String = "",
    val name: String = "",
    val have: Int = 0,
    val required: Int = 0,
)

data class StardewRecipe(
    val id: String,
    val name: String,
    val key: String = "",
    val description: String = "",
    val ready: Boolean = true,
    val crafted: Int = 0,
    val outputCount: Int = 1,
    val ingredients: List<StardewIngredient> = emptyList(),
)

data class StardewCraftingMessage(val recipes: List<StardewRecipe>) : GameMessage()

data class StardewCalendarDay(
    val day: Int,
    val festival: String = "",
    val festivalWhen: String = "",
    val festivalWhere: String = "",
    val names: String = "",
    val npc: String = "",
)

data class StardewOrder(
    val title: String,
    val objective: String = "",
    val daysLeft: Int = -1,
)

data class StardewOrdersMessage(val orders: List<StardewOrder>) : GameMessage()

data class StardewCookingMessage(val recipes: List<StardewRecipe>) : GameMessage()

data class StardewShipped(
    val id: String,
    val name: String,
    val count: Int = 1,
    val worth: Int = 0,
)

data class StardewShippingMessage(
    val total: Int = 0,
    val items: List<StardewShipped> = emptyList(),
) : GameMessage()

data class StardewTree(
    val id: String,
    val location: String,
    val count: Int,
    val name: String = "",
)

data class StardewTreesMessage(val trees: List<StardewTree>) : GameMessage()

data class StardewPetMessage(
    val name: String = "",
    val petted: Boolean = false,
    val bowl: Boolean = false,
) : GameMessage()

data class StardewMinesMessage(val deepest: Int = 0, val skull: Int = 0) : GameMessage()

data class StardewCollectionsMessage(
    val donated: Int = 0,
    val total: Int = 0,
    val carried: List<StardewRecipe> = emptyList(),
) : GameMessage()

data class StardewCalendarMessage(
    val season: String = "",
    val today: Int = 0,
    val days: List<StardewCalendarDay> = emptyList(),
) : GameMessage()

data class StardewCartMessage(val open: Boolean = false) : GameMessage()

data class ValheimPlayerMessage(
    val playerName: String = "",
    val x: Float = 0f,
    val z: Float = 0f,
    val angle: Float = 0f,
    val health: Float = 0f,
    val maxHealth: Float = 0f,
    val stamina: Float = 0f,
    val maxStamina: Float = 0f,
) : GameMessage()

data class ValheimHotbarMessage(val slots: List<ValheimItem>) : GameMessage()

data class ValheimInventoryMessage(val items: List<ValheimItem>) : GameMessage()

data class ValheimEffectsMessage(val effects: List<ValheimEffect>) : GameMessage()

data class ValheimSpriteMessage(
    val key: String,
    val png: String,
    val left: Int = 0,
    val top: Int = 0,
    val right: Int = 0,
    val bottom: Int = 0,
) : GameMessage()

data class ValheimWorldMessage(val worldName: String = "") : GameMessage()

data class ValheimMapMessage(val kind: String, val png: String) : GameMessage()

data class ValheimMirrorMessage(
    val key: String,
    val png: String,
    val width: Int = 0,
    val height: Int = 0,
) : GameMessage()

data class ValheimMapConfigMessage(val textureSize: Int = 0, val pixelSize: Float = 0f) :
    GameMessage()

private val json = Json {
    ignoreUnknownKeys = true
    isLenient = true
}

fun parseGameMessage(raw: String): GameMessage? = runCatching {
    val obj = json.parseToJsonElement(raw).jsonObject
    when (obj["type"]?.jsonPrimitive?.contentOrNull) {
        "game_info" -> GameInfoMessage(
            gameId = obj.str("gameId"),
            saveName = obj.str("saveName").ifEmpty { obj.str("worldName") },
        )

        "player_state" -> ValheimPlayerMessage(
            playerName = obj.str("playerName"),
            x = obj.real("x"),
            z = obj.real("z"),
            angle = obj.real("angle"),
            health = obj.real("health"),
            maxHealth = obj.real("maxHealth"),
            stamina = obj.real("stamina"),
            maxStamina = obj.real("maxStamina"),
        )

        "hotbar" -> ValheimHotbarMessage(
            obj.list("slots") {
                ValheimItem(
                    slot = it.int("slot"),
                    name = it.str("name"),
                    stack = it.int("stack"),
                    equipped = it.str("equipped") == "true",
                    iconKey = it.str("iconKey"),
                )
            },
        )

        "inventory" -> ValheimInventoryMessage(
            obj.list("items") {
                ValheimItem(
                    slot = it.int("x") + it.int("y") * VALHEIM_ROW,
                    name = it.str("name"),
                    stack = it.int("stack"),
                    equipped = it.str("equipped") == "true",
                    iconKey = it.str("iconKey"),
                    quality = it.int("quality"),
                    durability = it.real("durability", -1f),
                    maxDurability = it.real("maxDurability", -1f),
                )
            },
        )

        "status_effects" -> ValheimEffectsMessage(
            obj.list("effects") {
                ValheimEffect(
                    name = it.str("name"),
                    displayName = it.str("displayName"),
                    iconKey = it.str("iconKey"),
                    remaining = it.real("remaining"),
                    ttl = it.real("ttl"),
                )
            },
        )

        "ui_sprite" -> ValheimSpriteMessage(
            key = obj.str("key"),
            png = obj.str("data"),
            left = obj.real("borderLeft").toInt(),
            top = obj.real("borderTop").toInt(),
            right = obj.real("borderRight").toInt(),
            bottom = obj.real("borderBottom").toInt(),
        )

        "map_config" -> ValheimMapConfigMessage(
            textureSize = obj.int("textureSize"),
            pixelSize = obj.real("pixelSize"),
        )

        "ui_mirror" -> ValheimMirrorMessage(
            key = obj.str("key"),
            png = obj.str("data"),
            width = obj.int("width"),
            height = obj.int("height"),
        )

        "map_texture" -> ValheimMapMessage("terrain", obj.str("data"))
        "fog_texture" -> ValheimMapMessage("fog", obj.str("data"))

        "forest_texture", "height_texture", "map_icon", "pins_full", "pin_added",
        "pin_removed", "map_ping", "other_players", "hud_band_ready",
        -> null

        "notice" -> NoticeMessage(obj.str("text"))

        "sdv_day" -> StardewDayMessage(
            season = obj.str("season"),
            day = obj.int("day"),
            year = obj.int("year", 1),
            weekday = obj.str("weekday"),
            timeOfDay = obj.int("timeOfDay", 600),
            weatherToday = obj.str("weatherToday"),
            weatherTomorrow = obj.str("weatherTomorrow"),
            gold = obj.int("gold"),
            energy = obj.int("energy"),
            maxEnergy = obj.int("maxEnergy", 270),
            health = obj.int("health"),
            maxHealth = obj.int("maxHealth", 100),
            playerName = obj.str("playerName"),
            farmName = obj.str("farmName"),
            playerLevel = obj.int("playerLevel"),
            totalEarnings = obj.long("totalEarnings"),
            location = obj.str("location"),
            luck = obj.int("luck"),
            mapX = obj.int("mapX", -1),
            mapY = obj.int("mapY", -1),
        )

        "sdv_sprite" -> obj.int("inset").let { inset ->
            StardewSpriteMessage(
                id = obj.str("id"),
                png = obj.str("png"),
                inset = inset,
                insetX = obj.int("insetX", inset),
                insetY = obj.int("insetY", inset),
            )
        }

        "sdv_inventory" -> StardewInventoryMessage(
            items = obj.list("items") {
                StardewItem(
                    slot = it.int("slot"),
                    id = it.str("id"),
                    name = it.str("name"),
                    count = it.int("count"),
                    quality = it.int("quality"),
                    category = it.str("category"),
                    water = it.int("water", -1),
                    waterMax = it.int("waterMax"),
                    bottomless = it.flag("bottomless"),
                    cooldownMs = it.int("cooldownMs", -1),
                    cooldownMax = it.int("cooldownMax"),
                )
            },
            selected = obj.int("selected"),
            capacity = obj.int("capacity", 12),
        )

        "sdv_quests" -> StardewQuestsMessage(
            obj.list("quests") {
                StardewQuest(
                    index = it.int("index", -1),
                    title = it.str("title"),
                    detail = it.str("detail"),
                    objective = it.str("objective"),
                    daysLeft = it.int("daysLeft", -1),
                    reward = it.int("reward"),
                    daily = it.str("daily") == "1",
                    cancellable = it.str("cancellable") == "1",
                    complete = it.str("complete") == "1",
                )
            },
        )

        "sdv_villagers" -> StardewVillagersMessage(
            obj.list("villagers") {
                StardewVillager(
                    name = it.str("name"),
                    npc = it.str("npc"),
                    location = it.str("location"),
                    mapX = it.int("mapX", -1),
                    mapY = it.int("mapY", -1),
                    hearts = it.int("hearts"),
                    maxHearts = it.int("maxHearts", 10),
                    birthdaySeason = it.str("birthdaySeason"),
                    birthdayDay = it.int("birthdayDay"),
                    birthdayIn = it.int("birthdayIn", -1),
                    giftsThisWeek = it.int("giftsThisWeek"),
                    talkedToday = it.str("talkedToday") == "1",
                )
            },
        )

        "sdv_crops" -> StardewCropsMessage(
            obj.list("crops") {
                StardewCrop(
                    id = it.str("id"),
                    name = it.str("name"),
                    location = it.str("location"),
                    daysLeft = it.int("daysLeft"),
                    needsWater = it.str("needsWater") == "1",
                    ready = it.str("ready") == "1",
                )
            },
        )

        "sdv_machines" -> StardewMachinesMessage(
            obj.list("machines") {
                StardewMachine(
                    id = it.str("id"),
                    name = it.str("name"),
                    outputId = it.str("outputId"),
                    output = it.str("output"),
                    count = it.int("count", 1),
                    location = it.str("location"),
                    minutes = it.int("minutes"),
                    ready = it.str("ready") == "1",
                )
            },
        )

        "sdv_bundles" -> StardewBundlesMessage(
            obj.list("bundles") {
                StardewBundle(
                    room = it.str("room"),
                    name = it.str("name"),
                    have = it.int("have"),
                    required = it.int("required"),
                    missing = it.list("missing") { entry ->
                        StardewBundleItem(
                            id = entry.str("id"),
                            name = entry.str("name"),
                            count = entry.int("count", 1),
                            quality = entry.int("quality"),
                            carried = entry.str("carried") == "1",
                        )
                    },
                )
            },
        )

        "sdv_animals" -> StardewAnimalsMessage(
            obj.list("animals") {
                StardewAnimal(
                    name = it.str("name"),
                    type = it.str("type"),
                    spriteId = it.str("spriteId"),
                    hearts = it.int("hearts"),
                    petted = it.str("petted") == "1",
                    baby = it.str("baby") == "1",
                    produceId = it.str("produceId"),
                    produce = it.str("produce"),
                )
            },
        )

        "sdv_grounds" -> StardewGroundsMessage(
            obj.list("grounds") { it.str("id") }.filter { it.isNotEmpty() },
        )

        "sdv_config" -> StardewConfigMessage(
            hideGameHud = obj.flag("hideGameHud"),
            allowInventoryEdits = obj.flag("allowInventoryEdits"),
            allowQuestCancel = obj.flag("allowQuestCancel"),
            farmerMarker = obj.flag("farmerMarker"),
            sendCrops = obj.flag("sendCrops"),
            sendMachines = obj.flag("sendMachines"),
            sendAnimals = obj.flag("sendAnimals"),
            sendBundles = obj.flag("sendBundles"),
            sendVillagers = obj.flag("sendVillagers"),
            sendMap = obj.flag("sendMap"),
            sendCrafting = obj.flag("sendCrafting"),
        )

        "sdv_skills" -> StardewSkillsMessage(
            farming = obj.int("farming"),
            mining = obj.int("mining"),
            foraging = obj.int("foraging"),
            fishing = obj.int("fishing"),
            combat = obj.int("combat"),
            farmingNext = obj.int("farmingNext", -1),
            miningNext = obj.int("miningNext", -1),
            foragingNext = obj.int("foragingNext", -1),
            fishingNext = obj.int("fishingNext", -1),
            combatNext = obj.int("combatNext", -1),
        )

        "sdv_gifts" -> StardewGiftsMessage(
            obj.list("villagers") { entry ->
                StardewGiftVillager(
                    name = entry.str("name"),
                    carrying = entry.str("carrying") == "1",
                    loves = entry.list("loves") {
                        StardewBundleItem(
                            id = it.str("id"),
                            name = it.str("name"),
                            carried = it.str("carried") == "1",
                        )
                    },
                )
            },
        )

        "sdv_crafting" -> StardewCraftingMessage(
            obj.list("recipes") {
                StardewRecipe(
                    id = it.str("id"),
                    name = it.str("name"),
                    key = it.str("key"),
                    description = it.str("description"),
                    ready = it.flag("ready"),
                    crafted = it.int("crafted"),
                    outputCount = it.int("outputCount", 1),
                    ingredients = it.list("ingredients") { ingredient ->
                        StardewIngredient(
                            id = ingredient.str("id"),
                            name = ingredient.str("name"),
                            have = ingredient.int("have"),
                            required = ingredient.int("required"),
                        )
                    },
                )
            },
        )

        "sdv_calendar" -> StardewCalendarMessage(
            season = obj.str("season"),
            today = obj.int("today"),
            days = obj.list("days") {
                StardewCalendarDay(
                    day = it.int("day"),
                    festival = it.str("festival"),
                    festivalWhen = it.str("festivalWhen"),
                    festivalWhere = it.str("festivalWhere"),
                    names = it.str("names"),
                    npc = it.str("npc"),
                )
            },
        )

        "sdv_cart" -> StardewCartMessage(open = obj.str("open") == "1")

        "sdv_cooking" -> StardewCookingMessage(
            obj.list("recipes") { StardewRecipe(id = it.str("id"), name = it.str("name")) },
        )

        "sdv_shipping" -> StardewShippingMessage(
            total = obj.int("total"),
            items = obj.list("items") {
                StardewShipped(
                    id = it.str("id"),
                    name = it.str("name"),
                    count = it.int("count", 1),
                    worth = it.int("worth"),
                )
            },
        )

        "sdv_trees" -> StardewTreesMessage(
            obj.list("trees") {
                StardewTree(
                    id = it.str("id"),
                    location = it.str("location"),
                    count = it.int("count"),
                    name = it.str("name"),
                )
            },
        )

        "sdv_pet" -> StardewPetMessage(
            name = obj.str("name"),
            petted = obj.str("petted") == "1",
            bowl = obj.str("bowl") == "1",
        )

        "sdv_mines" -> StardewMinesMessage(
            deepest = obj.int("deepest"),
            skull = obj.int("skull"),
        )

        "sdv_collections" -> StardewCollectionsMessage(
            donated = obj.int("donated"),
            total = obj.int("total"),
            carried = obj.list("carried") {
                StardewRecipe(id = it.str("id"), name = it.str("name"))
            },
        )

        "sdv_orders" -> StardewOrdersMessage(
            obj.list("orders") {
                StardewOrder(
                    title = it.str("title"),
                    objective = it.str("objective"),
                    daysLeft = it.int("daysLeft", -1),
                )
            },
        )

        else -> null
    }
}.getOrNull()

private fun kotlinx.serialization.json.JsonObject.str(key: String): String =
    this[key]?.jsonPrimitive?.contentOrNull.orEmpty()

private fun kotlinx.serialization.json.JsonObject.int(key: String, fallback: Int = 0): Int =
    this[key]?.jsonPrimitive?.let { it.intOrNull ?: it.contentOrNull?.toIntOrNull() } ?: fallback

private fun kotlinx.serialization.json.JsonObject.long(key: String, fallback: Long = 0): Long =
    this[key]?.jsonPrimitive?.let { it.longOrNull ?: it.contentOrNull?.toLongOrNull() } ?: fallback

private fun kotlinx.serialization.json.JsonObject.flag(key: String): Boolean =
    this[key]?.jsonPrimitive?.contentOrNull?.let { it == "1" || it.equals("true", true) } ?: true

private const val VALHEIM_ROW = 8

private fun kotlinx.serialization.json.JsonObject.real(
    key: String,
    fallback: Float = 0f,
): Float = this[key]?.jsonPrimitive?.let {
    it.floatOrNull ?: it.contentOrNull?.toFloatOrNull()
} ?: fallback

private fun <T> kotlinx.serialization.json.JsonObject.list(
    key: String,
    item: (kotlinx.serialization.json.JsonObject) -> T,
): List<T> = this[key]?.jsonArray.orEmpty().mapNotNull {
    runCatching { item(it.jsonObject) }.getOrNull()
}

object Commands {
    fun selectSlot(slot: Int): String = """{"type":"select_slot","slot":$slot}"""

    fun moveItem(from: Int, to: Int): String = """{"type":"move_item","from":$from,"to":$to}"""

    fun shiftToolbar(right: Boolean): String =
        """{"type":"shift_toolbar","value":${if (right) 1 else 0}}"""

    fun requestRefresh(): String = """{"type":"refresh"}"""

    fun cancelQuest(index: Int): String = """{"type":"cancel_quest","slot":$index}"""

    fun eatSlot(slot: Int): String = """{"type":"eat_slot","slot":$slot}"""

    fun sortBag(): String = """{"type":"sort_bag"}"""

    fun craftRecipe(key: String): String =
        """{"type":"craft_recipe","key":"${key.jsonEscaped()}"}"""

    fun setOption(key: String, on: Boolean): String =
        """{"type":"set_option","key":"$key","value":${if (on) 1 else 0}}"""
}

private fun String.jsonEscaped(): String = buildString(length) {
    for (character in this@jsonEscaped) {
        when (character) {
            '"' -> append("\\\"")
            '\\' -> append("\\\\")
            '\b' -> append("\\b")
            '\u000C' -> append("\\f")
            '\n' -> append("\\n")
            '\r' -> append("\\r")
            '\t' -> append("\\t")
            else -> if (character.code < 0x20) {
                append("\\u")
                append(character.code.toString(16).padStart(4, '0'))
            } else {
                append(character)
            }
        }
    }
}
