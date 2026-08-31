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

/** Community Center progress using the same parchment, divider, and large-row language as the new pages. */
@Composable
fun StardewBundlesReferencePage(
    state: StardewState,
    modifier: Modifier = Modifier,
) {
    val rooms = remember(state.bundles) { referenceBundleRooms(state.bundles) }
    val carried = state.bundleItemsCarried.size
    val missing = state.bundles.sumOf { bundle ->
        (bundle.required - bundle.have).coerceAtLeast(0)
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .background(BUNDLE_PARCHMENT.copy(alpha = 0.30f)),
    ) {
        BundlePageHeader(
            state = state,
            roomCount = rooms.size,
            bundleCount = state.bundles.size,
            missing = missing,
            carried = carried,
        )
        BundleRule(strong = true)

        if (rooms.isEmpty()) {
            BundleEmptyState(
                state = state,
                modifier = Modifier.fillMaxWidth().weight(1f),
            )
        } else {
            LazyColumn(
                modifier = Modifier.fillMaxWidth().weight(1f),
                contentPadding = PaddingValues(bottom = 7.dp),
            ) {
                rooms.forEach { room ->
                    item(key = "room:${room.name}") {
                        BundleRoomHeader(state, room)
                    }
                    items(
                        items = room.bundles,
                        key = { bundle -> "bundle:${room.name}:${bundle.name}" },
                    ) { bundle ->
                        BundleProgressRow(state, bundle)
                    }
                }
            }
        }

        BundleRule(strong = true)
        BundleDateFooter(state)
    }
}

@Composable
private fun BundlePageHeader(
    state: StardewState,
    roomCount: Int,
    bundleCount: Int,
    missing: Int,
    carried: Int,
) {
    Row(
        modifier = Modifier.fillMaxWidth().height(58.dp).padding(horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(9.dp),
    ) {
        BundleSprite(state.sprites[StardewUi.TAB_SKILLS], 34.dp)
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = "Community Center",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
                color = BUNDLE_INK,
                maxLines = 1,
            )
            Text(
                text = "$roomCount rooms • $bundleCount bundles • $missing donations left",
                style = MaterialTheme.typography.bodySmall,
                color = BUNDLE_MUTED,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        if (carried > 0) {
            BundleSprite(state.sprites[StardewUi.CHECK_ON], 24.dp)
            Column(horizontalAlignment = Alignment.End) {
                Text(
                    text = carried.toString(),
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.Bold,
                    color = BUNDLE_READY,
                )
                Text(
                    text = "in backpack",
                    style = MaterialTheme.typography.labelSmall,
                    color = BUNDLE_READY,
                    maxLines = 1,
                )
            }
        }
    }
}

@Composable
private fun BundleRoomHeader(state: StardewState, room: ReferenceBundleRoom) {
    val firstSprite = room.bundles
        .asSequence()
        .flatMap { it.missing.asSequence() }
        .firstNotNullOfOrNull { item ->
            val spriteId = item.id.ifEmpty { StardewUi.COIN }
            state.sprites[spriteId]
        }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(48.dp)
            .background(BUNDLE_HEADER_WASH)
            .padding(horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        BundleSprite(firstSprite ?: state.sprites[StardewUi.TAB_SKILLS], 28.dp)
        Spacer(Modifier.width(8.dp))
        Text(
            text = room.name,
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.Bold,
            color = BUNDLE_INK,
            modifier = Modifier.weight(1f),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            text = "${room.have}/${room.required}",
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.Bold,
            color = if (room.have >= room.required && room.required > 0) {
                BUNDLE_READY
            } else {
                BUNDLE_MUTED
            },
        )
    }
    BundleProgressBar(room.have, room.required)
    BundleRule()
}

@Composable
private fun BundleProgressRow(state: StardewState, bundle: StardewBundle) {
    val needed = (bundle.required - bundle.have).coerceAtLeast(0)
    val carried = bundle.missing.count { it.carried }
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(if (carried > 0) BUNDLE_READY_WASH else BUNDLE_ROW_WASH)
            .padding(horizontal = 12.dp, vertical = 8.dp),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = bundle.name,
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.Bold,
                    color = BUNDLE_INK,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                Text(
                    text = when {
                        needed == 0 -> "Complete"
                        carried > 0 -> "$needed needed • $carried ready in backpack"
                        else -> "$needed more donation${if (needed == 1) "" else "s"} needed"
                    },
                    style = MaterialTheme.typography.bodySmall,
                    color = if (carried > 0 || needed == 0) BUNDLE_READY else BUNDLE_MUTED,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            Text(
                text = "${bundle.have}/${bundle.required}",
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.Bold,
                color = BUNDLE_MUTED,
            )
        }
        Spacer(Modifier.height(6.dp))
        if (bundle.missing.isEmpty()) {
            Text(
                text = "No remaining items.",
                style = MaterialTheme.typography.bodySmall,
                color = BUNDLE_MUTED,
            )
        } else {
            LazyRow(
                modifier = Modifier.fillMaxWidth().height(52.dp),
                horizontalArrangement = Arrangement.spacedBy(7.dp),
            ) {
                items(
                    items = bundle.missing,
                    key = { item -> "${item.id}:${item.name}:${item.count}:${item.quality}" },
                ) { item ->
                    BundleItemChip(state, item)
                }
            }
        }
    }
    BundleRule()
}

