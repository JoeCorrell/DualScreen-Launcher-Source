package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import org.junit.Test

class GameCatalogTest {
    @Test
    fun assassinsCreedUsesTheVerifiedGameAndSeparateDevelopmentPayload() {
        val game = checkNotNull(GameCatalog.byId("assassins-creed-ii"))
        assertThat(game.executableCandidates).containsExactly("AssassinsCreedIIGame.exe")
        assertThat(game.nativeDllOverrides).containsExactly("dinput8", "native,builtin")
        assertThat(game.isBeta).isTrue()
        assertThat(game.saveDirectory).isEmpty()
        assertThat(OnDemandGamePayloads.manages(game.id)).isTrue()
        assertThat(game.fileRules.single().sha256)
            .isEqualTo("23a73e5f2d035973ebe4afc056e5c56bad982d27142294fa44e7cd11732c8b92")
    }

    @Test
    fun catalogIsClosedAndEveryGameHasInstallAndLaunchRules() {
        assertThat(GameCatalog.games.map { it.id }).containsExactly(
            "skyrim-se",
            "assassins-creed-ii",
            "fallout3",
            "fallout4",
            "grim-dawn",
            "transformers-wfc",
            "deus-ex-hrdc",
            "dead-island-de",
            "isaac-repentance",
            "valheim",
            "stardew-valley",
        ).inOrder()
        assertThat(GameCatalog.games.map { it.id }.distinct()).hasSize(GameCatalog.games.size)

        GameCatalog.games.forEach { game ->
            assertThat(game.executableCandidates).isNotEmpty()
            assertThat(game.requiredFiles).isNotEmpty()
            assertThat(game.requiredBuild).isNotEmpty()
            if (game.id != "assassins-creed-ii") assertThat(game.saveDirectory).isNotEmpty()
            assertThat(game.saveDirectory).doesNotContain("..")
            assertThat(game.saveDirectory.startsWith("/")).isFalse()
            assertThat(game.id).matches("[a-z0-9-]+")
            game.prerequisites.forEach { prerequisite ->
                assertThat(prerequisite.url).startsWith("https://")
                assertThat(prerequisite.bytes).isGreaterThan(0)
                assertThat(prerequisite.sha256).matches("[0-9a-f]{64}")
                assertThat(prerequisite.installedFiles).isNotEmpty()
            }
        }
    }

    @Test
    fun playerFacingBuildRequirementsMatchNativeBuildGates() {
        assertThat(GameCatalog.byId("skyrim-se")?.requiredBuild).contains("1.5.97.0")
        assertThat(GameCatalog.byId("fallout3")?.requiredBuild).contains("1.7.0.4")
        assertThat(GameCatalog.byId("fallout4")?.requiredBuild).contains("1.11.240.0")
        assertThat(GameCatalog.byId("grim-dawn")?.requiredBuild).contains("1.3.0.7")
        assertThat(GameCatalog.byId("transformers-wfc")?.requiredBuild).contains("42650")
        assertThat(GameCatalog.byId("deus-ex-hrdc")?.requiredBuild).contains("2.0.66.0")
        assertThat(GameCatalog.byId("isaac-repentance")?.requiredBuild).contains("1.9.7.17.J460")
        assertThat(GameCatalog.byId("stardew-valley")?.requiredBuild).contains("1.6.15.24356")
    }

    @Test
    fun saveLocationsMatchEachGamesWindowsProfileConvention() {
        assertThat(GameCatalog.byId("skyrim-se")?.saveDirectory)
            .isEqualTo("Documents/My Games/Skyrim Special Edition/Saves")
        assertThat(GameCatalog.byId("fallout3")?.saveDirectory)
            .isEqualTo("Documents/My Games/Fallout3/Saves")
        assertThat(GameCatalog.byId("fallout4")?.saveDirectory)
            .isEqualTo("Documents/My Games/Fallout4/Saves")
        assertThat(GameCatalog.byId("grim-dawn")?.saveDirectory)
            .isEqualTo("Documents/My Games/Grim Dawn/save")
        assertThat(GameCatalog.byId("isaac-repentance")?.saveDirectory)
            .isEqualTo("Documents/My Games/Binding of Isaac Repentance+")
        assertThat(GameCatalog.byId("valheim")?.saveDirectory)
            .isEqualTo("AppData/LocalLow/IronGate/Valheim")
        assertThat(GameCatalog.byId("stardew-valley")?.saveDirectory)
            .isEqualTo("AppData/Roaming/StardewValley/Saves")
    }

