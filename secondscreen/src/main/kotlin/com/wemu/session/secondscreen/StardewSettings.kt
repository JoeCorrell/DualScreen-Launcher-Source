package com.wemu.session.secondscreen

import androidx.compose.foundation.Image
import androidx.compose.foundation.border
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.graphics.Color
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.thor.core.designsystem.theme.ThorTheme

@Composable
fun StardewSettingsPage(state: StardewState) {
    val prefs by StardewPrefs.state.collectAsStateWithLifecycle()
    val connected = SecondScreen.connectionState.collectAsStateWithLifecycle().value is
        ConnectionState.Connected
    val config = state.config
    val configurablePages = StardewPage.entries.filter { it != StardewPage.SETTINGS }
    val visiblePages = configurablePages.count { !prefs.hides(it.name) }

    StardewCard {
        SettingsCard("Appearance", first = true) {
        Note("Saved on this device for each farm. Changes appear immediately.")

        Switch(
            title = "Game-tile background",
            on = prefs.showGround,
            onChange = { on -> StardewPrefs.update { it.copy(showGround = on) } },
        )

        if (prefs.showGround) {
            Swatches(state = state, chosen = prefs.groundChoice)

            if (prefs.groundChoice != StardewPrefs.COLOUR) {
                Stepper(
                    title = "Background tile size",
                    value = prefs.groundZoom,
                    suffix = "x",
                    step = 1,
                    range = 1..4,
                    onChange = { value -> StardewPrefs.update { it.copy(groundZoom = value) } },
                )
                Stepper(
                    title = "Background shade",
                    value = prefs.groundShade,
                    suffix = "%",
                    step = 10,
                    range = 0..70,
                    onChange = { value -> StardewPrefs.update { it.copy(groundShade = value) } },
                )
            }

            if (prefs.groundChoice == StardewPrefs.COLOUR) {
                Note("Solid background colour")
                val colour = Color(
                    prefs.groundRed,
                    prefs.groundGreen,
                    prefs.groundBlue,
                )
                Channel(
                    label = "R",
                    value = prefs.groundRed,
                    colour = colour,
                    channel = 0,
                    onChange = { value ->
                        StardewPrefs.update { it.copy(groundRed = value) }
                    },
                )
                Channel(
                    label = "G",
                    value = prefs.groundGreen,
                    colour = colour,
                    channel = 1,
                    onChange = { value ->
                        StardewPrefs.update { it.copy(groundGreen = value) }
                    },
                )
                Channel(
                    label = "B",
                    value = prefs.groundBlue,
                    colour = colour,
                    channel = 2,
                    onChange = { value ->
                        StardewPrefs.update { it.copy(groundBlue = value) }
                    },
                )
            }
        }

        Switch(
            title = "Use Stardew Valley frames and buttons",
            on = prefs.gameFrames,
            onChange = { on -> StardewPrefs.update { it.copy(gameFrames = on) } },
        )

    }

        SettingsCard("Vibration alerts") {
        Switch(
            title = "Enable bottom-screen alerts",
            on = prefs.alerts,
            onChange = { on -> StardewPrefs.update { it.copy(alerts = on) } },
        )
        Note("Choose which events may vibrate the device. No game input is sent.")
        Switch(
            title = "Machine finishes",
            on = prefs.machineAlerts,
            enabled = prefs.alerts,
            onChange = { on -> StardewPrefs.update { it.copy(machineAlerts = on) } },
        )
        Switch(
            title = "Villager birthday",
            on = prefs.birthdayAlerts,
            enabled = prefs.alerts,
            onChange = { on -> StardewPrefs.update { it.copy(birthdayAlerts = on) } },
        )
        Switch(
            title = "Late-night warning",
            on = prefs.lateNightAlerts,
            enabled = prefs.alerts,
            onChange = { on -> StardewPrefs.update { it.copy(lateNightAlerts = on) } },
        )
        if (prefs.alerts && prefs.lateNightAlerts) {
            TimeStepper(
                title = "Warn at",
                value = prefs.lateNightAlertTime,
                onChange = { value ->
                    StardewPrefs.update { it.copy(lateNightAlertTime = value) }
                },
            )
        }
    }

        SettingsCard("Pages") {
        Note(
            "$visiblePages of ${configurablePages.size} pages visible. Hidden pages are " +
                "removed from navigation; Settings always remains available.",
        )
        Row(
            modifier = Modifier.fillMaxWidth().padding(bottom = 4.dp),
            horizontalArrangement = Arrangement.spacedBy(ThorTheme.dimens.spacingSmall),
        ) {
            Action(
                text = "Show all",
                enabled = visiblePages < configurablePages.size,
                onClick = { StardewPrefs.update { it.copy(hiddenPages = emptySet()) } },
            )
            Action(
                text = "Backpack + map",
                enabled = configurablePages.any {
                    it.name !in ESSENTIAL_PAGE_NAMES && !prefs.hides(it.name)
                },
                onClick = {
                    StardewPrefs.update {
                        it.copy(
                            hiddenPages = configurablePages
                                .filter { page -> page.name !in ESSENTIAL_PAGE_NAMES }
                                .mapTo(mutableSetOf()) { page -> page.name },
                        )
                    }
                },
            )
        }
        configurablePages.forEach { page ->
            Switch(
                title = page.title,
                on = !prefs.hides(page.name),
                onChange = { on ->
                    StardewPrefs.update {
                        it.copy(
                            hiddenPages = if (on) it.hiddenPages - page.name
                            else it.hiddenPages + page.name,
                        )
                    }
                },
            )
        }
    }

        SettingsCard("The game") {
        Note(
            if (connected) "Saved in the mod's config.json and kept after the game closes."
            else "Start Stardew Valley with the mod loaded to change these options.",
        )

        Switch(
            title = "Hide the game's HUD",
            on = config.hideGameHud,
            enabled = connected,
            onChange = { on -> SecondScreen.send(Commands.setOption("hideGameHud", on)) },
        )
        Switch(
            title = "Allow inventory changes",
            on = config.allowInventoryEdits,
            enabled = connected,
            onChange = { on -> SecondScreen.send(Commands.setOption("allowInventoryEdits", on)) },
        )
        Switch(
            title = "Allow quest cancellation",
            on = config.allowQuestCancel,
            enabled = connected,
            onChange = { on -> SecondScreen.send(Commands.setOption("allowQuestCancel", on)) },
        )
        Switch(
            title = "Show farmer on map",
            on = config.farmerMarker,
            enabled = connected && config.sendMap,
            onChange = { on -> SecondScreen.send(Commands.setOption("farmerMarker", on)) },
        )
    }

        SettingsCard("What the game sends") {
        Note("Disable data you never view to reduce the game's periodic scanning work.")

        SendSwitch("Map and markers", "sendMap", config.sendMap, connected)
        SendSwitch("Fields and fruit trees", "sendCrops", config.sendCrops, connected)
        SendSwitch("Machines", "sendMachines", config.sendMachines, connected)
        SendSwitch("Farm animals", "sendAnimals", config.sendAnimals, connected)
        SendSwitch("Community Center bundles", "sendBundles", config.sendBundles, connected)
        SendSwitch("Villagers and friendships", "sendVillagers", config.sendVillagers, connected)
        SendSwitch("Crafting recipes", "sendCrafting", config.sendCrafting, connected)
    }

        SettingsCard("This connection") {
        Line("Status", if (connected) "Connected" else "Waiting for game")
        Line("Port", SecondScreen.PORT.toString())
        Line("Farm save", state.saveName.ifEmpty { "—" })

        Line("Game art received", state.sprites.size.toString())
        Line("Background choices", state.grounds.distinct().size.toString())

        Row(
            modifier = Modifier.fillMaxWidth().padding(top = ThorTheme.dimens.spacingSmall),
            horizontalArrangement = Arrangement.spacedBy(ThorTheme.dimens.spacingSmall),
        ) {
            Action(
                text = "Refresh game data",
                enabled = connected,
                onClick = { SecondScreen.send(Commands.requestRefresh()) },
            )
            Action(
                text = "Reset panel settings",
                enabled = true,
                onClick = { StardewPrefs.reset() },
            )
        }
        }
    }
}

