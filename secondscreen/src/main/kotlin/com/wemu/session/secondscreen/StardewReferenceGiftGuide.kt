package com.wemu.session.secondscreen

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
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
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.thor.core.designsystem.theme.ThorTheme

/**
 * Loved-gift reference with a stable villager rail and a separately scrolling item list.
 * Every item and availability flag comes from the live `sdv_gifts` payload.
 *
 * <h2>The size this page is drawn for</h2>
 *
 * The Thor's lower panel is 1240x1080 physical pixels at density 2.30625, which
 * is **538 x 468 dp** — a phone-sized canvas, not a large one. Every fixed
 * height here is spent out of 468dp, and this page originally spent 153 of them
 * on chrome before the content row began: a 58dp page header, a 47dp filter
 * rail, an 87dp pane header on each side, a 42dp section bar and a 43dp footer.
 * What was left showed three of thirty villagers and two rows of gifts, at
 * sprite sizes larger than any other reference page uses.
 *
 * So the numbers below are not arbitrary and should not be raised without
 * redoing the arithmetic: chrome is now ~115dp, which leaves ~353dp of content,
 * about seven villagers and three and a half rows of gifts. A dp added to any
 * header here is a dp taken from the only part of the page anyone reads.
 */
@Composable
fun StardewGiftGuideReferencePage(
    state: StardewState,
    modifier: Modifier = Modifier,
) {
    val entries = remember(state.gifts, state.villagers) {
        stardewGiftGuideEntries(state.gifts, state.villagers)
    }
    var readyOnly by rememberSaveable { mutableStateOf(false) }
    var selectedName by rememberSaveable { mutableStateOf("") }
    val visible = remember(entries, readyOnly) {
        if (readyOnly) entries.filter { it.readyCount > 0 } else entries
    }
    val selected = visible.firstOrNull { it.name == selectedName } ?: visible.firstOrNull()

    Column(
        modifier = modifier
            .fillMaxSize()
            .background(GIFT_PARCHMENT.copy(alpha = 0.30f)),
    ) {
        GiftGuideHeader(state, entries)
        GiftRule(strong = true)
        GiftFilterRail(
            state = state,
            readyOnly = readyOnly,
            allCount = entries.size,
            readyCount = entries.count { it.readyCount > 0 },
            onReadyOnly = { readyOnly = it },
        )
        GiftRule()

        Row(modifier = Modifier.fillMaxWidth().weight(1f)) {
            Column(modifier = Modifier.weight(1f).fillMaxHeight()) {
                GiftVillagerRailHeader(
                    state = state,
                    entries = visible,
                )
                GiftRule()
                GiftVillagerRail(
                    state = state,
                    entries = visible,
                    selected = selected?.name.orEmpty(),
                    onSelect = { selectedName = it.name },
                    modifier = Modifier.fillMaxWidth().weight(1f),
                )
            }
            GiftVerticalRule()
            if (selected == null) {
                GiftGuideEmpty(
                    state = state,
                    readyOnly = readyOnly,
                    modifier = Modifier.weight(1f).fillMaxHeight(),
                )
            } else {
                GiftVillagerDetail(
                    state = state,
                    entry = selected,
                    modifier = Modifier.weight(1f).fillMaxHeight(),
                )
            }
        }

        GiftRule(strong = true)
        GiftGuideFooter(state, entries)
    }
}

