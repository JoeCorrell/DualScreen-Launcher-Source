package com.dualscreen.launcher.switch

data class SwitchGameDefinition(
    val id: String,
    val title: String,
    val titleId: String,
    val summary: String,
    val requiredVersion: String,
    val supportsDlc: Boolean = true,
) {
    val titleIdValue: Long get() = titleId.toLong(16)
    val programId: String get() = titleIdValue.toString()
    // Display metadata only: never change the user's installed/enabled content.
    val requiredVersionDescription: String
        get() = when {
            requiredVersion == "1.0.0" -> "$requiredVersion (base game, no update)"
            requiredVersion.startsWith("v") -> "Update $requiredVersion"
            else -> requiredVersion
        }
}

object SwitchCatalog {
    val games = listOf(
        SwitchGameDefinition(
            id = "mario-kart-8-deluxe",
            title = "Mario Kart 8 Deluxe",
            titleId = "0100152000022000",
            summary = "Live race companion, item control, and dual-display gameplay.",
            requiredVersion = "3.0.5",
        ),
        SwitchGameDefinition(
            id = "breath-of-the-wild",
            title = "The Legend of Zelda: Breath of the Wild",
            titleId = "01007EF00011E000",
            summary = "Live inventory, map, quests, player state, and dual-screen controls.",
            requiredVersion = "1.6.0",
        ),
        SwitchGameDefinition(
            id = "pokemon-legends-za",
            title = "Pokémon Legends: Z-A",
            titleId = "0100F43008C44000",
            summary = "Party, bag, Pokédex, quests, map, and touch-friendly actions.",
            requiredVersion = "v393216",
        ),
        SwitchGameDefinition(
            id = "skyward-sword-hd",
            title = "The Legend of Zelda: Skyward Sword HD",
            titleId = "01002DA013484000",
            summary = "Inventory, maps, status, guidance, and independent lower display.",
            requiredVersion = "1.0.1",
        ),
        SwitchGameDefinition(
            id = "xenoblade-chronicles-x",
            title = "Xenoblade Chronicles X: Definitive Edition",
            titleId = "0100453019AA8000",
            summary = "Explore Mira with an integrated Eden DS game profile and dual-display support.",
            requiredVersion = "1.0.2",
            supportsDlc = false,
        ),
        SwitchGameDefinition(
            id = "links-awakening",
            title = "The Legend of Zelda: Link's Awakening",
            titleId = "01006BB00C6F0000",
            summary = "Development profile: game launch enabled; touchscreen companion in progress.",
            requiredVersion = "1.0.0",
            supportsDlc = false,
        ),
        SwitchGameDefinition(
            id = "super-mario-odyssey",
            title = "Super Mario Odyssey",
            titleId = "0100000000010000",
            summary = "Development profile: native companion integration in progress.",
            requiredVersion = "1.0.0",
            supportsDlc = false,
        ),
    )

    fun byId(id: String?): SwitchGameDefinition? = games.firstOrNull { it.id == id }
    fun byTitleId(titleId: String?): SwitchGameDefinition? = games.firstOrNull {
        it.titleId.equals(titleId, ignoreCase = true)
    }
}
