package com.wemu.session.secondscreen

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.gestures.snapping.rememberSnapFlingBehavior
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.clickable
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.platform.LocalContext
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.ScrollState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.foundation.layout.heightIn
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.isSpecified
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.focusable
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.size
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.thor.core.designsystem.modifier.SurfaceLevel
import com.thor.core.designsystem.modifier.thorSurface
import com.thor.core.designsystem.theme.ThorTheme
import kotlinx.coroutines.flow.collect
import kotlin.math.roundToInt

@Composable
fun StardewScreen(modifier: Modifier = Modifier) {
    StardewPanelTheme {
        StardewScreenContent(modifier)
    }
}

@Composable
private fun StardewScreenContent(modifier: Modifier = Modifier) {
    val state by SecondScreen.stardew.collectAsStateWithLifecycle()
    val prefs by StardewPrefs.state.collectAsStateWithLifecycle()
    val dimens = ThorTheme.dimens
    var chosen by rememberSaveable { mutableStateOf(StardewPage.TODAY.name) }

    val context = LocalContext.current
    LaunchedEffect(Unit) {
        StardewPrefs.load(context)
        StardewHistory.load(context)
    }
    LaunchedEffect(state.saveName) {
        StardewPrefs.useSave(state.saveName)
        StardewHistory.useSave(state.saveName)
    }
    LaunchedEffect(state.year, state.season, state.day) { StardewHistory.observe(state) }
    LaunchedEffect(state.gold, state.shipping.total) { StardewHistory.record(state) }

    StardewAlerts()

    val pages = buildList {
        add(StardewPage.TODAY)
        add(StardewPage.CRAFTING)
        add(StardewPage.PROGRESS)
        add(StardewPage.MAP)
        add(StardewPage.JOURNAL)
        add(StardewPage.RELATIONSHIPS)
        add(StardewPage.GIFT_GUIDE)
        add(StardewPage.FIELDS)
        add(StardewPage.MACHINES)
        add(StardewPage.BUNDLES)
        add(StardewPage.CALENDAR)
        add(StardewPage.SETTINGS)

        removeAll { it != StardewPage.SETTINGS && prefs.hides(it.name) }
    }

    val index = pages.indexOfFirst { it.name == chosen }.let { at ->
        if (at >= 0) at else 0
    }

    val currentState by rememberUpdatedState(state)
    val currentPages by rememberUpdatedState(pages)
    val currentIndex by rememberUpdatedState(index)
    LaunchedEffect(Unit) {
        SecondScreen.stardewPanelControls.collect { control ->
            val livePages = currentPages
            if (livePages.isEmpty()) return@collect
            when (control) {
                StardewPanelControl.PREVIOUS_PAGE ->
                    chosen = livePages[
                        (currentIndex - 1 + livePages.size) % livePages.size
                    ].name
                StardewPanelControl.NEXT_PAGE ->
                    chosen = livePages[(currentIndex + 1) % livePages.size].name
                StardewPanelControl.PREVIOUS_SLOT -> {
                    val slot = (currentState.selectedSlot - 1 + StardewState.HOTBAR) %
                        StardewState.HOTBAR
                    SecondScreen.send(Commands.selectSlot(slot))
                }
                StardewPanelControl.NEXT_SLOT -> {
                    val slot = (currentState.selectedSlot + 1) % StardewState.HOTBAR
                    SecondScreen.send(Commands.selectSlot(slot))
                }
                StardewPanelControl.PREVIOUS_TOOLBAR ->
                    SecondScreen.send(Commands.shiftToolbar(right = false))
                StardewPanelControl.NEXT_TOOLBAR ->
                    SecondScreen.send(Commands.shiftToolbar(right = true))
                StardewPanelControl.SORT -> {
                    if (livePages[currentIndex] == StardewPage.TODAY &&
                        currentState.config.allowInventoryEdits
                    ) SecondScreen.send(Commands.sortBag())
                }
                StardewPanelControl.USE -> {
                    val item = currentState.itemAt(currentState.selectedSlot)
                    if (livePages[currentIndex] == StardewPage.TODAY && !item.empty &&
                        currentState.config.allowInventoryEdits
                    ) SecondScreen.send(Commands.eatSlot(item.slot))
                }
            }
        }
    }

    val colors = ThorTheme.colors
    val ground = state.backdrop

    val focus = remember { FocusRequester() }
    LaunchedEffect(Unit) { runCatching { focus.requestFocus() } }

    Column(
        modifier = modifier
            .fillMaxSize()
            .focusRequester(focus)
            .focusable()
            .onKeyEvent { event ->
                if (event.type != KeyEventType.KeyDown) return@onKeyEvent false
                when (event.key) {
                    Key.DirectionUp, Key.ButtonY -> {
                        val next = (state.selectedSlot - 1 + StardewState.HOTBAR) %
                            StardewState.HOTBAR
                        SecondScreen.send(Commands.selectSlot(next))
                        true
                    }

                    Key.DirectionDown, Key.ButtonA -> {
                        val next = (state.selectedSlot + 1) % StardewState.HOTBAR
                        SecondScreen.send(Commands.selectSlot(next))
                        true
                    }

                    Key.ButtonL1 -> {
                        SecondScreen.send(Commands.shiftToolbar(right = false))
                        true
                    }

                    Key.ButtonR1 -> {
                        SecondScreen.send(Commands.shiftToolbar(right = true))
                        true
                    }

                    Key.DirectionLeft, Key.PageUp -> {
                        chosen = pages[(index - 1 + pages.size) % pages.size].name
                        true
                    }

                    Key.DirectionRight, Key.PageDown -> {
                        chosen = pages[(index + 1) % pages.size].name
                        true
                    }

                    Key.ButtonX -> {
                        if (pages[index] != StardewPage.TODAY ||
                            !state.config.allowInventoryEdits
                        ) {
                            false
                        } else {
                            SecondScreen.send(Commands.sortBag())
                            true
                        }
                    }

                    Key.ButtonB -> {
                        val selected = state.itemAt(state.selectedSlot)
                        if (pages[index] != StardewPage.TODAY || selected.empty ||
                            !state.config.allowInventoryEdits
                        ) {
                            false
                        } else {
                            SecondScreen.send(Commands.eatSlot(selected.slot))
                            true
                        }
                    }

                    else -> false
                }
            }
            .drawBehind {

                if (prefs.showGround && prefs.groundChoice == StardewPrefs.COLOUR) {
                    drawRect(
                        Color(prefs.groundRed, prefs.groundGreen, prefs.groundBlue),
                    )
                    return@drawBehind
                }

                if (ground == null) return@drawBehind
                drawTiled(ground, prefs.groundZoom)

                drawRect(Color.Black.copy(alpha = prefs.groundShade / 100f))
            },
    ) {

        val pageArea = Modifier
            .fillMaxWidth()
            .weight(1f)
            .padding(horizontal = dimens.spacingSmall, vertical = dimens.spacingSmall)

        Box(modifier = pageArea) {
            Card(fill = true) {
                CompositionLocalProvider(LocalJoinedPanel provides true) {
                    PageBar(
                        state = state,
                        pages = pages,
                        page = index,
                        onPage = { chosen = it.name },
                    )
                    Rule()

                    Box(modifier = Modifier.fillMaxWidth().weight(1f)) {
                        when (pages[index]) {
                            StardewPage.TODAY ->
                                StardewBackpackReferencePage(
                                    state = state,
                                    modifier = Modifier.fillMaxSize(),
                                )

                            StardewPage.CRAFTING ->
                                StardewCraftingReferencePage(
                                    state = state,
                                    modifier = Modifier.fillMaxSize(),
                                    onCraftRecipe = if (
                                        state.config.allowInventoryEdits &&
                                        state.config.sendCrafting
                                    ) {
                                        { recipe ->
                                            if (recipe.ready && recipe.key.isNotEmpty()) {
                                                SecondScreen.send(Commands.craftRecipe(recipe.key))
                                            }
                                        }
                                    } else {
                                        null
                                    },
                                )

                            StardewPage.PROGRESS ->
                                StardewProgressReferencePage(
                                    state = state,
                                    modifier = Modifier.fillMaxSize(),
                                )

                            StardewPage.MAP ->
                                MapCard(state, fill = true, modifier = Modifier.fillMaxSize())

                            StardewPage.JOURNAL ->
                                StardewJournalReferencePage(
                                    state = state,
                                    modifier = Modifier.fillMaxSize(),
                                )

                            StardewPage.RELATIONSHIPS ->
                                StardewRelationshipsReferencePage(
                                    state = state,
                                    modifier = Modifier.fillMaxSize(),
                                )

                            StardewPage.GIFT_GUIDE ->
                                StardewGiftGuideReferencePage(
                                    state = state,
                                    modifier = Modifier.fillMaxSize(),
                                )

                            StardewPage.FIELDS ->
                                StardewFieldsReferencePage(
                                    state = state,
                                    modifier = Modifier.fillMaxSize(),
                                )

                            StardewPage.MACHINES ->
                                StardewMachinesReferencePage(
                                    state = state,
                                    modifier = Modifier.fillMaxSize(),
                                )

                            StardewPage.BUNDLES ->
                                StardewBundlesReferencePage(
                                    state = state,
                                    modifier = Modifier.fillMaxSize(),
                                )

                            StardewPage.CALENDAR ->
                                CalendarCard(state, fill = true)

                            StardewPage.SETTINGS -> Column(
                                modifier = Modifier
                                    .fillMaxSize()
                                    .verticalScroll(remember(index) { ScrollState(0) }),
                            ) {
                                StardewSettingsPage(state)
                            }

                        }
                    }
                }
            }
        }
    }
}

internal enum class StardewPage(
    val title: String,
    val scrolls: Boolean = false,

    val fits: Boolean = false,
) {
    TODAY("Backpack"),
    CRAFTING("Crafting"),
    PROGRESS("Progress"),
    MAP("Map"),
    JOURNAL("Journal"),
    RELATIONSHIPS("Relationships"),
    GIFT_GUIDE("Gift Guide"),
    FIELDS("Fields"),
    MACHINES("Machines"),
    BUNDLES("Bundles"),
    CALENDAR("Calendar"),
    SETTINGS("Settings", scrolls = true),
}

private const val GROUND_DIM = 0.30f

@Composable
private fun PageBar(
    state: StardewState,
    pages: List<StardewPage>,
    page: Int,
    onPage: (StardewPage) -> Unit,
) {
    val dimens = ThorTheme.dimens
    val listState = rememberLazyListState()
    LaunchedEffect(page) {
        if (page in pages.indices) {
            val layout = listState.layoutInfo
            val selected = layout.visibleItemsInfo.firstOrNull { it.index == page }
            val fullyVisible = selected != null &&
                selected.offset >= layout.viewportStartOffset &&
                selected.offset + selected.size <= layout.viewportEndOffset
            if (!fullyVisible) {
                listState.animateScrollToItem(pageWindowStart(page, pages.size))
            }
        }
    }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(52.dp)
            .padding(horizontal = dimens.spacingTiny, vertical = dimens.spacingTiny),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        PageRailArrow(
            state = state,
            sprite = state.sprites[StardewUi.ARROW_LEFT],
            description = "Previous page",
            enabled = pages.size > 1,
            onClick = {
                onPage(pages[wrappedPageIndex(page, -1, pages.size)])
            },
        )
        Spacer(Modifier.width(dimens.spacingTiny))
        BoxWithConstraints(
            modifier = Modifier.weight(1f).fillMaxHeight(),
        ) {
            val gap = dimens.spacingTiny
            val buttonWidth = (maxWidth - gap * (VISIBLE_PAGE_BUTTONS - 1)) /
                VISIBLE_PAGE_BUTTONS
            LazyRow(
                modifier = Modifier.fillMaxSize(),
                state = listState,
                flingBehavior = rememberSnapFlingBehavior(lazyListState = listState),
                horizontalArrangement = Arrangement.spacedBy(gap),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                itemsIndexed(pages, key = { _, item -> item.name }) { index, item ->
                    PageButton(
                        state = state,
                        item = item,
                        selected = index == page,
                        modifier = Modifier.width(buttonWidth),
                        onClick = { onPage(item) },
                    )
                }
            }
        }
        Spacer(Modifier.width(dimens.spacingTiny))
        PageRailArrow(
            state = state,
            sprite = state.sprites[StardewUi.ARROW_RIGHT],
            description = "Next page",
            enabled = pages.size > 1,
            onClick = {
                onPage(pages[wrappedPageIndex(page, 1, pages.size)])
            },
        )
    }
}

