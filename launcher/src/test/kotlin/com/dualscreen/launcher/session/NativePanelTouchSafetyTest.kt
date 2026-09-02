package com.dualscreen.launcher.session

import com.google.common.truth.Truth.assertThat
import org.junit.Test

class NativePanelTouchSafetyTest {
    @Test
    fun exactTerminalReplayIsRejectedWithoutSuppressingTheNextGesture() {
        val gate = NativeGestureCompletionGate()

        assertThat(gate.accept(downTime = 100L, eventTime = 140L)).isTrue()
        assertThat(gate.accept(downTime = 100L, eventTime = 140L)).isFalse()
        assertThat(gate.accept(downTime = 200L, eventTime = 240L)).isTrue()
    }

    @Test
    fun distinctCompletionTimesForOneDownAreNotCollapsed() {
        val gate = NativeGestureCompletionGate()

        assertThat(gate.accept(downTime = 100L, eventTime = 140L)).isTrue()
        assertThat(gate.accept(downTime = 100L, eventTime = 141L)).isTrue()
    }
}
