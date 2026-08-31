package com.wemu.session.secondscreen

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.thor.core.designsystem.theme.ThorTheme

/**
 * A fixed-shell field ledger. Individual crop tiles are compacted by location, watering state,
 * crop identity, and maturity so large farms remain readable without scrolling the whole page.
 */
@Composable
fun StardewFieldsReferencePage(
    state: StardewState,
    modifier: Modifier = Modifier,
) {
    val summary = remember(state.crops) { stardewFieldsSummary(state.crops) }

    Column(
        modifier = modifier
            .fillMaxSize()
            .background(FIELDS_PARCHMENT.copy(alpha = 0.30f)),
    ) {
        FieldsPageHeader(state, summary)
        FieldsRule(strong = true)

        when {
            !state.hasWorld -> FieldsEmptyState(
                state = state,
                title = "Waiting for a farm save",
                detail = "Load a save and live crop details will appear here.",
                modifier = Modifier.fillMaxWidth().weight(1f),
            )

            !state.config.sendCrops -> FieldsEmptyState(
                state = state,
                title = "Field data is turned off",
                detail = "Enable crop data in Settings to track watering and harvests.",
                modifier = Modifier.fillMaxWidth().weight(1f),
            )

            summary.total == 0 -> FieldsEmptyState(
                state = state,
                title = "No growing crops found",
                detail = "Planted crops will be grouped here by location and status.",
                modifier = Modifier.fillMaxWidth().weight(1f),
            )

            else -> FieldsCropList(
                state = state,
                summary = summary,
                modifier = Modifier.fillMaxWidth().weight(1f),
            )
        }

        FieldsRule(strong = true)
        FieldsDateFooter(state, summary)
    }
}

@Composable
private fun FieldsPageHeader(state: StardewState, summary: StardewFieldsSummary) {
    val firstCropSprite = summary.locations
        .asSequence()
        .flatMap { it.groups.asSequence() }
        .flatMap { it.rows.asSequence() }
        .firstNotNullOfOrNull { row -> state.sprites[row.id] }

    Row(
        modifier = Modifier.fillMaxWidth().height(58.dp).padding(horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(9.dp),
    ) {
        FieldsSprite(firstCropSprite ?: state.sprites[StardewUi.SEASON], 34.dp)
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = "Fields",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
                color = FIELDS_INK,
                maxLines = 1,
            )
            Text(
                text = fieldsHeaderDetail(state, summary),
                style = MaterialTheme.typography.bodySmall,
                color = FIELDS_MUTED,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }

        if (state.hasWorld && state.config.sendCrops && summary.total > 0) {
            Column(horizontalAlignment = Alignment.End) {
                Text(
                    text = "${summary.ready} ready",
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.Bold,
                    color = if (summary.ready > 0) FIELDS_READY else FIELDS_MUTED,
                    maxLines = 1,
                )
                Text(
                    text = if (summary.thirsty > 0) {
                        "${summary.thirsty} need water"
                    } else {
                        "Watering complete"
                    },
                    style = MaterialTheme.typography.labelSmall,
                    fontWeight = FontWeight.SemiBold,
                    color = if (summary.thirsty > 0) FIELDS_THIRSTY else FIELDS_WATERED,
                    maxLines = 1,
                )
            }
        }
    }
}

@Composable
private fun FieldsCropList(
    state: StardewState,
    summary: StardewFieldsSummary,
    modifier: Modifier = Modifier,
) {
    LazyColumn(
        modifier = modifier,
        contentPadding = PaddingValues(bottom = 6.dp),
    ) {
        summary.locations.forEach { location ->
            item(key = "field-location:${location.name}") {
                FieldsLocationHeader(state, location)
            }
            location.groups.forEach { group ->
                item(key = "field-status:${location.name}:${group.status.name}") {
                    FieldsStatusHeader(group)
                }
                items(
                    items = group.rows,
                    key = { row ->
                        "field-crop:${location.name}:${group.status.name}:${row.id}:${row.name}"
                    },
                ) { row ->
                    FieldsCropRow(state, row)
                }
            }
        }
    }
}

