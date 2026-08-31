package com.wemu.session.secondscreen

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.thor.core.designsystem.theme.ThorTheme
import kotlinx.coroutines.launch

/**
 * A one-screen journal laid out like Stardew Valley's menu: a scrolling quest
 * selector, a parchment detail view, and date/completion controls which never
 * scroll away.
 */
@Composable
fun StardewJournalReferencePage(
    state: StardewState,
    modifier: Modifier = Modifier,
) {
    var showCompleted by rememberSaveable { mutableStateOf(false) }
    var selectedKey by rememberSaveable { mutableStateOf<String?>(null) }
    val quests = remember(state.quests, showCompleted) {
        state.quests.filter { showCompleted || !it.complete }
    }
    val selected = quests.firstOrNull { socialQuestKey(it) == selectedKey }
        ?: quests.firstOrNull()

    LaunchedEffect(quests, selectedKey) {
        if (selectedKey == null || quests.none { socialQuestKey(it) == selectedKey }) {
            selectedKey = quests.firstOrNull()?.let(::socialQuestKey)
        }
    }

    Column(modifier = modifier.fillMaxSize()) {
        BoxWithConstraints(modifier = Modifier.fillMaxWidth().weight(1f)) {
            val selectorWidth = minOf(maxWidth * 0.31f, 300.dp).coerceAtLeast(150.dp)
            Row(modifier = Modifier.fillMaxSize()) {
                JournalQuestSelector(
                    state = state,
                    quests = quests,
                    selectedKey = selected?.let(::socialQuestKey),
                    onSelect = { selectedKey = socialQuestKey(it) },
                    modifier = Modifier.width(selectorWidth).fillMaxHeight(),
                )
                SocialVerticalRule()
                JournalQuestDetail(
                    state = state,
                    quest = selected,
                    modifier = Modifier.weight(1f).fillMaxHeight(),
                )
            }
        }
        SocialHorizontalRule()
        ReferenceSocialFooter(
            state = state,
            modifier = Modifier.fillMaxWidth().height(42.dp),
        ) {
            JournalCompletedToggle(
                state = state,
                checked = showCompleted,
                completedCount = state.quests.count { it.complete },
                onCheckedChange = { showCompleted = it },
            )
        }
    }
}

