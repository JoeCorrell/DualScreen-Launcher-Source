package com.wemu.session.secondscreen

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.thor.core.designsystem.theme.ThorTheme

/** The reference-style first page: player summary beside three vanilla inventory rows. */
@Composable
fun StardewBackpackReferencePage(
    state: StardewState,
    modifier: Modifier = Modifier,
) {
    val dimens = ThorTheme.dimens
    Row(
        modifier = modifier.fillMaxSize(),
        horizontalArrangement = Arrangement.spacedBy(dimens.spacingSmall),
    ) {
        ReferenceFarmerSidebar(
            state = state,
            modifier = Modifier.width(128.dp).fillMaxHeight(),
        )
        ReferenceVerticalRule()
        ReferenceBackpackPanel(
            state = state,
            modifier = Modifier.weight(1f).fillMaxHeight(),
        )
    }
}

@Composable
private fun ReferenceFarmerSidebar(
    state: StardewState,
    modifier: Modifier = Modifier,
) {
    val capacity = state.inventoryCapacity.coerceAtMost(StardewState.MAX_INVENTORY)
    val backpackLevel = referenceInventoryRows(capacity)
    val farmTitle = referenceFarmTitle(
        state.farmName.ifEmpty { state.saveName },
    )
    val playerStatus = listOfNotNull(
        state.location.takeIf(String::isNotBlank),
        state.luckLabel.takeIf { state.hasWorld && it.isNotBlank() },
    ).joinToString(" • ")

    Column(
        modifier = modifier.padding(horizontal = 6.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            text = farmTitle,
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.Bold,
            color = REFERENCE_INK,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.padding(vertical = 3.dp),
        )
        ReferenceRule()
        ReferenceSidebarLine(
            primary = state.season.ifEmpty { "Season" },
            secondary = referenceDayLabel(state.weekday, state.day),
            icon = state.sprites[StardewUi.SEASON],
        )
        ReferenceRule()
        ReferenceSidebarLine(
            primary = state.clock.replace("am", " am").replace("pm", " pm"),
            secondary = state.weatherToday,
            icon = state.sprites[StardewUi.TIME],
        )
        ReferenceRule()
        ReferenceMoney(state)
        ReferenceRule()

        Box(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .padding(vertical = 3.dp)
                .clip(ThorTheme.shapes.small)
                .background(REFERENCE_WASH),
            contentAlignment = Alignment.Center,
        ) {
            val farmer = state.sprites[StardewUi.FARMER] ?: state.sprites[StardewUi.PLAYER]
            if (farmer != null) {
                Image(
                    bitmap = farmer.image,
                    contentDescription = state.playerName.ifEmpty { "Farmer" },
                    modifier = Modifier.fillMaxSize().padding(5.dp),
                    contentScale = ContentScale.Fit,
                    filterQuality = FilterQuality.None,
                )
            }
        }

        Text(
            text = state.playerName.ifEmpty { "Farmer" },
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.Bold,
            color = REFERENCE_INK,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            text = "Level ${state.playerLevel}  •  Farmer",
            style = MaterialTheme.typography.labelMedium,
            color = REFERENCE_MUTED,
            maxLines = 1,
        )
        if (playerStatus.isNotEmpty()) {
            Text(
                text = playerStatus,
                style = MaterialTheme.typography.labelSmall,
                color = REFERENCE_MUTED,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.padding(vertical = 1.dp),
            )
        }
        ReferenceVital("Health", state.health, state.maxHealth, REFERENCE_HEALTH)
        ReferenceVital("Stamina", state.energy, state.maxEnergy, REFERENCE_ENERGY)
        ReferenceRule()

        Text(
            text = "BACKPACK LEVEL $backpackLevel",
            style = MaterialTheme.typography.labelLarge.copy(fontSize = 9.sp, lineHeight = 10.sp),
            fontWeight = FontWeight.Bold,
            color = REFERENCE_INK,
            modifier = Modifier.fillMaxWidth().padding(top = 3.dp),
            textAlign = TextAlign.Center,
            maxLines = 1,
            softWrap = false,
            overflow = TextOverflow.Clip,
        )
        Text(
            text = if (capacity >= StardewState.MAX_INVENTORY) {
                "$capacity slots  •  Maximum"
            } else {
                "$capacity slots  •  Upgrade at Pierre's"
            },
            style = MaterialTheme.typography.labelSmall,
            color = REFERENCE_MUTED,
            modifier = Modifier.padding(bottom = 3.dp),
        )
    }
}

