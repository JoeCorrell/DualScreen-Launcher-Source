package com.thor.core.model

import kotlinx.serialization.Serializable

/**
 * The appearance preferences the theme is generated from.
 *
 * Loki's equivalent is one field of a much larger `ThorSettings`, most of which
 * describes a launcher — docks, shelves, ROM directories, trailer autoplay. None
 * of that means anything here, so this is the subset the palette actually reads,
 * with the same names and the same defaults so a value set in Loki means the
 * same thing in this app.
 *
 * Every preference here is an *input* to palette generation rather than a patch
 * applied over a finished one. That is what makes them compose: pure black under
 * a hue shift under maximum contrast is a single palette built to satisfy all
 * three, not three transforms arguing over the same colours in whatever order
 * they happen to run.
 */
@Serializable
data class PersonalizationSettings(
    val themeId: ThemeId = ThemeId.MATERIAL,

    /**
     * Themes the user built, in the order they were made.
     *
     * Stored whole rather than as overrides over a bundled theme; see
     * [CustomTheme] for why.
     */
    val customThemes: List<CustomTheme> = emptyList(),

    /**
     * Which custom theme is applied, if one is.
     *
     * Beside [themeId] rather than replacing it, which is what makes deleting a
     * theme safe: clearing this falls back to whichever bundled theme was last
     * chosen rather than to an app with no palette at all.
     */
    val activeCustomThemeId: String? = null,

    /** Light or dark, for whichever theme is chosen. Every theme resolves both. */
    val themeMode: ThemeMode = ThemeMode.DARK,

    /**
     * How hard the palette separates text from ground.
     *
     * Distinct from [AccessibilitySettings.highContrast], which is a switch that
     * forces the maximum; this is the dial underneath it.
     */
    val contrastLevel: ContrastLevel = ContrastLevel.NORMAL,

    /** Scales every chroma in the palette, 0 to 2. 1 is the theme as designed. */
    val colorIntensity: Float = 1.0f,

    /** Rotates the whole palette in degrees, -180 to 180. Accent and greys together. */
    val accentHueShift: Float = 0f,

    /**
     * Draws the darkest surface as true black.
     *
     * A property of the screen rather than of a theme, which is why it sits over
     * all of them: the AYN Thor's panels switch pixels off at #000, so this is
     * both the deepest look available and the only one that costs less power.
     */
    val pureBlack: Boolean = false,

    /** Overrides the theme's own accent when set, as an ARGB long. */
    val accentOverrideArgb: Long? = null,

    /** Overrides how every panel is built. Null keeps the theme's own. */
    val surfaceStyleOverride: SurfaceStyle? = null,

    /** Scales the theme's background wash toward its accent. 0 is a flat ground. */
    val surfaceDepth: Float = 1.0f,

    /** Scales the theme's film grain. 0 removes it. */
    val grainAmount: Float = 1.0f,

    /** Follows the system wallpaper/dynamic colour when supported (API 31+). */
    val useDynamicColor: Boolean = false,

    /** How far the wallpaper is dimmed behind the interface, 0 to 1. */
    val wallpaperDim: Float = 0f,

    /**
     * The background effect, or null to take the theme's own choice.
     *
     * Loki stores this as a plain value defaulting to `WAVES`. Nullable here
     * because every ported `ThemeRecipe` already names a `defaultWallpaper` —
     * Vapor asks for Waves, Sakura for Mesh, Terminal for Particles — and a
     * non-null default would override all fourteen of them the moment the
     * settings file was written, which is not what picking a theme should do.
     * Null keeps the theme's answer; anything else is the user overruling it.
     */
    val animatedWallpaper: AnimatedWallpaper? = null,

    /**
     * Whether the wallpaper moves, as opposed to which wallpaper it is.
     *
     * Off. The effect still draws — a theme that asks for Mesh still gets Mesh,
     * and picking one still changes how the app looks — it simply holds still.
     *
     * Two reasons, and the second is the real one. A moving background asks for
     * a frame callback for as long as the app is on screen, which on a handheld
     * is a continuous draw behind a static list; and this device has a second
     * panel that is often showing that background beside a running game, on the
     * same GPU. Motion is the part of a wallpaper with the worst ratio of
     * noticed to paid for.
     *
     * Separate from the accessibility "reduce motion" setting, which is about
     * whether animation is tolerable rather than whether it is worth its cost.
     * Either being off is enough to stop it.
     */
    val animateWallpaper: Boolean = false,

    val cursorStyle: CursorStyle = CursorStyle.RING,
    val cursorAnimation: CursorAnimation = CursorAnimation.BREATHE,

    /** 0..1 intensity of the glow behind the cursor. */
    val highlightGlow: Float = 0.6f,

    val glassEffects: Boolean = true,

    /**
     * Overrides every corner in the app, so nothing is rounded on its own.
     *
     * Square by default, which overrides the starting theme's own radius — a
     * console grid reads as a grid when its cells share the panel's edges, and
     * [CornerStyle.THEME] is one selection away for anyone who wants the rounder
     * shape back.
     */
    val cornerStyle: CornerStyle = CornerStyle.SQUARE,

    /** Overrides the theme's motion personality. Null keeps the theme's own. */
    val motionOverride: MotionStyle? = null,

    /** Overrides the theme's typeface. Null keeps the theme's own. */
    val fontOverride: FontChoice? = null,

    val fontScale: Float = 1.0f,

    /**
     * How large the whole interface is drawn, as a multiplier.
     *
     * Distinct from [fontScale], which moves only the type and therefore
     * changes how a row is balanced — bigger words in the same box. This moves
     * every dp together: spacing, icons, corner radii, panel widths and the
     * text with them, so the layout keeps its proportions and simply arrives
     * closer or further away. It is the setting somebody means when they say
     * the interface is too small on a handheld held at reading distance, or
     * that too little fits on it.
     *
     * Applied on top of the per-screen calibration rather than instead of it,
     * so it stays a plain percentage of a sensible size on every panel and on a
     * television. See DesignScale.
     */
    val uiScale: Float = 1.0f,

    val transitionSpeed: Float = 1.0f,

    /** How a panel over artwork meets what is behind it; see [InfoPanelStyle]. */
    val infoPanelStyle: InfoPanelStyle = InfoPanelStyle.CARD,

    /**
     * Whether the bundled artwork is drawn.
     *
     * Carried through the theme rather than passed down, for the same reason it
     * is in Loki: the places that ask are several layers below the screen holding
     * the setting.
     */
    val bundledPlatformIcons: Boolean = true,
) {
    /**
     * These preferences in the form the palette generator takes.
     *
     * The only place the mapping lives, so nothing else has to remember that
     * light/dark is three-valued or that the intensity slider is a chroma
     * multiplier — and so a preview card and the live app cannot disagree about
     * what a setting means.
     *
     * @param systemDark what Android's own light/dark setting currently says,
     *   which is the answer [ThemeMode.SYSTEM] defers to
     */
    fun themeOptions(systemDark: Boolean = true): ThemeOptions = ThemeOptions(
        dark = when (themeMode) {
            ThemeMode.DARK -> true
            ThemeMode.LIGHT -> false
            ThemeMode.SYSTEM -> systemDark
        },
        contrast = contrastLevel,
        colorIntensity = colorIntensity,
        hueShift = accentHueShift,
        pureBlack = pureBlack,
        accentOverrideArgb = accentOverrideArgb,
        surfaceStyle = surfaceStyleOverride,
        depthScale = surfaceDepth,
        grainScale = grainAmount,
    )

    /**
     * The recipe in use: the applied custom theme, or the bundled one.
     *
     * Falls through to [themeId] whenever [activeCustomThemeId] names nothing,
     * which is the state left behind by deleting the applied theme and by a
     * settings file restored from a backup without it. The app has to be able to
     * draw in both cases.
     */
    val activeRecipe: ThemeRecipe
        get() = activeCustomThemeId
            ?.let { active -> customThemes.firstOrNull { it.id == active } }
            ?.toRecipe()
            ?: ThemeRecipe.of(themeId)

    /** Every theme the gallery offers: the bundled shelves, then the user's own. */
    val galleryRecipes: List<ThemeRecipe>
        get() = ThemeRecipe.ALL + customThemes.map(CustomTheme::toRecipe)

    /** What the gallery draws a tick on. See [ThemeRecipe.key]. */
    val activeThemeKey: String get() = activeRecipe.key

    /** The finished palette: [activeRecipe] resolved against everything else chosen. */
    fun resolveTheme(systemDark: Boolean = true): ThemeSpec =
        activeRecipe.resolve(themeOptions(systemDark))
}