@Composable
private fun PageRailArrow(
    state: StardewState,
    sprite: UiSprite?,
    description: String,
    enabled: Boolean,
    onClick: () -> Unit,
) {
    val frame = state.frame(StardewUi.BUTTON)
    val haptics = LocalHapticFeedback.current
    val base = Modifier.size(40.dp)
    val painted = if (frame != null) {
        base.drawBehind { drawNineSlice(frame, size) }
    } else {
        base
            .clip(ThorTheme.shapes.small)
            .background(ThorTheme.colors.surfaceHighest)
    }
    Box(
        modifier = painted.clickable(enabled = enabled, role = Role.Button) {
            haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
            onClick()
        },
        contentAlignment = Alignment.Center,
    ) {
        if (sprite != null) {
            Image(
                bitmap = sprite.image,
                contentDescription = description,
                modifier = Modifier.size(19.dp),
                contentScale = ContentScale.Fit,
                filterQuality = FilterQuality.None,
            )
        } else {
            Text(
                text = if (description.startsWith("Previous")) "<" else ">",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
                color = REFERENCE_TAB_INK,
            )
        }
    }
}

internal fun wrappedPageIndex(page: Int, delta: Int, pageCount: Int): Int {
    if (pageCount <= 0) return 0
    val current = page.coerceIn(0, pageCount - 1)
    return (current + delta).mod(pageCount)
}

internal fun pageWindowStart(
    page: Int,
    pageCount: Int,
    visibleButtons: Int = VISIBLE_PAGE_BUTTONS,
): Int {
    if (pageCount <= 0 || visibleButtons <= 0) return 0
    val current = page.coerceIn(0, pageCount - 1)
    val lastWindow = (pageCount - visibleButtons).coerceAtLeast(0)
    return (current - visibleButtons / 2).coerceIn(0, lastWindow)
}

private const val VISIBLE_PAGE_BUTTONS = 3

@Composable
private fun PageButton(
    state: StardewState,
    item: StardewPage,
    selected: Boolean,
    modifier: Modifier = Modifier,
    onClick: () -> Unit,
) {
    val colors = ThorTheme.colors
    val status = pageStatus(item, state)
    val frame = state.frame(StardewUi.BUTTON)
    val icon = state.sprites[pageIcon(item)]
    val haptics = LocalHapticFeedback.current
    val base = modifier.height(44.dp)
    val painted = if (frame != null) {
        base.drawBehind { drawNineSlice(frame, size) }
    } else {
        base
            .clip(ThorTheme.shapes.small)
            .background(colors.surfaceHighest)
    }

    Box(
        modifier = painted
            .semantics { this.selected = selected }
            .clickable(role = Role.Tab) {
                haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                onClick()
            },
        contentAlignment = Alignment.Center,
    ) {
        if (selected) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(5.dp)
                    .clip(ThorTheme.shapes.small)
                    .background(colors.cursor.copy(alpha = 0.14f)),
            )
        }

        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.Center,
            modifier = Modifier.padding(horizontal = 4.dp, vertical = 6.dp),
        ) {
            if (icon != null) {
                Image(
                    bitmap = icon.image,
                    contentDescription = null,
                    modifier = Modifier.size(20.dp),
                    contentScale = ContentScale.Fit,
                    filterQuality = FilterQuality.None,
                )
                if (item != StardewPage.SETTINGS) Spacer(modifier = Modifier.width(4.dp))
            }
            if (item != StardewPage.SETTINGS) {
                Text(
                    text = item.title,
                    style = MaterialTheme.typography.labelSmall,
                    fontWeight = if (selected) FontWeight.Bold else FontWeight.SemiBold,
                    color = REFERENCE_TAB_INK,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    textAlign = TextAlign.Center,
                )
            }
        }

        if (selected) {
            Box(
                modifier = Modifier
                    .align(Alignment.BottomCenter)
                    .padding(bottom = 5.dp)
                    .fillMaxWidth(0.44f)
                    .height(3.dp)
                    .clip(ThorTheme.shapes.pill)
                    .background(colors.cursor),
            )
        } else if (status.urgent || status.ready) {
            Box(
                modifier = Modifier
                    .align(Alignment.TopEnd)
                    .padding(top = 7.dp, end = 8.dp)
                    .size(6.dp)
                    .clip(ThorTheme.shapes.pill)
                    .background(if (status.urgent) colors.error else colors.cursor),
            )
        }
    }
}

private val REFERENCE_TAB_INK = Color(0xFF321B0F)

private fun pageIcon(page: StardewPage): String = when (page) {
    StardewPage.TODAY -> StardewUi.TAB_BACKPACK
    StardewPage.CRAFTING -> StardewUi.TAB_CRAFTING
    StardewPage.PROGRESS -> StardewUi.COIN
    StardewPage.MAP -> StardewUi.TAB_MAP
    StardewPage.JOURNAL -> StardewUi.TAB_JOURNAL
    StardewPage.RELATIONSHIPS -> StardewUi.TAB_RELATIONSHIPS
    StardewPage.GIFT_GUIDE -> StardewUi.HEART
    StardewPage.FIELDS -> StardewUi.SEASON
    StardewPage.MACHINES -> StardewUi.TAB_CRAFTING
    StardewPage.BUNDLES -> StardewUi.TAB_SKILLS
    StardewPage.CALENDAR -> StardewUi.SEASON
    StardewPage.SETTINGS -> StardewUi.TAB_SETTINGS
}

internal data class StardewPageStatus(
    val detail: String = "",
    val urgent: Boolean = false,
    val ready: Boolean = false,
)

internal fun pageStatus(page: StardewPage, state: StardewState): StardewPageStatus = when (page) {
    StardewPage.TODAY -> if (!state.hasWorld) {
        StardewPageStatus("Waiting")
    } else {
        StardewPageStatus(
            detail = state.clock,
            urgent = state.lateInDay ||
                (state.maxEnergy > 0 && state.energy * 4 <= state.maxEnergy) ||
                (state.maxHealth > 0 && state.health * 4 <= state.maxHealth),
        )
    }

    StardewPage.MAP -> StardewPageStatus(state.location.ifEmpty { "Valley" })

    StardewPage.CRAFTING -> StardewPageStatus(
        detail = "${state.recipes.count { it.ready }}/${state.recipes.size} ready",
        ready = state.recipes.any { it.ready },
    )

    StardewPage.PROGRESS -> StardewPageStatus(
        detail = when {
            state.collections.total > 0 ->
                "${state.collections.donated}/${state.collections.total} museum"
            state.mines.deepest > 0 -> "Mine level ${state.mines.deepest}"
            else -> "Farm records"
        },
        ready = state.collections.carried.isNotEmpty(),
    )

    StardewPage.MACHINES -> {
        val chores = state.machinesReady + state.animalsUnpetted.size +
            state.animalsWithProduce.size +
            state.trees.sumOf { it.count }
        StardewPageStatus(
            detail = if (chores == 0) "All clear" else "$chores ready",
            ready = chores > 0,
        )
    }

    StardewPage.JOURNAL -> {
        val urgent = state.activeQuests.count { it.daily || it.daysLeft in 0..1 }
        StardewPageStatus(
            detail = "${state.activeQuests.size} open",
            urgent = urgent > 0,
        )
    }

    StardewPage.RELATIONSHIPS -> StardewPageStatus(
        detail = "${state.villagers.size} villagers",
        urgent = state.birthdayToday != null,
    )

    StardewPage.GIFT_GUIDE -> StardewPageStatus(
        detail = if (state.giftableNow.isEmpty()) {
            "${state.gifts.size} villagers"
        } else {
            val count = state.giftableNow.size
            "$count ${if (count == 1) "gift" else "gifts"} in bag"
        },
        urgent = state.birthdayToday != null,
        ready = state.giftableNow.isNotEmpty(),
    )

    StardewPage.FIELDS -> {
        val ready = state.cropsReady.size
        val thirsty = state.cropsThirsty.size
        StardewPageStatus(
            detail = when {
                thirsty > 0 -> "$thirsty need water"
                ready > 0 -> "$ready ready"
                state.crops.isEmpty() -> "No crops"
                else -> "${state.crops.size} growing"
            },
            ready = ready > 0 || thirsty > 0,
        )
    }

    StardewPage.BUNDLES -> {
        val ready = state.bundleItemsCarried.size
        StardewPageStatus(
            detail = if (ready > 0) "$ready to donate" else "${state.bundles.size} active",
            ready = ready > 0,
        )
    }

    StardewPage.CALENDAR -> StardewPageStatus(
        detail = state.birthdayToday?.let { "${it.name}'s birthday" }
            ?: listOf(state.season, state.day.takeIf { it > 0 }?.toString().orEmpty())
                .filter { it.isNotEmpty() }
                .joinToString(" ")
                .ifEmpty { "Season" },
        urgent = state.birthdayToday != null,
    )

    StardewPage.SETTINGS -> StardewPageStatus("Panel")
}

@Composable
internal fun StardewCard(accent: Boolean = false, content: @Composable ColumnScope.() -> Unit) {
    Card(accent = accent, content = content)
}

@Composable
internal fun StardewSectionTitle(text: String) {
    SectionTitle(text)
}

private val LocalJoinedPanel = compositionLocalOf { false }

@Composable
private fun ColumnScope.Panels(
    panels: List<@Composable (Boolean) -> Unit>,
    equalSizes: Boolean = false,
) {
    if (panels.isEmpty()) return

    Card(fill = true) {
        CompositionLocalProvider(LocalJoinedPanel provides true) {
            panels.forEachIndexed { index, panel ->
                if (index > 0) Rule()

                val fills = equalSizes || index == panels.lastIndex
                val section = Modifier.fillMaxWidth().then(
                    if (fills) Modifier.weight(1f) else Modifier,
                )
                Box(modifier = section) { panel(fills) }
            }
        }
    }
}

@Composable
private fun Nothing(show: Boolean, message: String) {
    if (!show) return
    Card {
        Text(
            text = message,
            style = MaterialTheme.typography.bodyMedium,
            color = ThorTheme.colors.onSurfaceVariant,
        )
    }
}

@Composable
private fun JournalPage(state: StardewState, modifier: Modifier = Modifier) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens
    val quests = state.activeQuests
    var open by rememberSaveable { mutableIntStateOf(-1) }

    if (quests.isEmpty()) {
        Nothing(true, "Nothing in the journal.")
        return
    }

    Box(modifier = modifier) {
        Card(fill = true) {
            SectionTitle("Journal", "${quests.size} open")

            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .verticalScroll(rememberScrollState()),
            ) {
                quests.forEachIndexed { index, quest ->
                    if (index > 0) Rule()
                    val expanded = open == quest.index

                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { open = if (expanded) -1 else quest.index }
                            .padding(vertical = dimens.spacingTiny),
                    ) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text(
                                text = quest.title,
                                style = MaterialTheme.typography.bodyMedium.enlarged(),
                                fontWeight = FontWeight.Medium,
                                color = colors.onSurface,
                                modifier = Modifier.weight(1f),
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                            )
                            Deadline(quest)
                        }

                        if (quest.objective.isNotEmpty()) {
                            Text(
                                text = quest.objective,
                                style = MaterialTheme.typography.bodySmall.enlarged(),
                                color = colors.onSurfaceVariant,
                                maxLines = if (expanded) 4 else 2,
                                overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.padding(top = 2.dp),
                            )
                        }

                        if (expanded) {
                            if (quest.detail.isNotEmpty()) {
                                Text(
                                    text = quest.detail,
                                    style = MaterialTheme.typography.bodySmall.enlarged(),
                                    color = colors.onSurfaceVariant.copy(alpha = 0.8f),
                                    modifier = Modifier.padding(top = dimens.spacingTiny),
                                )
                            }

                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(top = dimens.spacingSmall),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
                            ) {
                                if (quest.reward > 0) {
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
                                        text = "%,dg".format(quest.reward),
                                        style = MaterialTheme.typography.labelSmall.enlarged(),
                                        color = colors.onSurfaceVariant,
                                    )
                                }

                                Box(modifier = Modifier.weight(1f))

                                if (quest.cancellable && quest.index >= 0) {
                                    Text(
                                        text = "Cancel",
                                        style = MaterialTheme.typography.labelSmall.enlarged(),
                                        fontWeight = FontWeight.Medium,
                                        color = colors.error,
                                        modifier = Modifier
                                            .clip(ThorTheme.shapes.small)
                                            .clickable {
                                                SecondScreen.send(
                                                    Commands.cancelQuest(quest.index),
                                                )
                                                open = -1
                                            }
                                            .padding(horizontal = 10.dp, vertical = 5.dp),
                                    )
                                }
                            }
                        }
                    }
                }
            }

        }
    }
}

@Composable
private fun Deadline(quest: StardewQuest) {
    val colors = ThorTheme.colors
    val text = when {
        quest.daysLeft > 1 -> "${quest.daysLeft} days"
        quest.daysLeft == 1 -> "last day"
        quest.daily -> "today"
        else -> return
    }
    val urgent = quest.daysLeft in 0..1

    Text(
        text = text,
        style = MaterialTheme.typography.labelSmall.enlarged(),
        fontWeight = FontWeight.Medium,
        color = if (urgent) colors.error else colors.onSurfaceVariant,
        modifier = Modifier
            .clip(ThorTheme.shapes.pill)
            .background(
                if (urgent) {
                    colors.error.copy(alpha = 0.14f)
                } else {
                    colors.surfaceHighest
                },
            )
            .padding(horizontal = 8.dp, vertical = 3.dp),
    )
}