@Composable
private fun ReferenceSidebarLine(
    primary: String,
    secondary: String = "",
    icon: UiSprite? = null,
) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (icon != null) {
            ReferenceIcon(icon, 20.dp)
            Spacer(modifier = Modifier.width(5.dp))
        }
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(
                text = primary,
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.SemiBold,
                color = REFERENCE_INK,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            if (secondary.isNotBlank()) {
                Text(
                    text = secondary,
                    style = MaterialTheme.typography.labelMedium,
                    color = REFERENCE_INK,
                    maxLines = 1,
                )
            }
        }
    }
}

@Composable
private fun ReferenceMoney(state: StardewState) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        state.sprites[StardewUi.COIN]?.let { coin ->
            Image(
                bitmap = coin.image,
                contentDescription = null,
                modifier = Modifier.size(18.dp),
                contentScale = ContentScale.Fit,
                filterQuality = FilterQuality.None,
            )
        }
        Text(
            text = "%,dg".format(state.gold),
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.SemiBold,
            color = REFERENCE_INK,
            modifier = Modifier.padding(start = 5.dp),
        )
    }
}

@Composable
private fun ReferenceVital(label: String, value: Int, maximum: Int, tint: Color) {
    val fraction = if (maximum <= 0) 0f else value.toFloat().div(maximum).coerceIn(0f, 1f)
    Row(
        modifier = Modifier.fillMaxWidth().padding(top = 3.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall.copy(fontSize = 8.sp, lineHeight = 9.sp),
            color = REFERENCE_INK,
            modifier = Modifier.width(38.dp),
            maxLines = 1,
            softWrap = false,
            overflow = TextOverflow.Clip,
        )
        Box(
            modifier = Modifier
                .weight(1f)
                .height(9.dp)
                .border(1.dp, REFERENCE_BORDER, ThorTheme.shapes.small)
                .padding(1.dp),
        ) {
            Box(Modifier.fillMaxHeight().fillMaxWidth(fraction).background(tint))
        }
        Text(
            text = "$value/$maximum",
            style = MaterialTheme.typography.labelSmall.copy(fontSize = 8.sp, lineHeight = 9.sp),
            color = REFERENCE_INK,
            textAlign = TextAlign.End,
            modifier = Modifier.width(39.dp).padding(start = 3.dp),
            maxLines = 1,
        )
    }
}

