package com.dualscreen.launcher

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.CloudDownload
import androidx.compose.material.icons.filled.FileDownload
import androidx.compose.material.icons.filled.Restore
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.dualscreen.launcher.switch.EdenSettingType
import com.dualscreen.launcher.switch.EdenSettingValue
import com.dualscreen.launcher.switch.EdenSettingChoice
import com.dualscreen.launcher.switch.displayValue
import com.dualscreen.launcher.switch.SwitchGameDefinition
import com.dualscreen.launcher.switch.SwitchGameSettings
import com.dualscreen.launcher.switch.SwitchOperationState
import com.dualscreen.launcher.switch.SwitchSystemState
import kotlin.math.roundToInt

@Composable
internal fun SwitchGameSettingsPage(
    game: SwitchGameDefinition,
    operation: SwitchOperationState,
    systemState: SwitchSystemState,
    loader: suspend () -> Result<SwitchGameSettings>,
    onSave: (SwitchGameSettings) -> Unit,
    onDriverSelected: (String) -> Unit,
    onOpenDriverDownloader: () -> Unit,
    onImportDriver: () -> Unit,
    onBack: () -> Unit,
) {
    EdenSettingsPage(
        scopeKey = game.id,
        eyebrow = "EDEN DS  •  PER-GAME CONFIGURATION",
        title = game.title,
        scopeDescription = "only this title is changed",
        initialCategory = "Graphics",
        global = false,
        operation = operation,
        systemState = systemState,
        loader = loader,
        onSave = onSave,
        onDriverSelected = onDriverSelected,
        onOpenDriverDownloader = onOpenDriverDownloader,
        onImportDriver = onImportDriver,
        onBack = onBack,
    )
}

@Composable
internal fun SwitchGlobalSettingsPage(
    operation: SwitchOperationState,
    systemState: SwitchSystemState,
    loader: suspend () -> Result<SwitchGameSettings>,
    onSave: (SwitchGameSettings) -> Unit,
    onDriverSelected: (String) -> Unit,
    onOpenDriverDownloader: () -> Unit,
    onImportDriver: () -> Unit,
    onBack: () -> Unit,
) {
    EdenSettingsPage(
        scopeKey = "eden-global",
        eyebrow = "EDEN DS  •  GLOBAL CONFIGURATION",
        title = "Eden global settings",
        scopeDescription = "used by every Switch title unless overridden",
        initialCategory = "System",
        global = true,
        operation = operation,
        systemState = systemState,
        loader = loader,
        onSave = onSave,
        onDriverSelected = onDriverSelected,
        onOpenDriverDownloader = onOpenDriverDownloader,
        onImportDriver = onImportDriver,
        onBack = onBack,
    )
}