/**
 * How every corner in the app is drawn.
 *
 * A single answer for the whole interface rather than a value each component
 * chooses for itself. Panels take their radius from the theme, but pills, tabs
 * and dialogs are shaped where they are written — so a hard-edged theme still
 * ends up with rounded furniture in it unless something says otherwise.
 */
@Serializable
enum class CornerStyle(val label: String) {
    /**
     * Every corner square, including the ones that are normally circles.
     *
     * First because it is the default, and a list whose default is third asks
     * the reader to hunt for where they already are.
     */
    SQUARE("Square"),

    /** Every corner rounded, generously and identically. */
    ROUNDED("Rounded"),

    /** Each theme's own radius, from hard-edged presets to very round ones. */
    THEME("Theme default"),
}

/**
 * The moving backgrounds a theme may name.
 *
 * Ported whole rather than trimmed to the two this app currently draws, because
 * every bundled theme in [ThemeRecipe] names one and dropping the others would
 * mean editing `Theme.kt` — which is the file that has to stay copy-identical to
 * Loki's for a theme added there to arrive here for free. An unrecognised value
 * costs a `when` branch; a diverged theme table costs an afternoon.
 */
@Serializable
enum class AnimatedWallpaper(val label: String) {
    /** Stacked flowing bands, and what a fresh install opens on. */
    WAVES("Waves"),

