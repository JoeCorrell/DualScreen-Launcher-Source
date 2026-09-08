package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import org.junit.Test

class Ac2TranslatorConfigurationTest {
    @Test fun ac2MigratesForcedWowBox64ToWorkingWemuRouting() {
        val extras = supportedGameTranslatorExtras("assassins-creed-ii")
        assertThat(extras["emulator"]).isEqualTo("FEXCore")
        assertThat(extras["ac2WowBox64"]).isEqualTo("0")
        assertThat(extras["fexcorePreset"]).isEqualTo("PERFORMANCE")
        assertThat(extras["box64Version"]).isEqualTo("0.4.2")
        assertThat(extras["box64Preset"]).isEqualTo("PERFORMANCE")
        val env = supportedGameLaunchEnvironment("assassins-creed-ii")
        assertThat(env).doesNotContain("BOX64_")
        assertThat(env).contains("FAKE_EVDEV_XBOX360=1")
        assertThat(env).doesNotContain("ALSA")
        assertThat(env.split(' ').filter { it.startsWith("FEX_") }).containsExactly(
            "FEX_X87REDUCEDPRECISION=0", "FEX_DYNAMICL1CACHE=1", "FEX_DISABLEL2CACHE=0",
        )
        for (game in GameCatalog.games.filter { it.id != "assassins-creed-ii" }) {
            assertThat(supportedGameLaunchEnvironment(game.id)).doesNotContain("FEX_")
        }
        for (game in GameCatalog.games.filter { it.id != "assassins-creed-ii" && it.id != "crysis3" }) {
            assertThat(supportedGameTranslatorExtras(game.id)).isEmpty()
            assertThat(supportedGameLaunchEnvironment(game.id)).doesNotContain("BOX64_")
        }
    }
}
