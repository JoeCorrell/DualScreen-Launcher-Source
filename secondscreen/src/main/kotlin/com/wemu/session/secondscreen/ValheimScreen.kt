package com.wemu.session.secondscreen

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.thor.core.designsystem.modifier.SurfaceLevel
import com.thor.core.designsystem.modifier.thorSurface
import com.thor.core.designsystem.theme.ThorTheme

@Composable
fun ValheimScreen(modifier: Modifier = Modifier) {
    val state by SecondScreen.valheim.collectAsStateWithLifecycle()
    val dimens = ThorTheme.dimens

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(dimens.spacingSmall),
        verticalArrangement = Arrangement.spacedBy(dimens.spacing),
    ) {
        val mirrored = state.mirrors.isNotEmpty()

        if (mirrored) {
            Mirror(state, "mirror:health")
            Mirror(state, "mirror:stamina")
            Mirror(state, "mirror:eitr")
            Mirror(state, "mirror:food")
        } else {
            VitalsCard(state)
        }

        if (state.hasMap) MinimapCard(state)

        if (!mirrored && state.food.isNotEmpty()) EffectsCard(state)

        if (state.hotbar.isNotEmpty()) HotbarCard(state)

        if (mirrored && state.mirrors.containsKey("mirror:inventory")) {
            Mirror(state, "mirror:inventory")
        } else if (state.inventory.isNotEmpty()) {
            BagCard(state)
        }
    }
}

@Composable
private fun Mirror(state: ValheimState, key: String) {
    val image = state.mirrors[key] ?: return
    if (image.width <= 0 || image.height <= 0) return

    Image(
        bitmap = image,
        contentDescription = null,
        modifier = Modifier
            .fillMaxWidth()
            .aspectRatio(image.width.toFloat() / image.height.toFloat()),
        contentScale = ContentScale.FillBounds,
        filterQuality = FilterQuality.None,
    )
}

@Composable
private fun VitalsCard(state: ValheimState) {
    val colors = ThorTheme.colors

    ValheimCard(state) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = state.playerName.ifEmpty { "Waiting for the game" },
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.SemiBold,
                color = colors.onSurface,
                modifier = Modifier.weight(1f),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            if (state.worldName.isNotEmpty()) {
                Text(
                    text = state.worldName,
                    style = MaterialTheme.typography.labelSmall,
                    color = colors.onSurfaceVariant,
                )
            }
        }

        GameBar(
            state = state,
            label = "Health",
            value = state.health,
            max = state.maxHealth,
            frame = "ui:health_frame",
            fill = "ui:health_fill",
            fallback = colors.error,
        )
        GameBar(
            state = state,
            label = "Stamina",
            value = state.stamina,
            max = state.maxStamina,
            frame = "ui:stamina_frame",
            fill = "ui:stamina_fill",
            fallback = colors.cursor,
        )
    }
}

@Composable
private fun GameBar(
    state: ValheimState,
    label: String,
    value: Float,
    max: Float,
    frame: String,
    fill: String,
    fallback: Color,
) {
    val colors = ThorTheme.colors
    val fraction = if (max > 0f) (value / max).coerceIn(0f, 1f) else 0f
    val frameSprite = state.piece(frame)
    val fillSprite = state.piece(fill)

    Column(modifier = Modifier.fillMaxWidth().padding(top = ThorTheme.dimens.spacingTiny)) {
        Row(modifier = Modifier.fillMaxWidth()) {
            Text(
                text = label,
                style = MaterialTheme.typography.labelSmall,
                color = colors.onSurfaceVariant,
                modifier = Modifier.weight(1f),
            )
            Text(
                text = value.toInt().toString() + " / " + max.toInt().toString(),
                style = MaterialTheme.typography.labelSmall,
                color = colors.onSurface,
            )
        }

        Box(
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = 2.dp)
                .height(BAR_HEIGHT)
                .then(
                    if (frameSprite != null) {
                        Modifier.drawBehind { drawNineSlice(frameSprite, size) }
                    } else {
                        Modifier.clip(ThorTheme.shapes.pill).background(colors.surfaceHighest)
                    },
                ),
        ) {
            Box(
                modifier = Modifier
                    .fillMaxWidth(fraction)
                    .height(BAR_HEIGHT)
                    .then(
                        if (fillSprite != null) {
                            Modifier.drawBehind { drawNineSlice(fillSprite, size) }
                        } else {
                            Modifier.clip(ThorTheme.shapes.pill).background(fallback)
                        },
                    ),
            )
        }
    }
}

@Composable
private fun MinimapCard(state: ValheimState) {
    val terrain = state.terrain ?: return
    val colors = ThorTheme.colors

    ValheimCard(state) {
        Text(
            text = "Map",
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.SemiBold,
            color = colors.onSurface,
        )
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = ThorTheme.dimens.spacingTiny)
                .aspectRatio(1f)
                .clip(ThorTheme.shapes.small),
        ) {
            Image(
                bitmap = terrain,
                contentDescription = "Map",
                modifier = Modifier.fillMaxSize(),
                contentScale = ContentScale.FillBounds,
                filterQuality = FilterQuality.None,
            )
            state.fog?.let { fog ->
                Image(
                    bitmap = fog,
                    contentDescription = null,
                    modifier = Modifier.fillMaxSize(),
                    contentScale = ContentScale.FillBounds,
                    filterQuality = FilterQuality.None,
                )
            }

            val half = state.mapSize * state.pixelSize / 2f
            if (half > 0f) {
                Canvas(modifier = Modifier.fillMaxSize()) {
                    val fx = ((state.x + half) / (half * 2f)).coerceIn(0f, 1f)
                    val fz = 1f - ((state.z + half) / (half * 2f)).coerceIn(0f, 1f)
                    val centre = Offset(size.width * fx, size.height * fz)
                    val radius = size.minDimension * MARKER
                    drawCircle(Color.Black.copy(alpha = 0.55f), radius * 2f, centre)
                    drawCircle(Color.White, radius * 1.4f, centre)
                    drawCircle(colors.cursor, radius, centre)
                }
            }
        }
    }
}

