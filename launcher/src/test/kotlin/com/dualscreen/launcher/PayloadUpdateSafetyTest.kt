package com.dualscreen.launcher

import com.google.common.truth.Truth.assertThat
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class PayloadUpdateSafetyTest {
    @get:Rule
    val temporary = TemporaryFolder()

    @Test
    fun currentPayloadTargetIsPreserved() {
        val target = temporary.newFile("current-payload-file")
        var hashReads = 0

        val current = payloadTargetIsCurrent(target, "expected") {
            hashReads++
            "expected"
        }

        assertThat(current).isTrue()
        assertThat(hashReads).isEqualTo(1)
    }

    @Test
    fun missingOrChangedPayloadTargetNeedsAnUpdate() {
        val missing = temporary.root.resolve("missing-payload-file")
        var hashReads = 0
        assertThat(
            payloadTargetIsCurrent(missing, "expected") {
                hashReads++
                "expected"
            },
        ).isFalse()
        assertThat(hashReads).isEqualTo(0)

        val changed = temporary.newFile("changed-payload-file")
        assertThat(payloadTargetIsCurrent(changed, "expected") { "different" }).isFalse()
    }
}
