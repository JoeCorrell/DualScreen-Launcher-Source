package com.thor.core.designsystem.pointer

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.State
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.getValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.positionInWindow
import androidx.compose.ui.unit.toSize

/**
 * Where the pointer is inside this window, or null when it is not in it.
 *
 * Ported from Loki unchanged. A `State` rather than a plain value, and that is
 * the whole reason it works: the cursor moves every frame the stick is held, and
 * a value read during composition would recompose every hoverable element sixty
 * times a second to find out that all but one are still not hovered. Handed over
 * as state, the read happens inside a [derivedStateOf] that notifies only when
 * the *answer* changes.
 *
 * Nothing in this fork provides a position yet — there is no on-screen cursor
 * driven by a stick here — so it stays null and every `isHovered` is false. It is
 * ported anyway because the frontend's surfaces read it, and a card that silently
 * dropped its hover state would be a difference that only shows up with a mouse
 * attached.
 */
val LocalPointerPosition = staticCompositionLocalOf<State<Offset?>> {
    mutableStateOf(null)
}

/**
 * Haptics for the pointer entering something.
 *
 * A lambda rather than the feedback object, so this file needs to know nothing
 * about how the app makes a click. Null where no one has provided it.
 */
val LocalPointerHoverFeedback = staticCompositionLocalOf<(() -> Unit)?> { null }

/**
 * Whether the pointer is over one element.
 *
 * The element reports its own bounds and answers the question itself; there is
 * no central registry of what is on screen. A registry would have to be told
 * when anything moved, scrolled or was removed, and the one thing every surface
 * already does correctly is lay itself out.
 */
@Stable
class PointerHoverState internal constructor(position: State<Offset?>) {
    internal var bounds by mutableStateOf(Rect.Zero)

    /**
     * True while the cursor is inside this element.
     *
     * Derived, not collected. Every hoverable thing on screen has one of these,
     * and a plain field written by a `snapshotFlow` collector would mean one
     * coroutine per element and a full re-evaluation of all of them every time
     * the cursor moved a pixel.
     *
     * False whenever the pointer is down, because the position is null then — so
     * nothing has to remember to switch the highlight off when the pointer is
     * put away, and no element can be left lit by a cursor that no longer exists.
     */
    private val hovered = derivedStateOf {
        val point = position.value
        point != null && !bounds.isEmpty && bounds.contains(point)
    }

    val isHovered: Boolean get() = hovered.value

    /**
     * Whether this element has seen its first hover answer yet.
     *
     * Plain, not snapshot state: it is read and written only by the effect that
     * fires the haptic, and making it observable would invalidate that effect's
     * own caller every time it was set.
     */
    internal var settled = false
}

/**
 * Tracks the pointer against one element, and buzzes when it arrives.
 *
 * Pair with [Modifier.pointerHover], which is what supplies the bounds. The
 * haptic fires on entry only — firing on exit as well would double every
 * movement across a grid, which reads as a rattle rather than as feedback.
 */
@Composable
fun rememberPointerHover(): PointerHoverState {
    val position = LocalPointerPosition.current
    val state = remember(position) { PointerHoverState(position) }

    // Keyed on the element, never on the callback. A caller that rebuilds its
    // lambda each recomposition would otherwise restart this effect.
    val feedback = rememberUpdatedState(LocalPointerHoverFeedback.current)

    /*
     * Keyed on the answer, so this coroutine is created only when the element is
     * actually entered or left — not once per element for the whole time it is
     * on screen, waking on every pixel the cursor moves.
     */
    val hovered = state.isHovered
    LaunchedEffect(state, hovered) {
        // The first value still has to light the highlight. It merely is not an
        // arrival, so it should not vibrate just because a new page composed
        // beneath a resting pointer.
        if (hovered && !state.settled) feedback.value?.invoke()
        state.settled = true
    }

    return state
}

/**
 * Reports this element's position so [PointerHoverState] can answer for it.
 *
 * Position and size rather than `boundsInWindow`, which intersects with every
 * ancestor's clip. That sounds harmless and is not: an element inside a scrolling
 * column reports a *truncated* rectangle near the edges, so the highlight died
 * before the cursor reached the element's actual edge.
 */
fun Modifier.pointerHover(state: PointerHoverState): Modifier =
    onGloballyPositioned { coordinates ->
        val origin = coordinates.positionInWindow()
        state.bounds = Rect(origin, coordinates.size.toSize())
    }