@Composable
private fun JournalQuestSelector(
    state: StardewState,
    quests: List<StardewQuest>,
    selectedKey: String?,
    onSelect: (StardewQuest) -> Unit,
    modifier: Modifier = Modifier,
) {
    val listState = rememberLazyListState()

    Column(modifier = modifier.padding(end = 3.dp)) {
        Row(
            modifier = Modifier.fillMaxWidth().height(40.dp).padding(horizontal = 7.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(5.dp),
        ) {
            SocialSprite(state.sprites[StardewUi.TAB_JOURNAL], 22.dp)
            Text(
                text = "Journal",
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.Bold,
                color = SOCIAL_INK,
                modifier = Modifier.weight(1f),
                maxLines = 1,
            )
            Text(
                text = quests.size.toString(),
                style = MaterialTheme.typography.labelLarge,
                fontWeight = FontWeight.Bold,
                color = SOCIAL_MUTED,
            )
        }
        SocialHorizontalRule()

        if (quests.isEmpty()) {
            Column(
                modifier = Modifier.fillMaxSize().padding(18.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
            ) {
                SocialSprite(state.sprites[StardewUi.CHECK_ON], 28.dp)
                Text(
                    text = "Nothing in the journal.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = SOCIAL_MUTED,
                    textAlign = TextAlign.Center,
                    modifier = Modifier.padding(top = 8.dp),
                )
            }
            return
        }

        Row(modifier = Modifier.fillMaxSize()) {
            LazyColumn(
                state = listState,
                modifier = Modifier.weight(1f).fillMaxHeight().padding(top = 3.dp),
                verticalArrangement = Arrangement.spacedBy(3.dp),
            ) {
                items(quests, key = ::socialQuestKey) { quest ->
                    JournalQuestChoice(
                        state = state,
                        quest = quest,
                        selected = socialQuestKey(quest) == selectedKey,
                        onClick = { onSelect(quest) },
                    )
                }
            }
            ReferenceScrollRail(
                state = state,
                listState = listState,
                totalItems = quests.size,
                modifier = Modifier.width(18.dp).fillMaxHeight().padding(start = 2.dp),
            )
        }
    }
}

@Composable
private fun JournalQuestChoice(
    state: StardewState,
    quest: StardewQuest,
    selected: Boolean,
    onClick: () -> Unit,
) {
    val frame = state.frame(StardewUi.BUTTON)
    val modifier = Modifier
        .fillMaxWidth()
        .height(48.dp)
        .then(
            if (frame != null) {
                Modifier.drawBehind { drawNineSlice(frame, size) }
            } else {
                Modifier
                    .border(1.dp, SOCIAL_BORDER, ThorTheme.shapes.small)
                    .background(if (selected) SOCIAL_SELECTED else SOCIAL_PARCHMENT_DARK)
            },
        )
        .clickable(role = Role.Tab, onClick = onClick)

    Box(modifier = modifier) {
        if (selected) {
            Box(
                Modifier
                    .fillMaxSize()
                    .padding(4.dp)
                    .clip(ThorTheme.shapes.small)
                    .background(SOCIAL_SELECTED.copy(alpha = 0.54f)),
            )
        }
        Row(
            modifier = Modifier.fillMaxSize().padding(horizontal = 6.dp, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(5.dp),
        ) {
            SocialSprite(
                state.sprites[
                    if (quest.complete) StardewUi.CHECK_ON else StardewUi.TAB_JOURNAL
                ],
                20.dp,
            )
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = quest.title.ifBlank { "Untitled quest" },
                    style = MaterialTheme.typography.bodySmall,
                    fontWeight = FontWeight.Bold,
                    color = SOCIAL_INK,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                Text(
                    text = journalQuestStatus(quest),
                    style = MaterialTheme.typography.labelSmall,
                    color = if (quest.daysLeft in 0..1 && !quest.complete) {
                        SOCIAL_URGENT
                    } else {
                        SOCIAL_MUTED
                    },
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
    }
}

@Composable
private fun JournalQuestDetail(
    state: StardewState,
    quest: StardewQuest?,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier
            .background(SOCIAL_PARCHMENT)
            .padding(horizontal = 18.dp, vertical = 10.dp),
    ) {
        if (quest == null) {
            Column(
                modifier = Modifier.fillMaxSize(),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
            ) {
                SocialSprite(state.sprites[StardewUi.TAB_JOURNAL], 42.dp)
                Text(
                    text = if (state.quests.isEmpty()) {
                        "Your journal is empty."
                    } else {
                        "No quests match this view."
                    },
                    style = MaterialTheme.typography.titleMedium,
                    color = SOCIAL_MUTED,
                    modifier = Modifier.padding(top = 12.dp),
                )
            }
            return
        }

        SocialScrollTitle(
            state = state,
            title = quest.title.ifBlank { "Quest" },
            modifier = Modifier.align(Alignment.CenterHorizontally),
        )

        Column(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .padding(top = 7.dp),
        ) {
            if (quest.detail.isNotBlank()) {
                Text(
                    text = quest.detail,
                    style = MaterialTheme.typography.bodyMedium,
                    color = SOCIAL_INK,
                    modifier = Modifier.fillMaxWidth(),
                    maxLines = 3,
                    overflow = TextOverflow.Ellipsis,
                )
            }

            if (quest.objective.isNotBlank()) {
                SocialHorizontalRule(Modifier.padding(vertical = 8.dp))
                Text(
                    text = "Objectives:",
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.Bold,
                    color = SOCIAL_INK,
                    modifier = Modifier.padding(bottom = 4.dp),
                )
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.Top,
                    horizontalArrangement = Arrangement.spacedBy(7.dp),
                ) {
                    SocialSprite(
                        state.sprites[
                            if (quest.complete) StardewUi.CHECK_ON else StardewUi.CHECK_OFF
                        ],
                        18.dp,
                    )
                    Text(
                        text = quest.objective.lines()
                            .map(String::trim)
                            .filter(String::isNotEmpty)
                            .joinToString("\n"),
                        style = MaterialTheme.typography.bodyMedium,
                        color = SOCIAL_INK,
                        modifier = Modifier.weight(1f),
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }

            Spacer(modifier = Modifier.weight(1f))
            if (quest.reward > 0) {
                SocialHorizontalRule(Modifier.padding(vertical = 6.dp))
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(7.dp),
                ) {
                    Text(
                        text = "Rewards:",
                        style = MaterialTheme.typography.labelLarge,
                        fontWeight = FontWeight.Bold,
                        color = SOCIAL_INK,
                    )
                    SocialSprite(state.sprites[StardewUi.COIN], 22.dp)
                    Text(
                        text = "%,dg".format(quest.reward),
                        style = MaterialTheme.typography.bodyMedium,
                        fontWeight = FontWeight.Bold,
                        color = SOCIAL_INK,
                    )
                }
            }
        }

        Row(
            modifier = Modifier.fillMaxWidth().height(34.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(9.dp),
        ) {
            Text(
                text = journalQuestStatus(quest),
                style = MaterialTheme.typography.labelLarge,
                fontWeight = FontWeight.SemiBold,
                color = if (quest.daysLeft in 0..1 && !quest.complete) {
                    SOCIAL_URGENT
                } else {
                    SOCIAL_MUTED
                },
                modifier = Modifier.weight(1f),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            if (!quest.complete && quest.cancellable &&
                state.config.allowQuestCancel && quest.index >= 0
            ) {
                SocialGameButton(
                    state = state,
                    label = "Cancel Quest",
                    onClick = { SecondScreen.send(Commands.cancelQuest(quest.index)) },
                )
            }
        }
    }
}

@Composable
private fun SocialScrollTitle(
    state: StardewState,
    title: String,
    modifier: Modifier = Modifier,
) {
    val scroll = state.frame(StardewUi.SCROLL)
    Box(
        modifier = modifier
            .fillMaxWidth(0.68f)
            .height(40.dp)
            .then(
                if (scroll != null) {
                    Modifier.drawBehind { drawNineSlice(scroll, size) }
                } else {
                    Modifier
                        .border(1.dp, SOCIAL_BORDER, ThorTheme.shapes.small)
                        .background(SOCIAL_PARCHMENT_DARK)
                },
            ),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = title,
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Bold,
            color = SOCIAL_INK,
            textAlign = TextAlign.Center,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.fillMaxWidth().padding(horizontal = 18.dp),
        )
    }
}

@Composable
private fun JournalCompletedToggle(
    state: StardewState,
    checked: Boolean,
    completedCount: Int,
    onCheckedChange: (Boolean) -> Unit,
) {
    Row(
        modifier = Modifier
            .clip(ThorTheme.shapes.small)
            .toggleable(
                value = checked,
                role = Role.Checkbox,
                onValueChange = onCheckedChange,
            )
            .padding(horizontal = 6.dp, vertical = 3.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(5.dp),
    ) {
        Text(
            text = if (completedCount > 0) {
                "Show Completed ($completedCount)"
            } else {
                "Show Completed"
            },
            style = MaterialTheme.typography.labelMedium,
            fontWeight = FontWeight.SemiBold,
            color = SOCIAL_INK,
        )
        SocialSprite(
            state.sprites[if (checked) StardewUi.CHECK_ON else StardewUi.CHECK_OFF],
            19.dp,
        )
    }
}

/**
 * A reference-style relationships ledger. The header and footer remain fixed;
 * every villager row is reachable through the visible, game-art scroll rail.
 */
@Composable
fun StardewRelationshipsReferencePage(
    state: StardewState,
    modifier: Modifier = Modifier,
) {
    val villagers = remember(state.villagers) {
        state.villagers.sortedWith(
            compareByDescending<StardewVillager> { it.hearts }
                .thenBy { it.name.lowercase() },
        )
    }
    val listState = rememberLazyListState()

    Column(modifier = modifier.fillMaxSize().background(SOCIAL_PARCHMENT)) {
        RelationshipHeader(state)
        SocialHorizontalRule()

        if (villagers.isEmpty()) {
            Column(
                modifier = Modifier.fillMaxWidth().weight(1f),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
            ) {
                SocialSprite(state.sprites[StardewUi.TAB_RELATIONSHIPS], 42.dp)
                Text(
                    text = "No relationships received yet.",
                    style = MaterialTheme.typography.titleMedium,
                    color = SOCIAL_MUTED,
                    modifier = Modifier.padding(top = 12.dp),
                )
            }
        } else {
            Row(modifier = Modifier.fillMaxWidth().weight(1f)) {
                LazyColumn(
                    state = listState,
                    modifier = Modifier.weight(1f).fillMaxHeight(),
                ) {
                    items(villagers, key = { it.npc.ifBlank { it.name } }) { villager ->
                        RelationshipRow(state, villager)
                        SocialHorizontalRule()
                    }
                }
                ReferenceScrollRail(
                    state = state,
                    listState = listState,
                    totalItems = villagers.size,
                    modifier = Modifier.width(20.dp).fillMaxHeight().padding(2.dp),
                )
            }
        }

        ReferenceSocialFooter(
            state = state,
            modifier = Modifier.fillMaxWidth().height(38.dp),
        ) {
            val talked = villagers.count { it.talkedToday }
            val giftsRemaining = villagers.sumOf { (2 - it.giftsThisWeek).coerceAtLeast(0) }
            Text(
                text = "Talked $talked/${villagers.size}  •  Gifts open $giftsRemaining",
                style = MaterialTheme.typography.labelSmall,
                color = SOCIAL_MUTED,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

@Composable
private fun RelationshipHeader(state: StardewState) {
    Row(
        modifier = Modifier.fillMaxWidth().height(42.dp).padding(horizontal = 9.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Spacer(modifier = Modifier.weight(0.31f))
        SocialVerticalRule()
        Row(
            modifier = Modifier.weight(0.42f),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.Center,
        ) {
            SocialSprite(state.sprites[StardewUi.HEART], 20.dp)
            Text(
                text = "Friendship (0–10 hearts)",
                style = MaterialTheme.typography.bodySmall,
                fontWeight = FontWeight.SemiBold,
                color = SOCIAL_INK,
                modifier = Modifier.padding(start = 5.dp),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        SocialVerticalRule()
        Row(
            modifier = Modifier.weight(0.27f),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.Center,
        ) {
            SocialSprite(state.sprites[StardewUi.SLOT_SELECTED], 20.dp)
            Column(modifier = Modifier.padding(start = 5.dp)) {
                Text(
                    text = "Gifts",
                    style = MaterialTheme.typography.bodySmall,
                    fontWeight = FontWeight.SemiBold,
                    color = SOCIAL_INK,
                )
                Text(
                    text = "This Week",
                    style = MaterialTheme.typography.labelSmall,
                    color = SOCIAL_MUTED,
                )
            }
        }
    }
}

@Composable
private fun RelationshipRow(state: StardewState, villager: StardewVillager) {
    Row(
        modifier = Modifier.fillMaxWidth().height(64.dp).padding(horizontal = 9.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(
            modifier = Modifier.weight(0.31f).fillMaxHeight().padding(vertical = 5.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(7.dp),
        ) {
            SocialPortrait(state, villager, 49.dp)
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = villager.name,
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.Bold,
                    color = SOCIAL_INK,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                if (villager.maxHearts > 10) {
                    Text(
                        text = "14-heart friendship",
                        style = MaterialTheme.typography.labelSmall,
                        color = SOCIAL_MUTED,
                        maxLines = 1,
                    )
                }
                Text(
                    text = socialBirthday(villager),
                    style = MaterialTheme.typography.labelSmall,
                    color = if (villager.birthdayIn == 0) SOCIAL_URGENT else SOCIAL_MUTED,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
        SocialVerticalRule()
        Box(
            modifier = Modifier
                .weight(0.42f)
                .fillMaxHeight(),
            contentAlignment = Alignment.Center,
        ) {
            SocialHearts(state, villager.hearts, villager.maxHearts)
        }
        SocialVerticalRule()
        Column(
            modifier = Modifier.weight(0.27f).fillMaxHeight(),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
        ) {
            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                repeat(2) { index ->
                    RelationshipStatusSlot(
                        state = state,
                        on = index < villager.giftsThisWeek,
                        label = "Gift ${index + 1}",
                    )
                }
                RelationshipStatusSlot(
                    state = state,
                    on = villager.talkedToday,
                    label = "Talk",
                )
            }
            Text(
                text = buildString {
                    append("${villager.giftsThisWeek.coerceIn(0, 2)}/2 gifts")
                    if (villager.talkedToday) append("  •  Talked")
                },
                style = MaterialTheme.typography.labelSmall,
                color = if (villager.talkedToday) SOCIAL_READY else SOCIAL_MUTED,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.padding(top = 2.dp),
            )
        }
    }
}

@Composable
private fun SocialPortrait(
    state: StardewState,
    villager: StardewVillager,
    size: Dp,
) {
    val portrait = state.sprites["npc:${villager.npc}"]
        ?: state.sprites["npc:${villager.name}"]
    Box(
        modifier = Modifier
            .size(size)
            .then(
                if (portrait == null) {
                    Modifier
                        .border(1.dp, SOCIAL_BORDER, ThorTheme.shapes.small)
                        .background(SOCIAL_PARCHMENT_DARK)
                } else {
                    Modifier
                },
            ),
        contentAlignment = Alignment.Center,
    ) {
        if (portrait != null) {
            Image(
                bitmap = portrait.image,
                contentDescription = villager.name,
                modifier = Modifier.fillMaxSize(),
                contentScale = ContentScale.Fit,
                filterQuality = FilterQuality.None,
            )
        } else {
            Text(
                text = villager.name.take(1).uppercase(),
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold,
                color = SOCIAL_MUTED,
            )
        }
    }
}

@Composable
private fun SocialHearts(state: StardewState, filled: Int, maximum: Int) {
    val maxHearts = maximum.coerceIn(1, 14)
    val full = state.sprites[StardewUi.HEART]
    val empty = state.sprites[StardewUi.HEART_EMPTY]

    if (full == null || empty == null) {
        Text(
            text = "${filled.coerceIn(0, maxHearts)}/$maxHearts hearts",
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.SemiBold,
            color = SOCIAL_INK,
        )
        return
    }

    BoxWithConstraints(modifier = Modifier.fillMaxWidth().padding(horizontal = 10.dp)) {
        val heartSize = (maxWidth / maxHearts).coerceAtMost(17.dp).coerceAtLeast(8.dp)
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            repeat(maxHearts) { index ->
                Image(
                    bitmap = if (index < filled) full.image else empty.image,
                    contentDescription = null,
                    modifier = Modifier.size(heartSize),
                    contentScale = ContentScale.Fit,
                    filterQuality = FilterQuality.None,
                )
            }
        }
    }
}

@Composable
private fun RelationshipStatusSlot(
    state: StardewState,
    on: Boolean,
    label: String,
) {
    val frame = state.frame(StardewUi.SLOT)
    Box(
        modifier = Modifier
            .size(30.dp)
            .then(
                if (frame != null) {
                    Modifier.drawBehind { drawNineSlice(frame, size) }
                } else {
                    Modifier
                        .border(1.dp, SOCIAL_BORDER, ThorTheme.shapes.small)
                        .background(SOCIAL_PARCHMENT_DARK)
                },
            ),
        contentAlignment = Alignment.Center,
    ) {
        SocialSprite(
            state.sprites[if (on) StardewUi.CHECK_ON else StardewUi.CHECK_OFF],
            14.dp,
            description = "$label: ${if (on) "done" else "open"}",
        )
    }
}

@Composable
private fun ReferenceScrollRail(
    state: StardewState,
    listState: LazyListState,
    totalItems: Int,
    modifier: Modifier = Modifier,
) {
    val scope = rememberCoroutineScope()
    val metrics by remember(listState, totalItems) {
        derivedStateOf {
            val visibleItems = listState.layoutInfo.visibleItemsInfo.size.coerceAtLeast(1)
            val maxFirst = (totalItems - visibleItems).coerceAtLeast(0)
            ReferenceScrollMetrics(
                visibleItems = visibleItems,
                progress = if (maxFirst == 0) {
                    0f
                } else {
                    listState.firstVisibleItemIndex.toFloat()
                        .div(maxFirst)
                        .coerceIn(0f, 1f)
                },
            )
        }
    }
    val visibleItems = metrics.visibleItems
    val progress = metrics.progress

    Column(modifier = modifier, horizontalAlignment = Alignment.CenterHorizontally) {
        ScrollArrow(
            state = state,
            up = true,
            enabled = listState.canScrollBackward,
        ) {
            val target = (listState.firstVisibleItemIndex - visibleItems).coerceAtLeast(0)
            scope.launch { listState.animateScrollToItem(target) }
        }
        BoxWithConstraints(
            modifier = Modifier
                .width(10.dp)
                .weight(1f)
                .border(1.dp, SOCIAL_BORDER)
                .background(SOCIAL_TRACK),
        ) {
            val fraction = if (totalItems <= 0) {
                1f
            } else {
                visibleItems.toFloat().div(totalItems).coerceIn(0.12f, 1f)
            }
            val thumbHeight = (maxHeight * fraction).coerceAtLeast(18.dp).coerceAtMost(maxHeight)
            val travel = (maxHeight - thumbHeight).coerceAtLeast(0.dp)
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .offset(y = travel * progress)
                    .height(thumbHeight)
                    .padding(2.dp)
                    .background(SOCIAL_SCROLL_THUMB),
            )
        }
        ScrollArrow(
            state = state,
            up = false,
            enabled = listState.canScrollForward,
        ) {
            val target = (listState.firstVisibleItemIndex + visibleItems)
                .coerceAtMost((totalItems - 1).coerceAtLeast(0))
            scope.launch { listState.animateScrollToItem(target) }
        }
    }
}

@Composable
private fun ScrollArrow(
    state: StardewState,
    up: Boolean,
    enabled: Boolean,
    onClick: () -> Unit,
) {
    val arrow = state.sprites[StardewUi.ARROW_RIGHT]
    Box(
        modifier = Modifier
            .size(18.dp)
            .clickable(enabled = enabled, role = Role.Button, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        if (arrow != null) {
            Image(
                bitmap = arrow.image,
                contentDescription = if (up) "Scroll up" else "Scroll down",
                modifier = Modifier
                    .size(14.dp)
                    .graphicsLayer {
                        rotationZ = if (up) -90f else 90f
                        alpha = if (enabled) 1f else 0.38f
                    },
                contentScale = ContentScale.Fit,
                filterQuality = FilterQuality.None,
            )
        }
    }
}

@Composable
private fun ReferenceSocialFooter(
    state: StardewState,
    modifier: Modifier = Modifier,
    trailing: @Composable RowScope.() -> Unit,
) {
    Row(
        modifier = modifier.background(SOCIAL_PARCHMENT_DARK).padding(horizontal = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        SocialSprite(state.sprites[StardewUi.SEASON], 20.dp)
        Text(
            text = socialDate(state),
            style = MaterialTheme.typography.labelMedium,
            fontWeight = FontWeight.SemiBold,
            color = SOCIAL_INK,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        SocialSprite(state.sprites[StardewUi.TIME], 18.dp)
        Text(
            text = state.clock.replace("am", " am").replace("pm", " pm"),
            style = MaterialTheme.typography.labelMedium,
            fontWeight = FontWeight.SemiBold,
            color = SOCIAL_INK,
            maxLines = 1,
        )
        Spacer(Modifier.weight(1f))
        trailing()
    }
}

@Composable
private fun SocialGameButton(
    state: StardewState,
    label: String,
    onClick: () -> Unit,
) {
    val frame = state.frame(StardewUi.BUTTON)
    Box(
        modifier = Modifier
            .height(30.dp)
            .width(94.dp)
            .then(
                if (frame != null) {
                    Modifier.drawBehind { drawNineSlice(frame, size) }
                } else {
                    Modifier
                        .border(1.dp, SOCIAL_BORDER, ThorTheme.shapes.small)
                        .background(SOCIAL_PARCHMENT_DARK)
                },
            )
            .clickable(role = Role.Button, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelMedium,
            fontWeight = FontWeight.Bold,
            color = SOCIAL_URGENT,
            maxLines = 1,
        )
    }
}

@Composable
private fun SocialSprite(
    sprite: UiSprite?,
    size: Dp,
    description: String? = null,
) {
    if (sprite == null) {
        Spacer(Modifier.size(size))
        return
    }
    Image(
        bitmap = sprite.image,
        contentDescription = description,
        modifier = Modifier.size(size),
        contentScale = ContentScale.Fit,
        filterQuality = FilterQuality.None,
    )
}

@Composable
private fun SocialHorizontalRule(modifier: Modifier = Modifier) {
    Box(modifier.fillMaxWidth().height(1.dp).background(SOCIAL_LINE))
}

@Composable
private fun SocialVerticalRule() {
    Box(Modifier.fillMaxHeight().width(1.dp).background(SOCIAL_LINE))
}

private fun socialQuestKey(quest: StardewQuest): String = "${quest.index}:${quest.title}"

private data class ReferenceScrollMetrics(
    val visibleItems: Int,
    val progress: Float,
)

private fun journalQuestStatus(quest: StardewQuest): String = when {
    quest.complete -> "Completed"
    quest.daysLeft > 1 -> "${quest.daysLeft} days left"
    quest.daysLeft == 1 -> "Last day"
    quest.daysLeft == 0 || quest.daily -> "Due today"
    else -> "Active"
}

private fun socialBirthday(villager: StardewVillager): String {
    val birthday = listOf(
        villager.birthdaySeason,
        villager.birthdayDay.takeIf { it > 0 }?.toString().orEmpty(),
    ).filter(String::isNotBlank).joinToString(" ")
    return when {
        villager.birthdayIn == 0 -> "Birthday today"
        birthday.isNotEmpty() -> "Birthday: $birthday"
        else -> "Birthday unknown"
    }
}

private fun socialDate(state: StardewState): String {
    if (state.season.isBlank()) return "Waiting for farm"
    val weekday = state.weekday
        .takeIf(String::isNotBlank)
        ?.take(3)
        ?.let { "$it." }
        .orEmpty()
    return listOf(weekday, "${state.day}, ${state.season}, Year ${state.year}")
        .filter(String::isNotBlank)
        .joinToString(" ")
}

private val SOCIAL_INK = Color(0xFF38200E)
private val SOCIAL_MUTED = Color(0xFF6E4A27)
private val SOCIAL_BORDER = Color(0xFF9B5517)
private val SOCIAL_LINE = Color(0xFFC9893E).copy(alpha = 0.62f)
private val SOCIAL_PARCHMENT = Color(0xFFFFD98B)
private val SOCIAL_PARCHMENT_DARK = Color(0xFFF2B65F)
private val SOCIAL_SELECTED = Color(0xFFFFC847)
private val SOCIAL_TRACK = Color(0xFFF0B257)
private val SOCIAL_SCROLL_THUMB = Color(0xFFB86A18)
private val SOCIAL_READY = Color(0xFF177A18)
private val SOCIAL_URGENT = Color(0xFF9D2C1D)