@Composable
private fun SendSwitch(title: String, key: String, on: Boolean, connected: Boolean) {
    Switch(
        title = title,
        on = on,
        enabled = connected,
        onChange = { enabled -> SecondScreen.send(Commands.setOption(key, enabled)) },
    )
}

@Composable
private fun Swatches(state: StardewState, chosen: String) {
    val prefs by StardewPrefs.state.collectAsStateWithLifecycle()

    if (state.grounds.isEmpty() && state.sprites[StardewUi.GROUND] == null) {
        Note("Start a game with the mod loaded to receive its background tiles.")
    }

    Note("Automatic and solid colour")
    SwatchRow(
        state = state,
        ids = listOf("", StardewPrefs.COLOUR),
        chosen = chosen,
        prefs = prefs,
        startIndex = 0,
    )

    groundChoiceGroups(state.grounds).forEach { group ->
        Note(group.title)
        group.ids.chunked(SWATCHES_PER_ROW).forEachIndexed { rowIndex, row ->
            SwatchRow(
                state = state,
                ids = row,
                chosen = chosen,
                prefs = prefs,
                startIndex = rowIndex * SWATCHES_PER_ROW,
            )
        }
    }
}

@Composable
private fun SwatchRow(
    state: StardewState,
    ids: List<String>,
    chosen: String,
    prefs: StardewPrefsState,
    startIndex: Int,
) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens

    Row(
        modifier = Modifier.fillMaxWidth().padding(bottom = 3.dp),
        horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
    ) {
        ids.forEachIndexed { index, id ->
            val colour = id == StardewPrefs.COLOUR
            val automatic = id.isEmpty()
            val sprite = when {
                colour -> null
                automatic -> state.sprites[StardewUi.GROUND]
                else -> state.sprites[id]
            }
            val selected = id == chosen

            Box(
                modifier = Modifier
                    .weight(1f)
                    .height(46.dp)
                    .clip(ThorTheme.shapes.small)
                    .background(
                        if (colour) {
                            Color(prefs.groundRed, prefs.groundGreen, prefs.groundBlue)
                        } else {
                            colors.surfaceHighest
                        },
                    )
                    .then(
                        if (sprite != null) {
                            Modifier.drawBehind {
                                // Preview several pixels-perfect copies instead of enlarging one
                                // 16px game tile to fill the entire choice button.
                                drawTiled(sprite, magnify = 1)
                                drawRect(Color.Black.copy(alpha = 0.08f))
                            }
                        } else {
                            Modifier
                        },
                    )
                    .border(
                        width = if (selected) 3.dp else 1.dp,
                        color = if (selected) colors.cursor
                        else colors.outline.copy(alpha = 0.55f),
                        shape = ThorTheme.shapes.small,
                    )
                    .clickable { StardewPrefs.update { it.copy(groundChoice = id) } },
                contentAlignment = Alignment.Center,
            ) {
                val label = when {
                    automatic -> "Season"
                    colour -> "RGB"
                    else -> (startIndex + index + 1).toString()
                }
                Text(
                    text = label,
                    style = MaterialTheme.typography.labelSmall,
                    fontWeight = FontWeight.Bold,
                    color = if (colour) Color.White else colors.onSurface,
                    modifier = Modifier
                        .clip(ThorTheme.shapes.small)
                        .background(colors.surface.copy(alpha = if (automatic) 0.78f else 0.62f))
                        .padding(horizontal = 4.dp, vertical = 1.dp),
                )
            }
        }
        repeat(SWATCHES_PER_ROW - ids.size) { Box(modifier = Modifier.weight(1f)) }
    }
}

