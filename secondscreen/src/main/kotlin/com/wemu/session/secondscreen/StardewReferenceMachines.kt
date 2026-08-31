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
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
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
 * A single, vertically scrolling farm dashboard. Crops deliberately live off this page: this
 * destination is for machines, fruit trees, and animals, with large touch-readable rows.
 */
@Composable
fun StardewMachinesReferencePage(
    state: StardewState,
    modifier: Modifier = Modifier,
) {
    LazyColumn(
        modifier = modifier
            .fillMaxSize()
            .background(MACHINE_PARCHMENT.copy(alpha = 0.28f)),
        contentPadding = PaddingValues(bottom = 5.dp),
    ) {
        item(key = "machines-header") {
            MachineSectionHeader(
                title = "Machines",
                detail = machineSectionDetail(
                    ready = state.machinesReady,
                    total = state.machines.size,
                    readyNoun = "ready",
                ),
                icon = state.sprites[StardewUi.TAB_CRAFTING]
                    ?: state.machinesByUrgency.firstNotNullOfOrNull { state.sprites[it.id] },
            )
        }
        if (state.machinesByUrgency.isEmpty()) {
            item(key = "machines-empty") {
                MachineEmptyRow("No machines are currently tracked.")
            }
        } else {
            itemsIndexed(
                items = state.machinesByUrgency,
                key = { index, machine ->
                    "machine:${machine.location}:${machine.id}:${machine.outputId}:$index"
                },
            ) { _, machine ->
                MachineRow(state, machine)
            }
        }

        item(key = "trees-section-rule") { MachineSectionRule() }
        item(key = "trees-header") {
            MachineSectionHeader(
                title = "Fruit Trees",
                detail = machineCountLabel(state.trees.size, "tree", "trees"),
                icon = state.trees.firstNotNullOfOrNull { state.sprites[it.id] }
                    ?: state.sprites[StardewUi.SEASON],
            )
        }
        if (state.trees.isEmpty()) {
            item(key = "trees-empty") {
                MachineEmptyRow("No fruit is ready on your trees.")
            }
        } else {
            itemsIndexed(
                items = state.trees,
                key = { index, tree -> "tree:${tree.location}:${tree.id}:$index" },
            ) { _, tree ->
                TreeRow(state, tree)
            }
        }

        item(key = "animals-section-rule") { MachineSectionRule() }
        item(key = "animals-header") {
            val chores = state.animalsUnpetted.size + state.animalsWithProduce.size
            MachineSectionHeader(
                title = "Animals",
                detail = if (chores > 0) "$chores need attention" else "All cared for",
                icon = state.sprites[StardewUi.TAB_RELATIONSHIPS]
                    ?: state.animals.firstNotNullOfOrNull { state.sprites[it.spriteId] },
            )
        }
        if (state.animals.isEmpty()) {
            item(key = "animals-empty") {
                MachineEmptyRow("No farm animals are currently tracked.")
            }
        } else {
            itemsIndexed(
                items = state.animals,
                key = { index, animal -> "animal:${animal.name}:${animal.type}:$index" },
            ) { _, animal ->
                AnimalRow(state, animal)
            }
        }

        item(key = "date-rule") { MachineSectionRule() }
        item(key = "date-footer") { MachineDateFooter(state) }
    }
}

