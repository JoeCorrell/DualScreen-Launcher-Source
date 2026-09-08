package com.dualscreen.launcher

enum class LowerPanelMode { NATIVE_WINDOW, STARDEW, VALHEIM, DIAGNOSTICS }

data class FileRule(
    val path: String,
    val bytes: Long? = null,
    val sha256: String? = null,
    val sha256AnyOf: Set<String> = emptySet(),
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
    /** Stable, Windows-safe directory name under /Dualscreen/Games. */
    val storageName: String = safeStorageSegment(title, "Game"),
    val summary: String,
    /** Player-facing build contract; validation below remains authoritative. */
    val requiredBuild: String,
    /** Visible until that game's complete dual-screen integration is release-ready. */
    val isBeta: Boolean = true,
    val executableCandidates: List<String>,
    /** Optional vendor configuration launcher; Play never uses this path. */
    val configurationLauncherCandidates: List<String> = emptyList(),
    /** Save root relative to Wine's drive_c/users/xuser profile; blank disables save management until verified. */
    val saveDirectory: String,
    /** Flat-save games may safely unwrap one arbitrary ZIP wrapper directory. */
    val stripSingleSaveArchiveRoot: Boolean = false,
    val requiredFiles: List<String>,
    val fileRules: List<FileRule> = emptyList(),
    val prerequisites: List<PrerequisitePackage> = emptyList(),
    val launchArguments: String = "",
    val panelMode: LowerPanelMode = LowerPanelMode.NATIVE_WINDOW,
    /** Hide the duplicate DirectInput view and expose only Wine's XInput pad. */
    val exclusiveXInput: Boolean = false,
    /** Executable-scoped Wine DLL policy; never promoted to prefix-wide env vars. */
    val nativeDllOverrides: Map<String, String> = emptyMap(),
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
            storageName = "Skyrim Special Edition",
            summary = "Live character, inventory, map, magic, and quest panel.",
            requiredBuild = "Skyrim Special Edition 1.5.97.0",
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
            id = "crysis3",
            title = "Crysis 3",
            summary = "Read-only native touchscreen trial. Live fields remain under validation; gameplay actions disabled.",
            requiredBuild = "Crysis 3 PC 1.2.1.1000 (verified 32-bit executable)",
            executableCandidates = listOf("Bin32/Crysis3.exe"),
            // Do not migrate saves from WEMU or expose the whole profile as a
            // save folder until checkpoint/save-file semantics are verified.
            saveDirectory = "",
            requiredFiles = listOf("Bin32/Crysis3.exe", "C3/Scripts.pak", "C3/GameData.pak", "C3/UI.pak"),
            fileRules = listOf(FileRule(
                path = "Bin32/Crysis3.exe",
                bytes = 86_636_544L,
                sha256AnyOf = setOf(
                    "f332e2fb466ec8ce44336a4af06cd498bb5a787d7352548c76f2e4ec008f5e8e",
                    "fffd52e0c483b55f04e0048dff0c45dd445734cfcf6470aa3d84fbf84040e055",
                ),
            )),
            panelMode = LowerPanelMode.NATIVE_WINDOW,
            nativeDllOverrides = mapOf("dinput8" to "native,builtin"),
            windowHints = listOf("crysis3", "crysis 3"),
        ),
        SupportedGame(
            id = "assassins-creed-ii",
            title = "Assassin's Creed II",
            storageName = "Assassins Creed II",
            summary = "Animus companion development build. Live integration is in progress.",
            requiredBuild = "Verified PC 32-bit executable, original or dev.18 landing patch",
            executableCandidates = listOf("AssassinsCreedIIGame.exe"),
            // AC II distributions store saves through different Ubisoft backends.
            // Preserve the existing prefix's saves until this build's path is verified.
            saveDirectory = "",
            requiredFiles = listOf("AssassinsCreedIIGame.exe", "DataPC.forge", "DataPC_extra.forge"),
            fileRules = listOf(FileRule(
                path = "AssassinsCreedIIGame.exe",
                // The exact landing-patch output adds a code section and has a
                // different length. Both complete hashes remain mandatory;
                // other modified executables still fail compatibility checks.
                sha256AnyOf = setOf(
                    "23a73e5f2d035973ebe4afc056e5c56bad982d27142294fa44e7cd11732c8b92",
                    "e0520f85b2cb3ac10ce59c8073cebd5853ab6395b3d2303f368b9e3203595abd",
                    "da55d92905936ecd79e287fc268ae4af59e242f8cbf6ab9b29a566847ff4f9a3",
                    "e680e6109c39828b4f5e842d2b53fae388638a36800bdb17b5e9eb127efefb39",
                    "89c7bbb81d44b5bed37b9e71302689742933e9732d918c1f1405619ebd58c088",
                ),
            )),
            nativeDllOverrides = mapOf("dinput8" to "native,builtin", "dsound" to "builtin,native"),
            windowHints = listOf("assassinscreediigame", "assassin's creed 2", "scimitarenginewindowclass"),
        ),
        SupportedGame(
            id = "dishonored",
            title = "Dishonored",
            summary = "Dual-screen map, powers, equipment, and journal interface in development.",
            requiredBuild = "Dishonored PC 32-bit, build 334700 (verified executable)",
            executableCandidates = listOf("Binaries/Win32/Dishonored.exe"),
            // Keep saves untouched until this distribution's backend is verified.
            saveDirectory = "",
            requiredFiles = listOf(
                "Binaries/Win32/Dishonored.exe",
                "DishonoredGame/CookedPCConsole/DishonoredGame.upk",
                "DishonoredGame/Config/DefaultEngine.ini",
            ),
            fileRules = listOf(FileRule(
                path = "Binaries/Win32/Dishonored.exe",
                bytes = 18_008_064L,
                sha256 = "67ced86b1675b8d27c742a84be41eaa7eba41c748a7f4ec1fe05d107d4f91ae5",
            )),
            nativeDllOverrides = mapOf("dinput8" to "native,builtin"),
            // Verified at 00637465-00637561 in this executable. Exclusive mode
            // collapsed Wine's stacked desktop; keep the actual client at HD.
            launchArguments = "-WINDOWED -ResX=1280 -ResY=720",
            windowHints = listOf("dishonored"),
        ),
        SupportedGame(
            id = "fallout3",
            title = "Fallout 3",
            isBeta = false,
            summary = "A permanent live Pip-Boy on the lower display.",
            requiredBuild = "Fallout 3 1.7.0.4",
            executableCandidates = listOf("Fallout3.exe"),
            configurationLauncherCandidates = listOf(
                "Fallout3Launcher.exe",
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
            requiredBuild = "Fallout 4 1.11.240.0",
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
            requiredBuild = "Grim Dawn 1.3.0.7 (32-bit executable)",
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
            id = "transformers-wfc",
            title = "Transformers: War for Cybertron",
            storageName = "Transformers War for Cybertron",
            summary = "Live combat, squad possession, and mission objectives.",
            requiredBuild = "PC 1.0 / Steam 42650 (32-bit TWFC.exe)",
            executableCandidates = listOf("Binaries/TWFC.exe"),
            saveDirectory = "Saved Games/Transformers(TM) - War For Cybertron (TM)",
            stripSingleSaveArchiveRoot = true,
            requiredFiles = listOf(
                "Binaries/TWFC.exe",
                "TransGame/CookedPC/TransGame.xxx",
                "TransGame/PCTOC.txt",
            ),
            fileRules = listOf(
                FileRule(
                    path = "Binaries/TWFC.exe",
                    bytes = 37_667_328L,
                    sha256 = "1db04b09b36094571fab861cd01ca175d44d610b5417f950f37523cd97eeb016",
                ),
            ),
            launchArguments = "-windowed -ResX=1280 -ResY=720",
            nativeDllOverrides = mapOf("dinput8" to "native,builtin"),
            windowHints = listOf("twfc", "transformers", "war for cybertron"),
        ),
        SupportedGame(
            id = "deus-ex-hrdc",
            title = "Deus Ex: Human Revolution Director's Cut",
            storageName = "Deus Ex Human Revolution Directors Cut",
            summary = "Inventory, augmentations, maps, objectives, and data.",
            requiredBuild = "Director's Cut 2.0.66.0",
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
            requiredBuild = "2016 PC build 1.0.0.0",
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
            id = "isaac-repentance",
            title = "The Binding of Isaac: Repentance+",
            storageName = "The Binding of Isaac - Repentance",
            summary = "Live dungeon map, stats, inventory, and run information.",
            requiredBuild = "Repentance+ v1.9.7.17.J460 (Windows 32-bit)",
            executableCandidates = listOf("isaac-ng.exe"),
            saveDirectory = "Documents/My Games/Binding of Isaac Repentance+",
            requiredFiles = listOf(
                "isaac-ng.exe",
                "resources/packed/repentance.a",
                "resources/scripts/socket/core.dll",
            ),
            fileRules = listOf(
                FileRule(
                    path = "isaac-ng.exe",
                    bytes = 9_362_944L,
                    sha256 = "e5faf6f513c6de9cc9ff1c335ef9c541e2b33fc623aa3b8ffbd2e6a6c0027bb0",
                ),
            ),
            panelMode = LowerPanelMode.NATIVE_WINDOW,
            exclusiveXInput = true,
            windowHints = listOf("isaac-ng", "binding of isaac", "repentance"),
        ),
        SupportedGame(
            id = "valheim",
            title = "Valheim",
            summary = "Player, inventory, crafting, containers, map, and chat.",
            requiredBuild = "Windows x64 build (BepInEx 5.4.23.5 added automatically)",
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
            isBeta = false,
            summary = "Backpack, farm, crafting, map, journal, and relationships.",
            requiredBuild = "Stardew Valley 1.6.15.24356 (GOG or Steam)",
            executableCandidates = listOf("Stardew Valley.exe"),
            saveDirectory = "AppData/Roaming/StardewValley/Saves",
            requiredFiles = listOf(
                "Stardew Valley.exe",
                "Stardew Valley.dll",
                "Content/Maps/springobjects.xnb",
            ),
            fileRules = listOf(
                FileRule(
                    path = "Stardew Valley.dll",
                    // Verified 1.6.15.24356 GOG/Steam inputs and their native
                    // dual-screen outputs. Accepting the outputs keeps the
                    // post-install verification and idempotent updater valid.
                    sha256AnyOf = setOf(
                        "078c04dcf34fdd8df3ec1ddce062d3dfd4a34d94614d806466adc78754a7693c",
                        "7f1e5b8e58d2758b78570ba771bbeb03d33522f62188bf6c32edf0cf626deaee",
                        "7579cca523d19470b4f842b9068b6bf9b8f96efe632b83471bc62cae7f7b3b90",
                        "f3a92e73f1cdf7c0d4eb5f995b2555244fbe16d33429e26c2b425f6eb1480c63",
                        "f3ebc7c7cda1199c3919fd2d1619722b3e045b2148aece7cf0bb9c8e4d47d466",
                        "9bbcb7d5be1d059f863173802e68d9ce1a48a0800963a2cfbcc359f696035b9c",
                    ),
                ),
            ),
            panelMode = LowerPanelMode.STARDEW,
            windowHints = listOf("stardew", "stardewvalley"),
        ),
    )

    fun byId(id: String?): SupportedGame? = games.firstOrNull { it.id == id }
}