@Composable
private fun ReferenceBackpackPanel(
    state: StardewState,
    modifier: Modifier = Modifier,
) {
    val haptics = LocalHapticFeedback.current
    val used = state.inventorySlots.take(StardewState.MAX_INVENTORY).count { !it.empty }
    val capacity = state.inventoryCapacity.coerceAtMost(StardewState.MAX_INVENTORY)
    val rows = referenceInventoryRows(capacity)
    val selected = state.itemAt(state.selectedSlot)

    Column(modifier = modifier) {
        Row(
            modifier = Modifier.fillMaxWidth().height(40.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            ReferenceIcon(state.sprites[StardewUi.TAB_BACKPACK], 26.dp)
            Text(
                text = "Backpack",
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold,
                color = REFERENCE_INK,
                modifier = Modifier.weight(1f),
            )
            Text(
                text = "$used/$capacity Slots Used",
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.SemiBold,
                color = REFERENCE_INK,
            )
            ReferenceSquareButton(
                state = state,
                sprite = state.sprites[StardewUi.SORT],
                description = "Sort backpack",
                enabled = state.config.allowInventoryEdits,
            ) {
                haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                SecondScreen.send(Commands.sortBag())
            }
        }
        ReferenceRule()

        BoxWithConstraints(modifier = Modifier.fillMaxWidth().weight(1f)) {
            val gap = REFERENCE_SLOT_GAP
            val lockedRows = (MAX_REFERENCE_ROWS - rows).coerceAtLeast(0)
            val lockedReserve = (REFERENCE_LOCKED_ROW_HEIGHT + 4.dp) * lockedRows
            val slotWidth =
                (maxWidth - gap * (HOTBAR_COLUMNS - 1)) / HOTBAR_COLUMNS
            val slotHeight =
                (maxHeight - REFERENCE_ROWS_CHROME - lockedReserve - gap * (rows - 1)) / rows
            val slotSize = minOf(slotWidth, slotHeight).coerceAtLeast(MIN_SLOT_SIZE)

            Column(
                modifier = Modifier.fillMaxSize(),
                verticalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                repeat(rows) { row ->
                    ReferenceInventoryRow(
                        state = state,
                        row = row,
                        slotSize = slotSize,
                    )
                }
                repeat(lockedRows) { offset ->
                    ReferenceLockedInventoryRow(
                        state = state,
                        row = rows + offset,
                        upgradesAway = offset + 1,
                    )
                }
                Spacer(modifier = Modifier.weight(1f))
                ReferenceSkills(state)
            }
        }

        ReferenceRule()
        Row(
            modifier = Modifier.fillMaxWidth().height(44.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            ReferenceIcon(state.sprites[StardewUi.TAB_BACKPACK], 26.dp)
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = if (selected.empty) {
                        "Slot ${state.selectedSlot + 1} is empty"
                    } else {
                        selected.name.ifEmpty { selected.category.ifEmpty { "Selected item" } }
                    },
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.SemiBold,
                    color = REFERENCE_INK,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                Text(
                    text = "L/R cycles rows • tap a slot to move items • hold to use",
                    style = MaterialTheme.typography.labelSmall,
                    color = REFERENCE_MUTED,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            ReferenceIcon(state.sprites[StardewUi.SLOT], 28.dp)
            ReferenceIcon(state.sprites[StardewUi.ARROW_RIGHT], 20.dp)
            ReferenceIcon(
                state.sprites[selected.id] ?: state.sprites[StardewUi.SLOT_SELECTED],
                32.dp,
            )
        }
    }
}

@Composable
private fun ReferenceLockedInventoryRow(
    state: StardewState,
    row: Int,
    upgradesAway: Int,
) {
    val frame = state.frame(StardewUi.BUTTON)
    val base = Modifier.fillMaxWidth().height(REFERENCE_LOCKED_ROW_HEIGHT)
    val painted = if (frame != null) {
        base.drawBehind { drawNineSlice(frame, size) }
    } else {
        base
            .clip(ThorTheme.shapes.small)
            .background(REFERENCE_WASH)
            .border(1.dp, REFERENCE_BORDER, ThorTheme.shapes.small)
    }
    Row(
        modifier = painted.padding(horizontal = 9.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(7.dp),
    ) {
        ReferenceIcon(state.sprites[StardewUi.TAB_BACKPACK], 20.dp)
        Text(
            text = "Row ${row + 1} (Locked)",
            style = MaterialTheme.typography.labelMedium,
            fontWeight = FontWeight.Bold,
            color = REFERENCE_INK,
            maxLines = 1,
        )
        Box(Modifier.height(17.dp).width(1.dp).background(REFERENCE_LINE))
        Text(
            text = if (upgradesAway == 1) {
                "Next backpack upgrade unlocks this hotbar row"
            } else {
                "Unlocks after $upgradesAway backpack upgrades"
            },
            style = MaterialTheme.typography.labelSmall,
            color = REFERENCE_MUTED,
            modifier = Modifier.weight(1f),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun ReferenceInventoryRow(state: StardewState, row: Int, slotSize: Dp) {
    val haptics = LocalHapticFeedback.current
    val start = row * HOTBAR_COLUMNS
    val items = (start until start + HOTBAR_COLUMNS).map(state::itemAt)

    Column(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(bottom = 2.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = if (row == 0) "Row 1 (Hotbar)" else "Row ${row + 1}",
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.SemiBold,
                color = REFERENCE_INK,
                modifier = Modifier.weight(1f),
            )
            if (row > 0) {
                ReferenceSquareButton(
                    state = state,
                    sprite = state.sprites[
                        if (row == 1) StardewUi.ARROW_RIGHT else StardewUi.ARROW_LEFT
                    ],
                    description = "Move row ${row + 1} to the hotbar",
                    enabled = state.config.allowInventoryEdits,
                    size = 22.dp,
                ) {
                    haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                    SecondScreen.send(Commands.shiftToolbar(right = row == 1))
                }
            }
        }
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(REFERENCE_SLOT_GAP),
        ) {
            items.forEach { item ->
                Slot(
                    item = item,
                    sprite = state.sprites[item.id],
                    frame = state.frame(
                        if (item.slot == state.selectedSlot) {
                            StardewUi.SLOT_SELECTED
                        } else {
                            StardewUi.SLOT
                        },
                    ),
                    selected = item.slot == state.selectedSlot,
                    enabled = state.config.allowInventoryEdits,
                    allowEmpty = true,
                    number = if (row == 0) referenceHotbarLabel(item.slot) else null,
                    modifier = Modifier.size(slotSize),
                    onClick = {
                        haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                        if (row == 0) {
                            SecondScreen.send(Commands.selectSlot(item.slot))
                        } else {
                            SecondScreen.send(Commands.moveItem(item.slot, state.selectedSlot))
                        }
                    },
                    onLongClick = {
                        if (!item.empty) {
                            haptics.performHapticFeedback(HapticFeedbackType.LongPress)
                            SecondScreen.send(Commands.eatSlot(item.slot))
                        }
                    },
                )
            }
        }
    }
}

@Composable
private fun ReferenceSkills(state: StardewState) {
    Column(modifier = Modifier.fillMaxWidth()) {
        ReferenceRule()
        Row(
            modifier = Modifier.fillMaxWidth().padding(top = 3.dp, bottom = 1.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            ReferenceIcon(state.sprites[StardewUi.TAB_SKILLS], 18.dp)
            Text(
                text = "Skills",
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.Bold,
                color = REFERENCE_INK,
            )
            Spacer(modifier = Modifier.weight(1f))
            Text(
                text = "Level ${state.playerLevel}",
                style = MaterialTheme.typography.labelMedium,
                fontWeight = FontWeight.SemiBold,
                color = REFERENCE_MUTED,
            )
        }
        Row(
            modifier = Modifier.fillMaxWidth().padding(bottom = 2.dp),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            ReferenceSkill("Farming", state.farming, Modifier.weight(1f))
            ReferenceSkill("Mining", state.mining, Modifier.weight(1f))
            ReferenceSkill("Foraging", state.foraging, Modifier.weight(1f))
            ReferenceSkill("Fishing", state.fishing, Modifier.weight(1f))
            ReferenceSkill("Combat", state.combat, Modifier.weight(1f))
        }
    }
}

@Composable
private fun ReferenceSkill(label: String, level: Int, modifier: Modifier = Modifier) {
    Column(modifier = modifier, horizontalAlignment = Alignment.CenterHorizontally) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            color = REFERENCE_MUTED,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            text = level.toString(),
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.Bold,
            color = REFERENCE_INK,
        )
    }
}

@Composable
private fun ReferenceSquareButton(
    state: StardewState,
    sprite: UiSprite?,
    description: String,
    enabled: Boolean,
    size: Dp = 34.dp,
    onClick: () -> Unit,
) {
    val frame = state.frame(StardewUi.BUTTON)
    val base = Modifier.size(size)
    Box(
        modifier = (if (frame != null) {
            base.drawBehind { drawNineSlice(frame, this.size) }
        } else {
            base.border(1.dp, REFERENCE_BORDER, ThorTheme.shapes.small)
        }).clickable(enabled = enabled, role = Role.Button, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        if (sprite != null) {
            Image(
                bitmap = sprite.image,
                contentDescription = description,
                modifier = Modifier.fillMaxSize().padding(6.dp),
                contentScale = ContentScale.Fit,
                filterQuality = FilterQuality.None,
            )
        }
    }
}

@Composable
private fun ReferenceIcon(sprite: UiSprite?, size: Dp) {
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
private fun ReferenceRule() {
    Box(Modifier.fillMaxWidth().height(1.dp).background(REFERENCE_LINE))
}

@Composable
private fun ReferenceVerticalRule() {
    Box(Modifier.fillMaxHeight().width(1.dp).background(REFERENCE_LINE))
}

/** A full inventory destination, sized for every vanilla and modded backpack row. */
@Composable
fun StardewInventoryPage(state: StardewState, modifier: Modifier = Modifier) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens
    val selected = state.itemAt(state.selectedSlot)
    val used = state.inventorySlots.count { !it.empty }

    BoxWithConstraints(modifier = modifier) {
        val rowCount =
            ((state.inventoryCapacity + HOTBAR_COLUMNS - 1) / HOTBAR_COLUMNS).coerceAtLeast(1)
        val gap = dimens.spacingTiny
        val widthBound =
            (maxWidth - INVENTORY_PANEL_RESERVE - gap * (HOTBAR_COLUMNS - 1)) /
                HOTBAR_COLUMNS
        val heightBound =
            (maxHeight - INVENTORY_CHROME - gap * (rowCount - 1)) / rowCount
        val slotSize = minOf(widthBound, heightBound).coerceAtLeast(MIN_SLOT_SIZE)

        InventoryPanel(state) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(dimens.spacing),
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = "Inventory",
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold,
                        color = colors.onSurface,
                    )
                    Text(
                        text = "$used of ${state.inventoryCapacity} slots used",
                        style = MaterialTheme.typography.bodySmall,
                        color = colors.onSurfaceVariant,
                    )
                }
            }

            StardewInventoryControls(state)
            InventorySectionTitle("Toolbelt · L/R changes row")
            StardewSelectedItemSummary(state, selected)
            StardewHotbar(
                state,
                modifier = Modifier.padding(top = dimens.spacingTiny),
                slotSize = slotSize,
            )

            Spacer(modifier = Modifier.height(dimens.spacingSmall))
            val backpackCapacity =
                (state.inventoryCapacity - StardewState.HOTBAR).coerceAtLeast(0)
            InventorySectionTitle(
                if (backpackCapacity > 0) "Backpack · $backpackCapacity slots" else "Backpack",
            )
            if (backpackCapacity == 0) {
                Text(
                    text = "Upgrade your backpack at Pierre's to unlock more rows.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = colors.onSurfaceVariant,
                    modifier = Modifier.padding(vertical = dimens.spacingSmall),
                )
            } else {
                StardewBackpack(state, slotSize = slotSize)
            }
        }
    }
}

@Composable
private fun InventoryPanel(
    state: StardewState,
    content: @Composable ColumnScope.() -> Unit,
) {
    val dimens = ThorTheme.dimens
    val panel = state.frame(StardewUi.PANEL)
    val painted = if (panel != null) {
        Modifier
            .fillMaxWidth()
            .drawBehind { drawNineSlice(panel, size) }
            .padding(
                horizontal = panel.left.dp.coerceAtLeast(dimens.spacing),
                vertical = panel.top.dp.coerceAtLeast(dimens.spacingSmall),
            )
    } else {
        // The game sends this frame before inventory content. Keep the brief
        // loading fallback unpainted instead of inventing a non-game panel.
        Modifier.fillMaxWidth().padding(dimens.spacing)
    }
    Column(modifier = painted, content = content)
}

@Composable
fun StardewHotbar(
    state: StardewState,
    modifier: Modifier = Modifier,
    slotSize: Dp? = null,
) {
    val haptics = LocalHapticFeedback.current
    FixedGrid(
        items = state.hotbarSlots,
        modifier = modifier,
        columns = StardewState.HOTBAR,
        slotSize = slotSize,
    ) { item ->
        Slot(
            item = item,
            sprite = state.sprites[item.id],
            frame = state.frame(
                if (item.slot == state.selectedSlot) StardewUi.SLOT_SELECTED else StardewUi.SLOT,
            ),
            selected = item.slot == state.selectedSlot,
            enabled = state.config.allowInventoryEdits,
            allowEmpty = true,
            number = referenceHotbarLabel(item.slot),
            modifier = Modifier.fillMaxWidth(),
            onClick = {
                haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                SecondScreen.send(Commands.selectSlot(item.slot))
            },
            onLongClick = {
                if (!item.empty) {
                    haptics.performHapticFeedback(HapticFeedbackType.LongPress)
                    SecondScreen.send(Commands.eatSlot(item.slot))
                }
            },
        )
    }
}

@Composable
fun StardewBackpack(
    state: StardewState,
    modifier: Modifier = Modifier,
    slotSize: Dp? = null,
) {
    val haptics = LocalHapticFeedback.current
    FixedGrid(
        items = state.backpackSlots,
        modifier = modifier,
        columns = HOTBAR_COLUMNS,
        slotSize = slotSize,
    ) { item ->
        Slot(
            item = item,
            sprite = state.sprites[item.id],
            frame = state.frame(StardewUi.SLOT),
            selected = false,
            enabled = state.config.allowInventoryEdits,
            // Empty destinations are useful: tapping one moves the active
            // toolbelt item into that slot. The old UI disabled these cells.
            allowEmpty = true,
            modifier = Modifier.fillMaxWidth(),
            onClick = {
                haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                SecondScreen.send(Commands.moveItem(item.slot, state.selectedSlot))
            },
            onLongClick = {
                if (!item.empty) {
                    haptics.performHapticFeedback(HapticFeedbackType.LongPress)
                    SecondScreen.send(Commands.eatSlot(item.slot))
                }
            },
        )
    }
}

@Composable
private fun FixedGrid(
    items: List<StardewItem>,
    modifier: Modifier,
    columns: Int,
    slotSize: Dp?,
    slot: @Composable (StardewItem) -> Unit,
) {
    val dimens = ThorTheme.dimens
    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        items.chunked(columns).forEach { row ->
            Row(
                modifier = if (slotSize == null) Modifier.fillMaxWidth() else Modifier,
                horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
            ) {
                row.forEach { item ->
                    val cell = if (slotSize != null) Modifier.size(slotSize) else Modifier.weight(1f)
                    Box(modifier = cell) { slot(item) }
                }
                repeat(columns - row.size) {
                    val cell = if (slotSize != null) {
                        Modifier.size(slotSize)
                    } else {
                        Modifier.weight(1f).aspectRatio(1f)
                    }
                    Spacer(modifier = cell)
                }
            }
        }
    }
}