    @Test
    fun releaseReadyGamesAreNotMarkedBeta() {
        val releaseGames = GameCatalog.games.filterNot { it.isBeta }.map { it.id }
        assertThat(releaseGames).containsExactly(
            "skyrim-se",
            "fallout3",
            "deus-ex-hrdc",
            "stardew-valley",
        )
        assertThat(GameCatalog.byId("fallout3")).isNotNull()
        assertThat(GameCatalog.byId("fallout4")).isNotNull()
        assertThat(GameCatalog.byId("grim-dawn")).isNotNull()
    }

    @Test
    fun publicStorageNamesAreStableAndWindowsSafe() {
        assertThat(GameCatalog.byId("skyrim-se")?.storageName).isEqualTo("Skyrim Special Edition")
        assertThat(GameCatalog.byId("fallout3")?.storageName).isEqualTo("Fallout 3")
        GameCatalog.games.forEach { game ->
            assertThat(game.storageName).doesNotContain(":")
            assertThat(game.storageName).doesNotContain("/")
            assertThat(game.storageName).doesNotContain("\\")
        }
        assertThat(safeStorageSegment("..", "Preset")).isEqualTo("Preset")
        assertThat(safeStorageSegment("  .My: Preset.  ", "Preset")).isEqualTo("My Preset")
    }

    @Test
    fun publicSavesReplaceOnlyAnEmptyPrivatePlaceholder() {
        assertThat(saveExposurePlan(publicHasFiles = true, privateHasFiles = false))
            .isEqualTo(SaveExposurePlan.PREFER_PUBLIC)
        assertThat(saveExposurePlan(publicHasFiles = true, privateHasFiles = true))
            .isEqualTo(SaveExposurePlan.PRESERVE_PRIVATE)
        assertThat(saveExposurePlan(publicHasFiles = false, privateHasFiles = true))
            .isEqualTo(SaveExposurePlan.MIGRATE_PRIVATE)
    }

    @Test
    fun bethesdaGamesExposeConfigurationLaunchersWithoutReplacingPlayTargets() {
        val skyrim = checkNotNull(GameCatalog.byId("skyrim-se"))
        val fallout3 = checkNotNull(GameCatalog.byId("fallout3"))
        val fallout4 = checkNotNull(GameCatalog.byId("fallout4"))

        assertThat(skyrim.executableCandidates).containsExactly("SkyrimSE.exe")
        assertThat(skyrim.configurationLauncherCandidates).containsExactly("SkyrimSELauncher.exe")
        assertThat(fallout3.executableCandidates).containsExactly("Fallout3.exe")
        assertThat(fallout3.configurationLauncherCandidates).containsExactly(
            "Fallout3Launcher.exe",
            "FalloutLauncher.exe",
            "FalloutLauncherSteam.exe",
            "FalloutLauncherEpic.exe",
            "FalloutLauncherGOG.exe",
        ).inOrder()
        assertThat(fallout4.executableCandidates).containsExactly("Fallout4.exe")
        assertThat(fallout4.configurationLauncherCandidates).containsExactly("Fallout4Launcher.exe")
        assertThat(GameCatalog.byId("grim-dawn")?.configurationLauncherCandidates).isEmpty()
    }

    @Test
    fun stardewUsesTheEmbeddedNativePatchWithoutSmapi() {
        val stardew = checkNotNull(GameCatalog.byId("stardew-valley"))
        assertThat(stardew.executableCandidates).containsExactly("Stardew Valley.exe")
        assertThat(stardew.prerequisites).isEmpty()
        assertThat(stardew.panelMode).isEqualTo(LowerPanelMode.STARDEW)
        assertThat(stardew.requiredFiles).contains("Stardew Valley.dll")
        assertThat(stardew.fileRules.single().sha256AnyOf).containsExactly(
            "078c04dcf34fdd8df3ec1ddce062d3dfd4a34d94614d806466adc78754a7693c",
            "7f1e5b8e58d2758b78570ba771bbeb03d33522f62188bf6c32edf0cf626deaee",
            "7579cca523d19470b4f842b9068b6bf9b8f96efe632b83471bc62cae7f7b3b90",
            "f3a92e73f1cdf7c0d4eb5f995b2555244fbe16d33429e26c2b425f6eb1480c63",
            "f3ebc7c7cda1199c3919fd2d1619722b3e045b2148aece7cf0bb9c8e4d47d466",
            "9bbcb7d5be1d059f863173802e68d9ce1a48a0800963a2cfbcc359f696035b9c",
        )
    }

