package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class Fallout3LaunchConfigurationTest {
    @get:Rule
    val temporary = TemporaryFolder()

    @Test
    fun seedsRegistryPreferencesAndControllerDefaults() {
        val container = temporary.newFolder("container")
        val prefix = container.resolve(".wine")
        prefix.mkdirs()
        prefix.resolve("system.reg").writeText("WINE REGISTRY Version 2\n")
        val game = temporary.newFolder("fallout3")
        game.resolve("Fallout.ini").writeText("[Controls]\nbUse Joystick=0\n")
        game.resolve("Fallout3").mkdirs()
        game.resolve("Fallout3/FalloutPrefs.ini").writeText("[Display]\niSize W=1280\n")

        val result = Fallout3LaunchConfiguration.ensure(
            container,
            game,
            "C:\\Games\\fallout3",
            1280,
            720,
        )

        assertThat(result.installedPath).isEqualTo("C:\\Games\\fallout3\\")
        assertThat(result.seededFalloutIni).isTrue()
        assertThat(result.seededPreferences).isTrue()
        assertThat(result.falloutIni.readText()).contains("bUse Joystick=1")
        assertThat(result.falloutIni.readText()).contains("bDisable360Controller=0")
        assertThat(result.preferencesFile.readText()).contains("bUse Joystick=1")
        assertThat(result.preferencesFile.readText()).contains("iSize W=1280")
        assertThat(result.preferencesFile.readText()).contains("iSize H=720")
        assertThat(result.systemRegistry.readText()).contains("Installed Path")
        assertThat(result.systemRegistry.readText()).contains("C:\\\\Games\\\\fallout3\\\\")
    }

    @Test
    fun preservesExistingPreferencesWhileRepairingControllerValues() {
        val container = temporary.newFolder("container-existing")
        val prefix = container.resolve(".wine")
        prefix.mkdirs()
        prefix.resolve("system.reg").writeText("WINE REGISTRY Version 2\n")
        val settings = container.resolve(
            ".wine/drive_c/users/xuser/Documents/My Games/Fallout3",
        )
        settings.mkdirs()
        settings.resolve("FALLOUT.INI").writeText(
            "[Display]\niSize W=1600\n[Controls]\nbUse Joystick=0\n" +
                "[Interface]\nbDisable360Controller=1\n",
        )
        settings.resolve("FalloutPrefs.ini").writeText(
            "[Display]\niSize W=1600\n[Controls]\nbUse Joystick=0\n" +
                "[Interface]\nbDisable360Controller=1\n",
        )
        val game = temporary.newFolder("fallout3-existing")

        val result = Fallout3LaunchConfiguration.ensure(
            container,
            game,
            "C:\\Games\\fallout3\\",
            1280,
            720,
        )

        assertThat(result.seededFalloutIni).isFalse()
        assertThat(result.seededPreferences).isFalse()
        assertThat(result.preferencesFile.readText()).contains("iSize W=1280")
        assertThat(result.preferencesFile.readText()).contains("iSize H=720")
        assertThat(result.preferencesFile.readText()).contains("bUse Joystick=1")
        assertThat(result.preferencesFile.readText()).contains("bDisable360Controller=0")
    }
}
