package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import org.junit.Test

class GameCatalogTest {
    @Test
    fun catalogIsClosedAndEveryGameHasInstallAndLaunchRules() {
        assertThat(GameCatalog.games.map { it.id }).containsExactly(
            "skyrim-se",
            "fallout3",
            "fallout4",
            "grim-dawn",
            "deus-ex-hrdc",
            "dead-island-de",
            "valheim",
            "stardew-valley",
        ).inOrder()
        assertThat(GameCatalog.games.map { it.id }.distinct()).hasSize(GameCatalog.games.size)

        GameCatalog.games.forEach { game ->
            assertThat(game.executableCandidates).isNotEmpty()
            assertThat(game.requiredFiles).isNotEmpty()
            assertThat(game.saveDirectory).isNotEmpty()
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
    fun saveLocationsMatchEachGamesWindowsProfileConvention() {
        assertThat(GameCatalog.byId("skyrim-se")?.saveDirectory)
            .isEqualTo("Documents/My Games/Skyrim Special Edition/Saves")
        assertThat(GameCatalog.byId("fallout3")?.saveDirectory)
            .isEqualTo("Documents/My Games/Fallout3/Saves")
        assertThat(GameCatalog.byId("fallout4")?.saveDirectory)
            .isEqualTo("Documents/My Games/Fallout4/Saves")
        assertThat(GameCatalog.byId("grim-dawn")?.saveDirectory)
            .isEqualTo("Documents/My Games/Grim Dawn/save")
        assertThat(GameCatalog.byId("valheim")?.saveDirectory)
            .isEqualTo("AppData/LocalLow/IronGate/Valheim")
        assertThat(GameCatalog.byId("stardew-valley")?.saveDirectory)
            .isEqualTo("AppData/Roaming/StardewValley/Saves")
    }

    @Test
    fun onlySkyrimAndDeusExAreNotMarkedBeta() {
        val releaseGames = GameCatalog.games.filterNot { it.isBeta }.map { it.id }
        assertThat(releaseGames).containsExactly("skyrim-se", "deus-ex-hrdc")
        assertThat(GameCatalog.byId("fallout3")).isNotNull()
        assertThat(GameCatalog.byId("fallout4")).isNotNull()
        assertThat(GameCatalog.byId("grim-dawn")).isNotNull()
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
        assertThat(stardew.panelMode).isEqualTo(LowerPanelMode.NATIVE_WINDOW)
        assertThat(stardew.requiredFiles).contains("Stardew Valley.dll")
        assertThat(stardew.fileRules.single().sha256AnyOf).containsExactly(
            "078c04dcf34fdd8df3ec1ddce062d3dfd4a34d94614d806466adc78754a7693c",
            "7f1e5b8e58d2758b78570ba771bbeb03d33522f62188bf6c32edf0cf626deaee",
            "7579cca523d19470b4f842b9068b6bf9b8f96efe632b83471bc62cae7f7b3b90",
            "f3a92e73f1cdf7c0d4eb5f995b2555244fbe16d33429e26c2b425f6eb1480c63",
        )
    }

    @Test
    fun addressSensitiveNativeModsAreVersionGated() {
        val skyrim = checkNotNull(GameCatalog.byId("skyrim-se"))
        val fallout3 = checkNotNull(GameCatalog.byId("fallout3"))
        val fallout4 = checkNotNull(GameCatalog.byId("fallout4"))
        val grimDawn = checkNotNull(GameCatalog.byId("grim-dawn"))
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
        assertThat(deusEx.fileRules.single().sha256)
            .isEqualTo("7a3ebf1397ecdb9f6bd3b624cb5346d6a3a24107e516126d0a1ede1c62a8e7fa")
        assertThat(deadIsland.fileRules.mapNotNull { it.bytes }).containsExactly(
            1_072_640L,
            11_481_024L,
            21_069_760L,
        ).inOrder()
    }
}