@Composable
private fun MachineSectionHeader(
    title: String,
    detail: String,
    icon: UiSprite?,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(48.dp)
            .background(MACHINE_WASH)
            .padding(horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        MachineSprite(icon, 30.dp)
        Spacer(Modifier.width(9.dp))
        Text(
            text = title,
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Bold,
            color = MACHINE_INK,
            modifier = Modifier.weight(1f),
            maxLines = 1,
        )
        Text(
            text = detail,
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.SemiBold,
            color = MACHINE_MUTED,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
    MachineReferenceRule()
}

@Composable
private fun MachineRow(state: StardewState, machine: StardewMachine) {
    val status = if (machine.ready) {
        machine.output.ifEmpty { "Ready to collect" } +
            if (machine.count > 1) " ×${machine.count}" else ""
    } else {
        "Ready in ${machineDuration(machine.minutes)}"
    }
    FarmDashboardRow(
        state = state,
        sprite = state.sprites[machine.id],
        title = machine.name,
        subtitle = machine.location.ifEmpty { "Farm" },
        status = status,
        ready = machine.ready,
        trailingSprite = state.sprites[machine.outputId],
    )
}

@Composable
private fun TreeRow(state: StardewState, tree: StardewTree) {
    val fruit = when (tree.count) {
        1 -> "1 fruit ready"
        else -> "${tree.count} fruit ready"
    }
    FarmDashboardRow(
        state = state,
        sprite = state.sprites[tree.id],
        title = tree.name.ifEmpty { "Fruit Tree" },
        subtitle = tree.location.ifEmpty { "Farm" },
        status = fruit,
        ready = tree.count > 0,
    )
}

@Composable
private fun AnimalRow(state: StardewState, animal: StardewAnimal) {
    val status = when {
        animal.produce.isNotEmpty() -> "${animal.produce} ready"
        !animal.petted -> "Needs petting"
        animal.baby -> "Young ${animal.type.ifEmpty { "animal" }}"
        else -> "Cared for"
    }
    val subtitle = buildList {
        animal.type.takeIf(String::isNotBlank)?.let(::add)
        add("${animal.hearts.coerceAtLeast(0)} hearts")
    }.joinToString(" • ")
    FarmDashboardRow(
        state = state,
        // Never substitute produce art for the animal. A missing animal sprite should be blank,
        // not a misleading item icon.
        sprite = state.sprites[animal.spriteId],
        title = animal.name.ifEmpty { animal.type.ifEmpty { "Farm Animal" } },
        subtitle = subtitle,
        status = status,
        ready = animal.produce.isNotEmpty() || !animal.petted,
        trailingSprite = animal.produceId
            .takeIf { it.isNotEmpty() && animal.produce.isNotEmpty() }
            ?.let(state.sprites::get),
    )
}

@Composable
private fun FarmDashboardRow(
    state: StardewState,
    sprite: UiSprite?,
    title: String,
    subtitle: String,
    status: String,
    ready: Boolean,
    trailingSprite: UiSprite? = null,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(78.dp)
            .background(if (ready) MACHINE_READY_WASH else MACHINE_TILE)
            .padding(horizontal = 12.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(11.dp),
    ) {
        MachineSpriteSlot(state, sprite, 58.dp)
        Column(
            modifier = Modifier.weight(1f).fillMaxHeight(),
            verticalArrangement = Arrangement.Center,
        ) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.Bold,
                color = MACHINE_INK,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                text = subtitle,
                style = MaterialTheme.typography.bodySmall,
                color = MACHINE_MUTED,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        if (trailingSprite != null) {
            MachineSpriteSlot(state, trailingSprite, 44.dp)
        }
        Text(
            text = status,
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.SemiBold,
            color = if (ready) MACHINE_READY else MACHINE_MUTED,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.width(132.dp),
        )
    }
    MachineReferenceRule()
}

@Composable
private fun MachineEmptyRow(message: String) {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(58.dp)
            .background(MACHINE_TILE)
            .padding(horizontal = 14.dp),
        contentAlignment = Alignment.CenterStart,
    ) {
        Text(
            text = message,
            style = MaterialTheme.typography.bodyMedium,
            color = MACHINE_MUTED,
        )
    }
}

@Composable
private fun MachineDateFooter(state: StardewState) {
    Row(
        modifier = Modifier.fillMaxWidth().height(48.dp).padding(horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        MachineSprite(state.sprites[StardewUi.SEASON], 28.dp)
        Spacer(Modifier.width(8.dp))
        Text(
            text = machineDateLabel(state),
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.SemiBold,
            color = MACHINE_INK,
            modifier = Modifier.weight(1f),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        MachineSprite(state.sprites[StardewUi.TIME], 25.dp)
        Spacer(Modifier.width(6.dp))
        Text(
            text = state.clock.replace("am", " am").replace("pm", " pm"),
            style = MaterialTheme.typography.bodyMedium,
            color = MACHINE_INK,
            maxLines = 1,
        )
    }
}

@Composable
private fun MachineSpriteSlot(state: StardewState, sprite: UiSprite?, size: Dp) {
    val frame = state.frame(StardewUi.SLOT)
    val base = Modifier.size(size)
    val painted = if (frame != null) {
        base.drawBehind { drawNineSlice(frame, this.size) }
    } else {
        base
            .clip(ThorTheme.shapes.small)
            .background(MACHINE_WASH)
            .border(1.dp, MACHINE_RULE, ThorTheme.shapes.small)
    }
    Box(modifier = painted, contentAlignment = Alignment.Center) {
        MachineSprite(sprite, size - 12.dp)
    }
}

@Composable
private fun MachineSprite(sprite: UiSprite?, size: Dp) {
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
private fun MachineSectionRule() {
    Box(Modifier.fillMaxWidth().height(3.dp).background(MACHINE_ACCENT.copy(alpha = 0.62f)))
}

@Composable
private fun MachineReferenceRule() {
    Box(Modifier.fillMaxWidth().height(1.dp).background(MACHINE_RULE))
}

private fun machineDuration(minutes: Int): String = when {
    minutes <= 0 -> "now"
    minutes < 60 -> "${minutes}m"
    minutes % (24 * 60) == 0 -> "${minutes / (24 * 60)}d"
    minutes >= 24 * 60 -> "${minutes / (24 * 60)}d ${(minutes % (24 * 60)) / 60}h"
    else -> "${minutes / 60}h ${minutes % 60}m"
}

private fun machineSectionDetail(ready: Int, total: Int, readyNoun: String): String = when {
    total == 0 -> "None placed"
    ready > 0 -> "$ready $readyNoun • $total total"
    else -> "$total working"
}

private fun machineCountLabel(count: Int, singular: String, plural: String): String =
    "$count ${if (count == 1) singular else plural} ready"

private fun machineDateLabel(state: StardewState): String {
    if (!state.hasWorld || state.day <= 0) return "Waiting for farm"
    val weekday = state.weekday.trim().take(3)
    val day = if (weekday.isEmpty()) state.day.toString() else "$weekday. ${state.day}"
    return "$day, ${state.season}, Year ${state.year.coerceAtLeast(1)}"
}

private val MACHINE_INK = Color(0xFF321B0F)
private val MACHINE_MUTED = Color(0xFF70451F)
private val MACHINE_READY = Color(0xFF19721F)
private val MACHINE_PARCHMENT = Color(0xFFFFD98E)
private val MACHINE_WASH = Color(0x28FFD789)
private val MACHINE_TILE = Color(0x20FFF0B8)
private val MACHINE_READY_WASH = Color(0x1F7DCB65)
private val MACHINE_RULE = Color(0x55A95F16)
private val MACHINE_ACCENT = Color(0xFFA95F16)
