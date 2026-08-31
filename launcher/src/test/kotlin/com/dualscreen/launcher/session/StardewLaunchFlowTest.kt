package com.dualscreen.launcher.session

import com.google.common.truth.Truth.assertThat
import org.junit.Test

class StardewLaunchFlowTest {
    @Test
    fun directPlayUsesDeferredBridgeLaunch() {
        assertThat(shouldUseDeferredStardewLaunch("stardew-valley", configurationLauncher = false))
            .isTrue()
    }

    @Test
    fun configurationLauncherAndOtherGamesRemainDirect() {
        assertThat(shouldUseDeferredStardewLaunch("stardew-valley", configurationLauncher = true))
            .isFalse()
        assertThat(shouldUseDeferredStardewLaunch("skyrim-se", configurationLauncher = false))
            .isFalse()
    }

    @Test
    fun deferredCommandQuotesExecutableAndPreservesArguments() {
        assertThat(
            deferredGuestCommand(
                "C:\\Games\\stardew-valley\\Stardew Valley.exe",
                "--debug \"profile one\"",
            ),
        ).isEqualTo(
            "\"C:\\Games\\stardew-valley\\Stardew Valley.exe\" --debug \"profile one\"",
        )
    }
}
