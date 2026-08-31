package com.wemu.session.secondscreen

import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.remember
import androidx.compose.ui.graphics.Color
import com.thor.core.designsystem.theme.LocalThorTheme
import com.thor.core.designsystem.theme.ThorColors
import com.thor.core.designsystem.theme.ThorTheme

/**
 * The original SecondScreen paper palette.
 *
 * The embedded panel must not inherit WEMU's active launcher palette: doing so
 * turns the ink blue (or another user-selected accent) over Stardew's tan menu
 * sprites. Keep this wrapper at the StardewScreen boundary so every page,
 * including inventory and settings, receives the same dark ink as the original
 * standalone SecondScreen app.
 */
@Composable
internal fun StardewPanelTheme(content: @Composable () -> Unit) {
    ThorTheme {
        val base = LocalThorTheme.current
        val themed = remember(base) { base.copy(colors = StardewPaperColors) }

        CompositionLocalProvider(
            LocalThorTheme provides themed,
            LocalContentColor provides StardewPaperColors.onSurface,
        ) {
            MaterialTheme(
                colorScheme = MaterialTheme.colorScheme.copy(
                    primary = StardewPaperColors.primary,
                    onPrimary = Color.White,
                    primaryContainer = StardewPaperColors.surfaceHighest,
                    onPrimaryContainer = StardewPaperColors.onSurface,
                    secondary = StardewPaperColors.secondary,
                    onSecondary = Color.White,
                    tertiary = StardewPaperColors.primary,
                    onTertiary = Color.White,
                    background = StardewPaperColors.background,
                    onBackground = StardewPaperColors.onBackground,
                    surface = StardewPaperColors.surface,
                    onSurface = StardewPaperColors.onSurface,
                    surfaceVariant = StardewPaperColors.surfaceHighest,
                    onSurfaceVariant = StardewPaperColors.onSurfaceVariant,
                    surfaceContainer = StardewPaperColors.surfaceElevated,
                    surfaceContainerHigh = StardewPaperColors.surfaceHighest,
                    surfaceContainerHighest = StardewPaperColors.surfaceHighest,
                    outline = StardewPaperColors.outline,
                    outlineVariant = StardewPaperColors.outline,
                    error = StardewPaperColors.error,
                    onError = Color.White,
                    scrim = StardewPaperColors.scrim,
                ),
                typography = MaterialTheme.typography,
                shapes = MaterialTheme.shapes,
                content = content,
            )
        }
    }
}

private val StardewPaperColors = ThorColors(
    primary = Color(0xFFC8791E),
    secondary = Color(0xFF8A4E12),
    accentEnd = Color(0xFF8A4E12),
    background = Color(0xFFF6EEDD),
    surface = Color(0xFFFFFAF0),
    surfaceElevated = Color(0xFFFFFAF0),
    surfaceHighest = Color(0xFFEADDC4),
    onBackground = Color(0xFF1B1714),
    onSurface = Color(0xFF1B1714),
    onSurfaceVariant = Color(0xFF5C5044),
    cursor = Color(0xFFC8791E),
    glow = Color(0xFFE0A64A),
    outline = Color(0xFFC9B79A),
    error = Color(0xFFA33223),
    scrim = Color(0x99000000),
    tints = listOf(
        Color(0xFFC8791E),
        Color(0xFF8A4E12),
        Color(0xFF6B8E4E),
        Color(0xFF4E7A8E),
        Color(0xFF8E4E6B),
    ),
)