@Composable
private fun EdenSettingsPage(
    scopeKey: String,
    eyebrow: String,
    title: String,
    scopeDescription: String,
    initialCategory: String,
    global: Boolean,
    operation: SwitchOperationState,
    systemState: SwitchSystemState,
    loader: suspend () -> Result<SwitchGameSettings>,
    onSave: (SwitchGameSettings) -> Unit,
    onDriverSelected: (String) -> Unit,
    onOpenDriverDownloader: () -> Unit,
    onImportDriver: () -> Unit,
    onBack: () -> Unit,
) {
    var settings by remember(scopeKey) { mutableStateOf<SwitchGameSettings?>(null) }
    var loadError by remember(scopeKey) { mutableStateOf<String?>(null) }
    var selectedCategory by remember(scopeKey) { mutableStateOf(initialCategory) }
    val busy = operation.running && if (global) {
        operation.gameId == null
    } else {
        operation.gameId == null || operation.gameId == scopeKey
    }
    val loadedSettings = settings

    LaunchedEffect(scopeKey) {
        loader().onSuccess {
            settings = it
            selectedCategory = it.values.firstOrNull { value -> value.category == initialCategory }?.category
                ?: it.values.firstOrNull()?.category.orEmpty()
        }.onFailure { loadError = it.message ?: "Could not read Eden settings." }
    }

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(horizontal = 16.dp, vertical = 12.dp),
        verticalArrangement = Arrangement.spacedBy(9.dp),
    ) {
        item {
            Row(verticalAlignment = Alignment.CenterVertically) {
                IconButton(
                    onClick = onBack,
                    modifier = Modifier.size(38.dp).border(1.dp, LauncherOutline, RectangleShape),
                ) {
                    Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                }
                Spacer(Modifier.width(11.dp))
                Column(Modifier.weight(1f)) {
                    Text(
                        eyebrow,
                        color = LauncherAccent,
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Bold,
                        fontSize = 8.sp,
                    )
                    Text(
                        title,
                        fontSize = 22.sp,
                        fontWeight = FontWeight.Bold,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
                PrimaryAction(
                    label = "Save changes",
                    icon = Icons.Default.Save,
                    onClick = { settings?.let(onSave) },
                    enabled = settings != null && !busy,
                    primary = true,
                    modifier = Modifier.width(150.dp),
                )
            }
        }

        if (operation.error != null || (busy && operation.detail.isNotBlank())) {
            item {
                LauncherCard(compact = true) {
                    Text(
                        operation.error ?: operation.detail,
                        color = if (operation.error == null) LauncherAccent else LauncherDanger,
                        fontSize = 10.sp,
                    )
                    if (busy) LauncherProgress(operation.fraction, Modifier.fillMaxWidth().height(4.dp))
                }
            }
        }

        item {
            LauncherCard(compact = true) {
                SectionTitle(
                    "GPU driver",
                    if (global) "Default driver for Switch games" else "Driver override for this game",
                )
                val current = settings?.selectedGpuDriverPath.orEmpty()
                LazyRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    items(systemState.gpuDrivers, key = { it.id }) { driver ->
                        CompactEdenChoice(
                            label = buildString {
                                append(driver.name)
                                if (driver.version.isNotBlank()) append(" • ${driver.version}")
                            },
                            selected = current == driver.path,
                            modifier = Modifier.width(224.dp).height(44.dp),
                            onClick = {
                                settings = settings?.copy(selectedGpuDriverPath = driver.path)
                                onDriverSelected(driver.path)
                            },
                        )
                    }
                }
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    PrimaryAction(
                        label = "Eden driver downloader",
                        icon = Icons.Default.CloudDownload,
                        onClick = onOpenDriverDownloader,
                        enabled = !busy,
                        primary = true,
                        modifier = Modifier.weight(1f),
                    )
                    PrimaryAction(
                        label = "Install driver ZIP",
                        icon = Icons.Default.FileDownload,
                        onClick = onImportDriver,
                        enabled = !busy,
                        modifier = Modifier.weight(1f),
                    )
                }
            }
        }

        when {
            loadError != null -> item {
                LauncherCard { Text(loadError.orEmpty(), color = LauncherDanger) }
            }
            loadedSettings == null -> item {
                LauncherCard {
                    Text(
                        if (global) "Reading Eden's complete global settings…"
                        else "Reading Eden's complete per-game settings…",
                        color = LauncherTextMuted,
                    )
                }
            }
            else -> {
                val value = loadedSettings
                val categories = value.values.map(EdenSettingValue::category).distinct()
                item {
                    LauncherCard(compact = true) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            SectionTitle(
                                if (global) "Global settings" else "Game settings",
                                "${value.values.size} Eden options • $scopeDescription",
                            )
                            Text(
                                if (busy) "SAVING" else "${categories.size} CATEGORIES",
                                color = LauncherAccent,
                                fontFamily = FontFamily.Monospace,
                                fontSize = 8.sp,
                            )
                        }
                        LazyRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            items(categories, key = { it }) { category ->
                                CompactEdenChoice(
                                    label = category,
                                    selected = selectedCategory == category,
                                    onClick = { selectedCategory = category },
                                )
                            }
                        }
                    }
                }

                val visible = value.values.filter { it.category == selectedCategory }
                items(visible, key = { it.key }) { setting ->
                    EdenSettingRow(
                        setting = setting,
                        enabled = !busy,
                        global = global,
                        onChange = { replacement ->
                            settings = value.copy(
                                values = value.values.map { current ->
                                    if (current.key == replacement.key) replacement else current
                                },
                            )
                        },
                    )
                }
            }
        }
    }
}

