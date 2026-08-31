package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class SkyrimGamepadConfigurationTest {
    @get:Rule
    val temporaryFolder = TemporaryFolder()

    @Test
    fun enablesExistingGamepadPreferenceWithoutReplacingOtherSettings() {
        val current = "[Display]\r\nbUseTAA=0\r\n\r\n[MAIN]\r\nbGamepadEnable=0\r\nbGamePadRumble=1\r\n"

        val updated = SkyrimGamepadConfiguration.enableGamepad(
            current,
            "[MAIN]\r\nbGamepadEnable=1\r\n",
        )

        assertThat(updated).contains("bGamepadEnable=1")
        assertThat(updated).contains("bUseTAA=0")
        assertThat(updated).contains("bGamePadRumble=1")
        assertThat(updated).doesNotContain("bGamepadEnable=0")
    }

    @Test
    fun installsBundledPreferenceIntoFreshWineProfile() {
        val containerRoot = temporaryFolder.newFolder("container")
        val gameRoot = temporaryFolder.newFolder("game")
        gameRoot.resolve("SkyrimGamepad.ini").writeText("[MAIN]\nbGamepadEnable=1\n")

        val result = SkyrimGamepadConfiguration.ensureEnabled(containerRoot, gameRoot)

        assertThat(result.changed).isTrue()
        assertThat(result.preferencesFile.readText()).contains("[MAIN]\nbGamepadEnable=1")
        assertThat(result.preferencesFile.readText()).contains("[Display]")
        assertThat(result.preferencesFile.readText()).contains("bBorderless=1")
        assertThat(result.preferencesFile.readText()).contains("iSize W=1280")
        assertThat(result.preferencesFile.readText()).contains("iSize H=720")
        assertThat(result.preferencesFile.path.replace('\\', '/'))
            .endsWith("/.wine/drive_c/users/xuser/Documents/My Games/Skyrim Special Edition/SkyrimPrefs.ini")
    }

    @Test
    fun secondApplyDoesNotRewritePreferences() {
        val containerRoot = temporaryFolder.newFolder("container")
        val gameRoot = temporaryFolder.newFolder("game")
        gameRoot.resolve("SkyrimGamepad.ini").writeText("[MAIN]\nbGamepadEnable=1\n")

        SkyrimGamepadConfiguration.ensureEnabled(containerRoot, gameRoot)
        val second = SkyrimGamepadConfiguration.ensureEnabled(containerRoot, gameRoot)

        assertThat(second.changed).isFalse()
    }

    @Test
    fun repairsOlderInstallThatPredatesBundledOverride() {
        val containerRoot = temporaryFolder.newFolder("old-container")
        val gameRoot = temporaryFolder.newFolder("old-game")

        SkyrimGamepadConfiguration.ensureEnabled(
            containerRoot,
            gameRoot,
            "[MAIN]\nbGamepadEnable=1\n",
        )

        assertThat(gameRoot.resolve("SkyrimGamepad.ini").isFile).isTrue()
    }

    @Test
    fun appliesFullscreenResolutionWithoutLosingExistingDisplaySettings() {
        val updated = SkyrimGamepadConfiguration.configurePreferences(
            current = "[Display]\r\nbUseTAA=1\r\nbBorderless=0\r\nbFull Screen=1\r\n" +
                "iSize W=1280\r\niSize H=720\r\n[MAIN]\r\nbGamepadEnable=0\r\n",
            bundled = "[MAIN]\r\nbGamepadEnable=1\r\n",
            resolution = GameResolution.FULL_HD,
        )

        assertThat(updated).contains("bGamepadEnable=1")
        assertThat(updated).contains("bBorderless=1")
        assertThat(updated).contains("bFull Screen=0")
        assertThat(updated).contains("iSize W=1920")
        assertThat(updated).contains("iSize H=1080")
        assertThat(updated).contains("bUseTAA=1")
    }

    @Test
    fun createsDisplaySectionForFreshPreferences() {
        val updated = SkyrimGamepadConfiguration.configurePreferences(
            current = "",
            bundled = "[MAIN]\nbGamepadEnable=1\n",
            resolution = GameResolution.HD_PLUS,
        )

        assertThat(updated).contains("[Display]")
        assertThat(updated).contains("bBorderless=1")
        assertThat(updated).contains("iSize W=1600")
        assertThat(updated).contains("iSize H=900")
    }
}
