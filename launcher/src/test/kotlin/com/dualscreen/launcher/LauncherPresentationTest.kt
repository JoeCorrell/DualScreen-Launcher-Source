package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import org.junit.Test

class LauncherPresentationTest {
    private val installed = InstalledGame(
        gameId = "fallout3",
        containerId = 3,
        shortcutPath = "shortcut.json",
        hostDirectory = "Fallout 3",
        installedBytes = 1024,
        payloadVersion = "test",
    )

    @Test
    fun `ready state requires an installed game`() {
        assertThat(resolveGameInstallState(installed, null, null))
            .isEqualTo(GameInstallState.READY)
        assertThat(resolveGameInstallState(null, null, null))
            .isEqualTo(GameInstallState.NOT_INSTALLED)
    }

    @Test
    fun `active work takes priority over installed state`() {
        val installing = InstallProgress(
            gameId = "fallout3",
            stage = InstallStage.EXTRACTING,
            fraction = 0.5f,
            running = true,
        )
        val importing = SaveImportProgress(
            gameId = "fallout3",
            stage = SaveImportStage.EXTRACTING,
            fraction = 0.5f,
            detail = "Extracting",
            log = emptyList(),
            running = true,
        )

        assertThat(resolveGameInstallState(installed, installing, null))
            .isEqualTo(GameInstallState.INSTALLING)
        assertThat(resolveGameInstallState(installed, null, importing))
            .isEqualTo(GameInstallState.IMPORTING_SAVE)
    }

    @Test
    fun `payload refresh has a dedicated mod update state`() {
        val updating = InstallProgress(
            gameId = "fallout3",
            stage = InstallStage.PATCHING,
            fraction = InstallStage.PATCHING.baseProgress,
            running = true,
        )

        assertThat(resolveGameInstallState(installed, updating, null))
            .isEqualTo(GameInstallState.UPDATING_MOD)
        assertThat(modUpdateFraction(updating)).isEqualTo(0f)
        assertThat(
            modUpdateFraction(
                updating.copy(
                    fraction = (
                        InstallStage.PATCHING.baseProgress +
                            InstallStage.CONFIGURING.baseProgress
                        ) / 2f,
                ),
            ),
        ).isWithin(0.001f).of(0.45f)
        assertThat(
            modUpdateFraction(
                updating.copy(
                    stage = InstallStage.VERIFYING,
                    fraction = InstallStage.VERIFYING.baseProgress,
                ),
            ),
        ).isEqualTo(0.96f)
    }

    @Test
    fun `errors remain visible after work stops`() {
        val failed = InstallProgress(
            gameId = "fallout3",
            stage = InstallStage.FAILED,
            error = "Invalid archive",
        )

        assertThat(resolveGameInstallState(null, failed, null))
            .isEqualTo(GameInstallState.ERROR)
    }
}