@Composable
private fun DayCard(
    state: StardewState,
    fill: Boolean = false,
    availablePages: Set<StardewPage>,
    onPage: (StardewPage) -> Unit,
) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens

    Card(fill = fill) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = state.date.ifEmpty { "Waiting for the farm" },
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold,
                    color = colors.onSurface,
                )
                if (state.location.isNotEmpty()) {
                    Text(
                        text = state.location,
                        style = MaterialTheme.typography.bodySmall,
                        color = colors.onSurfaceVariant,
                    )
                }
            }
            Text(
                text = state.clock,
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.Bold,

                color = if (state.lateInDay) colors.error else colors.onSurface,
            )
        }

        Box(
            modifier = Modifier
                .padding(top = dimens.spacingSmall)
                .fillMaxWidth()
                .height(4.dp)
                .clip(ThorTheme.shapes.pill)
                .background(colors.surfaceHighest),
        ) {
            Box(
                modifier = Modifier
                    .fillMaxWidth(state.dayProgress)
                    .height(4.dp)
                    .clip(ThorTheme.shapes.pill)
                    .background(if (state.lateInDay) colors.error else colors.cursor),
            )
        }

        Row(
            modifier = Modifier
                .padding(top = dimens.spacing)
                .fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(dimens.spacing),
        ) {
            Stat("Today", state.weatherToday.ifEmpty { "—" }, Modifier.weight(1f))

            Stat("Tomorrow", state.weatherTomorrow.ifEmpty { "—" }, Modifier.weight(1f))
            Stat(
                label = "Gold",
                value = "%,dg".format(state.gold),

                icon = state.sprites[StardewUi.COIN],
                modifier = Modifier.weight(1f),
            )
            Stat("Fortune", state.luckLabel, Modifier.weight(1f))
            Stat("Left", state.timeLeft, Modifier.weight(1f))
        }

        val earned = state.earnedToday
        if (earned != 0) {
            LineRow(
                left = if (earned > 0) "Earned today" else "Spent today",
                right = "%,dg".format(kotlin.math.abs(earned)),
                muted = true,
            )
        }

        if (state.cartToday) {
            LineRow(left = "The travelling cart is in the forest", right = "", urgent = true)
        }

        Row(
            modifier = Modifier
                .padding(top = dimens.spacingSmall)
                .fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(dimens.spacing),
        ) {
            Meter(
                label = "Energy",
                value = state.energy,
                max = state.maxEnergy,

                tint = colors.primary,
                modifier = Modifier.weight(1f),
            )
            Meter(
                label = "Health",
                value = state.health,
                max = state.maxHealth,
                tint = colors.error,
                modifier = Modifier.weight(1f),
            )
        }

        DailyPriorities(
            state = state,
            availablePages = availablePages,
            onPage = onPage,
        )
    }
}

private data class DailyPriority(
    val page: StardewPage,
    val label: String,
    val detail: String,
    val urgent: Boolean = true,
)

@Composable
private fun DailyPriorities(
    state: StardewState,
    availablePages: Set<StardewPage>,
    onPage: (StardewPage) -> Unit,
) {
    val farmChores = state.machinesReady + state.animalsUnpetted.size +
        state.animalsWithProduce.size +
        state.trees.sumOf { it.count }
    val urgentQuests = state.activeQuests.count { it.daily || it.daysLeft in 0..1 }
    val urgentOrders = state.orders.count { it.daysLeft in 0..1 }
    val readyBundles = state.bundleItemsCarried.size
    val priorities = buildList {
        val fieldTasks = state.cropsReady.size + state.cropsThirsty.size
        if (fieldTasks > 0 && StardewPage.FIELDS in availablePages) {
            add(DailyPriority(StardewPage.FIELDS, "Fields", "$fieldTasks to tend"))
        }
        if (farmChores > 0 && StardewPage.MACHINES in availablePages) {
            add(DailyPriority(StardewPage.MACHINES, "Machines", "$farmChores chores"))
        }
        if (urgentQuests > 0 && StardewPage.JOURNAL in availablePages) {
            add(DailyPriority(StardewPage.JOURNAL, "Journal", "$urgentQuests due soon"))
        }
        if (readyBundles > 0 && StardewPage.BUNDLES in availablePages) {
            add(DailyPriority(StardewPage.BUNDLES, "Bundles", "$readyBundles in bag"))
        }
        if (state.giftableNow.isNotEmpty() && StardewPage.GIFT_GUIDE in availablePages) {
            add(
                DailyPriority(
                    StardewPage.GIFT_GUIDE,
                    "Loved gifts",
                    "${state.giftableNow.size} ${
                        if (state.giftableNow.size == 1) "match" else "matches"
                    }",
                    urgent = false,
                ),
            )
        }
        if (StardewPage.CALENDAR in availablePages) {
            state.birthdayToday?.let { villager ->
                add(DailyPriority(StardewPage.CALENDAR, "Birthday", villager.name, urgent = false))
            }
        }
        if (urgentOrders > 0 && StardewPage.JOURNAL in availablePages) {
            add(DailyPriority(StardewPage.JOURNAL, "Orders", "$urgentOrders due soon"))
        }
    }
    if (priorities.isEmpty()) return

    val dimens = ThorTheme.dimens
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(top = dimens.spacingSmall),
    ) {
        SectionTitle("Priorities", "Tap to open")
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
        ) {
            priorities.take(5).forEach { priority ->
                PriorityButton(
                    state = state,
                    priority = priority,
                    modifier = Modifier.weight(1f),
                    onClick = { onPage(priority.page) },
                )
            }
        }
    }
}

@Composable
private fun PriorityButton(
    state: StardewState,
    priority: DailyPriority,
    modifier: Modifier = Modifier,
    onClick: () -> Unit,
) {
    val colors = ThorTheme.colors
    val frame = state.frame(StardewUi.BUTTON)
    val haptics = LocalHapticFeedback.current
    val base = modifier.heightIn(min = 44.dp)
    val painted = if (frame != null) {
        base.drawBehind { drawNineSlice(frame, size) }
    } else {
        base
            .clip(ThorTheme.shapes.small)
            .background(colors.surfaceHighest)
    }

    Column(
        modifier = painted
            .clickable(role = Role.Button) {
                haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                onClick()
            }
            .padding(horizontal = 8.dp, vertical = 7.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Text(
            text = priority.label.uppercase(),
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.Bold,
            color = if (priority.urgent) colors.error else colors.cursor,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            text = priority.detail,
            style = MaterialTheme.typography.labelSmall,
            color = colors.onSurface,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun HistoryCard(fill: Boolean = false) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens
    val days by StardewHistory.days.collectAsStateWithLifecycle()
    if (days.size < 2) return

    val peak = days.maxOf { maxOf(it.shipped, 1) }

    Card(fill = fill) {
        SectionTitle("Recent days", "${days.size} tracked")
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .then(if (fill) Modifier.weight(1f) else Modifier.height(90.dp))
                .padding(top = dimens.spacingTiny),
            horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
            verticalAlignment = Alignment.Bottom,
        ) {
            days.takeLast(10).forEach { day ->
                Column(
                    modifier = Modifier.weight(1f).fillMaxHeight(),
                    verticalArrangement = Arrangement.Bottom,
                    horizontalAlignment = Alignment.CenterHorizontally,
                ) {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .fillMaxHeight((day.shipped.toFloat() / peak).coerceIn(0.02f, 1f))
                            .clip(ThorTheme.shapes.small)
                            .background(colors.cursor.copy(alpha = 0.75f)),
                    )
                    Text(
                        text = day.label,
                        style = MaterialTheme.typography.labelSmall,
                        color = colors.onSurfaceVariant,
                        maxLines = 1,
                    )
                }
            }
        }
        LineRow(
            left = "Shipped on the last day",
            right = "%,dg".format(days.last().shipped),
            muted = true,
        )
    }
}

@Composable
private fun TreesCard(state: StardewState, fill: Boolean = false) {
    val dimens = ThorTheme.dimens

    Card(fill = fill) {
        SectionTitle("Fruit trees", "${state.trees.sumOf { it.count }} fruit")
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .then(if (fill) Modifier.weight(1f) else Modifier.heightIn(max = 120.dp))
                .verticalScroll(rememberScrollState()),
        ) {
            state.trees.groupBy { it.location }.entries.chunked(2).forEach { pair ->
                Row(
                    modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp),
                    horizontalArrangement = Arrangement.spacedBy(dimens.spacingSmall),
                ) {
                    pair.forEach { (place, trees) ->
                        TreeLocation(
                            state = state,
                            place = place,
                            trees = trees,
                            modifier = Modifier.weight(1f),
                        )
                    }
                    repeat(2 - pair.size) {
                        Spacer(modifier = Modifier.weight(1f))
                    }
                }
            }
        }
    }
}

@Composable
private fun TreeLocation(
    state: StardewState,
    place: String,
    trees: List<StardewTree>,
    modifier: Modifier = Modifier,
) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens
    Row(
        modifier = modifier,
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
    ) {
        trees.take(3).forEach { tree -> Icon(state.sprites[tree.id], size = 18.dp) }
        if (trees.size > 3) {
            Text(
                text = "+${trees.size - 3}",
                style = MaterialTheme.typography.labelSmall,
                color = colors.onSurfaceVariant,
            )
        }
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = place.ifEmpty { "Farm" },
                style = MaterialTheme.typography.bodySmall.enlarged(),
                color = colors.onSurface,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                text = "${trees.sumOf { it.count }} fruit",
                style = MaterialTheme.typography.labelSmall,
                color = colors.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

@Composable
private fun FarmPage(state: StardewState, modifier: Modifier = Modifier) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens
    Box(modifier = modifier) {
        Card(fill = true) {
            Row(
                modifier = Modifier.fillMaxWidth().weight(1f),
                horizontalArrangement = Arrangement.spacedBy(dimens.spacingSmall),
            ) {
                FarmMachines(state, Modifier.weight(1f).fillMaxHeight())
                VerticalRule()
                FarmAnimals(state, Modifier.weight(1f).fillMaxHeight())
                VerticalRule()
                FarmTrees(state, Modifier.weight(1f).fillMaxHeight())
            }

            Rule()
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(dimens.spacing),
            ) {
                FarmSummary(
                    label = "Fields",
                    value = if (state.cropsThirsty.isEmpty()) {
                        "Watered"
                    } else {
                        "${state.cropsThirsty.size} need water"
                    },
                    urgent = state.cropsThirsty.isNotEmpty(),
                    modifier = Modifier.weight(1f),
                )
                FarmSummary(
                    label = "Crafting",
                    value = "${state.recipes.size} recipes ready",
                    modifier = Modifier.weight(1f),
                )
                FarmSummary(
                    label = "Cooking",
                    value = "${state.cooking.size} recipes ready",
                    modifier = Modifier.weight(1f),
                )
            }
        }
    }
}

@Composable
private fun FarmMachines(state: StardewState, modifier: Modifier = Modifier) {
    val colors = ThorTheme.colors
    val ready = state.machinesByUrgency.filter { it.ready }
    val working = state.machinesByUrgency.filterNot { it.ready }
    val shown = ready + working

    Column(modifier = modifier) {
        SectionTitle(
            "Machines",
            listOfNotNull(
                ready.takeIf { it.isNotEmpty() }?.let { "${it.size} ready" },
                working.takeIf { it.isNotEmpty() }?.let { "${it.size} working" },
            ).joinToString(" · "),
        )
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .verticalScroll(rememberScrollState()),
        ) {
            if (shown.isEmpty()) {
                EmptySection("No active machines")
            } else {
                shown.forEach { machine ->
                    FarmMachineItem(
                        state = state,
                        machine = machine,
                        modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp),
                    )
                }
            }
        }
    }
}