@Composable
internal fun StardewInventoryControls(
    state: StardewState,
    modifier: Modifier = Modifier,
) {
    val haptics = LocalHapticFeedback.current
    val canEdit = state.config.allowInventoryEdits
    val hasBackpack = state.inventoryCapacity > StardewState.HOTBAR
    Row(
        modifier = modifier
            .fillMaxWidth()
            .padding(bottom = ThorTheme.dimens.spacingTiny),
        horizontalArrangement = Arrangement.spacedBy(ThorTheme.dimens.spacingTiny),
    ) {
        InventoryAction(
            state = state,
            label = "Previous row",
            enabled = canEdit && hasBackpack,
            modifier = Modifier.weight(1f),
            onClick = {
                haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                SecondScreen.send(Commands.shiftToolbar(right = false))
            },
        )
        InventoryAction(
            state = state,
            label = "Sort bag",
            enabled = canEdit,
            modifier = Modifier.weight(1f),
            onClick = {
                haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                SecondScreen.send(Commands.sortBag())
            },
        )
        InventoryAction(
            state = state,
            label = "Next row",
            enabled = canEdit && hasBackpack,
            modifier = Modifier.weight(1f),
            onClick = {
                haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                SecondScreen.send(Commands.shiftToolbar(right = true))
            },
        )
    }
}

