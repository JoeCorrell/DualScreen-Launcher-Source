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
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
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
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.thor.core.designsystem.theme.ThorTheme

/**
 * A compact, read-only progress ledger built exclusively from values sent by the game mod.
 * The outer page never scrolls; the museum and shipping rails own their scrolling instead.
 */
@Composable
fun StardewProgressReferencePage(
    state: StardewState,
    modifier: Modifier = Modifier,
) {
    val history by StardewHistory.days.collectAsStateWithLifecycle()
    val overview = remember(
        state.mines,
        state.collections,
        state.shipping,
        state.gold,
        state.totalEarnings,
    ) {
        stardewProgressOverview(state)
    }
    val recent = remember(history, state.gold) {
        stardewRecentHistory(history, state.gold)
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .background(PROGRESS_PARCHMENT.copy(alpha = 0.30f)),
    ) {
        ProgressPageHeader(state, overview)
        ProgressRule(strong = true)

        Row(modifier = Modifier.fillMaxWidth().weight(1f)) {
            Column(modifier = Modifier.weight(0.88f).fillMaxHeight()) {
                MiningProgressPanel(state, overview)
                ProgressRule(strong = true)
                MuseumProgressPanel(
                    state = state,
                    overview = overview,
                    modifier = Modifier.fillMaxWidth().weight(1f),
                )
            }
            ProgressVerticalRule()
            ShippingProgressPanel(
                state = state,
                overview = overview,
                modifier = Modifier.weight(1.12f).fillMaxHeight(),
            )
        }

        if (recent.days.isNotEmpty()) {
            ProgressRule(strong = true)
            ProgressHistoryStrip(state, recent)
        }
        ProgressRule(strong = true)
        ProgressDateFooter(state)
    }
}

@Composable
private fun ProgressPageHeader(state: StardewState, overview: StardewProgressOverview) {
    Row(
        modifier = Modifier.fillMaxWidth().height(58.dp).padding(horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(9.dp),
    ) {
        ProgressSprite(state.sprites[StardewUi.TAB_SKILLS], 35.dp)
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = "Farm Progress",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
                color = PROGRESS_INK,
                maxLines = 1,
            )
            Text(
                text = progressHeaderDetail(state, overview),
                style = MaterialTheme.typography.bodySmall,
                color = PROGRESS_MUTED,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        Column(horizontalAlignment = Alignment.End) {
            Text(
                text = "%,dg".format(overview.lifetimeEarnings),
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.Bold,
                color = PROGRESS_GOLD,
                maxLines = 1,
            )
            Text(
                text = "lifetime earnings",
                style = MaterialTheme.typography.labelSmall,
                color = PROGRESS_MUTED,
                maxLines = 1,
            )
        }
    }
}

@Composable
private fun MiningProgressPanel(state: StardewState, overview: StardewProgressOverview) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(PROGRESS_SECTION_WASH)
            .padding(horizontal = 12.dp, vertical = 10.dp),
    ) {
        ProgressSectionHeading(
            state = state,
            title = "Mining",
            detail = if (overview.mineComplete) "Bottom reached" else "${overview.mineFloorsLeft} floors remain",
            sprite = state.sprites[StardewUi.TAB_CRAFTING],
        )
        Spacer(Modifier.height(8.dp))
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            ProgressMetricCard(
                label = "THE MINES",
                value = "Floor ${overview.deepestMine}",
                detail = mineMilestoneDetail(overview),
                fraction = overview.mineFraction,
                modifier = Modifier.weight(1f),
            )
            ProgressMetricCard(
                label = "SKULL CAVERN",
                value = if (overview.skullFloor > 0) {
                    "Floor ${overview.skullFloor}"
                } else {
                    "Not entered"
                },
                detail = if (overview.skullFloor > 0) "Personal deepest" else "No depth recorded",
                fraction = null,
                modifier = Modifier.weight(1f),
            )
        }
    }
}

