package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import org.junit.Test

class InstallProgressTest {
    @Test
    fun failurePreservesTheLastRealOverallProgress() {
        val previous = InstallProgress(
            gameId = "skyrim-se",
            stage = InstallStage.VERIFYING,
            fraction = 0.96f,
            running = true,
        )

        assertThat(monotonicInstallFraction(previous, 0f)).isEqualTo(0.96f)
    }

    @Test
    fun consoleUsesTheMostRecentlyUpdatedGameNotMapInsertionOrder() {
        val recentlyUpdated = InstallProgress(
            gameId = "skyrim-se",
            updateSequence = 20,
        )
        val insertedLastButOlder = InstallProgress(
            gameId = "valheim",
            updateSequence = 10,
        )

        assertThat(latestInstallProgress(listOf(recentlyUpdated, insertedLastButOlder)))
            .isEqualTo(recentlyUpdated)
    }

    @Test
    fun activeInstallAlwaysWinsOverAnOlderCompletedEntry() {
        val active = InstallProgress(
            gameId = "skyrim-se",
            running = true,
            updateSequence = 10,
        )
        val completed = InstallProgress(
            gameId = "valheim",
            stage = InstallStage.COMPLETE,
            fraction = 1f,
            updateSequence = 20,
        )

        assertThat(latestInstallProgress(listOf(active, completed))).isEqualTo(active)
    }

    @Test
    fun logPercentIsExplicitlyTheSameOverallValueUsedByTheHeader() {
        assertThat(formatInstallLogLine(0.71f, "Extracting · Skyrim.esm"))
            .isEqualTo("[OVERALL  71%] Extracting · Skyrim.esm")
    }

    @Test
    fun supportedGameLaunchesSuppressInvisibleWineAddonDialogs() {
        assertThat(supportedGameLaunchEnvironment())
            .contains("WINEDLLOVERRIDES=icu=n;mscoree=d;mshtml=d")
    }
}