    @Test
    fun isaacUsesTheOfficialLuaApiAndNativeInProcessPanel() {
        val isaac = checkNotNull(GameCatalog.byId("isaac-repentance"))
        assertThat(isaac.executableCandidates).containsExactly("isaac-ng.exe")
        assertThat(isaac.panelMode).isEqualTo(LowerPanelMode.NATIVE_WINDOW)
        assertThat(isaac.requiredFiles).containsAtLeast(
            "resources/packed/repentance.a",
            "resources/scripts/socket/core.dll",
        )
        assertThat(isaac.fileRules.single().bytes).isEqualTo(9_362_944L)
        assertThat(isaac.fileRules.single().sha256)
            .isEqualTo("e5faf6f513c6de9cc9ff1c335ef9c541e2b33fc623aa3b8ffbd2e6a6c0027bb0")
        assertThat(isaac.nativeDllOverrides).isEmpty()
        assertThat(isaac.exclusiveXInput).isTrue()
    }

    @Test
    fun addressSensitiveNativeModsAreVersionGated() {
        val skyrim = checkNotNull(GameCatalog.byId("skyrim-se"))
        val fallout3 = checkNotNull(GameCatalog.byId("fallout3"))
        val fallout4 = checkNotNull(GameCatalog.byId("fallout4"))
        val grimDawn = checkNotNull(GameCatalog.byId("grim-dawn"))
        val transformers = checkNotNull(GameCatalog.byId("transformers-wfc"))
        val deusEx = checkNotNull(GameCatalog.byId("deus-ex-hrdc"))
        val deadIsland = checkNotNull(GameCatalog.byId("dead-island-de"))

        assertThat(skyrim.fileRules.single().sha256)
            .isEqualTo("5666e1bddd01bcab31ecf11691ef1a3f22e1541af79f2bc0e55318533cfe5d12")
        assertThat(skyrim.executableCandidates).containsExactly("SkyrimSE.exe")
        assertThat(skyrim.requiredFiles).contains("SkyrimSE.exe")
        assertThat(fallout3.executableCandidates).containsExactly("Fallout3.exe")
        assertThat(fallout3.requiredFiles).contains("Data/Fallout3.esm")
        assertThat(fallout3.fileRules.single().sha256)
            .isEqualTo("c3f97c2255fa041a851c17cf372d69aaadd8694e2dc4230ba556001bbfbd2f3e")
        assertThat(fallout4.executableCandidates).containsExactly("Fallout4.exe")
        assertThat(fallout4.requiredFiles).contains("Data/Fallout4.esm")
        assertThat(fallout4.fileRules.single().sha256)
            .isEqualTo("061c1ab3738612dbc46d748f02c27795a1003501c90c15d1e649d8a07fcbbb4e")
        assertThat(grimDawn.executableCandidates).containsExactly("Grim Dawn.exe")
        assertThat(grimDawn.requiredFiles).containsAtLeast("Game.dll", "Engine.dll")
        assertThat(grimDawn.fileRules.map { it.sha256 }).containsExactly(
            "aacf685f9a11f2a3ca41162473eca83c1b01540143a536453eee18dac97b80e8",
            "fa2e444b9d89496156f8e403d264c34651303b63250147de6ff2b489c048017f",
            "687adb5626df2ca6ad3a3048cd29b8ef73fda495ab3d0b622c10da9b1420d580",
        ).inOrder()
        assertThat(transformers.executableCandidates).containsExactly("Binaries/TWFC.exe")
        assertThat(transformers.requiredFiles).contains("Binaries/TWFC.exe")
        assertThat(transformers.fileRules.single().sha256)
            .isEqualTo("1db04b09b36094571fab861cd01ca175d44d610b5417f950f37523cd97eeb016")
        assertThat(transformers.nativeDllOverrides)
            .containsExactly("dinput8", "native,builtin")
        assertThat(deusEx.fileRules.single().sha256)
            .isEqualTo("7a3ebf1397ecdb9f6bd3b624cb5346d6a3a24107e516126d0a1ede1c62a8e7fa")
        assertThat(deadIsland.fileRules.mapNotNull { it.bytes }).containsExactly(
            1_072_640L,
            11_481_024L,
            21_069_760L,
        ).inOrder()
    }
}