    NONE("Static"),

    /** Multi-point mesh gradient — soft overlapping colour fields. */
    MESH("Mesh"),
    AURORA("Aurora"),

    /** Large defocused orbs drifting at different depths. */
    BOKEH("Bokeh"),
    PARTICLES("Particles"),
    STARFIELD("Starfield"),
    GRADIENT_DRIFT("Gradient drift"),

    /** Layered shapes moving at different rates. */
    PARALLAX_ART("Parallax"),

    /** Takes its hue from whatever is highlighted. */
    ADAPTIVE("Adaptive"),
}

@Serializable
enum class CursorStyle(val label: String) {
    RING("Ring"),
    FILL("Fill"),
    CORNERS("Corner brackets"),
    UNDERLINE("Underline"),
    SPOTLIGHT("Spotlight"),
}

@Serializable
enum class CursorAnimation(val label: String) {
    NONE("None"),
    BREATHE("Breathe"),
    PULSE("Pulse"),
    ROTATE("Rotate"),
    SHIMMER("Shimmer"),
}

/**
 * The typefaces a theme may ask for.
 *
 * Only families guaranteed present on any Android device, so a theme can never
 * fail to render because a font failed to download.
 */
@Serializable
enum class FontChoice(val label: String) {
    SYSTEM("System"),
    ROUNDED("Rounded"),
    MONO("Monospace"),
    PIXEL("Pixel"),
    SERIF("Serif"),
}

/**
 * How a panel drawn over artwork meets what is behind it.
 *
 * Matters here for the same reason it does in Loki: the container detail panel
 * sits over cover art that is itself the subject, so where the panel ends is a
 * decision rather than a detail.
 */
@Serializable
enum class InfoPanelStyle(val label: String, val description: String) {
    /**
     * A card: opaque, edged, with a shadow under it.
     *
     * Every value on it is read against a known colour rather than against
     * whatever the artwork happens to be doing there. The safe answer, and the
     * default.
     */
    CARD("Card", "An opaque panel with a defined edge"),

    /** No edge at all: the panel fades out into the artwork. */
    BLENDED("Blended", "Fades into the artwork with no visible edge"),
}

@Serializable
data class PerformanceSettings(
    /** Disables blur, animated backgrounds and previews in one switch. */
    val performanceMode: Boolean = false,
    val animationsEnabled: Boolean = true,
    val blurEnabled: Boolean = true,
)

@Serializable
data class AccessibilitySettings(
    val highContrast: Boolean = false,
    val largeText: Boolean = false,
    val reduceMotion: Boolean = false,
    val colorBlindMode: ColorBlindMode = ColorBlindMode.NONE,
    /** Extra multiplier on all touch target sizes. */
    val touchTargetScale: Float = 1.0f,
)

@Serializable
enum class ColorBlindMode(val label: String) {
    NONE("Off"),
    PROTANOPIA("Protanopia"),
    DEUTERANOPIA("Deuteranopia"),
    TRITANOPIA("Tritanopia"),
    GRAYSCALE("Grayscale"),
}
