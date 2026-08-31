package com.dualscreen.launcher

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.unit.dp

/**
 * A card-coloured block with the app's hairline around it.
 *
 * Three lines that appear on nearly every screen, so they are one composable.
 * The value is not the saved typing — it is that a panel added later cannot
 * accidentally arrive with a 2dp border or the card colour from a different
 * token, which is the whole failure mode a design system exists to prevent.
 */
@Composable
fun CompanionPanel(
    modifier: Modifier = Modifier,
    shape: Shape = CompanionTheme.shapes.card,
    content: @Composable ColumnScope.() -> Unit,
) {
    val colors = CompanionTheme.colors
    Column(
        modifier = modifier
            .clip(shape)
            .background(colors.card)
            .border(BorderStroke(1.dp, colors.outline), shape),
        content = content,
    )
}