internal data class GroundChoiceGroup(val title: String, val ids: List<String>)

internal fun groundChoiceGroups(ids: List<String>): List<GroundChoiceGroup> {
    val distinct = ids.filter { it.isNotBlank() }.distinct()
    val known = listOf(
        "walls_and_floors" to "Interior floors",
        "spring" to "Spring ground",
        "summer" to "Summer ground",
        "fall" to "Fall ground",
        "winter" to "Winter ground",
    )
    val consumed = mutableSetOf<String>()
    val groups = buildList {
        known.forEach { (key, title) ->
            val matches = distinct.filter { groundChoiceSource(it) == key }
            if (matches.isNotEmpty()) {
                add(GroundChoiceGroup(title, matches))
                consumed += matches
            }
        }
        val other = distinct.filterNot { it in consumed }
        if (other.isNotEmpty()) add(GroundChoiceGroup("Other game tiles", other))
    }
    return groups
}

internal fun groundChoiceSource(id: String): String = id
    .removePrefix("ui:ground:")
    .substringBefore(':')
    .lowercase()

private const val SWATCHES_PER_ROW = 8

@Composable
private fun Channel(
    label: String,
    value: Int,
    colour: Color,

    channel: Int,
    onChange: (Int) -> Unit,
) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens

    fun at(level: Int): Color = when (channel) {
        0 -> Color(level, (colour.green * 255).toInt(), (colour.blue * 255).toInt())
        1 -> Color((colour.red * 255).toInt(), level, (colour.blue * 255).toInt())
        else -> Color((colour.red * 255).toInt(), (colour.green * 255).toInt(), level)
    }

    Row(
        modifier = Modifier.fillMaxWidth().padding(vertical = dimens.spacingTiny),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.Bold,
            color = colors.onSurfaceVariant,
            modifier = Modifier.width(16.dp),
        )

        Box(
            modifier = Modifier
                .weight(1f)
                .height(22.dp)
                .clip(ThorTheme.shapes.pill)
                .background(
                    Brush.horizontalGradient(listOf(at(0), at(255))),
                )

                .pointerInput(channel) {
                    detectTapGestures { position ->
                        onChange(((position.x / size.width) * 255).toInt().coerceIn(0, 255))
                    }
                }
                .pointerInput(channel) {
                    detectHorizontalDragGestures { change, _ ->
                        onChange(((change.position.x / size.width) * 255).toInt().coerceIn(0, 255))
                    }
                },
            contentAlignment = Alignment.CenterStart,
        ) {

            Box(
                modifier = Modifier
                    .fillMaxWidth(value / 255f)
                    .height(22.dp),
                contentAlignment = Alignment.CenterEnd,
            ) {
                Box(
                    modifier = Modifier
                        .width(3.dp)
                        .height(22.dp)
                        .background(colors.onSurface),
                )
            }
        }

        Text(
            text = value.toString(),
            style = MaterialTheme.typography.labelSmall,
            color = colors.onSurface,
            modifier = Modifier.width(34.dp).padding(start = 8.dp),
        )
    }
}