@Composable
private fun FarmMachineItem(
    state: StardewState,
    machine: StardewMachine,
    modifier: Modifier = Modifier,
) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens
    Row(
        modifier = modifier,
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
    ) {
        Icon(state.sprites[machine.outputId] ?: state.sprites[machine.id], size = 20.dp)
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = machine.output.ifEmpty { machine.name },
                style = MaterialTheme.typography.bodySmall,
                fontWeight = if (machine.ready) FontWeight.Medium else FontWeight.Normal,
                color = colors.onSurface,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                text = if (machine.ready) {
                    "Ready · ${machine.location}"
                } else {
                    machineTime(machine.minutes) + readyAt(state.timeOfDay, machine.minutes)
                },
                style = MaterialTheme.typography.labelSmall,
                color = if (machine.ready) colors.cursor else colors.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

@Composable
private fun FarmAnimals(state: StardewState, modifier: Modifier = Modifier) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens
    val needsCare = state.animalsUnpetted
    val produce = state.animalsWithProduce
    val babies = state.animals.count { it.baby }

    Column(modifier = modifier) {
        SectionTitle(
            "Animals",
            listOfNotNull(
                needsCare.takeIf { it.isNotEmpty() }?.let { "${it.size} need care" },
                produce.takeIf { it.isNotEmpty() }?.let { "${it.size} collect" },
            ).joinToString(" · ").ifEmpty { "All cared for" },
        )

        Column(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .verticalScroll(rememberScrollState()),
        ) {
        needsCare.take(FARM_LIST_LIMIT).chunked(FARM_ITEMS_PER_ROW).forEach { row ->
            Row(
                modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(dimens.spacingSmall),
            ) {
                row.forEach { animal ->
                    Row(
                        modifier = Modifier.weight(1f),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
                    ) {
                        Icon(state.sprites[animal.produceId], size = 18.dp)
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = animal.name,
                                style = MaterialTheme.typography.bodySmall,
                                color = colors.onSurface,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                            )
                            Text(
                                text = if (animal.baby) "${animal.type} · Young" else animal.type,
                                style = MaterialTheme.typography.labelSmall,
                                color = colors.onSurfaceVariant,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                            )
                        }
                    }
                }
                repeat(FARM_ITEMS_PER_ROW - row.size) {
                    Spacer(modifier = Modifier.weight(1f))
                }
            }
        }
        if (needsCare.size > FARM_LIST_LIMIT) {
            Text(
                text = "+${needsCare.size - FARM_LIST_LIMIT} more need care",
                style = MaterialTheme.typography.labelSmall,
                color = colors.onSurfaceVariant,
            )
        }

        if (produce.isNotEmpty()) {
            if (needsCare.isNotEmpty()) Rule()
            Text(
                text = "PRODUCE READY",
                style = MaterialTheme.typography.labelSmall,
                fontWeight = FontWeight.SemiBold,
                color = colors.onSurfaceVariant,
            )
            produce.groupBy { it.produceId to it.produce }.entries
                .take(FARM_LIST_LIMIT)
                .chunked(FARM_ITEMS_PER_ROW)
                .forEach { produceRow ->
                Row(
                    modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(dimens.spacingSmall),
                ) {
                    produceRow.forEach { entry ->
                        val (kind, animals) = entry
                        Row(
                            modifier = Modifier.weight(1f),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
                        ) {
                            Icon(state.sprites[kind.first], size = 18.dp)
                            Text(
                                text = kind.second.ifEmpty { "Produce" },
                                style = MaterialTheme.typography.bodySmall,
                                color = colors.onSurface,
                                modifier = Modifier.weight(1f),
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                            )
                            Text(
                                text = "×${animals.size}",
                                style = MaterialTheme.typography.labelSmall,
                                color = colors.cursor,
                            )
                        }
                    }
                    repeat(FARM_ITEMS_PER_ROW - produceRow.size) {
                        Spacer(modifier = Modifier.weight(1f))
                    }
                }
            }
        } else if (needsCare.isEmpty()) {
            EmptySection(if (babies > 0) "$babies young · no chores due" else "No chores due")
        }
        }
    }
}

@Composable
private fun FarmTrees(state: StardewState, modifier: Modifier = Modifier) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens
    val locations = state.trees.groupBy { it.location }.entries.toList()
    val fruit = state.trees.sumOf { it.count }

    Column(modifier = modifier) {
        SectionTitle("Fruit trees", if (fruit > 0) "$fruit fruit ready" else "None ready")
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .verticalScroll(rememberScrollState()),
        ) {
        if (locations.isEmpty()) {
            EmptySection("No fruit waiting")
        } else {
            locations.take(FARM_LIST_LIMIT).chunked(FARM_ITEMS_PER_ROW).forEach { row ->
                Row(
                    modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(dimens.spacingSmall),
                ) {
                    row.forEach { (place, trees) ->
                        Row(
                            modifier = Modifier.weight(1f),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
                        ) {
                            Icon(state.sprites[trees.first().id], size = 20.dp)
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = place.ifEmpty { "Farm" },
                                    style = MaterialTheme.typography.bodySmall,
                                    color = colors.onSurface,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis,
                                )
                                Text(
                                    text = "${trees.size} trees · ${trees.sumOf { it.count }} fruit",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = colors.onSurfaceVariant,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis,
                                )
                            }
                        }
                    }
                    repeat(FARM_ITEMS_PER_ROW - row.size) {
                        Spacer(modifier = Modifier.weight(1f))
                    }
                }
            }
            if (locations.size > FARM_LIST_LIMIT) {
                Text(
                    text = "+${locations.size - FARM_LIST_LIMIT} more locations",
                    style = MaterialTheme.typography.labelSmall,
                    color = colors.onSurfaceVariant,
                )
            }
        }
        }
    }
}

@Composable
private fun EmptySection(text: String) {
    Text(
        text = text,
        style = MaterialTheme.typography.bodySmall.enlarged(),
        color = ThorTheme.colors.onSurfaceVariant,
        modifier = Modifier.padding(vertical = ThorTheme.dimens.spacingSmall),
    )
}

@Composable
private fun FarmSummary(
    label: String,
    value: String,
    modifier: Modifier = Modifier,
    urgent: Boolean = false,
) {
    val colors = ThorTheme.colors
    Column(modifier = modifier) {
        Text(
            text = label.uppercase(),
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.SemiBold,
            color = colors.onSurfaceVariant,
        )
        Text(
            text = value,
            style = MaterialTheme.typography.bodySmall.enlarged(),
            fontWeight = FontWeight.Medium,
            color = if (urgent) colors.error else colors.onSurface,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun VerticalRule() {
    Box(
        modifier = Modifier
            .fillMaxHeight()
            .width(1.dp)
            .background(ThorTheme.colors.outline.copy(alpha = 0.35f)),
    )
}

private const val FARM_LIST_LIMIT = Int.MAX_VALUE

private const val FARM_ITEMS_PER_ROW = 1

@Composable
private fun PetCard(state: StardewState, fill: Boolean = false) {
    Card(accent = true, fill = fill) {
        SectionTitle(state.pet.name.ifEmpty { "Your pet" })
        if (!state.pet.petted) {
            LineRow(left = "Not petted today", right = "", urgent = true)
        }
        if (!state.pet.bowl) {
            LineRow(left = "Water bowl is empty", right = "", urgent = true)
        }
    }
}

@Composable
private fun ShippingCard(state: StardewState, fill: Boolean = false) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens

    Card(fill = fill) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(modifier = Modifier.weight(1f)) { SectionTitle("Shipping bin") }
            Row(verticalAlignment = Alignment.CenterVertically) {
                state.sprites[StardewUi.COIN]?.let { coin ->
                    Image(
                        bitmap = coin.image,
                        contentDescription = null,
                        modifier = Modifier.size(16.dp).padding(end = 4.dp),
                        contentScale = ContentScale.Fit,
                        filterQuality = FilterQuality.None,
                    )
                }
                Text(
                    text = "%,d".format(state.shipping.total),
                    style = MaterialTheme.typography.bodyMedium.enlarged(),
                    fontWeight = FontWeight.Medium,
                    color = colors.onSurface,
                )
            }
        }
        Row(
            modifier = Modifier.fillMaxWidth().padding(top = dimens.spacingTiny),
            horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
        ) {
            state.shipping.items.take(10).forEach { shipped ->
                Icon(state.sprites[shipped.id], size = 20.dp)
            }
        }
    }
}

@Composable
private fun TodayBirthdayCard(villager: StardewVillager, fill: Boolean = false) {
    val colors = ThorTheme.colors
    val state by SecondScreen.stardew.collectAsStateWithLifecycle()

    Card(accent = true, fill = fill) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(ThorTheme.dimens.spacingSmall),
        ) {
            Portrait(state, villager.npc, size = 44.dp)
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = "${villager.name}'s birthday is today",
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.SemiBold,
                    color = colors.onSurface,
                )
                if (villager.hearts > 0) {
                    Hearts(
                        filled = villager.hearts,
                        max = villager.maxHearts,
                        full = state.sprites[StardewUi.HEART],
                        empty = state.sprites[StardewUi.HEART_EMPTY],
                    )
                } else {
                    Text(
                        text = "You have not met yet",
                        style = MaterialTheme.typography.bodySmall,
                        color = colors.onSurfaceVariant,
                    )
                }
            }
        }
    }
}

