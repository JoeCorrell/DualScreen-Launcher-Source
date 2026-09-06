package com.dualscreen.launcher

import com.dualscreen.launcher.switch.SwitchSystemState
import com.google.common.truth.Truth.assertThat
import java.io.File
import org.junit.Test

/** Wiring regressions: installed choices must never navigate to the downloader. */
class SwitchDriverSelectionTest {
    private fun source(name: String) =
        File("src/main/kotlin/com/dualscreen/launcher/$name").readText()

    @Test
    fun installedDriverButtonsSelectTheirPathWithoutNavigating() {
        val ui = source("LauncherSettingsGuideUi.kt")
        val choices = ui.substringAfter("items(switchSystemState.gpuDrivers")
            .substringBefore("label = \"Open Eden driver downloader\"")
        assertThat(choices).contains("onClick = { onSwitchGpuDriver(driver.path) }")
        assertThat(choices).doesNotContain("onClick = onOpenSwitchDriverDownloader")
        assertThat(choices).contains("driver.path == switchSystemState.selectedGpuDriverPath")
        assertThat(choices).contains("enabled = switchSystemState.initialized && !switchOperation.running")
        assertThat(ui.substringAfter("label = \"Open Eden driver downloader\""))
            .contains("onClick = onOpenSwitchDriverDownloader")
        assertThat(source("LauncherScreen.kt"))
            .contains("onSwitchGpuDriver = model::selectSwitchGlobalGpuDriver")
    }

    @Test
    fun globalSelectionIsReadFromNativeSettingsAndRefreshedAfterSaving() {
        val backend = source("switch/EdenSwitchBackend.kt")
        assertThat(backend).contains("selectedGpuDriverPath = StringSetting.DRIVER_PATH.getString(true)")
        val selection = backend.substringAfter("suspend fun selectGlobalGpuDriver(")
            .substringBefore("suspend fun createLaunchIntent(")
        assertThat(selection).contains("StringSetting.DRIVER_PATH.setString(driverPath)")
        assertThat(selection.substringAfter("NativeConfig.saveGlobalConfig()"))
            .contains("refreshSystemState()")
        val save = backend.substringAfter("suspend fun saveGlobalSettings(")
            .substringBefore("suspend fun selectGlobalGpuDriver(")
        assertThat(save.substringAfter("NativeConfig.saveGlobalConfig()"))
            .contains("refreshSystemState()")
    }

    @Test
    fun unloadedStateDoesNotPretendSystemDriverIsSelected() {
        val state = SwitchSystemState()
        assertThat(state.selectedGpuDriverPath).isNull()
        assertThat(state.copy(selectedGpuDriverPath = "").selectedGpuDriverPath).isEmpty()
        assertThat(state.copy(selectedGpuDriverPath = "/drivers/turnip.zip").selectedGpuDriverPath)
            .isEqualTo("/drivers/turnip.zip")
    }
}