@Composable
private fun MuseumProgressPanel(
    state: StardewState,
    overview: StardewProgressOverview,
    modifier: Modifier = Modifier,
) {
    val donations = remember(state.collections.carried) {
        state.collections.carried.distinctBy { it.id to it.name }
    }
    Column(modifier = modifier.padding(horizontal = 12.dp, vertical = 10.dp)) {
        ProgressSectionHeading(
            state = state,
            title = "Museum",
            detail = when {
                overview.museumTotal <= 0 -> "Waiting for collection totals"
                overview.museumComplete -> "Collection complete"
                else -> "${overview.museumRemaining} pieces remain"
            },
            sprite = donations.firstNotNullOfOrNull { state.sprites[it.id] }
                ?: state.sprites[StardewUi.TAB_JOURNAL],
        )
        Spacer(Modifier.height(8.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                text = "${overview.museumDonated}/${overview.museumTotal}",
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.Bold,
                color = PROGRESS_INK,
            )
            Spacer(Modifier.width(10.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = "${overview.museumPercent}% donated",
                    style = MaterialTheme.typography.bodySmall,
                    fontWeight = FontWeight.SemiBold,
                    color = if (overview.museumComplete) PROGRESS_READY else PROGRESS_MUTED,
                )
                ProgressBar(overview.museumFraction, Modifier.padding(top = 4.dp))
            }
        }
        Spacer(Modifier.height(10.dp))
        Text(
            text = if (donations.isEmpty()) {
                "NO DONATIONS IN BACKPACK"
            } else {
                "READY TO DONATE · ${donations.size}"
            },
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.Bold,
            color = if (donations.isEmpty()) PROGRESS_MUTED else PROGRESS_READY,
            maxLines = 1,
        )
        Spacer(Modifier.height(5.dp))
        if (donations.isEmpty()) {
            Text(
                text = if (state.hasWorld) {
                    "New artifacts and minerals in your backpack will appear here."
                } else {
                    "Waiting for museum progress from the farm."
                },
                style = MaterialTheme.typography.bodySmall,
                color = PROGRESS_MUTED,
                modifier = Modifier.padding(top = 4.dp),
            )
        } else {
            LazyRow(
                modifier = Modifier.fillMaxWidth().weight(1f),
                contentPadding = PaddingValues(end = 4.dp),
                horizontalArrangement = Arrangement.spacedBy(7.dp),
            ) {
                items(
                    items = donations,
                    key = { item -> "museum:${item.id}:${item.name}" },
                ) { item ->
                    MuseumDonationChip(state, item)
                }
            }
        }
    }
}

