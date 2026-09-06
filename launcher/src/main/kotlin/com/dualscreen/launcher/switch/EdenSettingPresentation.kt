package com.dualscreen.launcher.switch

import android.content.Context
import android.util.Log
import org.yuzu.yuzu_emu.features.settings.model.view.IntSingleChoiceSetting
import org.yuzu.yuzu_emu.features.settings.model.view.SettingsItem
import org.yuzu.yuzu_emu.features.settings.model.view.SingleChoiceSetting
import org.yuzu.yuzu_emu.features.settings.model.view.SliderSetting
import org.yuzu.yuzu_emu.features.settings.model.view.SpinBoxSetting
import org.yuzu.yuzu_emu.features.settings.model.view.StringSingleChoiceSetting

/** Reuse the embedded Eden UI's own localized labels, enum mappings and ranges. */
internal fun EdenSettingValue.withEdenPresentation(context: Context): EdenSettingValue {
    val item = SettingsItem.settingsItems[key] ?: return this
    val options = when (item) {
        is SingleChoiceSetting -> {
            val labels = context.resources.getStringArray(item.choicesId).toList()
            val values = context.resources.getIntArray(item.valuesId).map(Int::toString)
            // Some upstream tables have unused trailing labels/values (overlay
            // positions and app language). Never invent an enum value or hide the
            // entire setting when that happens; show only complete native pairs.
            val count = minOf(labels.size, values.size)
            if (labels.size != values.size) {
                Log.w("EdenSettingPresentation", "$key has ${labels.size} labels and ${values.size} values; using $count valid pairs")
            }
            edenSettingChoices(labels.take(count), values.take(count)).map { choice ->
                if (choice.value.toInt() in item.warnChoices && item.warningMessage != 0) {
                    choice.copy(warning = context.getString(item.warningMessage))
                } else choice
            }
        }
        is IntSingleChoiceSetting -> edenSettingChoices(item.choices.toList(), item.values.map(Int::toString))
        is StringSingleChoiceSetting -> edenSettingChoices(item.choices.toList(), item.values.toList())
        else -> emptyList()
    }
    return copy(
        label = item.title.ifBlank { label },
        description = item.description,
        choices = options,
        minimum = when (item) { is SliderSetting -> item.min; is SpinBoxSetting -> item.min; else -> null },
        maximum = when (item) { is SliderSetting -> item.max; is SpinBoxSetting -> item.max; else -> null },
        slider = item is SliderSetting,
        units = (item as? SliderSetting)?.units.orEmpty(),
    )
}
