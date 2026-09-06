package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import java.io.File
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class Ac2GamepadConfigurationTest {
    @get:Rule val temporary = TemporaryFolder()

    @Test fun onlyChangesInputSectionAndPreservesGraphicsAndBindings() {
        val original = "[Graphics]\r\nSelectedInput=unrelated\r\nWidth=1920\r\n" +
            "[Input]\r\nSelectedInput=KeyboardMouse2\r\nInvertMouse=1\r\n" +
            "[KeyboardMouse2]\r\nButton1=32\r\n"
        val result = Ac2GamepadConfiguration.selectController(original)
        assertThat(result).isEqualTo(original.replace("SelectedInput=KeyboardMouse2", "SelectedInput=${Ac2GamepadConfiguration.CONTROLLER_NAME}"))
        assertThat(Ac2GamepadConfiguration.selectController(result)).isEqualTo(result)
    }

    @Test fun addsMissingSectionAndMissingKey() {
        for (original in listOf("", "[Graphics]\nWidth=1920", "[Input]\nInvertMouse=1\n[Graphics]\nWidth=1920\n")) {
            val result = Ac2GamepadConfiguration.selectController(original)
            assertThat(result).contains("[Input]\nSelectedInput=${Ac2GamepadConfiguration.CONTROLLER_NAME}\n")
            assertThat(Ac2GamepadConfiguration.selectController(result)).isEqualTo(result)
        }
    }

    @Test fun insertsMissingKeyWithoutDamagingWindowsNewlines() {
        val result = Ac2GamepadConfiguration.selectController("[Input]\r\nInvertMouse=1\r\n")
        assertThat(result).isEqualTo("[Input]\r\nSelectedInput=${Ac2GamepadConfiguration.CONTROLLER_NAME}\r\nInvertMouse=1\r\n")
    }

    @Test fun backsUpExistingLegacyPreferencesOnceAndDoesNotTouchSaves() {
        val root = temporary.newFolder()
        val preferences = File(root, ".wine/drive_c/users/xuser/Application Data/Ubisoft/Assassin's Creed 2/Assassin2.ini")
        preferences.parentFile.mkdirs()
        val original = "[Input]\nSelectedInput=KeyboardMouse2\n[Graphics]\nWidth=1920\n"
        preferences.writeText(original)
        val save = File(preferences.parentFile, "save.dat").apply { writeText("player save") }
        val first = Ac2GamepadConfiguration.ensureEnabled(root)
        assertThat(first.preferencesFile).isEqualTo(preferences)
        assertThat(first.changed).isTrue()
        assertThat(Ac2GamepadConfiguration.ensureEnabled(root).changed).isFalse()
        assertThat(File(preferences.parentFile, "Assassin2.ini.before-controller").readText()).isEqualTo(original)
        assertThat(save.readText()).isEqualTo("player save")
    }

    @Test fun xboxIdentityIsLimitedToAc2LaunchEnvironment() {
        assertThat(supportedGameLaunchEnvironment("assassins-creed-ii")).contains("FAKE_EVDEV_XBOX360=1")
        for (game in GameCatalog.games.filter { it.id != "assassins-creed-ii" }) {
            assertThat(supportedGameLaunchEnvironment(game.id)).doesNotContain("FAKE_EVDEV_XBOX360")
        }
    }
}