@Composable
private fun BirthdaysCard(state: StardewState, fill: Boolean = false) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens

    Card(fill = fill) {
        SectionTitle(
            "Birthdays this week",
            "${state.birthdaysSoon.count { it.birthdayIn > 0 }} upcoming",
        )
        state.birthdaysSoon.filter { it.birthdayIn > 0 }.forEach { villager ->
            Row(
                modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(dimens.spacingSmall),
            ) {
                Portrait(state, villager.npc)
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = villager.name,
                        style = MaterialTheme.typography.bodyMedium.enlarged(),
                        color = colors.onSurface,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                    Hearts(
                        filled = villager.hearts,
                        max = villager.maxHearts,
                        full = state.sprites[StardewUi.HEART],
                        empty = state.sprites[StardewUi.HEART_EMPTY],
                    )
                }
                Text(
                    text = if (villager.birthdayIn == 1) {
                        "tomorrow"
                    } else {
                        "in ${villager.birthdayIn} days"
                    },
                    style = MaterialTheme.typography.labelSmall.enlarged(),
                    color = if (villager.birthdayIn == 1) colors.cursor else colors.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
private fun Portrait(state: StardewState, npc: String, size: androidx.compose.ui.unit.Dp = 34.dp) {
    val sprite = state.sprites["npc:" + npc] ?: return
    Image(
        bitmap = sprite.image,
        contentDescription = null,
        modifier = Modifier.size(size).clip(ThorTheme.shapes.small),
        contentScale = ContentScale.Crop,
        filterQuality = FilterQuality.None,
    )
}

@Composable
private fun CropsCard(state: StardewState, fill: Boolean = false) {

    val thirsty = state.cropsThirsty
    if (thirsty.isEmpty()) return

    Card(fill = fill) {
        SectionTitle("Fields", "${thirsty.size} need water")

        thirsty.groupBy { it.location }
            .entries
            .sortedByDescending { it.value.size }
            .take(4)
            .forEach { (location, crops) ->
                LineRow(
                    left = location.ifEmpty { "Farm" },
                    right = "${crops.size}",
                    muted = true,
                )
            }
    }
}

@Composable
private fun MapCard(
    state: StardewState,
    modifier: Modifier = Modifier,
    fill: Boolean = false,
) {
    val map = state.sprites[StardewUi.MAP]
    val colors = ThorTheme.colors
    val source = map?.image

    if (source == null || source.width <= 0 || source.height <= 0) {
        Box(modifier = modifier) {
            Card(fill = fill) {
                SectionTitle("Valley")
                Text(
                    text = "The map has not arrived from the game yet.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = colors.onSurfaceVariant,
                )
            }
        }
        return
    }

    val ratio = source.width.toFloat() / source.height.toFloat()
    val frame = state.frame(StardewUi.PANEL)
    var zoom by rememberSaveable { mutableFloatStateOf(MAP_MIN_ZOOM) }
    var panX by rememberSaveable { mutableFloatStateOf(0f) }
    var panY by rememberSaveable { mutableFloatStateOf(0f) }
    var viewportSize by remember { mutableStateOf(IntSize.Zero) }
    var selectedNpc by rememberSaveable { mutableStateOf("") }

    val selectedVillager = state.villagers.firstOrNull { villagerKey(it) == selectedNpc }

    val focusOn: (Float, Float, Float) -> Unit = { across, down, minimumZoom ->
        if (viewportSize.width > 0 && viewportSize.height > 0) {
            val nextZoom = clampedMapZoom(maxOf(zoom, minimumZoom))
            zoom = nextZoom
            panX = focusedMapTranslation(across, nextZoom, viewportSize.width.toFloat())
            panY = focusedMapTranslation(down, nextZoom, viewportSize.height.toFloat())
        }
    }

    LaunchedEffect(
        selectedNpc,
        selectedVillager?.onMap,
        viewportSize,
    ) {
        when {
            selectedNpc.isNotEmpty() && selectedVillager == null -> selectedNpc = ""
            selectedVillager?.onMap == true -> focusOn(
                selectedVillager.mapAcross,
                selectedVillager.mapDown,
                MAP_SELECTED_ZOOM,
            )
        }
    }

    val changeZoom: (Float) -> Unit = { requested ->
        val oldZoom = zoom
        val nextZoom = clampedMapZoom(requested)
        if (nextZoom != oldZoom) {
            panX = transformedMapTranslation(
                current = panX,
                oldZoom = oldZoom,
                newZoom = nextZoom,
                gestureFromCenter = 0f,
                pan = 0f,
                extent = viewportSize.width.toFloat(),
            )
            panY = transformedMapTranslation(
                current = panY,
                oldZoom = oldZoom,
                newZoom = nextZoom,
                gestureFromCenter = 0f,
                pan = 0f,
                extent = viewportSize.height.toFloat(),
            )
            zoom = nextZoom
        }
    }

    BoxWithConstraints(
        modifier = modifier.then(if (fill) Modifier.fillMaxSize() else Modifier.fillMaxWidth()),
    ) {
        val mapHeight = minOf(
            maxWidth / ratio,
            (maxHeight - MAP_FOOTER_MIN_HEIGHT).coerceAtLeast(0.dp),
        )
        val mapWidth = minOf(maxWidth, mapHeight * ratio)

        Column(modifier = Modifier.fillMaxSize()) {
            Box(
                modifier = Modifier.fillMaxWidth().height(mapHeight),
                contentAlignment = Alignment.Center,
            ) {
                Box(
                    modifier = Modifier
                        .size(width = mapWidth, height = mapHeight)
                        .then(
                            if (frame != null) {
                                Modifier.drawBehind { drawNineSlice(frame, size) }
                            } else {
                                Modifier
                                    .border(2.dp, MAP_REFERENCE_BORDER, ThorTheme.shapes.small)
                                    .background(colors.surface)
                            },
                        )
                        .padding(MAP_FRAME_INSET),
                ) {
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .clip(ThorTheme.shapes.small)
                            .clipToBounds()
                            .onSizeChanged { measured ->
                                viewportSize = measured
                                panX = clampedMapTranslation(
                                    panX,
                                    zoom,
                                    measured.width.toFloat(),
                                )
                                panY = clampedMapTranslation(
                                    panY,
                                    zoom,
                                    measured.height.toFloat(),
                                )
                            }
                            .pointerInput(viewportSize) {
                                detectTransformGestures { centroid, gesturePan, gestureZoom, _ ->
                                    if (viewportSize.width <= 0 || viewportSize.height <= 0) {
                                        return@detectTransformGestures
                                    }
                                    val oldZoom = zoom
                                    val nextZoom = clampedMapZoom(oldZoom * gestureZoom)
                                    val center = Offset(
                                        viewportSize.width / 2f,
                                        viewportSize.height / 2f,
                                    )
                                    panX = transformedMapTranslation(
                                        current = panX,
                                        oldZoom = oldZoom,
                                        newZoom = nextZoom,
                                        gestureFromCenter = centroid.x - center.x,
                                        pan = gesturePan.x,
                                        extent = viewportSize.width.toFloat(),
                                    )
                                    panY = transformedMapTranslation(
                                        current = panY,
                                        oldZoom = oldZoom,
                                        newZoom = nextZoom,
                                        gestureFromCenter = centroid.y - center.y,
                                        pan = gesturePan.y,
                                        extent = viewportSize.height.toFloat(),
                                    )
                                    zoom = nextZoom
                                }
                            },
                    ) {
                        Image(
                            bitmap = source,
                            contentDescription = "Map of the valley",
                            modifier = Modifier
                                .fillMaxSize()
                                .graphicsLayer {
                                    scaleX = zoom
                                    scaleY = zoom
                                    translationX = panX
                                    translationY = panY
                                },
                            contentScale = ContentScale.FillBounds,
                            filterQuality = FilterQuality.None,
                        )

                        if (state.onMap && plausibleMapPoint(state.mapAcross, state.mapDown)) {
                            MapMarker(
                                state = state,
                                sprite = state.sprites[StardewUi.PLAYER],
                                label = state.playerName.ifEmpty { "Farmer" },
                                across = state.mapAcross,
                                down = state.mapDown,
                                viewport = viewportSize,
                                zoom = zoom,
                                panX = panX,
                                panY = panY,
                                selected = false,
                            )
                        }

                        selectedVillager?.takeIf { it.onMap }?.let { villager ->
                            MapMarker(
                                state = state,
                                sprite = state.sprites["npc:${villager.npc}"],
                                label = villager.name,
                                across = villager.mapAcross,
                                down = villager.mapDown,
                                viewport = viewportSize,
                                zoom = zoom,
                                panX = panX,
                                panY = panY,
                                selected = true,
                            )
                        }

                        val locationLabel = selectedVillager?.let { villager ->
                            val where = villager.location.ifEmpty { "Location unavailable" }
                            "${villager.name} - $where"
                        } ?: state.location
                        LocationScroll(
                            name = locationLabel,
                            state = state,
                            modifier = Modifier
                                .align(Alignment.BottomCenter)
                                .padding(bottom = 7.dp),
                        )

                        Column(
                            modifier = Modifier.align(Alignment.BottomEnd).padding(8.dp),
                            verticalArrangement = Arrangement.spacedBy(3.dp),
                        ) {
                            MapZoomButton(
                                state = state,
                                label = "+",
                                enabled = zoom < MAP_MAX_ZOOM,
                                onClick = { changeZoom(zoom + MAP_ZOOM_STEP) },
                            )
                            MapZoomButton(
                                state = state,
                                label = "−",
                                enabled = zoom > MAP_MIN_ZOOM,
                                onClick = { changeZoom(zoom - MAP_ZOOM_STEP) },
                            )
                        }
                    }
                }
            }

            Rule()
            MapFooter(
                state = state,
                selectedVillager = selectedVillager,
                modifier = Modifier.fillMaxWidth().weight(1f),
                onPlayer = {
                    selectedNpc = ""
                    if (state.onMap && plausibleMapPoint(state.mapAcross, state.mapDown)) {
                        focusOn(state.mapAcross, state.mapDown, MAP_MIN_ZOOM)
                    }
                },
                onVillager = { villager ->
                    selectedNpc = villagerKey(villager)
                    if (villager.onMap) {
                        focusOn(villager.mapAcross, villager.mapDown, MAP_SELECTED_ZOOM)
                    }
                },
            )
        }
    }
}

@Composable
private fun LocationScroll(
    name: String,
    state: StardewState,
    modifier: Modifier = Modifier,
) {
    val colors = ThorTheme.colors
    if (name.isEmpty()) return
    val scroll = state.frame(StardewUi.SCROLL)

    Box(
        modifier = modifier.then(
            if (scroll != null) {
                Modifier.drawBehind { drawNineSlice(scroll, size) }
            } else {

                Modifier
                    .clip(ThorTheme.shapes.pill)
                    .background(colors.surface.copy(alpha = 0.85f))
            },
        ),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = name,
            style = MaterialTheme.typography.labelLarge,
            fontWeight = FontWeight.Bold,
            color = colors.onSurface,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 5.dp),
        )
    }
}

@Composable
private fun MapMarker(
    state: StardewState,
    sprite: UiSprite?,
    label: String,
    across: Float,
    down: Float,
    viewport: IntSize,
    zoom: Float,
    panX: Float,
    panY: Float,
    selected: Boolean,
) {
    if (viewport.width <= 0 || viewport.height <= 0) return
    val markerSize = if (selected) 43.dp else 37.dp
    val density = LocalDensity.current
    val markerPixels = with(density) { markerSize.toPx() }
    val centerX = mapMarkerScreenCoordinate(
        fraction = across,
        extent = viewport.width.toFloat(),
        zoom = zoom,
        translation = panX,
    )
    val centerY = mapMarkerScreenCoordinate(
        fraction = down,
        extent = viewport.height.toFloat(),
        zoom = zoom,
        translation = panY,
    )
    if (centerX < -markerPixels || centerX > viewport.width + markerPixels ||
        centerY < -markerPixels || centerY > viewport.height + markerPixels
    ) {
        return
    }

    val slot = state.frame(if (selected) StardewUi.SLOT_SELECTED else StardewUi.SLOT)
    val base = Modifier
        .offset {
            IntOffset(
                (centerX - markerPixels / 2f).roundToInt(),
                (centerY - markerPixels / 2f).roundToInt(),
            )
        }
        .size(markerSize)
    val painted = if (slot != null) {
        base.drawBehind { drawNineSlice(slot, size) }
    } else {
        base
            .clip(ThorTheme.shapes.small)
            .background(
                if (selected) {
                    ThorTheme.colors.cursor.copy(alpha = 0.88f)
                } else {
                    Color(0xFFFFD080).copy(alpha = 0.94f)
                },
            )
            .border(1.dp, MAP_REFERENCE_BORDER, ThorTheme.shapes.small)
    }

    Box(modifier = painted, contentAlignment = Alignment.Center) {
        if (sprite != null) {
            Image(
                bitmap = sprite.image,
                contentDescription = "$label is here",
                modifier = Modifier.fillMaxSize().padding(4.dp),
                contentScale = ContentScale.Fit,
                filterQuality = FilterQuality.None,
            )
        } else {
            Text(
                text = label.take(1).uppercase().ifEmpty { "?" },
                style = MaterialTheme.typography.labelLarge,
                fontWeight = FontWeight.Bold,
                color = MAP_REFERENCE_INK,
            )
        }
    }
}

@Composable
private fun MapZoomButton(
    state: StardewState,
    label: String,
    enabled: Boolean,
    onClick: () -> Unit,
) {
    val frame = state.frame(StardewUi.BUTTON)
    val haptics = LocalHapticFeedback.current
    val base = Modifier.size(42.dp)
    val painted = if (frame != null) {
        base.drawBehind { drawNineSlice(frame, size) }
    } else {
        base
            .clip(ThorTheme.shapes.small)
            .background(Color(0xFFFFC66D))
            .border(1.dp, MAP_REFERENCE_BORDER, ThorTheme.shapes.small)
    }
    Box(
        modifier = painted.clickable(enabled = enabled, role = Role.Button) {
            haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
            onClick()
        },
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.headlineSmall,
            fontWeight = FontWeight.Bold,
            color = MAP_REFERENCE_INK.copy(alpha = if (enabled) 1f else 0.42f),
            textAlign = TextAlign.Center,
        )
    }
}

@Composable
private fun MapFooter(
    state: StardewState,
    selectedVillager: StardewVillager?,
    modifier: Modifier = Modifier,
    onPlayer: () -> Unit,
    onVillager: (StardewVillager) -> Unit,
) {
    BoxWithConstraints(modifier = modifier) {
        val portraitSize = when {
            maxHeight < 88.dp -> 31.dp
            maxHeight < 104.dp -> 37.dp
            else -> 43.dp
        }
        Row(
            modifier = Modifier.fillMaxSize().padding(horizontal = 5.dp, vertical = 2.dp),
            horizontalArrangement = Arrangement.spacedBy(7.dp),
        ) {
            MapFarmSummary(
                state = state,
                modifier = Modifier.weight(0.38f).fillMaxHeight(),
                onPlayer = onPlayer,
            )
            Box(
                modifier = Modifier
                    .fillMaxHeight()
                    .width(1.dp)
                    .background(MAP_REFERENCE_LINE),
            )
            MapVillagerSummary(
                state = state,
                selectedVillager = selectedVillager,
                portraitSize = portraitSize,
                modifier = Modifier.weight(0.62f).fillMaxHeight(),
                onVillager = onVillager,
            )
        }
    }
}

@Composable
private fun MapFarmSummary(
    state: StardewState,
    modifier: Modifier = Modifier,
    onPlayer: () -> Unit,
) {
    val rawFarm = state.farmName.ifEmpty { state.saveName }.ifEmpty { "Farm" }
    val farm = if (rawFarm.endsWith("Farm", ignoreCase = true)) rawFarm else "$rawFarm Farm"
    Column(modifier = modifier.padding(horizontal = 3.dp)) {
        Text(
            text = "FARM",
            style = MaterialTheme.typography.labelSmall.copy(fontSize = 8.sp),
            fontWeight = FontWeight.Bold,
            color = MAP_REFERENCE_MUTED,
        )
        Text(
            text = farm,
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.SemiBold,
            color = MAP_REFERENCE_INK,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        MapMoneyLine(
            state = state,
            label = "Current funds",
            value = "%,dg".format(state.gold),
            showCoin = true,
        )
        MapMoneyLine(
            state = state,
            label = "Total earnings",
            value = "%,dg".format(state.totalEarnings),
            showCoin = false,
        )

        Row(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .clickable(role = Role.Button, onClick = onPlayer),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            state.sprites[StardewUi.PLAYER]?.let { player ->
                Image(
                    bitmap = player.image,
                    contentDescription = null,
                    modifier = Modifier.size(23.dp),
                    contentScale = ContentScale.Fit,
                    filterQuality = FilterQuality.None,
                )
                Spacer(modifier = Modifier.width(4.dp))
            }
            Text(
                text = state.playerName.ifEmpty { "Farmer" },
                style = MaterialTheme.typography.labelMedium,
                fontWeight = FontWeight.SemiBold,
                color = MAP_REFERENCE_INK,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1f, fill = false),
            )
            if (state.pet.name.isNotEmpty()) {
                Spacer(modifier = Modifier.width(5.dp))
                state.sprites[StardewUi.PET]?.let { pet ->
                    Image(
                        bitmap = pet.image,
                        contentDescription = null,
                        modifier = Modifier.size(19.dp),
                        contentScale = ContentScale.Fit,
                        filterQuality = FilterQuality.None,
                    )
                    Spacer(modifier = Modifier.width(3.dp))
                }
                Text(
                    text = state.pet.name,
                    style = MaterialTheme.typography.labelMedium,
                    color = MAP_REFERENCE_INK,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f, fill = false),
                )
            }
        }
    }
}