@Composable
private fun EdenSettingRow(
    setting: EdenSettingValue,
    enabled: Boolean,
    global: Boolean,
    onChange: (EdenSettingValue) -> Unit,
) {
    val displayedValue = if (!global && setting.useGlobal) setting.globalValue else setting.value
    val editable = enabled && (global || !setting.useGlobal)
    LauncherCard(compact = true) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text(setting.label, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
                Text(
                    setting.key,
                    color = LauncherTextMuted,
                    fontFamily = FontFamily.Monospace,
                    fontSize = 8.sp,
                )
            }
            if (!global) {
                TextButton(
                    onClick = {
                        onChange(setting.copy(value = displayedValue, useGlobal = !setting.useGlobal))
                    },
                    enabled = enabled,
                    shape = RectangleShape,
                ) {
                    Text(if (setting.useGlobal) "Using global" else "Per game", fontSize = 9.sp)
                }
            }
            IconButton(
                onClick = { onChange(setting.copy(value = setting.defaultValue, useGlobal = false)) },
                enabled = enabled,
                modifier = Modifier.size(34.dp),
            ) {
                Icon(Icons.Default.Restore, contentDescription = "Restore default", modifier = Modifier.size(17.dp))
            }
            when {
                setting.type == EdenSettingType.BOOLEAN -> Switch(
                    checked = displayedValue == "true" || displayedValue == "1",
                    enabled = editable,
                    onCheckedChange = { onChange(setting.copy(value = it.toString(), useGlobal = false)) },
                )
                setting.choices.isNotEmpty() -> EdenChoiceDropdown(
                    setting = setting,
                    value = displayedValue,
                    enabled = editable,
                    onSelected = { onChange(setting.copy(value = it, useGlobal = false)) },
                )
                setting.slider && setting.minimum != null && setting.maximum != null -> Column(
                    Modifier.width(220.dp),
                ) {
                    Text("$displayedValue${setting.units}", fontSize = 11.sp)
                    Slider(
                        value = (displayedValue.toFloatOrNull() ?: setting.minimum.toFloat())
                            .coerceIn(setting.minimum.toFloat(), setting.maximum.toFloat()),
                        valueRange = setting.minimum.toFloat()..setting.maximum.toFloat(),
                        enabled = editable,
                        onValueChange = {
                            onChange(setting.copy(value = it.roundToInt().toString(), useGlobal = false))
                        },
                    )
                }
                else -> OutlinedTextField(
                    value = displayedValue,
                    onValueChange = { input ->
                        val filtered = if (setting.type == EdenSettingType.STRING) {
                            input.take(4096)
                        } else {
                            input.filterIndexed { index, char -> char.isDigit() || (index == 0 && char == '-') }
                        }
                        onChange(setting.copy(value = filtered, useGlobal = false))
                    },
                    enabled = editable,
                    singleLine = true,
                    modifier = Modifier.width(180.dp),
                    shape = RectangleShape,
                    textStyle = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
                )
            }
        }
        if (setting.description.isNotBlank()) {
            Text(setting.description, color = LauncherTextMuted, fontSize = 10.sp)
        }
        Text(
            "Default: ${setting.displayValue(setting.defaultValue)}" + if (setting.runtimeModifiable) "  •  live-change capable" else "",
            color = LauncherTextMuted,
            fontSize = 8.sp,
        )
    }
}

@Composable
private fun EdenChoiceDropdown(
    setting: EdenSettingValue,
    value: String,
    enabled: Boolean,
    onSelected: (String) -> Unit,
) {
    var expanded by remember(setting.key) { mutableStateOf(false) }
    var pending by remember(setting.key) { mutableStateOf<EdenSettingChoice?>(null) }
    LaunchedEffect(enabled) {
        if (!enabled) { expanded = false; pending = null }
    }
    Box {
        OutlinedButton(
            onClick = { expanded = true },
            enabled = enabled,
            modifier = Modifier.width(220.dp).heightIn(min = 48.dp),
            shape = RectangleShape,
        ) {
            Text(setting.displayValue(value), modifier = Modifier.weight(1f), fontSize = 11.sp)
            Icon(Icons.Default.ArrowDropDown, contentDescription = "Choose ${setting.label}")
        }
        DropdownMenu(
            expanded = expanded && enabled,
            onDismissRequest = { expanded = false },
            modifier = Modifier.width(280.dp).heightIn(max = 360.dp),
        ) {
            setting.choices.forEach { choice ->
                DropdownMenuItem(
                    text = {
                        Text(choice.label, fontWeight = if (choice.value == value) FontWeight.Bold else FontWeight.Normal)
                    },
                    onClick = {
                        expanded = false
                        if (choice.warning != null) pending = choice else onSelected(choice.value)
                    },
                )
            }
        }
    }
    pending?.let { choice ->
        AlertDialog(
            onDismissRequest = { pending = null },
            title = { Text(setting.label) },
            text = { Text(choice.warning.orEmpty()) },
            confirmButton = {
                TextButton(onClick = { pending = null; onSelected(choice.value) }, enabled = enabled) {
                    Text("Continue")
                }
            },
            dismissButton = { TextButton(onClick = { pending = null }) { Text("Cancel") } },
        )
    }
}

@Composable
private fun CompactEdenChoice(
    label: String,
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    TextButton(
        onClick = onClick,
        modifier = modifier
            .background(if (selected) LauncherAccent.copy(alpha = 0.16f) else LauncherPanelRaised)
            .border(1.dp, if (selected) LauncherAccent else LauncherOutline, RectangleShape)
            .height(36.dp),
        shape = RectangleShape,
        contentPadding = PaddingValues(horizontal = 11.dp, vertical = 0.dp),
    ) {
        Text(
            label,
            color = if (selected) LauncherAccent else MaterialTheme.colorScheme.onSurface,
            fontSize = 9.sp,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}
