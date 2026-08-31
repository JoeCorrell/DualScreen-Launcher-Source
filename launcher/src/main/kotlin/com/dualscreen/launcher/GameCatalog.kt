package com.dualscreen.launcher

enum class LowerPanelMode { NATIVE_WINDOW, STARDEW, VALHEIM }

data class FileRule(
    val path: String,
    val bytes: Long? = null,
    val sha256: String? = null,
)

data class PrerequisitePackage(
    val name: String,
    val version: String,
    val url: String,
    val bytes: Long,
    val sha256: String,
    val nestedZipSuffix: String? = null,
    val installedFiles: List<String>,
)

data class SupportedGame(
    val id: String,
    val title: String,
    val summary: String,
    /** Visible until that game's complete dual-screen integration is release-ready. */
    val isBeta: Boolean = true,
    val executableCandidates: List<String>,
    /** Optional vendor configuration launcher; Play never uses this path. */
    val configurationLauncherCandidates: List<String> = emptyList(),
    /** Save root relative to Wine's drive_c/users/xuser profile. */
    val saveDirectory: String,
    /** Flat-save games may safely unwrap one arbitrary ZIP wrapper directory. */
    val stripSingleSaveArchiveRoot: Boolean = false,
    val requiredFiles: List<String>,
    val fileRules: List<FileRule> = emptyList(),
    val prerequisites: List<PrerequisitePackage> = emptyList(),
    val launchArguments: String = "",
    val panelMode: LowerPanelMode = LowerPanelMode.NATIVE_WINDOW,
    val windowHints: List<String> = executableCandidates,
)

/**
 * The complete, deliberately closed library supported by this product.
 * Adding a game requires an installer payload and validation rules in the APK;
 * users cannot create arbitrary entries or containers from the interface.
 */