@Composable
private fun MapMoneyLine(
    state: StardewState,
    label: String,
    value: String,
    showCoin: Boolean,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall.copy(fontSize = 9.sp),
            color = MAP_REFERENCE_MUTED,
            modifier = Modifier.weight(1f),
            maxLines = 1,
        )
        if (showCoin) {
            state.sprites[StardewUi.COIN]?.let { coin ->
                Image(
                    bitmap = coin.image,
                    contentDescription = null,
                    modifier = Modifier.size(12.dp),
                    contentScale = ContentScale.Fit,
                    filterQuality = FilterQuality.None,
                )
            }
        }
        Text(
            text = value,
            style = MaterialTheme.typography.labelMedium,
            fontWeight = FontWeight.SemiBold,
            color = MAP_REFERENCE_INK,
            maxLines = 1,
        )
    }
}

@Composable
private fun MapVillagerSummary(
    state: StardewState,
    selectedVillager: StardewVillager?,
    portraitSize: androidx.compose.ui.unit.Dp,
    modifier: Modifier = Modifier,
    onVillager: (StardewVillager) -> Unit,
) {
    val villagers = remember(state.villagers) { state.villagers.sortedBy { it.name.lowercase() } }
    Column(modifier = modifier) {
        Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(
                text = "VILLAGERS",
                style = MaterialTheme.typography.labelSmall.copy(fontSize = 8.sp),
                fontWeight = FontWeight.Bold,
                color = MAP_REFERENCE_MUTED,
                modifier = Modifier.weight(1f),
            )
            selectedVillager?.let { villager ->
                Text(
                    text = listOf(villager.name, villager.location)
                        .filter { it.isNotEmpty() }
                        .joinToString(" - "),
                    style = MaterialTheme.typography.labelSmall.copy(fontSize = 9.sp),
                    color = MAP_REFERENCE_INK,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1.6f),
                    textAlign = TextAlign.End,
                )
            }
        }

        if (villagers.isEmpty()) {
            Box(
                modifier = Modifier.fillMaxWidth().height(portraitSize),
                contentAlignment = Alignment.CenterStart,
            ) {
                Text(
                    text = "Villager locations have not arrived yet.",
                    style = MaterialTheme.typography.labelSmall,
                    color = MAP_REFERENCE_MUTED,
                )
            }
        } else {
            LazyRow(
                modifier = Modifier.fillMaxWidth().height(portraitSize),
                horizontalArrangement = Arrangement.spacedBy(3.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                items(items = villagers, key = { villagerKey(it) }) { villager ->
                    MapVillagerPortrait(
                        state = state,
                        villager = villager,
                        selected = selectedVillager?.let(::villagerKey) == villagerKey(villager),
                        size = portraitSize,
                        onClick = { onVillager(villager) },
                    )
                }
            }
        }

        Box(
            modifier = Modifier
                .fillMaxWidth()
                .padding(vertical = 2.dp)
                .height(1.dp)
                .background(MAP_REFERENCE_LINE),
        )
        Row(
            modifier = Modifier.fillMaxWidth().weight(1f),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            MapFooterStat(
                label = "Season",
                value = state.season.ifEmpty { "—" },
                icon = state.sprites[StardewUi.SEASON],
                modifier = Modifier.weight(1f),
            )
            MapFooterDivider()
            MapFooterStat(
                label = "Day",
                value = mapDayLabel(state.weekday, state.day),
                modifier = Modifier.weight(1f),
            )
            MapFooterDivider()
            MapFooterStat(
                label = "Time",
                value = state.clock.replace("am", " am").replace("pm", " pm"),
                icon = state.sprites[StardewUi.TIME],
                modifier = Modifier.weight(1f),
            )
        }
    }
}