@Composable
private fun SettingsCard(
    title: String,
    first: Boolean = false,
    content: @Composable ColumnScope.() -> Unit,
) {
    if (!first) SettingsDivider()
    Column(modifier = Modifier.fillMaxWidth().padding(vertical = ThorTheme.dimens.spacingSmall)) {
        StardewSectionTitle(title)
        content()
    }
}

@Composable
private fun SettingsDivider() {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = ThorTheme.dimens.spacingSmall)
            .height(1.dp)
            .background(ThorTheme.colors.outline.copy(alpha = 0.35f)),
    )
}

@Composable
private fun Note(text: String) {
    Text(
        text = text,
        style = MaterialTheme.typography.labelSmall,
        color = ThorTheme.colors.onSurfaceVariant,
        modifier = Modifier.padding(bottom = ThorTheme.dimens.spacingTiny),
    )
}

@Composable
private fun Switch(
    title: String,
    on: Boolean,
    enabled: Boolean = true,
    onChange: (Boolean) -> Unit,
) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens

    val alpha = if (enabled) 1f else 0.45f

    val state by SecondScreen.stardew.collectAsStateWithLifecycle()
    val box = state.frame(if (on) StardewUi.CHECK_ON else StardewUi.CHECK_OFF)

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(ThorTheme.shapes.small)
            .clickable(enabled = enabled) { onChange(!on) }
            .padding(vertical = dimens.spacingTiny),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = title,
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.Medium,
            color = colors.onSurface.copy(alpha = alpha),
            modifier = Modifier.weight(1f),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )

        if (box != null) {
            Image(
                bitmap = box.image,
                contentDescription = null,
                modifier = Modifier.size(22.dp).alpha(alpha),
                contentScale = ContentScale.Fit,
                filterQuality = FilterQuality.None,
            )
        } else {

            Box(
                modifier = Modifier
                    .size(width = 34.dp, height = 18.dp)
                    .clip(ThorTheme.shapes.pill)
                    .background(
                        if (on) colors.cursor.copy(alpha = alpha)
                        else colors.surfaceHighest.copy(alpha = alpha),
                    ),
                contentAlignment = if (on) Alignment.CenterEnd else Alignment.CenterStart,
            ) {
                Box(
                    modifier = Modifier
                        .padding(horizontal = 3.dp)
                        .size(12.dp)
                        .clip(ThorTheme.shapes.pill)
                        .background(colors.onSurface.copy(alpha = alpha)),
                )
            }
        }
    }
}