@Composable
private fun MuseumDonationChip(state: StardewState, item: StardewRecipe) {
    Column(
        modifier = Modifier
            .width(92.dp)
            .fillMaxHeight()
            .clip(ThorTheme.shapes.small)
            .background(PROGRESS_READY_WASH)
            .border(1.dp, PROGRESS_READY.copy(alpha = 0.65f), ThorTheme.shapes.small)
            .padding(horizontal = 5.dp, vertical = 6.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        ProgressSpriteSlot(state, state.sprites[item.id], 42.dp)
        Text(
            text = item.name,
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.SemiBold,
            color = PROGRESS_INK,
            modifier = Modifier.padding(top = 4.dp),
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun ShippingProgressPanel(
    state: StardewState,
    overview: StardewProgressOverview,
    modifier: Modifier = Modifier,
) {
    Column(modifier = modifier) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(PROGRESS_SECTION_WASH)
                .padding(horizontal = 12.dp, vertical = 10.dp),
        ) {
            ProgressSectionHeading(
                state = state,
                title = "Shipping Bin",
                detail = when {
                    overview.shipments.isNotEmpty() && overview.shippingMayBePartial ->
                        "${overview.shippedUnits}+ units shown · partial item list"
                    overview.shipments.isNotEmpty() ->
                        "${overview.shippedUnits} units shown · ${overview.shipments.size} types"
                    overview.shippingValue > 0 -> "Live total received"
                    else -> "Nothing shipped today"
                },
                sprite = state.sprites[StardewUi.COIN],
            )
            Spacer(Modifier.height(7.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.Bottom,
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = "CURRENT BIN VALUE",
                        style = MaterialTheme.typography.labelSmall,
                        fontWeight = FontWeight.Bold,
                        color = PROGRESS_MUTED,
                    )
                    Text(
                        text = "%,dg".format(overview.shippingValue),
                        style = MaterialTheme.typography.headlineSmall,
                        fontWeight = FontWeight.Bold,
                        color = if (overview.shippingValue > 0) PROGRESS_GOLD else PROGRESS_INK,
                    )
                }
                Column(horizontalAlignment = Alignment.End) {
                    Text(
                        text = "WALLET",
                        style = MaterialTheme.typography.labelSmall,
                        fontWeight = FontWeight.Bold,
                        color = PROGRESS_MUTED,
                    )
                    Text(
                        text = "%,dg".format(overview.walletGold),
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.SemiBold,
                        color = PROGRESS_INK,
                    )
                }
            }
        }
        ProgressRule()

        if (overview.shipments.isEmpty()) {
            Column(
                modifier = Modifier.fillMaxSize().padding(24.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
            ) {
                ProgressSprite(state.sprites[StardewUi.COIN], 42.dp)
                Text(
                    text = if (!state.hasWorld) {
                        "Waiting for the farm"
                    } else if (overview.shippingValue > 0) {
                        "Shipping total received"
                    } else {
                        "The shipping bin is empty"
                    },
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.Bold,
                    color = PROGRESS_INK,
                    modifier = Modifier.padding(top = 9.dp),
                )
                Text(
                    text = if (!state.hasWorld) {
                        "Live shipping-bin contents and values will appear here."
                    } else if (overview.shippingValue > 0) {
                        "The game reported the sale value without individual item details."
                    } else {
                        "Items placed in the bin will be listed with their live sale value."
                    },
                    style = MaterialTheme.typography.bodySmall,
                    color = PROGRESS_MUTED,
                    modifier = Modifier.padding(top = 4.dp),
                )
            }
        } else {
            LazyColumn(
                modifier = Modifier.fillMaxWidth().weight(1f),
                contentPadding = PaddingValues(bottom = 5.dp),
            ) {
                items(
                    items = overview.shipments,
                    key = { shipment -> "shipping:${shipment.id}:${shipment.name}" },
                ) { shipment ->
                    ShippingProgressRow(state, shipment, overview.shippingValue)
                }
            }
        }
    }
}

@Composable
private fun ShippingProgressRow(
    state: StardewState,
    shipment: StardewProgressShipment,
    totalValue: Int,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(64.dp)
            .background(PROGRESS_ROW_WASH)
            .padding(horizontal = 12.dp, vertical = 7.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(9.dp),
    ) {
        ProgressSpriteSlot(state, state.sprites[shipment.id], 48.dp)
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = shipment.name.ifEmpty { "Shipped item" },
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.SemiBold,
                color = PROGRESS_INK,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                text = shippingUnitDetail(shipment),
                style = MaterialTheme.typography.labelSmall,
                color = PROGRESS_MUTED,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            if (totalValue > 0 && shipment.worth > 0) {
                ProgressBar(
                    fraction = shipment.worth.toFloat() / totalValue,
                    modifier = Modifier.padding(top = 3.dp),
                )
            }
        }
        Text(
            text = "%,dg".format(shipment.worth),
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.Bold,
            color = PROGRESS_GOLD,
            maxLines = 1,
        )
    }
    ProgressRule()
}

@Composable
private fun ProgressMetricCard(
    label: String,
    value: String,
    detail: String,
    fraction: Float?,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier
            .height(86.dp)
            .clip(ThorTheme.shapes.small)
            .background(PROGRESS_CARD_WASH)
            .border(1.dp, PROGRESS_RULE, ThorTheme.shapes.small)
            .padding(horizontal = 9.dp, vertical = 7.dp),
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.Bold,
            color = PROGRESS_MUTED,
            maxLines = 1,
        )
        Text(
            text = value,
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.Bold,
            color = PROGRESS_INK,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            text = detail,
            style = MaterialTheme.typography.labelSmall,
            color = PROGRESS_MUTED,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        if (fraction != null) ProgressBar(fraction, Modifier.padding(top = 5.dp))
    }
}