@Composable
private fun FieldsLocationHeader(state: StardewState, location: StardewFieldLocation) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(43.dp)
            .background(FIELDS_LOCATION_WASH)
            .padding(horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        FieldsSprite(state.sprites[StardewUi.SEASON], 25.dp)
        Text(
            text = location.name,
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.Bold,
            color = FIELDS_INK,
            modifier = Modifier.weight(1f),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            text = buildList {
                add("${location.total} planted")
                if (location.ready > 0) add("${location.ready} ready")
                if (location.thirsty > 0) add("${location.thirsty} thirsty")
            }.joinToString(" • "),
            style = MaterialTheme.typography.labelMedium,
            fontWeight = FontWeight.SemiBold,
            color = if (location.thirsty > 0) FIELDS_THIRSTY else FIELDS_MUTED,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
    FieldsRule()
}

@Composable
private fun FieldsStatusHeader(group: StardewFieldStatusGroup) {
    val tint = fieldsStatusColour(group.status)
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(29.dp)
            .background(fieldsStatusWash(group.status))
            .padding(horizontal = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(7.dp),
    ) {
        Box(Modifier.size(8.dp).clip(ThorTheme.shapes.pill).background(tint))
        Text(
            text = group.status.title.uppercase(),
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.Bold,
            color = tint,
            modifier = Modifier.weight(1f),
            maxLines = 1,
        )
        Text(
            text = "${group.total} crop${if (group.total == 1) "" else "s"}",
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.SemiBold,
            color = FIELDS_MUTED,
            maxLines = 1,
        )
    }
    FieldsRule()
}

@Composable
private fun FieldsCropRow(state: StardewState, row: StardewFieldCropRow) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(58.dp)
            .background(fieldsStatusWash(row.status).copy(alpha = 0.48f))
            .padding(horizontal = 12.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(9.dp),
    ) {
        FieldsSpriteSlot(
            state = state,
            sprite = state.sprites[row.id],
            description = row.name,
            size = 46.dp,
        )
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = row.name.ifEmpty { "Growing crop" },
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.Bold,
                color = FIELDS_INK,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                text = fieldsCropDetail(row),
                style = MaterialTheme.typography.labelSmall,
                fontWeight = FontWeight.SemiBold,
                color = fieldsStatusColour(row.status),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        Text(
            text = "×${row.count}",
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.Bold,
            color = FIELDS_INK,
            maxLines = 1,
        )
    }
    FieldsRule()
}

@Composable
private fun FieldsEmptyState(
    state: StardewState,
    title: String,
    detail: String,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier.padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        FieldsSprite(state.sprites[StardewUi.SEASON], 46.dp)
        Text(
            text = title,
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Bold,
            color = FIELDS_INK,
            modifier = Modifier.padding(top = 10.dp),
        )
        Text(
            text = detail,
            style = MaterialTheme.typography.bodyMedium,
            color = FIELDS_MUTED,
            modifier = Modifier.padding(top = 4.dp),
        )
    }
}

