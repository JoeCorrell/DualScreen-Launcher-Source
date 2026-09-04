package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import org.junit.Test
import java.nio.file.Files

class WfcDisplayConfigurationTest {
    @Test
    fun configuresAWindowedUpperPanelWithoutDroppingExistingSettings() {
        val current = """
            [SystemSettings]
            ResX=1920
            ResY=1080
            Fullscreen=True
            MotionBlur=True

            [WinDrv.WindowsClient]
            StartupFullscreen=True
            MinDesiredFrameRate=35
        """.trimIndent() + "\n"

        val updated = WfcDisplayConfiguration.configure(current, GameResolution.HD)

        assertThat(updated).contains("ResX=1280")
        assertThat(updated).contains("ResY=720")
        assertThat(updated).contains("Fullscreen=False")
        assertThat(updated).contains("StartupFullscreen=False")
        assertThat(updated).contains("MotionBlur=True")
        assertThat(updated).contains("MinDesiredFrameRate=35")
    }

    @Test
    fun createsAndThenVerifiesTheRealUe3ProfileIdempotently() {
        val root = Files.createTempDirectory("wfc-display").toFile()
        try {
            root.resolve(".wine").mkdirs()
            root.resolve(".wine/user.reg").writeText("WINE REGISTRY Version 2\n")
            val first = WfcDisplayConfiguration.ensure(root, GameResolution.HD)
            val second = WfcDisplayConfiguration.ensure(root, GameResolution.HD)

            assertThat(first.changed).isTrue()
            assertThat(second.changed).isFalse()
            assertThat(first.profile.path.replace('\\', '/'))
                .endsWith("Documents/My Games/Transformers/TransGame/Config/TransEngine.ini")
            assertThat(first.profile.readText()).contains("Fullscreen=False")
            assertThat(first.nativeResolution)
                .isEqualTo("Width:1280 Height:720 Fullscreen:0")
            assertThat(first.userRegistry.readText()).contains(
                "[Software\\\\Activision\\\\TransformersWFC]",
            )
            assertThat(first.userRegistry.readText()).contains(
                "\"ScreenResolution\"=\"Width:1280 Height:720 Fullscreen:0\"",
            )
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun usesTheExactNativePcSettingManagerRegistryFormat() {
        assertThat(WfcDisplayConfiguration.nativeResolution(GameResolution.HD))
            .isEqualTo("Width:1280 Height:720 Fullscreen:0")
    }

    @Test
    fun launchArgumentsForceTheViewportBeforeSavedConfigCanOverrideIt() {
        assertThat(WfcDisplayConfiguration.launchArguments(GameResolution.HD))
            .isEqualTo("-windowed -ResX=1280 -ResY=720")
    }

    @Test(expected = IllegalArgumentException::class)
    fun rejectsAResolutionThatWouldViolateTheNativeViewportContract() {
        WfcDisplayConfiguration.launchArguments(GameResolution.FULL_HD)
    }
}
