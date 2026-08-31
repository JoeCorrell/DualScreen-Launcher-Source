package com.dualscreen.launcher

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp

/**
 * Dualscreen Companion's own palette, type and shapes.
 *
 * Deliberately not `ThorTheme`. That palette is the launcher's — user-themable,
 * six accents, a light mode — and this app is one fixed look taken from the
 * reference design: a warm near-black with a single amber, where the only
 * colour on the screen is the thing you are meant to press. A themable surface
 * cannot promise that, and the promise is the design.
 *
 * Kept as a value class behind a `staticCompositionLocalOf` rather than as
 * top-level constants so the two panels read the same instance and a preview
 * can substitute one without touching the call sites.
 */
@Immutable
data class CompanionColors(
    /** Behind everything, including the gap between the two panels. */
    val backdrop: Color,
    /** The app's own sheet, inset from the backdrop. */
    val surface: Color,
    /** A card sitting on [surface]. */
    val card: Color,
    /** The block under a card's key art, one step lighter than the art. */
    val cardFooter: Color,
    /** Key-art fallback, before real cover art has been fetched. */
    val artFill: Color,
    /** The diagonal hatch drawn over [artFill]. */
    val artHatch: Color,
    /** Hairline around cards, buttons and preview boxes. */
    val outline: Color,
    /** The same hairline once its control has focus or is hovered. */
    val outlineStrong: Color,
    /** The one accent. Fills the primary action and nothing else. */
    val accent: Color,
    /** Accent as text, on a dark ground, where a fill would be too loud. */
    val accentText: Color,
    /** Text and icons drawn on top of an [accent] fill. */
    val onAccent: Color,
    /** Body and heading text. */
    val text: Color,
    /** Mono metadata: the specification lines, the eyebrows, the units. */
    val textMuted: Color,
    /** A failed verify or a broken install. */
    val danger: Color,
    /** The hairline for a control in the [danger] state. */
    val dangerOutline: Color,
    /** A finished, healthy install. */
    val ok: Color,
)

/**
 * The reference palette, sampled from the supplied design.
 *
 * Every neutral is warm — the blacks carry a few points of red and green over
 * blue. That is what keeps the amber from reading as a sticker on top of a grey
 * app; a neutral-grey shell under this accent looks like two designs.
 */
val CompanionDark = CompanionColors(
    backdrop = Color(0xFF080807),
    surface = Color(0xFF100F0C),
    card = Color(0xFF16150F),
    cardFooter = Color(0xFF13120D),
    artFill = Color(0xFF262319),
    artHatch = Color(0x14FFFFFF),
    outline = Color(0xFF2A2722),
    outlineStrong = Color(0xFF4A443A),
    accent = Color(0xFFF0A63C),
    accentText = Color(0xFFE9A23B),
    onAccent = Color(0xFF14100A),
    text = Color(0xFFF2EFE8),
    textMuted = Color(0xFF8A8479),
    danger = Color(0xFFD9583F),
    dangerOutline = Color(0xFF7A342A),
    ok = Color(0xFF9BBF6A),
)

/**
 * Type, in two families and nothing else.
 *
 * The split is the whole typographic idea in the reference: proportional bold
 * for names, monospace uppercase for everything measurable. A game's title is
 * read; its size, its mod version and its state are *scanned*, and a mono grid
 * with wide tracking is what makes a column of them scannable at arm's length.
 *
 * Sans is the platform's — Roboto — rather than a bundled geometric face. The
 * reference is set in a Poppins-like grotesque; matching it exactly costs a
 * ~150 KB font asset in an app whose point is being lightweight. The weights and
 * the tracking below are tuned to sit as close to it as Roboto gets.
 */
@Immutable
data class CompanionTypography(
    /** The app's name, once, in the header. */
    val appTitle: TextStyle,
    /** A game's name on its own page. */
    val display: TextStyle,
    /** A game's name on a card. */
    val cardTitle: TextStyle,
    /** Section headings inside a page. */
    val section: TextStyle,
    /** Ordinary prose. Rare on purpose. */
    val body: TextStyle,
    /** The mono workhorse: specs, states, units. */
    val mono: TextStyle,
    /** Mono, one step down, for a label under a preview box. */
    val monoSmall: TextStyle,
    /** The text inside a button. Mono, because a button is a label. */
    val action: TextStyle,
)

/**
 * How far apart the mono type is set.
 *
 * In `em` rather than `sp` so the 10sp label under a preview box and the 13sp
 * text in a button open up by the same *proportion*. Tracked in absolute units
 * they drift: the small one looks spaced out and the large one looks tight.
 */
private val MONO_TRACKING = 0.14.em

/*
 * Sized for 832 x 468 dp, which is what the Thor's upper panel actually is.
 *
 * The reference is drawn at a much taller aspect, and its type transplanted
 * one-for-one does not fit: at 26sp the app title and its device line ate an
 * eighth of the panel, and the four nav pills ran off the right-hand edge with
 * "SETTINGS" wrapping to two lines inside its own pill.
 *
 * So every size below is one step down from the reference's, and the *ratios*
 * between them are what has been preserved — the title is still roughly twice
 * the mono, the card title still sits between them. That is what carries the
 * design language onto a short screen; matching the absolute points would not.
 */
