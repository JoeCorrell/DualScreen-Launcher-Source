package com.dualscreen.launcher.session

import com.dualscreen.launcher.GameCatalog
import com.google.common.truth.Truth.assertThat
import org.junit.Test

class SessionWindowMatchingTest {
    private val fallout3 = checkNotNull(GameCatalog.byId("fallout3"))
    private val transformers = checkNotNull(GameCatalog.byId("transformers-wfc"))

    @Test
    fun `steam launcher executable class dismisses configuration launch cover`() {
        assertThat(
            matchesSessionWindow(
                " FalloutLauncherSteam.exe",
                fallout3,
                configurationLauncher = true,
            ),
        ).isTrue()
    }

    @Test
    fun `spaced launcher title is matched without punctuation`() {
        assertThat(
            matchesSessionWindow(
                "Fallout Launcher Steam launcher-window",
                fallout3,
                configurationLauncher = true,
            ),
        ).isTrue()
    }

    @Test
    fun `configuration utility cannot masquerade as direct game window`() {
        assertThat(
            matchesSessionWindow(
                " FalloutLauncherSteam.exe",
                fallout3,
                configurationLauncher = false,
            ),
        ).isFalse()
    }

    @Test
    fun `direct fallout executable still matches play session`() {
        assertThat(
            matchesSessionWindow(
                "Fallout 3 Fallout3.exe",
                fallout3,
                configurationLauncher = false,
            ),
        ).isTrue()
    }

    @Test
    fun `catalog subdirectory does not hide direct WFC executable window`() {
        assertThat(matchesDirectExecutableWindow(" twfc.exe", transformers)).isTrue()
    }
}