@Composable
internal fun StardewSelectedItemSummary(state: StardewState, item: StardewItem) {
    val colors = ThorTheme.colors
    val details = buildList {
        if (item.category.isNotBlank()) add(item.category)
        qualityName(item.quality)?.let(::add)
        if (item.count > 1) add("${item.count} in stack")
        if (item.wateringCan) {
            add(if (item.bottomless) "Bottomless" else "${item.water}/${item.waterMax} water")
        }
        if (item.weapon) {
            add(if (item.cooldownMs > 0) "Special ${cooldownText(item.cooldownMs)}" else "Special ready")
        }
    }.joinToString(" · ")

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 3.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        val sprite = state.sprites[item.id]
        if (sprite != null) {
            Image(
                bitmap = sprite.image,
                contentDescription = null,
                modifier = Modifier.size(28.dp),
                contentScale = ContentScale.Fit,
                filterQuality = FilterQuality.None,
            )
        }
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = if (item.empty) "Empty tool slot" else item.name,
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.SemiBold,
                color = colors.onSurface,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                text = if (details.isEmpty()) "Tool slot ${state.selectedSlot + 1}" else details,
                style = MaterialTheme.typography.bodySmall,
                color = colors.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        Text(
            text = "SLOT ${state.selectedSlot + 1}",
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.Bold,
            color = colors.cursor,
        )
    }
}