@Composable
private fun GiftGuideHeader(state: StardewState, entries: List<StardewGiftGuideEntry>) {
    val readyVillagers = entries.count { it.readyCount > 0 }
    val readyItems = entries.sumOf { it.readyCount }
    Row(
        modifier = Modifier.fillMaxWidth().height(42.dp).padding(horizontal = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(9.dp),
    ) {
        GiftSprite(state.sprites[StardewUi.TAB_RELATIONSHIPS], 26.dp)
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = "Gift Guide",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
                color = GIFT_INK,
                maxLines = 1,
            )
            Text(
                text = when {
                    !state.hasWorld -> "Waiting for known gift tastes"
                    !state.config.sendVillagers -> "Villager data is disabled in mod settings"
                    entries.isEmpty() -> "No known gift tastes received"
                    else -> "${entries.size} villagers · loved gifts from your game"
                },
                style = MaterialTheme.typography.bodySmall,
                color = GIFT_MUTED,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        if (readyItems > 0) {
            GiftSprite(state.sprites[StardewUi.CHECK_ON], 24.dp)
            Column(horizontalAlignment = Alignment.End) {
                Text(
                    text = "$readyItems for $readyVillagers",
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.Bold,
                    color = GIFT_READY,
                    maxLines = 1,
                )
                Text(
                    text = "matches in backpack",
                    style = MaterialTheme.typography.labelSmall,
                    color = GIFT_READY,
                    maxLines = 1,
                )
            }
        }
    }
}

@Composable
private fun GiftFilterRail(
    state: StardewState,
    readyOnly: Boolean,
    allCount: Int,
    readyCount: Int,
    onReadyOnly: (Boolean) -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(36.dp)
            .background(GIFT_FILTER_WASH)
            .padding(horizontal = 8.dp, vertical = 5.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        GiftFilterButton(
            state = state,
            text = "All $allCount",
            selected = !readyOnly,
            onClick = { onReadyOnly(false) },
            modifier = Modifier.weight(1f),
        )
        GiftFilterButton(
            state = state,
            text = "In backpack $readyCount",
            selected = readyOnly,
            onClick = { onReadyOnly(true) },
            modifier = Modifier.weight(1f),
        )
    }
}

@Composable
private fun GiftFilterButton(
    state: StardewState,
    text: String,
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val frame = state.frame(if (selected) StardewUi.SLOT_SELECTED else StardewUi.BUTTON)
    val base = modifier.height(26.dp)
    val painted = if (frame != null) {
        base.drawBehind { drawNineSlice(frame, this.size) }
    } else {
        base
            .clip(ThorTheme.shapes.small)
            .background(if (selected) GIFT_SELECTED else GIFT_CARD_WASH)
            .border(1.dp, if (selected) GIFT_GOLD else GIFT_RULE, ThorTheme.shapes.small)
    }
    Box(
        modifier = painted.clickable(role = Role.Button, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = text,
            style = MaterialTheme.typography.labelSmall,
            fontWeight = if (selected) FontWeight.Bold else FontWeight.SemiBold,
            color = GIFT_INK,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun GiftVillagerRailHeader(
    state: StardewState,
    entries: List<StardewGiftGuideEntry>,
) {
    val readyVillagers = entries.count { it.readyCount > 0 }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(56.dp)
            .background(GIFT_DETAIL_WASH)
            .padding(horizontal = 10.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        GiftSpriteSlot(
            state = state,
            sprite = state.sprites[StardewUi.TAB_RELATIONSHIPS],
            size = 64.dp,
        )
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = "Villagers",
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold,
                color = GIFT_INK,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                text = "${entries.size} shown",
                style = MaterialTheme.typography.bodySmall,
                fontWeight = FontWeight.SemiBold,
                color = GIFT_MUTED,
                maxLines = 1,
            )
            Text(
                text = if (readyVillagers > 0) {
                    "$readyVillagers with a loved gift in backpack"
                } else {
                    "Select a villager to view loved gifts"
                },
                style = MaterialTheme.typography.labelSmall,
                color = if (readyVillagers > 0) GIFT_READY else GIFT_MUTED,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

@Composable
private fun GiftVillagerRail(
    state: StardewState,
    entries: List<StardewGiftGuideEntry>,
    selected: String,
    onSelect: (StardewGiftGuideEntry) -> Unit,
    modifier: Modifier = Modifier,
) {
    if (entries.isEmpty()) {
        Box(modifier, contentAlignment = Alignment.Center) {
            Text(
                text = when {
                    !state.hasWorld -> "Waiting for the farm"
                    !state.config.sendVillagers -> "Villager data is disabled"
                    else -> "No villagers in this view"
                },
                style = MaterialTheme.typography.bodySmall,
                color = GIFT_MUTED,
            )
        }
        return
    }

    LazyColumn(
        modifier = modifier,
        contentPadding = PaddingValues(bottom = 5.dp),
    ) {
        items(items = entries, key = { entry -> "gift-villager:${entry.name}" }) { entry ->
            GiftVillagerRow(
                state = state,
                entry = entry,
                selected = entry.name == selected,
                onClick = { onSelect(entry) },
            )
        }
    }
}

@Composable
private fun GiftVillagerRow(
    state: StardewState,
    entry: StardewGiftGuideEntry,
    selected: Boolean,
    onClick: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(46.dp)
            .background(
                when {
                    selected -> GIFT_SELECTED.copy(alpha = 0.34f)
                    entry.readyCount > 0 -> GIFT_READY_WASH
                    else -> GIFT_ROW_WASH
                },
            )
            .clickable(role = Role.Button, onClick = onClick)
            .padding(horizontal = 8.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        GiftPortrait(state, entry, 34.dp)
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = entry.name,
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = if (selected) FontWeight.Bold else FontWeight.SemiBold,
                color = GIFT_INK,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                text = giftVillagerRailDetail(entry),
                style = MaterialTheme.typography.labelSmall,
                color = if (entry.readyCount > 0) GIFT_READY else GIFT_MUTED,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        if (entry.readyCount > 0) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                GiftSprite(state.sprites[StardewUi.CHECK_ON], 17.dp)
                Text(
                    text = entry.readyCount.toString(),
                    style = MaterialTheme.typography.labelSmall,
                    fontWeight = FontWeight.Bold,
                    color = GIFT_READY,
                )
            }
        }
    }
    GiftRule()
}

@Composable
private fun GiftVillagerDetail(
    state: StardewState,
    entry: StardewGiftGuideEntry,
    modifier: Modifier = Modifier,
) {
    Column(modifier = modifier) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .height(56.dp)
                .background(GIFT_DETAIL_WASH)
                .padding(horizontal = 10.dp, vertical = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            GiftPortrait(state, entry, 40.dp)
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = entry.name,
                    style = MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.Bold,
                    color = GIFT_INK,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                GiftHearts(state, entry.villager)
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = giftBirthdayDetail(entry.villager),
                        style = MaterialTheme.typography.labelSmall,
                        color = GIFT_MUTED,
                        modifier = Modifier.weight(1f),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                    if (entry.villager?.talkedToday == true) {
                        Text(
                            text = "Spoken today",
                            style = MaterialTheme.typography.labelSmall,
                            fontWeight = FontWeight.SemiBold,
                            color = GIFT_READY,
                            maxLines = 1,
                        )
                    }
                }
            }
            Column(horizontalAlignment = Alignment.End) {
                Text(
                    text = giftWeeklyDetail(entry.villager),
                    style = MaterialTheme.typography.bodySmall,
                    fontWeight = FontWeight.SemiBold,
                    color = GIFT_INK,
                    maxLines = 1,
                )
                Text(
                    text = "gifts this week",
                    style = MaterialTheme.typography.labelSmall,
                    color = GIFT_MUTED,
                    maxLines = 1,
                )
            }
        }
        GiftRule()

        Row(
            modifier = Modifier
                .fillMaxWidth()
                .height(28.dp)
                .padding(horizontal = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = "LOVED GIFTS",
                style = MaterialTheme.typography.labelMedium,
                fontWeight = FontWeight.Bold,
                color = GIFT_INK,
                modifier = Modifier.weight(1f),
            )
            Text(
                text = if (entry.readyCount > 0) {
                    "${entry.readyCount} in backpack"
                } else {
                    "${entry.gifts.size} known"
                },
                style = MaterialTheme.typography.labelSmall,
                fontWeight = FontWeight.SemiBold,
                color = if (entry.readyCount > 0) GIFT_READY else GIFT_MUTED,
            )
        }
        GiftRule()

        LazyColumn(
            modifier = Modifier.fillMaxWidth().weight(1f),
            contentPadding = PaddingValues(horizontal = 8.dp, vertical = 6.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            items(
                items = entry.gifts.chunked(GIFT_COLUMNS),
                key = { row -> row.joinToString("|") { "${it.id}:${it.name}" } },
            ) { row ->
                Row(
                    modifier = Modifier.fillMaxWidth().height(70.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    row.forEach { gift ->
                        GiftItemCard(
                            state = state,
                            gift = gift,
                            modifier = Modifier.weight(1f).fillMaxHeight(),
                        )
                    }
                    repeat(GIFT_COLUMNS - row.size) { Spacer(Modifier.weight(1f)) }
                }
            }
        }
    }
}

@Composable
private fun GiftItemCard(
    state: StardewState,
    gift: StardewBundleItem,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier
            .clip(ThorTheme.shapes.small)
            .background(if (gift.carried) GIFT_READY_WASH else GIFT_CARD_WASH)
            .border(
                1.dp,
                if (gift.carried) GIFT_READY.copy(alpha = 0.68f) else GIFT_RULE,
                ThorTheme.shapes.small,
            )
            .padding(horizontal = 5.dp, vertical = 4.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        GiftSpriteSlot(state, state.sprites[gift.id], 28.dp)
        Text(
            text = gift.name,
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.SemiBold,
            color = GIFT_INK,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        if (gift.carried) {
            Text(
                text = "IN BACKPACK",
                style = MaterialTheme.typography.labelSmall,
                fontWeight = FontWeight.Bold,
                color = GIFT_READY,
                maxLines = 1,
            )
        }
    }
}

@Composable
private fun GiftGuideEmpty(
    state: StardewState,
    readyOnly: Boolean,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier.padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        GiftSprite(state.sprites[StardewUi.TAB_RELATIONSHIPS], 38.dp)
        Text(
            text = when {
                !state.hasWorld -> "Waiting for the farm"
                !state.config.sendVillagers -> "Villager data is disabled"
                readyOnly -> "No loved gifts in your backpack"
                else -> "No gift tastes received yet"
            },
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Bold,
            color = GIFT_INK,
            modifier = Modifier.padding(top = 9.dp),
        )
        Text(
            text = when {
                !state.hasWorld -> "Live gift and relationship details will appear after loading a save."
                !state.config.sendVillagers ->
                    "Enable villager data in the Stardew mod settings to use the guide."
                readyOnly ->
                    "The guide updates automatically when a loved item enters your inventory."
                else -> "Known villagers and their live loved-gift data will appear here."
            },
            style = MaterialTheme.typography.bodySmall,
            color = GIFT_MUTED,
            modifier = Modifier.padding(top = 4.dp),
        )
    }
}

@Composable
private fun GiftHearts(state: StardewState, villager: StardewVillager?) {
    if (villager == null) {
        Text(
            text = "Friendship data unavailable",
            style = MaterialTheme.typography.labelSmall,
            color = GIFT_MUTED,
        )
        return
    }
    Row(
        horizontalArrangement = Arrangement.spacedBy(1.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        val shownMax = villager.maxHearts.coerceIn(1, 10)
        repeat(shownMax) { index ->
            GiftSprite(
                sprite = state.sprites[
                    if (index < villager.hearts) StardewUi.HEART else StardewUi.HEART_EMPTY
                ],
                size = 13.dp,
            )
        }
        Spacer(Modifier.width(4.dp))
        Text(
            text = "${villager.hearts}/${villager.maxHearts}",
            style = MaterialTheme.typography.labelSmall,
            color = GIFT_MUTED,
            maxLines = 1,
        )
    }
}

@Composable
private fun GiftPortrait(state: StardewState, entry: StardewGiftGuideEntry, size: Dp) {
    val npc = entry.villager?.npc?.ifEmpty { entry.name } ?: entry.name
    val portrait = state.sprites["npc:$npc"]
    val frame = state.frame(StardewUi.SLOT)
    val base = Modifier.size(size)
    val painted = if (frame != null) {
        base.drawBehind { drawNineSlice(frame, this.size) }
    } else {
        base
            .clip(ThorTheme.shapes.small)
            .background(GIFT_CARD_WASH)
            .border(1.dp, GIFT_RULE, ThorTheme.shapes.small)
    }
    Box(painted, contentAlignment = Alignment.Center) {
        GiftSprite(portrait ?: state.sprites[StardewUi.TAB_RELATIONSHIPS], size - 8.dp)
    }
}

@Composable
private fun GiftSpriteSlot(state: StardewState, sprite: UiSprite?, size: Dp) {
    val frame = state.frame(StardewUi.SLOT)
    val base = Modifier.size(size)
    val painted = if (frame != null) {
        base.drawBehind { drawNineSlice(frame, this.size) }
    } else {
        base
            .clip(ThorTheme.shapes.small)
            .background(GIFT_CARD_WASH)
            .border(1.dp, GIFT_RULE, ThorTheme.shapes.small)
    }
    Box(painted, contentAlignment = Alignment.Center) {
        GiftSprite(sprite, size - 10.dp)
    }
}

@Composable
private fun GiftSprite(sprite: UiSprite?, size: Dp) {
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
private fun GiftGuideFooter(state: StardewState, entries: List<StardewGiftGuideEntry>) {
    val available = entries.sumOf { it.readyCount }
    Row(
        modifier = Modifier.fillMaxWidth().height(32.dp).padding(horizontal = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        GiftSprite(state.sprites[StardewUi.SEASON], 25.dp)
        Spacer(Modifier.width(7.dp))
        Text(
            text = giftGuideDate(state),
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.SemiBold,
            color = GIFT_INK,
            modifier = Modifier.weight(1f),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        if (available > 0) {
            GiftSprite(state.sprites[StardewUi.CHECK_ON], 18.dp)
            Spacer(Modifier.width(5.dp))
            Text(
                text = "$available loved matches in backpack",
                style = MaterialTheme.typography.bodySmall,
                fontWeight = FontWeight.SemiBold,
                color = GIFT_READY,
                maxLines = 1,
            )
        }
    }
}

@Composable
private fun GiftRule(strong: Boolean = false) {
    Box(
        Modifier
            .fillMaxWidth()
            .height(if (strong) 2.dp else 1.dp)
            .background(if (strong) GIFT_GOLD else GIFT_RULE),
    )
}

@Composable
private fun GiftVerticalRule() {
    Box(Modifier.fillMaxHeight().width(2.dp).background(GIFT_GOLD))
}

internal data class StardewGiftGuideEntry(
    val name: String,
    val gifts: List<StardewBundleItem>,
    val villager: StardewVillager?,
) {
    val readyCount: Int get() = gifts.count { it.carried }
}

internal fun stardewGiftGuideEntries(
    gifts: List<StardewGiftVillager>,
    villagers: List<StardewVillager>,
): List<StardewGiftGuideEntry> {
    val relations = villagers.associateBy { it.name.lowercase() }
    return gifts
        .filter { it.name.isNotBlank() && it.loves.isNotEmpty() }
        .map { gift ->
            StardewGiftGuideEntry(
                name = gift.name,
                gifts = gift.loves.distinctBy { it.id to it.name },
                villager = relations[gift.name.lowercase()],
            )
        }
        .sortedWith(
            compareByDescending<StardewGiftGuideEntry> { it.readyCount > 0 }
                .thenBy { it.name },
        )
}

internal fun giftWeeklyDetail(villager: StardewVillager?): String = when {
    villager == null -> "—"
    villager.giftsThisWeek >= WEEKLY_GIFT_LIMIT -> "${villager.giftsThisWeek}/$WEEKLY_GIFT_LIMIT"
    else -> "${villager.giftsThisWeek.coerceAtLeast(0)}/$WEEKLY_GIFT_LIMIT"
}

private fun giftVillagerRailDetail(entry: StardewGiftGuideEntry): String = when {
    entry.readyCount > 0 ->
        "${entry.readyCount} loved match${if (entry.readyCount == 1) "" else "es"} in backpack"
    entry.villager != null -> "${entry.villager.hearts}/${entry.villager.maxHearts} hearts · ${entry.gifts.size} loved gifts"
    else -> "${entry.gifts.size} loved gifts known"
}

private fun giftBirthdayDetail(villager: StardewVillager?): String {
    if (villager == null) return "Birthday unknown"
    if (villager.birthdayIn == 0) return "Birthday today"
    val date = listOf(
        villager.birthdaySeason,
        villager.birthdayDay.takeIf { it > 0 }?.toString().orEmpty(),
    ).filter(String::isNotBlank).joinToString(" ")
    return if (date.isEmpty()) "Birthday unknown" else "Birthday · $date"
}

private fun giftGuideDate(state: StardewState): String {
    if (!state.hasWorld || state.day <= 0) return "Waiting for farm"
    val weekday = state.weekday.trim().take(3)
    val day = if (weekday.isEmpty()) state.day.toString() else "$weekday. ${state.day}"
    return "$day, ${state.season}, Year ${state.year.coerceAtLeast(1)}"
}

private const val GIFT_COLUMNS = 3
private const val WEEKLY_GIFT_LIMIT = 2

private val GIFT_INK = Color(0xFF321B0F)
private val GIFT_MUTED = Color(0xFF70451F)
private val GIFT_READY = Color(0xFF19721F)
private val GIFT_GOLD = Color(0xFFC47A12)
private val GIFT_PARCHMENT = Color(0xFFFFD98E)
private val GIFT_FILTER_WASH = Color(0x32FFD789)
private val GIFT_DETAIL_WASH = Color(0x32FFD789)
private val GIFT_ROW_WASH = Color(0x18FFF0B8)
private val GIFT_CARD_WASH = Color(0x2AFFF0B8)
private val GIFT_READY_WASH = Color(0x307DCB65)
private val GIFT_SELECTED = Color(0xFFFFC847)
private val GIFT_RULE = Color(0x55A95F16)
