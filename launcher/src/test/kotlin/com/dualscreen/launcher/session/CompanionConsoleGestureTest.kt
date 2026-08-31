package com.dualscreen.launcher.session

import com.google.common.truth.Truth.assertThat
import org.junit.Test

class CompanionConsoleGestureTest {
    @Test
    fun nativePanelRouter_reservesOnlyCenteredBottomStrip() {
        assertThat(isConsoleGestureStart(0.50f, 0.96f)).isTrue()
        assertThat(isConsoleGestureStart(0.39f, 0.96f)).isFalse()
        assertThat(isConsoleGestureStart(0.61f, 0.96f)).isFalse()
        assertThat(isConsoleGestureStart(0.50f, 0.93f)).isFalse()
    }

    @Test
    fun consoleGesture_requiresFullUpwardTravel() {
        assertThat(shouldToggleConsole(travelPx = -52f, thresholdPx = 52f)).isTrue()
        assertThat(shouldToggleConsole(travelPx = -51.9f, thresholdPx = 52f)).isFalse()
        assertThat(shouldToggleConsole(travelPx = 80f, thresholdPx = 52f)).isFalse()
        assertThat(shouldToggleConsole(travelPx = -80f, thresholdPx = 0f)).isFalse()
    }
}