@Composable
private fun EffectsCard(state: ValheimState) {
    val colors = ThorTheme.colors
    val dimens = ThorTheme.dimens

    ValheimCard(state, accent = state.expiring != null) {
        Text(
            text = "Food and buffs",
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.SemiBold,
            color = colors.onSurface,
        )
        state.food.forEachIndexed { index, effect ->
            val low = effect.left < LOW
            val slot = state.piece("ui:food_slot" + index) ?: state.piece("ui:slot")

            Row(
                modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
            ) {
                Box(
                    modifier = Modifier
                        .size(SLOT)
                        .then(
                            if (slot != null) {
                                Modifier.drawBehind { drawNineSlice(slot, size) }
                            } else {
                                Modifier.clip(ThorTheme.shapes.small)
                                    .background(colors.surfaceHighest)
                            },
                        ),
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(state.sprites[effect.iconKey], 26.dp)
                }

                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = effect.displayName.ifEmpty { effect.name },
                        style = MaterialTheme.typography.bodyMedium,
                        color = colors.onSurface,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                    GameBar(
                        state = state,
                        label = "",
                        value = effect.remaining,
                        max = effect.ttl,
                        frame = "ui:food_frame",
                        fill = "ui:health_fill",
                        fallback = if (low) colors.error else colors.cursor,
                    )
                }

                Text(
                    text = effect.clock,
                    style = MaterialTheme.typography.labelSmall,
                    fontWeight = FontWeight.Medium,
                    color = if (low) colors.error else colors.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
private fun HotbarCard(state: ValheimState) {
    val dimens = ThorTheme.dimens

    ValheimCard(state) {
        Text(
            text = "Hotbar",
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.SemiBold,
            color = ThorTheme.colors.onSurface,
        )
        Row(
            modifier = Modifier.fillMaxWidth().padding(top = dimens.spacingTiny),
            horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
        ) {
            state.hotbar.sortedBy { it.slot }.forEach { item ->
                Slot(state, item, Modifier.weight(1f))
            }
        }
    }
}

@Composable
private fun BagCard(state: ValheimState) {
    val dimens = ThorTheme.dimens
    val rows = state.inventory.sortedBy { it.slot }.chunked(COLUMNS)

    ValheimCard(state) {
        Text(
            text = "Inventory",
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.SemiBold,
            color = ThorTheme.colors.onSurface,
        )
        Column(
            modifier = Modifier.fillMaxWidth().padding(top = dimens.spacingTiny),
            verticalArrangement = Arrangement.spacedBy(dimens.spacingTiny),
        ) {
            rows.forEach { row ->
                Row(horizontalArrangement = Arrangement.spacedBy(dimens.spacingTiny)) {
                    row.forEach { item -> Slot(state, item, Modifier.weight(1f)) }
                    repeat(COLUMNS - row.size) { Box(modifier = Modifier.weight(1f)) }
                }
            }
        }
    }
}

@Composable
private fun Slot(state: ValheimState, item: ValheimItem, modifier: Modifier = Modifier) {
    val colors = ThorTheme.colors
    val frame = state.piece("ui:hotbar_slot") ?: state.piece("ui:slot")

    Box(
        modifier = modifier
            .aspectRatio(1f)
            .then(
                if (frame != null) {
                    Modifier.drawBehind { drawNineSlice(frame, size) }
                } else {
                    Modifier.clip(ThorTheme.shapes.small).background(colors.surfaceHighest)
                },
            ),
        contentAlignment = Alignment.Center,
    ) {
        if (item.equipped) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .clip(ThorTheme.shapes.small)
                    .background(colors.cursor.copy(alpha = 0.22f)),
            )
        }

        Icon(state.sprites[item.iconKey], 30.dp)

        if (item.stack > 1) {
            Text(
                text = item.stack.toString(),
                style = MaterialTheme.typography.labelSmall,
                fontWeight = FontWeight.Bold,
                color = colors.onSurface,
                modifier = Modifier.align(Alignment.BottomEnd).padding(2.dp),
            )
        }

        val wear = item.wear
        if (wear != null && wear < 1f) {
            Box(
                modifier = Modifier
                    .align(Alignment.BottomStart)
                    .fillMaxWidth(wear)
                    .height(2.dp)
                    .background(if (wear < LOW) colors.error else colors.cursor),
            )
        }
    }
}

@Composable
private fun Icon(sprite: UiSprite?, size: Dp) {
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
private fun ValheimCard(
    state: ValheimState,
    accent: Boolean = false,
    content: @Composable () -> Unit,
) {
    val colors = ThorTheme.colors
    val panel = state.piece("ui:panel")

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .then(
                if (panel != null) {
                    Modifier
                        .drawBehind { drawNineSlice(panel, size) }
                        .padding(panel.left.coerceAtLeast(12).dp)
                } else {
                    Modifier
                        .thorSurface(
                            shape = ThorTheme.shapes.panel,
                            color = if (accent) {
                                colors.cursor.copy(alpha = 0.12f)
                            } else {
                                colors.surface
                            },
                            level = SurfaceLevel.RAISED,
                        )
                        .padding(ThorTheme.dimens.spacing)
                },
            ),
    ) {
        content()
    }
}

private const val COLUMNS = 8
private const val LOW = 0.25f
private const val MARKER = 0.018f
private val BAR_HEIGHT = 10.dp
private val SLOT = 34.dp