@Composable
private fun ProgressSectionHeading(
    state: StardewState,
    title: String,
    detail: String,
    sprite: UiSprite?,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(7.dp),
    ) {
        ProgressSprite(sprite, 25.dp)
        Text(
            text = title,
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.Bold,
            color = PROGRESS_INK,
            modifier = Modifier.weight(1f),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            text = detail,
            style = MaterialTheme.typography.labelSmall,
            color = PROGRESS_MUTED,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun ProgressBar(fraction: Float, modifier: Modifier = Modifier) {
    Box(
        modifier = modifier
            .fillMaxWidth()
            .height(5.dp)
            .clip(ThorTheme.shapes.pill)
            .background(PROGRESS_TRACK),
    ) {
        Box(
            Modifier
                .fillMaxWidth(fraction.coerceIn(0f, 1f))
                .fillMaxHeight()
                .background(PROGRESS_GOLD),
        )
    }
}

@Composable
private fun ProgressSpriteSlot(state: StardewState, sprite: UiSprite?, size: Dp) {
    val frame = state.frame(StardewUi.SLOT)
    val base = Modifier.size(size)
    val painted = if (frame != null) {
        base.drawBehind { drawNineSlice(frame, this.size) }
    } else {
        base
            .clip(ThorTheme.shapes.small)
            .background(PROGRESS_CARD_WASH)
            .border(1.dp, PROGRESS_RULE, ThorTheme.shapes.small)
    }
    Box(painted, contentAlignment = Alignment.Center) {
        ProgressSprite(sprite, size - 10.dp)
    }
}

@Composable
private fun ProgressSprite(sprite: UiSprite?, size: Dp) {
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
private fun ProgressHistoryStrip(state: StardewState, recent: StardewRecentHistory) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(62.dp)
            .background(PROGRESS_SECTION_WASH)
            .padding(horizontal = 10.dp, vertical = 5.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        ProgressSprite(state.sprites[StardewUi.COIN], 25.dp)
        Spacer(Modifier.width(7.dp))
        Column(modifier = Modifier.width(137.dp)) {
            Text(
                text = "RECENT ${recent.days.size} DAYS",
                style = MaterialTheme.typography.labelSmall,
                fontWeight = FontWeight.Bold,
                color = PROGRESS_INK,
                maxLines = 1,
            )
            Text(
                text = "Shipping-bin values",
                style = MaterialTheme.typography.labelSmall,
                color = PROGRESS_MUTED,
                maxLines = 1,
            )
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    text = "Wallet net change",
                    style = MaterialTheme.typography.labelSmall,
                    color = PROGRESS_MUTED,
                    modifier = Modifier.weight(1f),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                Text(
                    text = compactProgressGold(recent.walletNetChange, signed = true),
                    style = MaterialTheme.typography.labelSmall,
                    fontWeight = FontWeight.Bold,
                    color = when {
                        recent.walletNetChange > 0 -> PROGRESS_READY
                        recent.walletNetChange < 0 -> PROGRESS_LOSS
                        else -> PROGRESS_MUTED
                    },
                    maxLines = 1,
                )
            }
        }
        Spacer(Modifier.width(8.dp))
        Box(Modifier.fillMaxHeight().width(1.dp).background(PROGRESS_RULE))
        Spacer(Modifier.width(7.dp))
        Row(
            modifier = Modifier.weight(1f).fillMaxHeight(),
            horizontalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            recent.days.forEach { day ->
                ProgressHistoryDay(
                    day = day,
                    peakBinValue = recent.peakBinValue,
                    modifier = Modifier.weight(1f).fillMaxHeight(),
                )
            }
            repeat((RECENT_HISTORY_DAYS - recent.days.size).coerceAtLeast(0)) {
                Spacer(Modifier.weight(1f))
            }
        }
    }
}

@Composable
private fun ProgressHistoryDay(
    day: StardewDayRecord,
    peakBinValue: Int,
    modifier: Modifier = Modifier,
) {
    val binValue = day.shipped.coerceAtLeast(0)
    val fraction = if (peakBinValue <= 0) 0f else binValue.toFloat() / peakBinValue
    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            text = day.label,
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.SemiBold,
            color = PROGRESS_INK,
            maxLines = 1,
            overflow = TextOverflow.Clip,
        )
        Box(
            modifier = Modifier.fillMaxWidth().weight(1f).background(PROGRESS_TRACK),
            contentAlignment = Alignment.BottomCenter,
        ) {
            Box(
                Modifier
                    .fillMaxWidth(0.58f)
                    .fillMaxHeight(fraction.coerceIn(0f, 1f))
                    .background(PROGRESS_GOLD),
            )
        }
        Text(
            text = compactProgressGold(binValue.toLong()),
            style = MaterialTheme.typography.labelSmall,
            color = PROGRESS_MUTED,
            maxLines = 1,
            overflow = TextOverflow.Clip,
        )
    }
}

