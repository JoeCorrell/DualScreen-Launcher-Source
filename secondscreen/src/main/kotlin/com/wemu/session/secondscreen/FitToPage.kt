package com.wemu.session.secondscreen

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.TransformOrigin
import androidx.compose.ui.layout.Placeable
import androidx.compose.ui.layout.SubcomposeLayout
import androidx.compose.ui.unit.Constraints
import kotlin.math.roundToInt

@Composable
fun FitToPage(
    modifier: Modifier = Modifier,

    grow: Boolean = true,
    content: @Composable () -> Unit,
) {
    SubcomposeLayout(modifier) { constraints ->
        val width = constraints.maxWidth
        val height = constraints.maxHeight

        if (height == Constraints.Infinity || height <= 0 || width <= 0) {
            val plain = subcompose("plain", content).first().measure(constraints)
            return@SubcomposeLayout layout(plain.width, plain.height) { plain.place(0, 0) }
        }

        fun measureAt(slot: Any, scale: Float): Placeable =
            subcompose(slot, content)
                .first()
                .measure(Constraints(maxWidth = (width / scale).roundToInt().coerceAtLeast(1)))

        fun scaleFor(placeable: Placeable): Float = when {
            placeable.height <= 0 -> 1f
            else -> (height.toFloat() / placeable.height)
                .coerceAtMost(if (grow) MAXIMUM_SCALE else 1f)
        }

        var placeable = measureAt("pass1", 1f)
        var scale = scaleFor(placeable)

        var round = 0
        while (round < MAXIMUM_ROUNDS) {
            val next = measureAt("pass${round + 2}", scale)
            val nextScale = scaleFor(next)
            placeable = next
            val settledEnough = kotlin.math.abs(nextScale - scale) <= scale * TOLERANCE
            scale = nextScale
            round++
            if (settledEnough) break
        }

        val settled = placeable
        val finalScale = minOf(scale, height.toFloat() / settled.height.coerceAtLeast(1))

        layout(width, height) {
            settled.placeWithLayer(0, 0) {
                scaleX = finalScale
                scaleY = finalScale

                transformOrigin = TransformOrigin(0f, 0f)
            }
        }
    }
}

private const val MAXIMUM_SCALE = 1.25f

private const val MAXIMUM_ROUNDS = 4

private const val TOLERANCE = 0.01f