@Composable
private fun BundleItemChip(state: StardewState, item: StardewBundleItem) {
    val sprite = state.sprites[item.id.ifEmpty { StardewUi.COIN }]
    Row(
        modifier = Modifier
            .width(148.dp)
            .fillMaxHeight()
            .clip(ThorTheme.shapes.small)
            .background(if (item.carried) BUNDLE_CARRIED else BUNDLE_ITEM_WASH)
            .border(1.dp, if (item.carried) BUNDLE_READY else BUNDLE_RULE, ThorTheme.shapes.small)
            .padding(horizontal = 6.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        BundleSpriteSlot(state, sprite, 40.dp)
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = item.name,
                style = MaterialTheme.typography.bodySmall,
                fontWeight = FontWeight.SemiBold,
                color = BUNDLE_INK,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                text = bundleItemDetail(item),
                style = MaterialTheme.typography.labelSmall,
                color = if (item.carried) BUNDLE_READY else BUNDLE_MUTED,
                maxLines = 1,
            )
        }
        if (item.carried) BundleSprite(state.sprites[StardewUi.CHECK_ON], 16.dp)
    }
}

@Composable
private fun BundleProgressBar(have: Int, required: Int) {
    val fraction = if (required <= 0) 0f else have.toFloat().div(required).coerceIn(0f, 1f)
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(5.dp)
            .background(BUNDLE_PROGRESS_TRACK),
    ) {
        Box(
            Modifier
                .fillMaxWidth(fraction)
                .fillMaxHeight()
                .background(BUNDLE_GOLD),
        )
    }
}

@Composable
private fun BundleEmptyState(state: StardewState, modifier: Modifier = Modifier) {
    Column(
        modifier = modifier.padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        BundleSprite(state.sprites[StardewUi.CHECK_ON] ?: state.sprites[StardewUi.TAB_SKILLS], 46.dp)
        Text(
            text = "No active Community Center bundles",
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Bold,
            color = BUNDLE_INK,
            modifier = Modifier.padding(top = 10.dp),
        )
        Text(
            text = "Completed rooms and newly unlocked bundles will update from the game.",
            style = MaterialTheme.typography.bodyMedium,
            color = BUNDLE_MUTED,
            modifier = Modifier.padding(top = 4.dp),
        )
    }
}

