package com.thor.core.designsystem.theme

import com.google.common.truth.Truth.assertThat
import com.thor.core.model.MotionStyle
import org.junit.Test

class ThorMotionTest {

    @Test
    fun reduceMotionCollapsesEveryReusableDuration() {
        val motion = ThorMotion(
            style = MotionStyle.FLUID,
            speedMultiplier = 0.5f,
            reduceMotion = true,
        )

        assertThat(motion.cursorMillis).isEqualTo(0)
        assertThat(motion.controlMillis).isEqualTo(0)
        assertThat(motion.selectionMillis).isEqualTo(0)
        assertThat(motion.progressMillis).isEqualTo(0)
        assertThat(motion.panelMillis).isEqualTo(0)
        assertThat(motion.backdropMillis).isEqualTo(0)
    }

    @Test
    fun fasterPreferenceShortensControlAndProgressFeedback() {
        val normal = ThorMotion(MotionStyle.SMOOTH, speedMultiplier = 1f, reduceMotion = false)
        val fast = ThorMotion(MotionStyle.SMOOTH, speedMultiplier = 2f, reduceMotion = false)

        assertThat(fast.controlMillis).isLessThan(normal.controlMillis)
        assertThat(fast.progressMillis).isLessThan(normal.progressMillis)
    }
}