object GameCatalog {
    val games: List<SupportedGame> = listOf(
        SupportedGame(
            id = "skyrim-se",
            title = "The Elder Scrolls V: Skyrim Special Edition",
            summary = "Live character, inventory, map, magic, and quest panel.",
            isBeta = false,
            // SkyrimSecondScreen bootstraps through the bundled DXGI proxy.
            // The verified runtime is SkyrimSE.exe 1.5.97; SKSE's loader EXE is
            // neither part of this mod payload nor required by this launch path.
            executableCandidates = listOf("SkyrimSE.exe"),
            configurationLauncherCandidates = listOf("SkyrimSELauncher.exe"),
            saveDirectory = "Documents/My Games/Skyrim Special Edition/Saves",
            stripSingleSaveArchiveRoot = true,
            requiredFiles = listOf("SkyrimSE.exe", "Data/Skyrim.esm"),
            fileRules = listOf(
                FileRule(
                    path = "SkyrimSE.exe",
                    sha256 = "5666e1bddd01bcab31ecf11691ef1a3f22e1541af79f2bc0e55318533cfe5d12",
                ),
            ),
            windowHints = listOf("skyrim", "skyrimse"),
        ),
        SupportedGame(
            id = "fallout3",
            title = "Fallout 3",
            summary = "A permanent live Pip-Boy on the lower display.",
            executableCandidates = listOf("Fallout3.exe"),
            configurationLauncherCandidates = listOf(
                "FalloutLauncher.exe",
                "FalloutLauncherSteam.exe",
                "FalloutLauncherEpic.exe",
                "FalloutLauncherGOG.exe",
            ),
            saveDirectory = "Documents/My Games/Fallout3/Saves",
            stripSingleSaveArchiveRoot = true,
            requiredFiles = listOf("Fallout3.exe", "Data/Fallout3.esm"),
            fileRules = listOf(
                FileRule(
                    path = "Fallout3.exe",
                    bytes = 16_855_040L,
                    sha256 = "c3f97c2255fa041a851c17cf372d69aaadd8694e2dc4230ba556001bbfbd2f3e",
                ),
            ),
            windowHints = listOf("fallout3", "fallout 3"),
        ),
        SupportedGame(
            id = "fallout4",
            title = "Fallout 4",
            summary = "A permanent live Pip-Boy interface on the lower display.",
            executableCandidates = listOf("Fallout4.exe"),
            configurationLauncherCandidates = listOf("Fallout4Launcher.exe"),
            saveDirectory = "Documents/My Games/Fallout4/Saves",
            stripSingleSaveArchiveRoot = true,
            requiredFiles = listOf("Fallout4.exe", "Data/Fallout4.esm"),
            fileRules = listOf(
                FileRule(
                    path = "Fallout4.exe",
                    bytes = 55_343_016L,
                    sha256 = "061c1ab3738612dbc46d748f02c27795a1003501c90c15d1e649d8a07fcbbb4e",
                ),
            ),
            windowHints = listOf("fallout4", "fallout 4"),
        ),
        SupportedGame(
            id = "grim-dawn",
            title = "Grim Dawn",
            summary = "Live character, inventory, skills, devotion, quests, and map data.",
            // The current native bridge is exact-build-gated to the root PE32
            // executable. The unrelated x64 copy must never be selected first.
            executableCandidates = listOf("Grim Dawn.exe"),
            saveDirectory = "Documents/My Games/Grim Dawn/save",
            requiredFiles = listOf("Grim Dawn.exe", "Game.dll", "Engine.dll"),
            fileRules = listOf(
                FileRule(
                    path = "Grim Dawn.exe",
                    bytes = 3_812_864L,
                    sha256 = "aacf685f9a11f2a3ca41162473eca83c1b01540143a536453eee18dac97b80e8",
                ),
                FileRule(
                    path = "Game.dll",
                    bytes = 8_871_936L,
                    sha256 = "fa2e444b9d89496156f8e403d264c34651303b63250147de6ff2b489c048017f",
                ),
                FileRule(
                    path = "Engine.dll",
                    bytes = 3_668_480L,
                    sha256 = "687adb5626df2ca6ad3a3048cd29b8ef73fda495ab3d0b622c10da9b1420d580",
                ),
            ),
            windowHints = listOf("grim dawn", "grimdawn"),
        ),
        SupportedGame(
            id = "deus-ex-hrdc",
            title = "Deus Ex: Human Revolution Director's Cut",
            summary = "Inventory, augmentations, maps, objectives, and data.",
            isBeta = false,
            executableCandidates = listOf("DXHRDC.exe"),
            saveDirectory = "Documents/Square Enix/Deus Ex - Human Revolution Director's Cut",
            stripSingleSaveArchiveRoot = true,
            requiredFiles = listOf("DXHRDC.exe", "bigfile.000"),
            fileRules = listOf(
                FileRule(
                    path = "DXHRDC.exe",
                    sha256 = "7a3ebf1397ecdb9f6bd3b624cb5346d6a3a24107e516126d0a1ede1c62a8e7fa",
                ),
            ),
            windowHints = listOf("dxhrdc", "deus ex"),
        ),
        SupportedGame(
            id = "dead-island-de",
            title = "Dead Island Definitive Edition",
            summary = "Live status, inventory, map, skills, and touch hotbar.",
            executableCandidates = listOf("DeadIslandGame.exe"),
            saveDirectory = "Documents/DeadIslandDE/out/save",
            stripSingleSaveArchiveRoot = true,
            requiredFiles = listOf(
                "DeadIslandGame.exe",
                "engine_x64_rwdi.dll",
                "gamedll_x64_rwdi.dll",
            ),
            fileRules = listOf(
                FileRule("DeadIslandGame.exe", bytes = 1_072_640),
                FileRule("engine_x64_rwdi.dll", bytes = 11_481_024),
                FileRule("gamedll_x64_rwdi.dll", bytes = 21_069_760),
            ),
            windowHints = listOf("deadislandgame", "dead island"),
        ),
        SupportedGame(
            id = "valheim",
            title = "Valheim",
            summary = "Player, inventory, crafting, containers, map, and chat.",
            executableCandidates = listOf("valheim.exe"),
            saveDirectory = "AppData/LocalLow/IronGate/Valheim",
            requiredFiles = listOf("valheim.exe", "valheim_Data/globalgamemanagers"),
            launchArguments = "-force-vulkan",
            prerequisites = listOf(
                PrerequisitePackage(
                    name = "BepInEx",
                    version = "5.4.23.5",
                    url = "https://github.com/BepInEx/BepInEx/releases/download/v5.4.23.5/BepInEx_win_x64_5.4.23.5.zip",
                    bytes = 639_118,
                    sha256 = "82f9878551030f54657792c0740d9d51a09500eeae1fba21106b0c441e6732c4",
                    installedFiles = listOf("winhttp.dll", "doorstop_config.ini", "BepInEx/core/BepInEx.dll"),
                ),
            ),
            panelMode = LowerPanelMode.VALHEIM,
            windowHints = listOf("valheim"),
        ),
        SupportedGame(
            id = "stardew-valley",
            title = "Stardew Valley",
            summary = "Backpack, farm, crafting, map, journal, and relationships.",
            executableCandidates = listOf("StardewModdingAPI.exe"),
            saveDirectory = "AppData/Roaming/StardewValley/Saves",
            requiredFiles = listOf("Stardew Valley.exe", "Content/Maps/springobjects.xnb"),
            prerequisites = listOf(
                PrerequisitePackage(
                    name = "SMAPI",
                    version = "4.5.2",
                    url = "https://github.com/Pathoschild/SMAPI/releases/download/4.5.2/SMAPI-4.5.2-installer.zip",
                    bytes = 41_889_142,
                    sha256 = "dd01ddca7b566bfe0d3b3d2d03833496abc56c53da976241f2ab443f5484acc4",
                    nestedZipSuffix = "/internal/windows/install.dat",
                    installedFiles = listOf(
                        "StardewModdingAPI.exe",
                        "StardewModdingAPI.dll",
                        "smapi-internal/0Harmony.dll",
                    ),
                ),
            ),
            panelMode = LowerPanelMode.STARDEW,
            windowHints = listOf("stardew", "stardewvalley"),
        ),
    )

    fun byId(id: String?): SupportedGame? = games.firstOrNull { it.id == id }
}
