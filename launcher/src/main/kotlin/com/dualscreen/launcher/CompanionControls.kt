package com.dualscreen.launcher

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.foundation.interaction.collectIsHoveredAsState
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/**
 * What a control is *for*, which is the only thing a call site has to decide.
 *
 * The reference allows exactly one filled amber control per screen — the thing
 * you came to press — and draws everything else as a hairline outline. Making
 * that a tone rather than a set of colour parameters is what keeps the rule
 * enforceable: there is no way to spell "a second amber button" without
 * choosing [Tone.PRIMARY] twice, which is visible in review.
 */
enum class Tone {
    /** Filled amber. Play, Install, Continue. One per screen. */
    PRIMARY,

    /** Hairline outline. Everything that is not the point of the screen. */
    SECONDARY,

    /** Hairline outline in red. Only ever offered against a broken install. */
    DANGER,

    /** Outline, dimmed, non-interactive. */
    DISABLED,
}

/**
 * Height, in the two sizes the reference uses.
 *
 * Both clear the 48dp touch target this app is held at arm's length to use —
 * [Size.COMPACT] is the header pill, [Size.FULL] is a card's call to action and
 * the Play button on a game's page.
 */
enum class Size(val height: Dp, val horizontalPadding: Dp) {
    COMPACT(42.dp, 14.dp),
    FULL(48.dp, 18.dp),
}

/**
 * How a control reacts to focus, hover and press, in one place.
 *
 * Controller focus has to be *louder* here than in a mouse-driven app: the
 * focused control is the only thing telling somebody holding a d-pad where they
 * are, and on a shelf of near-identical cards a subtle border change is
 * genuinely missable. So focus brightens the outline, lifts the fill and — for
 * outline tones — pulls the accent into the border.
 *
 * Press is a scale rather than a colour change, because the amber fill has
 * nowhere brighter to go and a darkening would read as the button turning off
 * at the moment it turns on.
 */
@Composable
private fun rememberControlVisuals(
    tone: Tone,
    interactionSource: MutableInteractionSource,
): ControlVisuals {
    val colors = CompanionTheme.colors
    val focused by interactionSource.collectIsFocusedAsState()
    val hovered by interactionSource.collectIsHoveredAsState()
    val pressed by interactionSource.collectIsPressedAsState()
    val active = focused || hovered

    val fill by animateColorAsState(
        targetValue = when {
            tone == Tone.PRIMARY -> colors.accent
            active -> Color.White.copy(alpha = 0.06f)
            else -> Color.Transparent
        },
        animationSpec = tween(FEEDBACK_MS),
        label = "controlFill",
    )
    val border by animateColorAsState(
        targetValue = when {
            tone == Tone.PRIMARY -> Color.Transparent
            tone == Tone.DANGER -> if (active) colors.danger else colors.dangerOutline
            tone == Tone.DISABLED -> colors.outline
            active -> colors.accent
            else -> colors.outline
        },
        animationSpec = tween(FEEDBACK_MS),
        label = "controlBorder",
    )
    val content = when (tone) {
        Tone.PRIMARY -> colors.onAccent
        Tone.DANGER -> colors.danger
        Tone.DISABLED -> colors.textMuted
        Tone.SECONDARY -> colors.text
    }
    val scale by animateFloatAsState(
        targetValue = if (pressed) 0.97f else 1f,
        animationSpec = tween(FEEDBACK_MS),
        label = "controlScale",
    )
    return ControlVisuals(fill, border, content, scale)
}

private data class ControlVisuals(
    val fill: Color,
    val border: Color,
    val content: Color,
    val scale: Float,
)

private const val FEEDBACK_MS = 120

/**
 * The one button in this app.
 *
 * Every call to action on every screen is this, in one of four [Tone]s and two
 * [Size]s. The reference has no other button shape, and a launcher this small
 * does not need one — a second button component is how "Import ZIP" and "Add
 * Files" end up 2dp different in height on two screens that sit side by side.
 */