@Composable
private fun ProgressDateFooter(state: StardewState) {
    Row(
        modifier = Modifier.fillMaxWidth().height(43.dp).padding(horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        ProgressSprite(state.sprites[StardewUi.SEASON], 26.dp)
        Spacer(Modifier.width(7.dp))
        Text(
            text = progressDate(state),
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.SemiBold,
            color = PROGRESS_INK,
            modifier = Modifier.weight(1f),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        ProgressSprite(state.sprites[StardewUi.TIME], 23.dp)
        Spacer(Modifier.width(6.dp))
        Text(
            text = state.clock.replace("am", " am").replace("pm", " pm"),
            style = MaterialTheme.typography.bodyMedium,
            color = PROGRESS_INK,
            maxLines = 1,
        )
    }
}

@Composable
private fun ProgressRule(strong: Boolean = false) {
    Box(
        Modifier
            .fillMaxWidth()
            .height(if (strong) 2.dp else 1.dp)
            .background(if (strong) PROGRESS_GOLD else PROGRESS_RULE),
    )
}

@Composable
private fun ProgressVerticalRule() {
    Box(Modifier.fillMaxHeight().width(2.dp).background(PROGRESS_GOLD))
}

internal data class StardewProgressShipment(
    val id: String,
    val name: String,
    val count: Int,
    val worth: Int,
)

internal data class StardewProgressOverview(
    val deepestMine: Int,
    val mineFloorsLeft: Int,
    val mineFraction: Float,
    val nextMineMilestone: Int?,
    val skullFloor: Int,
    val museumDonated: Int,
    val museumTotal: Int,
    val museumRemaining: Int,
    val museumFraction: Float,
    val museumPercent: Int,
    val shippingValue: Int,
    val shippedUnits: Int,
    val shippingMayBePartial: Boolean,
    val shipments: List<StardewProgressShipment>,
    val walletGold: Int,
    val lifetimeEarnings: Long,
) {
    val mineComplete: Boolean get() = deepestMine >= MINE_BOTTOM
    val museumComplete: Boolean get() = museumTotal > 0 && museumDonated >= museumTotal
}

internal fun stardewProgressOverview(state: StardewState): StardewProgressOverview {
    val deepest = state.mines.deepest.coerceIn(0, MINE_BOTTOM)
    val museumTotal = state.collections.total.coerceAtLeast(0)
    val museumDonated = state.collections.donated.coerceIn(0, museumTotal.coerceAtLeast(0))
    val shipments = aggregateProgressShipments(state.shipping.items)
    return StardewProgressOverview(
        deepestMine = deepest,
        mineFloorsLeft = (MINE_BOTTOM - deepest).coerceAtLeast(0),
        mineFraction = deepest.toFloat() / MINE_BOTTOM,
        nextMineMilestone = nextMineElevatorFloor(deepest),
        skullFloor = state.mines.skull.coerceAtLeast(0),
        museumDonated = museumDonated,
        museumTotal = museumTotal,
        museumRemaining = (museumTotal - museumDonated).coerceAtLeast(0),
        museumFraction = if (museumTotal == 0) 0f else museumDonated.toFloat() / museumTotal,
        museumPercent = if (museumTotal == 0) 0 else (museumDonated * 100 / museumTotal),
        shippingValue = state.shipping.total.coerceAtLeast(0),
        shippedUnits = shipments.sumOf { it.count },
        shippingMayBePartial = state.shipping.items.size >= SHIPPING_DETAIL_LIMIT,
        shipments = shipments,
        walletGold = state.gold.coerceAtLeast(0),
        lifetimeEarnings = state.totalEarnings.coerceAtLeast(0L),
    )
}

internal fun aggregateProgressShipments(items: List<StardewShipped>): List<StardewProgressShipment> =
    items
        .groupBy { it.id to it.name }
        .map { (identity, matches) ->
            StardewProgressShipment(
                id = identity.first,
                name = identity.second,
                count = matches.sumOf { it.count.coerceAtLeast(0) },
                worth = matches.sumOf { it.worth.coerceAtLeast(0) },
            )
        }
        .sortedWith(compareByDescending<StardewProgressShipment> { it.worth }.thenBy { it.name })

internal fun nextMineElevatorFloor(deepest: Int): Int? {
    val safe = deepest.coerceIn(0, MINE_BOTTOM)
    if (safe >= MINE_BOTTOM) return null
    return (((safe / ELEVATOR_INTERVAL) + 1) * ELEVATOR_INTERVAL).coerceAtMost(MINE_BOTTOM)
}

internal data class StardewRecentHistory(
    val days: List<StardewDayRecord>,
    val walletNetChange: Long,
    val peakBinValue: Int,
)

internal fun stardewRecentHistory(
    records: List<StardewDayRecord>,
    currentWallet: Int,
): StardewRecentHistory {
    val days = records.takeLast(RECENT_HISTORY_DAYS)
    return StardewRecentHistory(
        days = days,
        walletNetChange = days.firstOrNull()?.let { first ->
            currentWallet.toLong() - first.gold.toLong()
        } ?: 0L,
        peakBinValue = days.maxOfOrNull { it.shipped.coerceAtLeast(0) } ?: 0,
    )
}

internal fun compactProgressGold(value: Long, signed: Boolean = false): String {
    val negative = value < 0
    val magnitude = kotlin.math.abs(value)
    val body = when {
        magnitude >= 1_000_000L -> compactProgressUnit(magnitude, 1_000_000L, "m")
        magnitude >= 10_000L -> compactProgressUnit(magnitude, 1_000L, "k")
        else -> "%,dg".format(magnitude)
    }
    val prefix = when {
        negative -> "−"
        signed && value > 0 -> "+"
        else -> ""
    }
    return prefix + body
}

private fun compactProgressUnit(value: Long, unit: Long, suffix: String): String {
    val whole = value / unit
    val tenth = (value % unit) / (unit / 10)
    return if (tenth == 0L) "$whole$suffix" else "$whole.$tenth$suffix"
}

private fun progressHeaderDetail(
    state: StardewState,
    overview: StardewProgressOverview,
): String {
    if (!state.hasWorld) return "Waiting for farm progress"
    return buildList {
        add("Mine ${overview.deepestMine}/$MINE_BOTTOM")
        if (overview.museumTotal > 0) add("Museum ${overview.museumPercent}%")
        if (overview.shippingValue > 0) add("%,dg bin value".format(overview.shippingValue))
    }.joinToString(" · ")
}

private fun mineMilestoneDetail(overview: StardewProgressOverview): String = when {
    overview.mineComplete -> "All $MINE_BOTTOM floors explored"
    overview.nextMineMilestone == null -> "Keep descending"
    else -> "Next elevator: ${overview.nextMineMilestone}"
}

private fun shippingUnitDetail(shipment: StardewProgressShipment): String {
    val count = shipment.count.coerceAtLeast(0)
    val each = if (count > 0) shipment.worth / count else 0
    return if (count > 1 && each > 0) "×$count · %,dg each".format(each) else "×$count"
}

private fun progressDate(state: StardewState): String {
    if (!state.hasWorld || state.day <= 0) return "Waiting for farm"
    val weekday = state.weekday.trim().take(3)
    val day = if (weekday.isEmpty()) state.day.toString() else "$weekday. ${state.day}"
    return "$day, ${state.season}, Year ${state.year.coerceAtLeast(1)}"
}

private const val MINE_BOTTOM = 120
private const val ELEVATOR_INTERVAL = 5
private const val SHIPPING_DETAIL_LIMIT = 12
private const val RECENT_HISTORY_DAYS = 7

private val PROGRESS_INK = Color(0xFF321B0F)
private val PROGRESS_MUTED = Color(0xFF70451F)
private val PROGRESS_READY = Color(0xFF19721F)
private val PROGRESS_LOSS = Color(0xFF9D2C1D)
private val PROGRESS_GOLD = Color(0xFFC47A12)
private val PROGRESS_PARCHMENT = Color(0xFFFFD98E)
private val PROGRESS_SECTION_WASH = Color(0x32FFD789)
private val PROGRESS_ROW_WASH = Color(0x18FFF0B8)
private val PROGRESS_CARD_WASH = Color(0x2AFFF0B8)
private val PROGRESS_READY_WASH = Color(0x307DCB65)
private val PROGRESS_RULE = Color(0x55A95F16)
private val PROGRESS_TRACK = Color(0x396F421B)
