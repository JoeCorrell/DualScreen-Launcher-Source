package com.dualscreen.launcher

import com.dualscreen.launcher.switch.EdenSettingType
import com.dualscreen.launcher.switch.EdenSettingValue
import com.dualscreen.launcher.switch.displayValue
import com.dualscreen.launcher.switch.edenSettingChoices
import com.google.common.truth.Truth.assertThat
import java.io.File
import org.junit.Test

class EdenSettingControlsTest {
    private fun setting(type: EdenSettingType = EdenSettingType.INTEGER) = EdenSettingValue(
        key = "test", label = "Test", category = "Graphics", type = type,
        value = "6", defaultValue = "0", useGlobal = false, runtimeModifiable = false,
    )

    @Test
    fun choicesKeepNativeValuesRatherThanUsingListIndices() {
        val choices = edenSettingChoices(listOf("Off", "Option A", "Option B"), listOf("0", "2", "6"))
        assertThat(choices.map { it.value }).containsExactly("0", "2", "6").inOrder()
        assertThat(setting().copy(choices = choices).displayValue("6")).isEqualTo("Option B")
        assertThat(setting().copy(choices = choices).displayValue("9")).isEqualTo("9")
    }

    @Test(expected = IllegalArgumentException::class)
    fun mismatchedArraysAreRejected() {
        edenSettingChoices(listOf("One", "Two"), listOf("0"))
    }

    @Test(expected = IllegalArgumentException::class)
    fun duplicateStoredValuesAreRejected() {
        edenSettingChoices(listOf("One", "Two"), listOf("0", "0"))
    }

    @Test
    fun booleanLabelsDoNotShowRawNumbers() {
        val toggle = setting(EdenSettingType.BOOLEAN)
        assertThat(toggle.displayValue("1")).isEqualTo("On")
        assertThat(toggle.displayValue("true")).isEqualTo("On")
        assertThat(toggle.displayValue("0")).isEqualTo("Off")
        assertThat(toggle.displayValue("false")).isEqualTo("Off")
    }

    @Test
    fun nativeMetadataProvidesControlsWithoutDuplicatingEnumTables() {
        val metadata = File("src/main/kotlin/com/dualscreen/launcher/switch/EdenSettingPresentation.kt").readText()
        assertThat(metadata).contains("SettingsItem.settingsItems[key]")
        assertThat(metadata).contains("getStringArray(item.choicesId)")
        assertThat(metadata).contains("getIntArray(item.valuesId)")
        assertThat(metadata).contains("item.warnChoices")
        assertThat(metadata).contains("item.min")
        assertThat(metadata).contains("item.max")
        val ui = File("src/main/kotlin/com/dualscreen/launcher/SwitchSettingsUi.kt").readText()
        assertThat(ui).contains("setting.type == EdenSettingType.BOOLEAN -> Switch(")
        assertThat(ui).contains("setting.choices.isNotEmpty() -> EdenChoiceDropdown(")
        assertThat(ui).contains("DropdownMenu(")
        assertThat(ui).contains("onSelected(choice.value)")
        assertThat(ui).contains("setting.globalValue else setting.value")
    }
}