@Composable
private fun InventoryAction(
    state: StardewState,
    label: String,
    enabled: Boolean,
    modifier: Modifier = Modifier,
    onClick: () -> Unit,
) {
    val colors = ThorTheme.colors
    val frame = state.frame(StardewUi.BUTTON)
    val base = modifier.heightIn(min = 40.dp)
    val painted = if (frame != null) {
        base.drawBehind { drawNineSlice(frame, size) }
    } else {
        base
            .clip(ThorTheme.shapes.small)
            .background(colors.surfaceHighest)
    }
    Box(
        modifier = painted.clickable(
            enabled = enabled,
            role = Role.Button,
            onClick = onClick,
        ),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = label.uppercase(),
            style = MaterialTheme.typography.labelLarge,
            fontWeight = FontWeight.Bold,
            color = if (enabled) colors.onSurface else colors.onSurfaceVariant.copy(alpha = 0.45f),
            textAlign = TextAlign.Center,
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 8.dp),
        )
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun Slot(
    item: StardewItem,
    sprite: UiSprite?,
    frame: UiSprite?,
    selected: Boolean,
    enabled: Boolean,
    modifier: Modifier = Modifier,
    allowEmpty: Boolean = false,
    number: String? = null,
    onClick: () -> Unit,
    onLongClick: () -> Unit = {},
) {
    val colors = ThorTheme.colors
    val interactive = enabled && (allowEmpty || !item.empty)

    val base = modifier.aspectRatio(1f)
    val painted = if (frame != null) {
        base.drawBehind { drawNineSlice(frame, size) }
    } else base

    Box(
        modifier = painted.combinedClickable(
            enabled = interactive,
            role = Role.Button,
            onClick = onClick,
            onLongClick = onLongClick,
        ),
        contentAlignment = Alignment.Center,
    ) {
        when {
            sprite != null -> Image(
                bitmap = sprite.image,
                contentDescription = item.name.ifEmpty { "Empty inventory slot" },
                modifier = Modifier.fillMaxSize().padding(4.dp),
                contentScale = ContentScale.Fit,
                filterQuality = FilterQuality.None,
            )

            !item.empty -> Text(
                text = item.name.take(12),
                style = MaterialTheme.typography.labelSmall,
                color = colors.onSurfaceVariant,
                textAlign = TextAlign.Center,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.padding(3.dp),
            )
        }

        if (number != null) {
            Text(
                text = number,
                style = MaterialTheme.typography.labelSmall,
                fontWeight = FontWeight.Bold,
                color = if (selected) colors.cursor else colors.onSurfaceVariant,
                modifier = Modifier.align(Alignment.TopStart).padding(2.dp),
            )
        }

        if (item.count > 1) {
            Text(
                text = item.count.toString(),
                style = MaterialTheme.typography.labelSmall,
                fontWeight = FontWeight.Bold,
                color = colors.onSurface,
                modifier = Modifier.align(Alignment.BottomEnd).padding(2.dp),
            )
        }

        WaterGauge(
            item = item,
            modifier = Modifier.align(Alignment.BottomCenter),
        )

        WeaponCooldownGauge(
            item = item,
            modifier = Modifier.align(Alignment.BottomCenter),
        )

        WeaponCooldown(
            item = item,
            modifier = Modifier.align(Alignment.TopEnd),
        )
    }
}