@Composable
private fun MapVillagerPortrait(
    state: StardewState,
    villager: StardewVillager,
    selected: Boolean,
    size: androidx.compose.ui.unit.Dp,
    onClick: () -> Unit,
) {
    val frame = state.frame(if (selected) StardewUi.SLOT_SELECTED else StardewUi.SLOT)
    val portrait = state.sprites["npc:${villager.npc}"]
    val base = Modifier.size(size)
    val painted = if (frame != null) {
        base.drawBehind { drawNineSlice(frame, this.size) }
    } else {
        base
            .clip(ThorTheme.shapes.small)
            .background(Color(0x33FFD080))
            .border(
                width = if (selected) 2.dp else 1.dp,
                color = if (selected) ThorTheme.colors.cursor else MAP_REFERENCE_BORDER,
                shape = ThorTheme.shapes.small,
            )
    }
    Box(
        modifier = painted
            .semantics { this.selected = selected }
            .clickable(role = Role.Button, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        if (portrait != null) {
            Image(
                bitmap = portrait.image,
                contentDescription = "Show ${villager.name} on the map",
                modifier = Modifier.fillMaxSize().padding(3.dp),
                contentScale = ContentScale.Crop,
                filterQuality = FilterQuality.None,
            )
        } else {
            Text(
                text = villager.name.take(1).uppercase().ifEmpty { "?" },
                style = MaterialTheme.typography.labelLarge,
                fontWeight = FontWeight.Bold,
                color = MAP_REFERENCE_INK,
            )
        }
        if (villager.onMap) {
            Box(
                modifier = Modifier
                    .align(Alignment.BottomEnd)
                    .padding(3.dp)
                    .size(5.dp)
                    .clip(ThorTheme.shapes.pill)
                    .background(if (selected) ThorTheme.colors.cursor else Color(0xFF4D8D27)),
            )
        }
    }
}

@Composable
private fun RowScope.MapFooterStat(
    label: String,
    value: String,
    modifier: Modifier = Modifier,
    icon: UiSprite? = null,
) {
    Row(
        modifier = modifier,
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.Center,
    ) {
        if (icon != null) {
            Image(
                bitmap = icon.image,
                contentDescription = null,
                modifier = Modifier.size(18.dp),
                contentScale = ContentScale.Fit,
                filterQuality = FilterQuality.None,
            )
            Spacer(modifier = Modifier.width(3.dp))
        }
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(
                text = label.uppercase(),
                style = MaterialTheme.typography.labelSmall.copy(fontSize = 7.sp),
                color = MAP_REFERENCE_MUTED,
                maxLines = 1,
            )
            Text(
                text = value,
                style = MaterialTheme.typography.labelMedium,
                fontWeight = FontWeight.SemiBold,
                color = MAP_REFERENCE_INK,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

@Composable
private fun RowScope.MapFooterDivider() {
    Box(
        modifier = Modifier
            .fillMaxHeight(0.72f)
            .width(1.dp)
            .background(MAP_REFERENCE_LINE),
    )
}

internal fun clampedMapZoom(value: Float): Float =
    if (value.isFinite()) value.coerceIn(MAP_MIN_ZOOM, MAP_MAX_ZOOM) else MAP_MIN_ZOOM

internal fun clampedMapTranslation(value: Float, zoom: Float, extent: Float): Float {
    if (!value.isFinite() || !extent.isFinite() || extent <= 0f) return 0f
    val limit = extent * (clampedMapZoom(zoom) - MAP_MIN_ZOOM) / 2f
    return value.coerceIn(-limit, limit)
}

internal fun transformedMapTranslation(
    current: Float,
    oldZoom: Float,
    newZoom: Float,
    gestureFromCenter: Float,
    pan: Float,
    extent: Float,
): Float {
    val before = clampedMapZoom(oldZoom)
    val after = clampedMapZoom(newZoom)
    val ratio = after / before
    val raw = current * ratio + gestureFromCenter * (1f - ratio) + pan
    return clampedMapTranslation(raw, after, extent)
}

internal fun focusedMapTranslation(fraction: Float, zoom: Float, extent: Float): Float {
    if (!fraction.isFinite() || !extent.isFinite() || extent <= 0f) return 0f
    val raw = (0.5f - fraction.coerceIn(0f, 1f)) * extent * clampedMapZoom(zoom)
    return clampedMapTranslation(raw, zoom, extent)
}

internal fun mapMarkerScreenCoordinate(
    fraction: Float,
    extent: Float,
    zoom: Float,
    translation: Float,
): Float = extent * (0.5f + clampedMapZoom(zoom) * (fraction.coerceIn(0f, 1f) - 0.5f)) +
    translation

private fun plausibleMapPoint(across: Float, down: Float): Boolean =
    across > -OFF_MAP && across < 1f + OFF_MAP && down > -OFF_MAP && down < 1f + OFF_MAP

private fun villagerKey(villager: StardewVillager): String =
    villager.npc.ifEmpty { villager.name }

private fun mapDayLabel(weekday: String, day: Int): String {
    if (day <= 0) return "—"
    val short = weekday.trim().take(3).let { if (it.isEmpty()) "Day" else "$it." }
    return "$short $day"
}

private const val MAP_MIN_ZOOM = 1f

private const val MAP_MAX_ZOOM = 4f

private const val MAP_SELECTED_ZOOM = 2f

private const val MAP_ZOOM_STEP = 0.5f

private val MAP_FRAME_INSET = 5.dp

private val MAP_FOOTER_MIN_HEIGHT = 86.dp

private val MAP_REFERENCE_INK = Color(0xFF321B0F)

private val MAP_REFERENCE_MUTED = Color(0xFF70451F)

private val MAP_REFERENCE_BORDER = Color(0xFFA95F16)

private val MAP_REFERENCE_LINE = Color(0x55A95F16)

private const val OFF_MAP = 0.1f

private val MACHINE_LIST_HEIGHT = 120.dp

private val BUNDLE_NAME_WIDTH = 130.dp

private fun skillText(level: Int): String =
    if (level >= MAX_SKILL_LEVEL) "Lv $level · MAX" else "Lv $level"

private const val MAX_SKILL_LEVEL = 10

@Composable
private fun CalendarCard(state: StardewState, fill: Boolean = false) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens
    if (state.calendar.days.isEmpty()) {
        Card(fill = fill) {
            SectionTitle("Calendar")
            EmptySection("The season calendar has not arrived from the game yet.")
        }
        return
    }
    val today = state.calendar.today
    val known = remember(state.calendar) { state.calendar.days.associateBy { it.day } }
    var selectedDay by rememberSaveable(state.calendar.season, today) { mutableIntStateOf(today) }
    val selected = known[selectedDay]
    val selectedCart = cartDay(selectedDay)
    val birthdays = state.calendar.days.count { it.names.isNotEmpty() }
    val festivals = state.calendar.days.count { it.festival.isNotEmpty() }

    Card(fill = fill) {
        SectionTitle(
            state.calendar.season.ifEmpty { "Calendar" },
            "Day $today of $SEASON_DAYS · $birthdays birthdays · $festivals festivals",
        )

        CalendarLegend()

        Row(
            modifier = Modifier.fillMaxWidth().padding(top = dimens.spacingTiny),
            horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
        ) {
            WEEKDAYS.forEach { weekday ->
                Text(
                    text = weekday.uppercase(),
                    style = MaterialTheme.typography.labelSmall,
                    fontWeight = FontWeight.SemiBold,
                    color = colors.onSurfaceVariant,
                    textAlign = TextAlign.Center,
                    modifier = Modifier.weight(1f),
                )
            }
        }

        Column(
            modifier = Modifier
                .fillMaxWidth()
                .then(if (fill) Modifier.weight(1f) else Modifier.height(210.dp))
                .padding(top = 3.dp),
            verticalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
        ) {
            (1..SEASON_DAYS).chunked(7).forEach { week ->
                Row(
                    modifier = Modifier.fillMaxWidth().weight(1f),
                    horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
                ) {
                    week.forEach { day ->
                        CalendarDayCell(
                            state = state,
                            day = day,
                            entry = known[day],
                            today = day == today,
                            selected = day == selectedDay,
                            cart = cartDay(day),
                            modifier = Modifier.weight(1f),
                            onClick = { selectedDay = day },
                        )
                    }
                }
            }
        }

        Rule()
        CalendarDetail(
            state = state,
            day = selectedDay,
            entry = selected,
            cart = selectedCart,
            today = today,
        )
    }
}

@Composable
private fun CalendarLegend() {
    val colors = ThorTheme.colors
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(ThorTheme.dimens.spacing),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CalendarLegendItem("Today", colors.cursor)
        CalendarLegendItem("Birthday", colors.surfaceHighest)
        CalendarLegendItem("Festival", colors.cursor.copy(alpha = 0.45f))
        CalendarLegendItem("Merchant", colors.outline.copy(alpha = 0.45f))
    }
}

@Composable
private fun RowScope.CalendarLegendItem(label: String, color: Color) {
    Box(
        modifier = Modifier
            .size(7.dp)
            .clip(ThorTheme.shapes.small)
            .background(color),
    )
    Text(
        text = label,
        style = MaterialTheme.typography.labelSmall,
        color = ThorTheme.colors.onSurfaceVariant,
    )
}

@Composable
private fun CalendarDayCell(
    state: StardewState,
    day: Int,
    entry: StardewCalendarDay?,
    today: Boolean,
    selected: Boolean,
    cart: Boolean,
    modifier: Modifier = Modifier,
    onClick: () -> Unit,
) {
    val colors = ThorTheme.colors
    val birthday = !entry?.names.isNullOrEmpty()
    val festival = !entry?.festival.isNullOrEmpty()
    val cellShape = ThorTheme.shapes.small
    Box(
        modifier = modifier
            .fillMaxHeight()
            .clip(cellShape)
            .background(
                when {
                    selected -> colors.cursor.copy(alpha = 0.18f)
                    today -> colors.surfaceHighest
                    festival -> colors.cursor.copy(alpha = 0.09f)
                    birthday -> colors.surfaceHighest.copy(alpha = 0.58f)
                    cart -> colors.outline.copy(alpha = 0.07f)
                    else -> Color.Transparent
                },
            )
            .border(
                width = if (selected || today) 1.dp else 0.5.dp,
                color = when {
                    selected -> colors.cursor.copy(alpha = 0.85f)
                    today -> colors.cursor.copy(alpha = 0.55f)
                    else -> colors.outline.copy(alpha = 0.20f)
                },
                shape = cellShape,
            )
            .clickable(onClick = onClick)
            .padding(4.dp),
    ) {
        Text(
            text = day.toString(),
            style = MaterialTheme.typography.labelSmall,
            fontWeight = if (today) FontWeight.Bold else FontWeight.Medium,
            color = when {
                today -> colors.cursor
                day < state.calendar.today -> colors.onSurfaceVariant.copy(alpha = 0.55f)
                else -> colors.onSurface
            },
            modifier = Modifier.align(Alignment.TopStart),
        )

        if (today) {
            Text(
                text = "TODAY",
                style = MaterialTheme.typography.labelSmall.copy(fontSize = 7.sp),
                fontWeight = FontWeight.Bold,
                color = colors.cursor,
                modifier = Modifier.align(Alignment.TopEnd),
            )
        }

        val portrait = entry?.npc?.takeIf { it.isNotEmpty() }?.let { state.sprites["npc:$it"] }
        Column(
            modifier = Modifier.align(Alignment.Center).padding(top = 5.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            when {
                portrait != null -> Image(
                    bitmap = portrait.image,
                    contentDescription = entry.names,
                    modifier = Modifier.size(29.dp).clip(ThorTheme.shapes.small),
                    contentScale = ContentScale.Crop,
                    filterQuality = FilterQuality.None,
                )

                festival -> Text(
                    text = "★",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    color = colors.cursor,
                )

                cart -> Text(
                    text = "CART",
                    style = MaterialTheme.typography.labelSmall.copy(fontSize = 8.sp),
                    fontWeight = FontWeight.Bold,
                    color = colors.onSurfaceVariant,
                )
            }

            val eventName = when {
                festival -> entry!!.festival
                birthday -> entry!!.names.substringBefore(',')
                cart -> "Merchant"
                else -> ""
            }
            if (eventName.isNotEmpty()) {
                Text(
                    text = eventName,
                    style = MaterialTheme.typography.labelSmall.copy(fontSize = 8.sp, lineHeight = 9.sp),
                    color = if (festival) colors.cursor else colors.onSurfaceVariant,
                    textAlign = TextAlign.Center,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }

            val eventType = when {
                birthday -> "Birthday"
                festival -> "Festival"
                cart -> "Cindersap Forest"
                else -> ""
            }
            if (eventType.isNotEmpty()) {
                Text(
                    text = eventType,
                    style = MaterialTheme.typography.labelSmall.copy(
                        fontSize = 7.sp,
                        lineHeight = 8.sp,
                    ),
                    fontWeight = if (birthday) FontWeight.SemiBold else FontWeight.Normal,
                    color = when {
                        birthday -> colors.cursor
                        festival -> colors.onSurfaceVariant
                        else -> colors.onSurfaceVariant.copy(alpha = 0.78f)
                    },
                    textAlign = TextAlign.Center,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }

        if (today) {
            Box(
                modifier = Modifier
                    .align(Alignment.BottomCenter)
                    .fillMaxWidth()
                    .height(2.dp)
                    .background(colors.cursor),
            )
        }
    }
}

@Composable
private fun CalendarDetail(
    state: StardewState,
    day: Int,
    entry: StardewCalendarDay?,
    cart: Boolean,
    today: Int,
) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens
    val headline = when {
        !entry?.festival.isNullOrEmpty() -> entry!!.festival
        !entry?.names.isNullOrEmpty() -> entry!!.names
        cart -> "Travelling merchant"
        else -> "No scheduled events"
    }
    val details = buildList {
        if (!entry?.names.isNullOrEmpty()) add("Birthday")
        if (!entry?.festivalWhere.isNullOrEmpty()) add(entry!!.festivalWhere)
        if (!entry?.festivalWhen.isNullOrEmpty()) add(entry!!.festivalWhen)
        if (cart) add("Merchant in Cindersap Forest")
    }.joinToString(" · ")
    val distance = when (val days = day - today) {
        0 -> "Today"
        1 -> "Tomorrow"
        in 2..Int.MAX_VALUE -> "In $days days"
        else -> "${-days} days ago"
    }

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(ThorTheme.shapes.small)
            .background(colors.surfaceHighest.copy(alpha = 0.34f))
            .padding(horizontal = dimens.spacingSmall, vertical = dimens.spacingTiny),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(dimens.spacingSmall),
    ) {
        entry?.npc?.takeIf { it.isNotEmpty() }?.let { npc -> Portrait(state, npc, 34.dp) }
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = "${weekdayFull(day)}, ${state.calendar.season} $day · $distance",
                style = MaterialTheme.typography.labelSmall,
                color = colors.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                text = headline,
                style = MaterialTheme.typography.bodyMedium.enlarged(),
                fontWeight = FontWeight.Medium,
                color = colors.onSurface,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            if (details.isNotEmpty()) {
                Text(
                    text = details,
                    style = MaterialTheme.typography.labelSmall,
                    color = colors.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
    }
}

private const val SEASON_DAYS = 28

private val WEEKDAYS = listOf("Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun")

private val WEEKDAYS_FULL =
    listOf("Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday")

private fun weekday(day: Int): String = WEEKDAYS[(day - 1).mod(WEEKDAYS.size)]

private fun weekdayFull(day: Int): String = WEEKDAYS_FULL[(day - 1).mod(WEEKDAYS_FULL.size)]

private fun cartDay(day: Int): Boolean = weekday(day) == "Fri" || weekday(day) == "Sun"

@Composable
private fun OrdersCard(state: StardewState, fill: Boolean = false) {
    val colors = ThorTheme.colors

    Card(fill = fill) {
        SectionTitle("Special orders", state.orders.size.toString())
        state.orders.forEachIndexed { index, order ->
            if (index > 0) Rule()
            Column(modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = order.title,
                        style = MaterialTheme.typography.bodyMedium.enlarged(),
                        fontWeight = FontWeight.Medium,
                        color = colors.onSurface,
                        modifier = Modifier.weight(1f),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                    if (order.daysLeft >= 0) {
                        Text(
                            text = if (order.daysLeft == 1) "last day" else "${order.daysLeft} days",
                            style = MaterialTheme.typography.labelSmall.enlarged(),
                            fontWeight = FontWeight.Medium,
                            color = if (order.daysLeft <= 1) colors.error else colors.onSurfaceVariant,
                            modifier = Modifier
                                .clip(ThorTheme.shapes.pill)
                                .background(
                                    if (order.daysLeft <= 1) {
                                        colors.error.copy(alpha = 0.14f)
                                    } else {
                                        colors.surfaceHighest
                                    },
                                )
                                .padding(horizontal = 8.dp, vertical = 3.dp),
                        )
                    }
                }
                if (order.objective.isNotEmpty()) {
                    Text(
                        text = order.objective,
                        style = MaterialTheme.typography.bodySmall.enlarged(),
                        color = colors.onSurfaceVariant,
                        modifier = Modifier.padding(top = 2.dp),
                    )
                }
            }
        }
    }
}

@Composable
private fun GiftsCard(state: StardewState, fill: Boolean = false) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens

    Card(accent = true, fill = fill) {
        SectionTitle("Gifts in your bag", state.giftableNow.size.toString())
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .then(if (fill) Modifier.weight(1f) else Modifier)
                .verticalScroll(rememberScrollState()),
        ) {
            state.giftableNow.forEach { villager ->
                Row(
                    modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
                ) {
                    Text(
                        text = villager.name,
                        style = MaterialTheme.typography.bodyMedium.enlarged(),
                        color = colors.onSurface,
                        modifier = Modifier.weight(1f),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )

                    villager.loves.filter { it.carried }.take(4).forEach { gift ->
                        Icon(state.sprites[gift.id], size = 22.dp)
                    }
                }
            }
        }
    }
}

@Composable
private fun CraftingCard(state: StardewState, fill: Boolean = false) {
    val dimens = ThorTheme.dimens
    val colors = ThorTheme.colors

    Card(fill = fill) {
        SectionTitle(
            if (state.cooking.isEmpty()) "Can make now" else "Can make and cook now",
            "${state.recipes.size + state.cooking.size} available",
        )
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .then(if (fill) Modifier.weight(1f) else Modifier.heightIn(max = 160.dp))
                .verticalScroll(rememberScrollState()),
        ) {
            (state.recipes + state.cooking).chunked(2).forEach { pair ->
                Row(
                    modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp),
                    horizontalArrangement = Arrangement.spacedBy(dimens.spacingSmall),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    pair.forEach { recipe ->
                        Row(
                            modifier = Modifier.weight(1f),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
                        ) {
                            Icon(state.sprites[recipe.id], size = 18.dp)
                            Text(
                                text = recipe.name,
                                style = MaterialTheme.typography.bodySmall,
                                color = colors.onSurfaceVariant,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                            )
                        }
                    }
                    repeat(2 - pair.size) { Box(modifier = Modifier.weight(1f)) }
                }
            }
        }
    }
}

private const val LIST_TEXT_SCALE = 1.25f

private fun androidx.compose.ui.text.TextStyle.enlarged(): androidx.compose.ui.text.TextStyle =
    copy(
        fontSize = fontSize * LIST_TEXT_SCALE,
        lineHeight = if (lineHeight.isSpecified) lineHeight * LIST_TEXT_SCALE else lineHeight,
    )

@Composable
private fun MachinesCard(state: StardewState, fill: Boolean = false) {
    val colors = ThorTheme.colors
    val ready = state.machinesByUrgency.filter { it.ready }
    val working = state.machinesByUrgency.filter { !it.ready }

    Card(accent = ready.isNotEmpty(), fill = fill) {
        SectionTitle(
            "Machines",
            if (ready.isNotEmpty()) "${ready.size} ready" else "${working.size} working",
        )

        Column(
            modifier = Modifier
                .fillMaxWidth()
                .then(
                    if (fill) Modifier.weight(1f) else Modifier.heightIn(max = MACHINE_LIST_HEIGHT),
                )
                .verticalScroll(rememberScrollState()),
        ) {
            ready.forEach { machine -> MachineRow(state, machine, ready = true) }

            if (working.isNotEmpty()) {
                LineRow(left = "Still working", right = "${working.size}", muted = true)
                working.forEach { machine -> MachineRow(state, machine, ready = false) }
            }
        }
    }
}

@Composable
private fun MachineRow(state: StardewState, machine: StardewMachine, ready: Boolean) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens

    Row(
        modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
    ) {

        Icon(state.sprites[machine.id], size = 18.dp)
        Icon(state.sprites[machine.outputId], size = 18.dp)
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = machine.output.ifEmpty { machine.name },
                style = MaterialTheme.typography.bodyMedium,
                color = if (ready) colors.onSurface else colors.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            if (machine.location.isNotEmpty()) {
                Text(
                    text = machine.location,
                    style = MaterialTheme.typography.labelSmall,
                    color = colors.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
        Text(
            text = if (ready) "Ready" else machineTime(machine.minutes) +
                readyAt(state.timeOfDay, machine.minutes),
            style = MaterialTheme.typography.bodySmall,
            fontWeight = if (ready) FontWeight.Medium else FontWeight.Normal,
            color = if (ready) colors.cursor else colors.onSurfaceVariant,
        )
    }
}

private fun readyAt(now: Int, minutes: Int): String {
    if (minutes <= 0 || minutes > MINUTES_IN_A_DAY) return ""
    val nowMinutes = (now / 100) * 60 + now % 100
    val then = nowMinutes + minutes
    if (then >= MINUTES_IN_A_DAY + BEDTIME_MINUTES) return ""
    val hours = (then / 60) % 24
    val rest = then % 60
    val suffix = if (hours < 12) "am" else "pm"
    val shown = if (hours % 12 == 0) 12 else hours % 12
    return "  %d:%02d%s".format(shown, rest, suffix)
}

private const val MINUTES_IN_A_DAY = 24 * 60
private const val BEDTIME_MINUTES = 2 * 60

private fun machineTime(minutes: Int): String = when {
    minutes <= 0 -> "Soon"
    minutes < 60 -> "${minutes}m"
    minutes < 1200 -> "${minutes / 60}h"
    else -> "${(minutes + 1199) / 1200}d"
}

@Composable
private fun AnimalsCard(state: StardewState, fill: Boolean = false) {
    val unpetted = state.animalsUnpetted
    val produce = state.animalsWithProduce
    if (unpetted.isEmpty() && produce.isEmpty()) return

    val dimens = ThorTheme.dimens
    Card(fill = fill) {
        SectionTitle(
            "Animals",
            listOfNotNull(
                unpetted.takeIf { it.isNotEmpty() }?.let { "${it.size} need care" },
                produce.takeIf { it.isNotEmpty() }?.let { "${it.size} collect" },
            ).joinToString(" / "),
        )

        if (unpetted.isNotEmpty()) {
            if (unpetted.size <= 6) {
                unpetted.forEach { animal ->
                    LineRow(left = animal.name, right = animal.type, muted = true)
                }
            }
        }

        if (produce.isNotEmpty()) {
            Row(
                modifier = Modifier.fillMaxWidth().padding(top = 2.dp),
                horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
            ) {
                produce.take(12).forEach { animal ->
                    Icon(state.sprites[animal.produceId], size = 20.dp)
                }
            }
        }
    }
}

@Composable
private fun BundlesCard(state: StardewState, fill: Boolean = false) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens
    if (state.bundles.isEmpty()) {
        Card(fill = fill) {
            SectionTitle("Community Center")
            EmptySection("No active Community Center bundles were found.")
        }
        return
    }
    val carried = state.bundleItemsCarried
    val rooms = state.bundlesByProgress.groupBy { it.room }.entries.sortedBy { entry ->
        ROOM_ORDER.indexOf(entry.key).let { if (it < 0) Int.MAX_VALUE else it }
    }
    val completed = state.bundles.count { it.required > 0 && it.have >= it.required }

    Card(accent = carried.isNotEmpty(), fill = fill) {
        SectionTitle(
            "Community Center",
            "$completed/${state.bundles.size} bundles · ${carried.size} matches in bag",
        )

        if (carried.isNotEmpty()) {
            Text(
                text = "READY TO DONATE",
                style = MaterialTheme.typography.labelSmall,
                fontWeight = FontWeight.SemiBold,
                color = colors.cursor,
                modifier = Modifier.padding(bottom = 2.dp),
            )
            carried.take(6).chunked(3).forEach { matches ->
                Row(
                    modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(dimens.spacingSmall),
                ) {
                    matches.forEach { (bundle, item) ->
                        Row(
                            modifier = Modifier.weight(1f),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
                        ) {
                            Icon(state.sprites[item.id], size = 20.dp)
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = if (item.count > 1) "${item.name} ×${item.count}" else item.name,
                                    style = MaterialTheme.typography.bodySmall.enlarged(),
                                    color = colors.onSurface,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis,
                                )
                                Text(
                                    text = bundle.name,
                                    style = MaterialTheme.typography.labelSmall,
                                    color = colors.cursor,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis,
                                )
                            }
                        }
                    }
                    repeat(3 - matches.size) { Spacer(modifier = Modifier.weight(1f)) }
                }
            }
            Rule()
        }

        Column(
            modifier = Modifier
                .fillMaxWidth()
                .then(if (fill) Modifier.weight(1f) else Modifier),
        ) {
            rooms.chunked(3).forEachIndexed { rowIndex, roomRow ->
                Row(
                    modifier = Modifier.fillMaxWidth().height(IntrinsicSize.Min),
                    horizontalArrangement = Arrangement.spacedBy(dimens.spacingSmall),
                ) {
                    roomRow.forEachIndexed { column, room ->
                        if (column > 0) VerticalRule()
                        BundleRoom(
                            state = state,
                            room = room.key,
                            bundles = room.value,
                            modifier = Modifier.weight(1f),
                        )
                    }
                    repeat(3 - roomRow.size) { Spacer(modifier = Modifier.weight(1f)) }
                }
                if (rowIndex < rooms.chunked(3).lastIndex) Rule()
            }
        }
    }
}