@Composable
private fun FieldsDateFooter(state: StardewState, summary: StardewFieldsSummary) {
    Row(
        modifier = Modifier.fillMaxWidth().height(43.dp).padding(horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(7.dp),
    ) {
        FieldsSprite(state.sprites[StardewUi.SEASON], 25.dp)
        Text(
            text = fieldsDate(state),
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.SemiBold,
            color = FIELDS_INK,
            modifier = Modifier.weight(1f),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        if (state.hasWorld && state.weatherToday.isNotEmpty()) {
            // `ui:time` is sourced from Stardew's current weather icon strip.
            FieldsSprite(state.sprites[StardewUi.TIME], 22.dp)
            Text(
                text = state.weatherToday,
                style = MaterialTheme.typography.labelMedium,
                fontWeight = FontWeight.SemiBold,
                color = FIELDS_MUTED,
                maxLines = 1,
            )
        }
        if (summary.total > 0) {
            Box(Modifier.fillMaxHeight(0.55f).width(1.dp).background(FIELDS_RULE))
        }
        Text(
            text = if (state.hasWorld) {
                state.clock.replace("am", " am").replace("pm", " pm")
            } else {
                "—"
            },
            style = MaterialTheme.typography.bodyMedium,
            color = FIELDS_INK,
            maxLines = 1,
        )
    }
}

@Composable
private fun FieldsSpriteSlot(
    state: StardewState,
    sprite: UiSprite?,
    description: String,
    size: Dp,
) {
    val frame = state.frame(StardewUi.SLOT)
    val base = Modifier.size(size)
    val painted = if (frame != null) {
        base.drawBehind { drawNineSlice(frame, this.size) }
    } else {
        base
            .clip(ThorTheme.shapes.small)
            .background(FIELDS_CARD_WASH)
            .border(1.dp, FIELDS_RULE, ThorTheme.shapes.small)
    }
    Box(painted, contentAlignment = Alignment.Center) {
        if (sprite != null) {
            Image(
                bitmap = sprite.image,
                contentDescription = description,
                modifier = Modifier.size(size - 10.dp),
                contentScale = ContentScale.Fit,
                filterQuality = FilterQuality.None,
            )
        }
    }
}

@Composable
private fun FieldsSprite(sprite: UiSprite?, size: Dp) {
    if (sprite == null) {
        Spacer(Modifier.size(size))
        return
    }
    Image(
        bitmap = sprite.image,
        contentDescription = null,
        modifier = Modifier.size(size),
        contentScale = ContentScale.Fit,
        filterQuality = FilterQuality.None,
    )
}

@Composable
private fun FieldsRule(strong: Boolean = false) {
    Box(
        Modifier
            .fillMaxWidth()
            .height(if (strong) 2.dp else 1.dp)
            .background(if (strong) FIELDS_GOLD else FIELDS_RULE),
    )
}

internal enum class StardewFieldStatus(val title: String) {
    THIRSTY("Needs Water"),
    READY("Ready"),
    WATERED("Watered"),
}

internal data class StardewFieldCropRow(
    val id: String,
    val name: String,
    val status: StardewFieldStatus,
    val count: Int,
    val minimumDays: Int,
    val maximumDays: Int,
)

internal data class StardewFieldStatusGroup(
    val status: StardewFieldStatus,
    val total: Int,
    val rows: List<StardewFieldCropRow>,
)

internal data class StardewFieldLocation(
    val name: String,
    val total: Int,
    val ready: Int,
    val thirsty: Int,
    val groups: List<StardewFieldStatusGroup>,
)

internal data class StardewFieldsSummary(
    val total: Int,
    val ready: Int,
    val thirsty: Int,
    val watered: Int,
    val locations: List<StardewFieldLocation>,
)

internal fun stardewFieldsSummary(crops: List<StardewCrop>): StardewFieldsSummary {
    val locations = crops
        .groupBy { crop -> crop.location.trim().ifEmpty { "Farm" } }
        .map { (locationName, locationCrops) ->
            val groups = StardewFieldStatus.entries.mapNotNull { status ->
                val matching = locationCrops.filter { stardewFieldStatus(it) == status }
                if (matching.isEmpty()) return@mapNotNull null

                val rows = matching
                    .groupBy { crop -> crop.id to crop.name }
                    .map { (identity, rowCrops) ->
                        StardewFieldCropRow(
                            id = identity.first,
                            name = identity.second,
                            status = status,
                            count = rowCrops.size,
                            minimumDays = rowCrops.minOf { it.daysLeft.coerceAtLeast(0) },
                            maximumDays = rowCrops.maxOf { it.daysLeft.coerceAtLeast(0) },
                        )
                    }
                    .sortedWith(
                        compareBy<StardewFieldCropRow> { it.minimumDays }
                            .thenBy { it.name.lowercase() }
                            .thenBy { it.id },
                    )

                StardewFieldStatusGroup(
                    status = status,
                    total = matching.size,
                    rows = rows,
                )
            }

            StardewFieldLocation(
                name = locationName,
                total = locationCrops.size,
                ready = locationCrops.count { stardewFieldStatus(it) == StardewFieldStatus.READY },
                thirsty = locationCrops.count {
                    stardewFieldStatus(it) == StardewFieldStatus.THIRSTY
                },
                groups = groups,
            )
        }
        .sortedWith(
            compareBy<StardewFieldLocation> { if (it.name.equals("Farm", true)) 0 else 1 }
                .thenBy { it.name.lowercase() },
        )

    val ready = crops.count { stardewFieldStatus(it) == StardewFieldStatus.READY }
    val thirsty = crops.count { stardewFieldStatus(it) == StardewFieldStatus.THIRSTY }
    return StardewFieldsSummary(
        total = crops.size,
        ready = ready,
        thirsty = thirsty,
        watered = (crops.size - ready - thirsty).coerceAtLeast(0),
        locations = locations,
    )
}

internal fun stardewFieldStatus(crop: StardewCrop): StardewFieldStatus = when {
    crop.ready -> StardewFieldStatus.READY
    crop.needsWater -> StardewFieldStatus.THIRSTY
    else -> StardewFieldStatus.WATERED
}

internal fun fieldsCropDays(row: StardewFieldCropRow): String {
    if (row.status == StardewFieldStatus.READY) return "Ready to harvest"
    val low = row.minimumDays.coerceAtLeast(0)
    val high = row.maximumDays.coerceAtLeast(low)
    return when {
        high <= 0 -> "Less than a day remaining"
        low == high && low == 1 -> "1 day remaining"
        low == high -> "$low days remaining"
        low == 0 -> "Up to $high days remaining"
        else -> "$low–$high days remaining"
    }
}

private fun fieldsCropDetail(row: StardewFieldCropRow): String = when (row.status) {
    StardewFieldStatus.READY -> fieldsCropDays(row)
    StardewFieldStatus.THIRSTY -> "Needs water • ${fieldsCropDays(row)}"
    StardewFieldStatus.WATERED -> "Watered • ${fieldsCropDays(row)}"
}

private fun fieldsHeaderDetail(state: StardewState, summary: StardewFieldsSummary): String = when {
    !state.hasWorld -> "Waiting for live crop data"
    !state.config.sendCrops -> "Crop data disabled in Settings"
    summary.total == 0 -> "No planted crops currently tracked"
    else -> "${summary.total} crops • ${summary.locations.size} " +
        "location${if (summary.locations.size == 1) "" else "s"}"
}

private fun fieldsDate(state: StardewState): String {
    if (!state.hasWorld || state.day <= 0) return "Waiting for farm"
    val weekday = state.weekday.trim().take(3)
    val day = if (weekday.isEmpty()) state.day.toString() else "$weekday. ${state.day}"
    return "$day, ${state.season}, Year ${state.year.coerceAtLeast(1)}"
}

private fun fieldsStatusColour(status: StardewFieldStatus): Color = when (status) {
    StardewFieldStatus.THIRSTY -> FIELDS_THIRSTY
    StardewFieldStatus.READY -> FIELDS_READY
    StardewFieldStatus.WATERED -> FIELDS_WATERED
}

private fun fieldsStatusWash(status: StardewFieldStatus): Color = when (status) {
    StardewFieldStatus.THIRSTY -> FIELDS_THIRSTY_WASH
    StardewFieldStatus.READY -> FIELDS_READY_WASH
    StardewFieldStatus.WATERED -> FIELDS_WATERED_WASH
}

private val FIELDS_INK = Color(0xFF321B0F)
private val FIELDS_MUTED = Color(0xFF70451F)
private val FIELDS_READY = Color(0xFF19721F)
private val FIELDS_THIRSTY = Color(0xFF2A67A8)
private val FIELDS_WATERED = Color(0xFF477A36)
private val FIELDS_GOLD = Color(0xFFC47A12)
private val FIELDS_PARCHMENT = Color(0xFFFFD98E)
private val FIELDS_LOCATION_WASH = Color(0x32FFD789)
private val FIELDS_CARD_WASH = Color(0x2AFFF0B8)
private val FIELDS_READY_WASH = Color(0x247DCB65)
private val FIELDS_THIRSTY_WASH = Color(0x223E8CD1)
private val FIELDS_WATERED_WASH = Color(0x18A9D38A)
private val FIELDS_RULE = Color(0x55A95F16)