@Composable
private fun WaterGauge(item: StardewItem, modifier: Modifier = Modifier) {
    if (!item.wateringCan) return
    val fraction = if (item.bottomless) {
        1f
    } else {
        (item.water.toFloat() / item.waterMax).coerceIn(0f, 1f)
    }
    Box(
        modifier = modifier
            .fillMaxWidth()
            .padding(horizontal = 4.dp, vertical = 2.dp)
            .height(WATER_BAR_HEIGHT)
            .clip(ThorTheme.shapes.small)
            .background(Color.Black.copy(alpha = 0.48f)),
        contentAlignment = Alignment.CenterStart,
    ) {
        Box(
            modifier = Modifier
                .fillMaxHeight()
                .fillMaxWidth(fraction)
                .background(WATER_BLUE),
        )
    }
}

@Composable
private fun WeaponCooldown(item: StardewItem, modifier: Modifier = Modifier) {
    if (!item.weapon || item.cooldownMs <= 0) return
    Text(
        text = cooldownText(item.cooldownMs),
        style = MaterialTheme.typography.labelSmall.copy(fontSize = 8.sp, lineHeight = 9.sp),
        fontWeight = FontWeight.Bold,
        color = Color.White,
        modifier = modifier
            .clip(ThorTheme.shapes.small)
            .background(Color.Black.copy(alpha = 0.82f))
            .padding(horizontal = 3.dp, vertical = 1.dp),
        maxLines = 1,
    )
}