@Composable
private fun BundleRoom(
    state: StardewState,
    room: String,
    bundles: List<StardewBundle>,
    modifier: Modifier = Modifier,
) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens
    val have = bundles.sumOf { it.have.coerceAtMost(it.required) }
    val required = bundles.sumOf { it.required }
    val fraction = if (required <= 0) 0f else have.toFloat() / required

    Column(modifier = modifier.padding(vertical = 2.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                text = room.uppercase(),
                style = MaterialTheme.typography.labelSmall,
                fontWeight = FontWeight.Bold,
                color = colors.onSurface,
                modifier = Modifier.weight(1f),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                text = "$have/$required",
                style = MaterialTheme.typography.labelSmall,
                color = if (have >= required && required > 0) colors.cursor else colors.onSurfaceVariant,
            )
        }
        ProgressLine(fraction, Modifier.padding(top = 2.dp, bottom = 3.dp))

        bundles.forEach { bundle ->
            val done = bundle.required > 0 && bundle.have >= bundle.required
            Row(
                modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
            ) {
                Text(
                    text = bundle.name,
                    style = MaterialTheme.typography.bodySmall,
                    color = if (done) colors.onSurfaceVariant else colors.onSurface,
                    modifier = Modifier.weight(1f),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                if (!done) {
                    bundle.missing.take(3).forEach { item ->
                        if (item.id.isNotEmpty()) Icon(state.sprites[item.id], size = 16.dp)
                    }
                }
                Text(
                    text = if (done) "Done" else "${bundle.have}/${bundle.required}",
                    style = MaterialTheme.typography.labelSmall,
                    fontWeight = if (done) FontWeight.Medium else FontWeight.Normal,
                    color = if (done) colors.cursor else colors.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
private fun ProgressLine(fraction: Float, modifier: Modifier = Modifier) {
    val colors = ThorTheme.colors
    Box(
        modifier = modifier
            .fillMaxWidth()
            .height(4.dp)
            .clip(ThorTheme.shapes.pill)
            .background(colors.surfaceHighest),
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth(fraction.coerceIn(0f, 1f))
                .height(4.dp)
                .clip(ThorTheme.shapes.pill)
                .background(colors.cursor),
        )
    }
}

private val ROOM_ORDER = listOf(
    "Crafts Room",
    "Pantry",
    "Fish Tank",
    "Boiler Room",
    "Bulletin Board",
    "Vault",
    "Abandoned Joja Mart",
)

@Composable
private fun Rule() {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = ThorTheme.dimens.spacingTiny)
            .height(1.dp)
            .background(ThorTheme.colors.outline.copy(alpha = 0.35f)),
    )
}

@Composable
private fun Icon(sprite: UiSprite?, size: androidx.compose.ui.unit.Dp) {
    if (sprite == null) return
    Image(
        bitmap = sprite.image,
        contentDescription = null,
        modifier = Modifier.size(size),
        contentScale = ContentScale.Fit,
        filterQuality = FilterQuality.None,
    )
}

@Composable
private fun SkillsCard(state: StardewState, fill: Boolean = false) {
    Card(fill = fill) {
        SectionTitle(
            "Skills",
            "${state.farming + state.mining + state.foraging + state.fishing + state.combat} levels",
        )
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(ThorTheme.dimens.spacingSmall),
        ) {

            Stat("Farm", skillText(state.farming), Modifier.weight(1f))
            Stat("Mine", skillText(state.mining), Modifier.weight(1f))
            Stat("Forage", skillText(state.foraging), Modifier.weight(1f))
            Stat("Fish", skillText(state.fishing), Modifier.weight(1f))
            Stat("Combat", skillText(state.combat), Modifier.weight(1f))
        }
    }
}

@Composable
private fun Meter(
    label: String,
    value: Int,
    max: Int,
    tint: androidx.compose.ui.graphics.Color,
    modifier: Modifier = Modifier,
) {
    val colors = ThorTheme.colors
    val fraction = if (max <= 0) 0f else (value.toFloat() / max).coerceIn(0f, 1f)
    Column(modifier = modifier) {
        Row(modifier = Modifier.fillMaxWidth()) {
            Text(
                text = label.uppercase(),
                style = MaterialTheme.typography.labelSmall,
                color = colors.onSurfaceVariant,
                modifier = Modifier.weight(1f),
            )
            Text(
                text = "$value/$max",
                style = MaterialTheme.typography.labelSmall,
                color = colors.onSurfaceVariant,
            )
        }
        Box(
            modifier = Modifier
                .padding(top = 2.dp)
                .fillMaxWidth()
                .height(6.dp)
                .clip(ThorTheme.shapes.pill)
                .background(colors.surfaceHighest),
        ) {
            Box(
                modifier = Modifier
                    .fillMaxWidth(fraction)
                    .height(6.dp)
                    .clip(ThorTheme.shapes.pill)
                    .background(tint),
            )
        }
    }
}

@Composable
private fun Card(
    accent: Boolean = false,

    fill: Boolean = false,
    content: @Composable ColumnScope.() -> Unit,
) {
    if (LocalJoinedPanel.current) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .then(if (fill) Modifier.fillMaxHeight() else Modifier),
            content = content,
        )
        return
    }

    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens
    val state by SecondScreen.stardew.collectAsStateWithLifecycle()
    val prefs by StardewPrefs.state.collectAsStateWithLifecycle()
    val panel = state.sprites[StardewUi.PANEL]

    val panelArt = if (prefs.gameFrames) panel else null
    val stretch = if (fill) Modifier.fillMaxHeight() else Modifier
    val painted = if (panelArt != null) {
        Modifier
            .fillMaxWidth()
            .then(stretch)
            .drawBehind { drawNineSlice(panelArt, size) }

            .padding(
                horizontal = panelArt.left.dp.coerceAtLeast(dimens.spacing),
                vertical = panelArt.top.dp.coerceAtLeast(dimens.spacingSmall),
            )
    } else {
        Modifier
            .fillMaxWidth()
            .then(stretch)
            .thorSurface(
                shape = ThorTheme.shapes.panel,
                color = if (accent) colors.cursor.copy(alpha = ACCENT_FILL) else colors.surface,
                level = SurfaceLevel.RAISED,
            )
            .padding(dimens.spacing)
    }

    Column(modifier = painted, content = content)
}

@Composable
private fun SectionTitle(text: String, summary: String = "") {
    val colors = ThorTheme.colors
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(bottom = ThorTheme.dimens.spacingSmall),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = text.uppercase(),
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.SemiBold,
            color = colors.onSurfaceVariant,
            modifier = Modifier.weight(1f),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        if (summary.isNotEmpty()) {
            Text(
                text = summary,
                style = MaterialTheme.typography.labelSmall,
                fontWeight = FontWeight.Medium,
                color = colors.cursor,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

@Composable
private fun Stat(
    label: String,
    value: String,
    modifier: Modifier = Modifier,

    icon: UiSprite? = null,
) {
    val colors = ThorTheme.colors
    Column(modifier = modifier) {
        Text(
            text = label.uppercase(),
            style = MaterialTheme.typography.labelSmall,
            color = colors.onSurfaceVariant,
        )
        Row(verticalAlignment = Alignment.CenterVertically) {
            if (icon != null) {
                Image(
                    bitmap = icon.image,
                    contentDescription = null,
                    modifier = Modifier.size(16.dp).padding(end = 4.dp),
                    contentScale = ContentScale.Fit,
                    filterQuality = FilterQuality.None,
                )
            }
            Text(
                text = value,
                style = MaterialTheme.typography.bodyLarge,
                fontWeight = FontWeight.Medium,
                color = colors.onSurface,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

@Composable
private fun Hearts(filled: Int, max: Int, full: UiSprite?, empty: UiSprite?) {

    if (full == null || empty == null) {
        Text(
            text = "$filled/$max",
            style = MaterialTheme.typography.bodyMedium.enlarged(),
            color = ThorTheme.colors.onSurfaceVariant,
        )
        return
    }
    Row(horizontalArrangement = Arrangement.spacedBy(1.dp)) {
        repeat(max.coerceIn(0, 14)) { index ->
            Image(
                bitmap = (if (index < filled) full else empty).image,
                contentDescription = null,
                modifier = Modifier.size(18.dp),
                contentScale = ContentScale.Fit,
                filterQuality = FilterQuality.None,
            )
        }
    }
}

@Composable
private fun LineRow(
    left: String,
    right: String,
    urgent: Boolean = false,
    muted: Boolean = false,
) {
    val colors = ThorTheme.colors
    Row(
        modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = left,
            style = MaterialTheme.typography.bodyMedium,
            color = when {
                urgent -> colors.error
                muted -> colors.onSurfaceVariant
                else -> colors.onSurface
            },
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f),
        )
        if (right.isNotEmpty()) {
            Text(
                text = right,
                style = MaterialTheme.typography.bodySmall,
                color = if (urgent) colors.error else colors.onSurfaceVariant,
                maxLines = 1,
            )
        }
    }
}

private const val ACCENT_FILL = 0.16f