val CompanionType = CompanionTypography(
    appTitle = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.Bold,
        fontSize = 21.sp,
        lineHeight = 25.sp,
        letterSpacing = (-0.4).sp,
    ),
    display = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.Bold,
        fontSize = 32.sp,
        lineHeight = 36.sp,
        letterSpacing = (-0.8).sp,
    ),
    cardTitle = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.Bold,
        fontSize = 15.sp,
        lineHeight = 18.sp,
        letterSpacing = (-0.2).sp,
    ),
    section = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.SemiBold,
        fontSize = 14.sp,
        lineHeight = 18.sp,
    ),
    body = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.Normal,
        fontSize = 13.sp,
        lineHeight = 18.sp,
    ),
    mono = TextStyle(
        fontFamily = FontFamily.Monospace,
        fontWeight = FontWeight.Medium,
        fontSize = 11.sp,
        lineHeight = 14.sp,
        letterSpacing = MONO_TRACKING,
    ),
    monoSmall = TextStyle(
        fontFamily = FontFamily.Monospace,
        fontWeight = FontWeight.Medium,
        fontSize = 9.sp,
        lineHeight = 12.sp,
        letterSpacing = MONO_TRACKING,
    ),
    action = TextStyle(
        fontFamily = FontFamily.Monospace,
        fontWeight = FontWeight.Bold,
        fontSize = 11.sp,
        lineHeight = 14.sp,
        letterSpacing = 0.12.em,
    ),
)

/**
 * Corner radii, in three sizes and no more.
 *
 * The reference uses one radius per role — sheet, card, control — and reuses it
 * everywhere that role appears. Two cards with different corners on one shelf is
 * the single fastest way to make a grid look assembled rather than designed.
 */
@Immutable
data class CompanionShapes(
    /** The app sheet and any full-height panel. */
    val sheet: RoundedCornerShape,
    /** Cards, preview boxes, the art block inside a card. */
    val card: RoundedCornerShape,
    /** Every button and pill. */
    val control: RoundedCornerShape,
)

val CompanionShapeSet = CompanionShapes(
    sheet = RoundedCornerShape(24.dp),
    card = RoundedCornerShape(14.dp),
    control = RoundedCornerShape(12.dp),
)

/** WEMU's in-session console is deliberately rectilinear, not launcher chrome. */
val CompanionConsoleShapeSet = CompanionShapes(
    sheet = RoundedCornerShape(0.dp),
    card = RoundedCornerShape(0.dp),
    control = RoundedCornerShape(0.dp),
)

/**
 * Spacing, as the handful of steps the reference actually uses.
 *
 * Named for where they are used rather than by number, because the decision at
 * a call site is "this is the gutter between cards", not "this is 16".
 */
@Immutable
data class CompanionSpacing(
    /** Inset from the panel edge to its content. */
    val page: Dp = 16.dp,
    /** Between cards on a shelf. */
    val gutter: Dp = 12.dp,
    /** Inside a card, around its footer text. */
    val card: Dp = 12.dp,
    /** Between a label and the thing it labels. */
    val tight: Dp = 6.dp,
    /** Between stacked blocks inside a page. */
    val block: Dp = 14.dp,
)

val CompanionSpace = CompanionSpacing()

val LocalCompanionColors = staticCompositionLocalOf { CompanionDark }
val LocalCompanionType = staticCompositionLocalOf { CompanionType }
val LocalCompanionShapes = staticCompositionLocalOf { CompanionShapeSet }
val LocalCompanionSpace = staticCompositionLocalOf { CompanionSpace }

/**
 * Reaches the tokens from anywhere in the tree.
 *
 * `CompanionTheme.colors.accent` reads better at a call site than a
 * `LocalCompanionColors.current.accent`, and it is the shape Material and the
 * rest of this codebase already use.
 */
object CompanionTheme {
    val colors: CompanionColors
        @Composable get() = LocalCompanionColors.current
    val type: CompanionTypography
        @Composable get() = LocalCompanionType.current
    val shapes: CompanionShapes
        @Composable get() = LocalCompanionShapes.current
    val space: CompanionSpacing
        @Composable get() = LocalCompanionSpace.current
}

/**
 * Wraps the app in the Companion look.
 *
 * A `MaterialTheme` still goes underneath it, with a scheme derived from these
 * colours. Not because this app styles itself from Material — it does not, every
 * surface here is drawn by hand — but because `Text`, `Icon` and the ripple all
 * resolve their defaults through it, and leaving Material's stock purple in
 * place means the one control someone forgets to colour arrives in purple.
 *
 * There is no light mode and no `darkTheme` parameter to ask for one. A launcher
 * for a handheld that is used in the dark, over key art that is mostly dark,
 * does not have a second palette worth maintaining — and a parameter that only
 * ever takes one value is a promise the rest of the app would start believing.
 */
@Composable
fun CompanionTheme(
    colors: CompanionColors = CompanionDark,
    content: @Composable () -> Unit,
) {
    val scheme = darkColorScheme(
        primary = colors.accent,
        onPrimary = colors.onAccent,
        background = colors.surface,
        onBackground = colors.text,
        surface = colors.card,
        onSurface = colors.text,
        surfaceVariant = colors.artFill,
        onSurfaceVariant = colors.textMuted,
        outline = colors.outline,
        error = colors.danger,
    )

    CompositionLocalProvider(
        LocalCompanionColors provides colors,
        LocalCompanionType provides CompanionType,
        LocalCompanionShapes provides CompanionShapeSet,
        LocalCompanionSpace provides CompanionSpace,
        LocalContentColor provides colors.text,
    ) {
        MaterialTheme(
            colorScheme = scheme,
            typography = Typography(),
            content = content,
        )
    }
}