@Composable
private fun WeaponCooldownGauge(item: StardewItem, modifier: Modifier = Modifier) {
    if (!item.weapon || item.cooldownMs <= 0 || item.cooldownMax <= 0) return
    val ready = weaponCooldownReadyFraction(item.cooldownMs, item.cooldownMax)
    Box(
        modifier = modifier
            .fillMaxWidth()
            .padding(horizontal = 4.dp, vertical = 2.dp)
            .height(WATER_BAR_HEIGHT)
            .clip(ThorTheme.shapes.small)
            .background(Color.Black.copy(alpha = 0.48f)),
        contentAlignment = Alignment.CenterStart,
    ) {
        Box(
            modifier = Modifier
                .fillMaxHeight()
                .fillMaxWidth(ready)
                .background(COOLDOWN_READY),
        )
    }
}

private fun cooldownText(milliseconds: Int): String = when {
    milliseconds <= 0 -> "Ready"
    milliseconds >= 10_000 -> "${(milliseconds + 999) / 1000}s"
    else -> "%.1fs".format(milliseconds / 1000f)
}

@Composable
private fun InventorySectionTitle(text: String) {
    Text(
        text = text.uppercase(),
        style = MaterialTheme.typography.labelSmall,
        fontWeight = FontWeight.SemiBold,
        color = ThorTheme.colors.onSurfaceVariant,
        modifier = Modifier.padding(top = 4.dp, bottom = 2.dp),
    )
}

private fun qualityName(quality: Int): String? = when (quality) {
    1 -> "Silver quality"
    2 -> "Gold quality"
    4 -> "Iridium quality"
    else -> null
}

private const val HOTBAR_COLUMNS = 12
private const val MAX_REFERENCE_ROWS = 3
private val INVENTORY_PANEL_RESERVE = 48.dp
private val INVENTORY_CHROME = 156.dp
private val MIN_SLOT_SIZE = 12.dp
private val WATER_BAR_HEIGHT = 4.dp
private val WATER_BLUE = Color(0xFF2A9DF4)
private val COOLDOWN_READY = Color(0xFFF2A52B)
private val REFERENCE_ROWS_CHROME = 76.dp
private val REFERENCE_LOCKED_ROW_HEIGHT = 32.dp
private val REFERENCE_SLOT_GAP = 2.dp
private val REFERENCE_INK = Color(0xFF321B0F)
private val REFERENCE_MUTED = Color(0xFF70451F)
private val REFERENCE_BORDER = Color(0xFFA95F16)
private val REFERENCE_LINE = Color(0x55A95F16)
private val REFERENCE_WASH = Color(0x26FFD789)
private val REFERENCE_HEALTH = Color(0xFFCC2936)
private val REFERENCE_ENERGY = Color(0xFF43A62B)

internal fun referenceInventoryRows(capacity: Int): Int =
    ((capacity + HOTBAR_COLUMNS - 1) / HOTBAR_COLUMNS).coerceIn(1, 3)

internal fun referenceHotbarLabel(slot: Int): String = when (slot.mod(HOTBAR_COLUMNS)) {
    in 0..8 -> (slot.mod(HOTBAR_COLUMNS) + 1).toString()
    9 -> "0"
    10 -> "-"
    else -> "="
}

internal fun weaponCooldownReadyFraction(remaining: Int, maximum: Int): Float {
    if (maximum <= 0) return 0f
    return (1f - remaining.coerceAtLeast(0).toFloat() / maximum).coerceIn(0f, 1f)
}

private fun referenceFarmTitle(rawName: String): String {
    val name = rawName.trim()
    if (name.isEmpty()) return "Your Farm"
    return if (name.endsWith("farm", ignoreCase = true)) name else "$name Farm"
}

private fun referenceDayLabel(weekday: String, day: Int): String {
    if (day <= 0) return ""
    return listOf(weekday.trim(), day.toString())
        .filter(String::isNotEmpty)
        .joinToString(" ")
}
