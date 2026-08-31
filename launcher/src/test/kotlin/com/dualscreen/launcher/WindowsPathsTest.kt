package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class WindowsPathsTest {
    @get:Rule
    val temporaryFolder = TemporaryFolder()

    @Test
    fun resolvesWindowsPathsWithoutAndroidCaseSensitivity() {
        val root = temporaryFolder.newFolder("game")
        val data = root.resolve("DATA").apply { mkdirs() }
        val executable = root.resolve("SKYRIMSE.EXE").apply { writeBytes(byteArrayOf(1)) }
        val master = data.resolve("skyrim.ESM").apply { writeBytes(byteArrayOf(2)) }

        assertThat(resolveWindowsFile(root, "SkyrimSE.exe")).isEqualTo(executable)
        assertThat(resolveWindowsFile(root, "Data/Skyrim.esm")).isEqualTo(master)
        assertThat(resolveLaunchExecutable(root, checkNotNull(GameCatalog.byId("skyrim-se"))))
            .isEqualTo("SKYRIMSE.EXE")
    }

    @Test
    fun rejectsPathsThatCouldEscapeTheGameRoot() {
        val root = temporaryFolder.newFolder("game-root")

        assertThat(resolveWindowsFile(root, "../SkyrimSE.exe")).isNull()
    }

    @Test
    fun resolvesConfigurationLauncherSeparatelyFromDirectPlayExecutable() {
        val root = temporaryFolder.newFolder("fallout4")
        root.resolve("Fallout4.exe").writeBytes(byteArrayOf(1))
        val launcher = root.resolve("FALLOUT4LAUNCHER.EXE").apply { writeBytes(byteArrayOf(2)) }
        val game = checkNotNull(GameCatalog.byId("fallout4"))

        assertThat(resolveLaunchExecutable(root, game)).isEqualTo("Fallout4.exe")
        assertThat(resolveConfigurationLauncherExecutable(root, game))
            .isEqualTo(launcher.name)
    }

    @Test
    fun resolvesFallout3SteamLauncherRegardlessOfCapitalization() {
        val root = temporaryFolder.newFolder("fallout3")
        val launcher = root.resolve("falloutlaunchersteam.EXE").apply { writeBytes(byteArrayOf(3)) }
        val game = checkNotNull(GameCatalog.byId("fallout3"))

        assertThat(resolveConfigurationLauncherExecutable(root, game))
            .isEqualTo(launcher.name)
    }
}