@Composable
private fun Stepper(
    title: String,
    value: Int,
    suffix: String,
    step: Int,
    range: IntRange,
    onChange: (Int) -> Unit,
) {
    val colors = ThorTheme.colors
    Row(
        modifier = Modifier.fillMaxWidth().padding(vertical = ThorTheme.dimens.spacingTiny),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = title,
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.Medium,
            color = colors.onSurface,
            modifier = Modifier.weight(1f),
        )
        Action(
            text = "−",
            enabled = value > range.first,
            onClick = { onChange((value - step).coerceIn(range)) },
        )
        Text(
            text = "$value$suffix",
            style = MaterialTheme.typography.bodyMedium,
            color = colors.onSurface,
            modifier = Modifier.padding(horizontal = 8.dp),
        )
        Action(
            text = "+",
            enabled = value < range.last,
            onClick = { onChange((value + step).coerceIn(range)) },
        )
    }
}

@Composable
private fun TimeStepper(title: String, value: Int, onChange: (Int) -> Unit) {
    val safeValue = normalizeLateNightAlertTime(value)
    val colors = ThorTheme.colors
    Row(
        modifier = Modifier.fillMaxWidth().padding(vertical = ThorTheme.dimens.spacingTiny),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = title,
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.Medium,
            color = colors.onSurface,
            modifier = Modifier.weight(1f),
        )
        Action(
            text = "−",
            enabled = safeValue > 2200,
            onClick = { onChange(normalizeLateNightAlertTime(safeValue - 100)) },
        )
        Text(
            text = formatStardewTime(safeValue),
            style = MaterialTheme.typography.bodyMedium,
            color = colors.onSurface,
            modifier = Modifier.width(72.dp),
        )
        Action(
            text = "+",
            enabled = safeValue < 2500,
            onClick = { onChange(normalizeLateNightAlertTime(safeValue + 100)) },
        )
    }
}

internal fun formatStardewTime(value: Int): String {
    val hour24 = (value / 100) % 24
    val hour12 = when (val hour = hour24 % 12) {
        0 -> 12
        else -> hour
    }
    return "$hour12:00 ${if (hour24 < 12) "am" else "pm"}"
}

@Composable
private fun Action(text: String, enabled: Boolean, onClick: () -> Unit) {
    val colors = ThorTheme.colors
    val state by SecondScreen.stardew.collectAsStateWithLifecycle()
    val box = state.frame(StardewUi.BUTTON)
    val alpha = if (enabled) 1f else 0.4f

    Text(
        text = text,
        style = MaterialTheme.typography.labelSmall,
        fontWeight = FontWeight.Medium,
        color = if (enabled) colors.onSurface else colors.onSurfaceVariant.copy(alpha = 0.4f),
        modifier = Modifier
            .then(
                if (box != null) {
                    Modifier.alpha(alpha).drawBehind { drawNineSlice(box, size) }
                } else {
                    Modifier
                        .clip(ThorTheme.shapes.small)
                        .background(colors.surfaceHighest.copy(alpha = alpha))
                },
            )
            .clickable(enabled = enabled, onClick = onClick)
            .padding(horizontal = 12.dp, vertical = 7.dp),
    )
}

@Composable
private fun Line(label: String, value: String) {
    val colors = ThorTheme.colors
    Row(modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodySmall,
            color = colors.onSurfaceVariant,
            modifier = Modifier.weight(1f),
        )
        Text(
            text = value,
            style = MaterialTheme.typography.bodySmall,
            color = colors.onSurface,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

private val ESSENTIAL_PAGE_NAMES = setOf(StardewPage.TODAY.name, StardewPage.MAP.name)