@Composable
private fun BundleDateFooter(state: StardewState) {
    Row(
        modifier = Modifier.fillMaxWidth().height(43.dp).padding(horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        BundleSprite(state.sprites[StardewUi.SEASON], 26.dp)
        Spacer(Modifier.width(7.dp))
        Text(
            text = referenceBundleDate(state),
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.SemiBold,
            color = BUNDLE_INK,
            modifier = Modifier.weight(1f),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        BundleSprite(state.sprites[StardewUi.TIME], 23.dp)
        Spacer(Modifier.width(6.dp))
        Text(
            text = state.clock.replace("am", " am").replace("pm", " pm"),
            style = MaterialTheme.typography.bodyMedium,
            color = BUNDLE_INK,
            maxLines = 1,
        )
    }
}

@Composable
private fun BundleSpriteSlot(state: StardewState, sprite: UiSprite?, size: Dp) {
    val frame = state.frame(StardewUi.SLOT)
    val base = Modifier.size(size)
    val painted = if (frame != null) {
        base.drawBehind { drawNineSlice(frame, this.size) }
    } else {
        base
            .clip(ThorTheme.shapes.small)
            .background(BUNDLE_ITEM_WASH)
            .border(1.dp, BUNDLE_RULE, ThorTheme.shapes.small)
    }
    Box(painted, contentAlignment = Alignment.Center) {
        BundleSprite(sprite, size - 10.dp)
    }
}

@Composable
private fun BundleSprite(sprite: UiSprite?, size: Dp) {
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
private fun BundleRule(strong: Boolean = false) {
    Box(
        Modifier
            .fillMaxWidth()
            .height(if (strong) 2.dp else 1.dp)
            .background(if (strong) BUNDLE_GOLD else BUNDLE_RULE),
    )
}

internal data class ReferenceBundleRoom(
    val name: String,
    val bundles: List<StardewBundle>,
    val have: Int,
    val required: Int,
)

internal fun referenceBundleRooms(bundles: List<StardewBundle>): List<ReferenceBundleRoom> =
    bundles
        .groupBy { it.room.ifEmpty { "Other" } }
        .map { (name, roomBundles) ->
            ReferenceBundleRoom(
                name = name,
                bundles = roomBundles.sortedBy { (it.required - it.have).coerceAtLeast(0) },
                have = roomBundles.sumOf { it.have.coerceAtMost(it.required) },
                required = roomBundles.sumOf { it.required },
            )
        }
        .sortedWith(compareBy<ReferenceBundleRoom> { referenceBundleRoomRank(it.name) }.thenBy { it.name })

private fun referenceBundleRoomRank(room: String): Int =
    BUNDLE_ROOM_ORDER.indexOf(room).let { if (it < 0) Int.MAX_VALUE else it }

private fun bundleItemDetail(item: StardewBundleItem): String = buildString {
    append("×${item.count.coerceAtLeast(1)}")
    when (item.quality) {
        1 -> append(" • Silver")
        2 -> append(" • Gold")
        4 -> append(" • Iridium")
    }
    if (item.carried) append(" • Ready")
}

private fun referenceBundleDate(state: StardewState): String {
    if (!state.hasWorld || state.day <= 0) return "Waiting for farm"
    val weekday = state.weekday.trim().take(3)
    val day = if (weekday.isEmpty()) state.day.toString() else "$weekday. ${state.day}"
    return "$day, ${state.season}, Year ${state.year.coerceAtLeast(1)}"
}

private val BUNDLE_ROOM_ORDER = listOf(
    "Crafts Room",
    "Pantry",
    "Fish Tank",
    "Boiler Room",
    "Bulletin Board",
    "Vault",
    "Abandoned Joja Mart",
)

private val BUNDLE_INK = Color(0xFF321B0F)
private val BUNDLE_MUTED = Color(0xFF70451F)
private val BUNDLE_READY = Color(0xFF19721F)
private val BUNDLE_GOLD = Color(0xFFC47A12)
private val BUNDLE_PARCHMENT = Color(0xFFFFD98E)
private val BUNDLE_HEADER_WASH = Color(0x32FFD789)
private val BUNDLE_ROW_WASH = Color(0x18FFF0B8)
private val BUNDLE_READY_WASH = Color(0x1C7DCB65)
private val BUNDLE_ITEM_WASH = Color(0x2AFFF0B8)
private val BUNDLE_CARRIED = Color(0x307DCB65)
private val BUNDLE_RULE = Color(0x55A95F16)
private val BUNDLE_PROGRESS_TRACK = Color(0x396F421B)