@Composable
fun CompanionButton(
    label: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    tone: Tone = Tone.SECONDARY,
    size: Size = Size.FULL,
    icon: ImageVector? = null,
    enabled: Boolean = true,
) {
    val shapes = CompanionTheme.shapes
    val type = CompanionTheme.type
    val effectiveTone = if (enabled) tone else Tone.DISABLED
    val interactionSource = remember { MutableInteractionSource() }
    val visuals = rememberControlVisuals(effectiveTone, interactionSource)

    Box(
        modifier = modifier
            .defaultMinSize(minHeight = size.height)
            .graphicsScale(visuals.scale)
            .clip(shapes.control)
            .background(visuals.fill)
            .border(BorderStroke(1.dp, visuals.border), shapes.control)
            .clickable(
                interactionSource = interactionSource,
                indication = null,
                enabled = enabled,
                onClick = onClick,
            )
            .focusable(enabled = enabled, interactionSource = interactionSource)
            .padding(horizontal = size.horizontalPadding),
        contentAlignment = Alignment.Center,
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            if (icon != null) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = visuals.content,
                    modifier = Modifier.size(18.dp),
                )
            }
            /*
             * One line, never wrapped.
             *
             * A control's label is its identity, and a pill is sized to the text
             * it was given — so letting it wrap does not make the pill taller,
             * it splits the word across two lines inside a fixed height and
             * clips. "SETTINGS" came out as "SETTIN / GS" on the device, which
             * is the visible form of this bug.
             */
            Text(
                text = label.uppercase(),
                style = type.action,
                color = visuals.content,
                textAlign = TextAlign.Center,
                maxLines = 1,
                softWrap = false,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

/**
 * A header pill that is a navigation destination rather than an action.
 *
 * Same silhouette as [CompanionButton] deliberately — the reference's header is
 * a single row of identically-shaped pills — but selection is carried by a
 * filled ground and full-strength text rather than by the accent, which stays
 * reserved for the primary action further down the page. A selected nav pill
 * and a Play button competing in the same amber is the one thing that would
 * break the "one accent, one action" rule the whole palette rests on.
 */
@Composable
fun CompanionNavPill(
    label: String,
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val colors = CompanionTheme.colors
    val shapes = CompanionTheme.shapes
    val type = CompanionTheme.type
    val interactionSource = remember { MutableInteractionSource() }
    val focused by interactionSource.collectIsFocusedAsState()
    val hovered by interactionSource.collectIsHoveredAsState()
    val active = focused || hovered

    val fill by animateColorAsState(
        targetValue = when {
            selected -> Color.White.copy(alpha = 0.10f)
            active -> Color.White.copy(alpha = 0.05f)
            else -> Color.Transparent
        },
        animationSpec = tween(FEEDBACK_MS),
        label = "navFill",
    )
    val border by animateColorAsState(
        targetValue = when {
            active -> colors.accent
            selected -> colors.outlineStrong
            else -> colors.outline
        },
        animationSpec = tween(FEEDBACK_MS),
        label = "navBorder",
    )

    Box(
        modifier = modifier
            .defaultMinSize(minHeight = Size.COMPACT.height)
            .clip(shapes.control)
            .background(fill)
            .border(BorderStroke(1.dp, border), shapes.control)
            .clickable(
                interactionSource = interactionSource,
                indication = null,
                onClick = onClick,
            )
            .focusable(interactionSource = interactionSource)
            .padding(horizontal = Size.COMPACT.horizontalPadding),
        contentAlignment = Alignment.Center,
    ) {
        // Same one-line rule as [CompanionButton]; see the note there.
        Text(
            text = label.uppercase(),
            style = type.action,
            color = if (selected) colors.text else colors.textMuted,
            maxLines = 1,
            softWrap = false,
        )
    }
}

/**
 * Mono metadata, in the app's one muted voice.
 *
 * A composable rather than a style constant because every one of these is
 * uppercase, and doing that at the call site meant half of them were written
 * `"ready"` and shouted by the style while the other half were written
 * `"READY"` and shouted twice — which looks identical until somebody
 * lowercases the style.
 */
@Composable
fun MonoLabel(
    text: String,
    modifier: Modifier = Modifier,
    color: Color = CompanionTheme.colors.textMuted,
    small: Boolean = false,
    /**
     * One line by default, because nearly every one of these sits in a fixed
     * row of a card whose height was chosen without room for a second.
     */
    maxLines: Int = 1,
) {
    Text(
        text = text.uppercase(),
        style = if (small) CompanionTheme.type.monoSmall else CompanionTheme.type.mono,
        color = color,
        maxLines = maxLines,
        overflow = TextOverflow.Ellipsis,
        modifier = modifier,
    )
}

/**
 * The press shrink, applied without disturbing layout.
 *
 * `graphicsLayer` rather than a layout modifier on purpose: the button must
 * shrink *visually* and keep its measured size, or a row of them would reflow
 * every time one was held down.
 */
private fun Modifier.graphicsScale(scale: Float): Modifier =
    if (scale == 1f) this else graphicsLayer(scaleX = scale, scaleY = scale)
