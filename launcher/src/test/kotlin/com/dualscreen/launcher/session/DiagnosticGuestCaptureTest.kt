package com.dualscreen.launcher.session

import com.google.common.truth.Truth.assertThat
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class DiagnosticGuestCaptureTest {
    @get:Rule val temporary = TemporaryFolder()

    @Test fun retainsLateFaultAfterRotationAndExportsPriorEngineLog() {
        val logs = temporary.newFolder("logs")
        val gameLog = temporary.newFile("Game.log").apply { writeText("previous crash") }
        val capture = DiagnosticGuestCapture(logs, gameLog, { throw it }, 128, false)
        try {
            capture.offer("startup".repeat(30))
            capture.flush()
            capture.offer("late execute fault c0000005 info[0]=8")
            capture.flush()
            assertThat(logs.resolve("guest-tail.log").readText()).contains("late execute fault")
            assertThat(logs.resolve("guest-tail.1.log").readText()).contains("startup")
            assertThat(logs.resolve("Game-before-launch.log").readText()).isEqualTo("previous crash")
            assertThat(logs.resolve("Game-latest.log").readText()).isEqualTo("previous crash")
        } finally { capture.close(); capture.awaitClosed() }
    }

    @Test fun overflowRetainsNewestFaultAndReportsMissingLines() {
        val logs = temporary.newFolder("overflow")
        val capture = DiagnosticGuestCapture(logs, temporary.root.resolve("missing.log"), { throw it }, startWorker = false)
        try {
            repeat(4200) { capture.offer("line $it") }
            capture.offer("last fault")
            capture.flush()
            val result = logs.resolve("guest-tail.log").readText()
            assertThat(result).contains("Queue overflow: 105")
            assertThat(result).contains("last fault")
            assertThat(result).doesNotContain("\nline 0\n")
        } finally { capture.close(); capture.awaitClosed() }
    }

    @Test fun windowReadyDoesNotStopDiagnosticCapture() {
        val source = java.io.File("src/main/kotlin/com/dualscreen/launcher/session/GameSessionActivity.kt").readText()
        assertThat(source).contains("if (!diagnosticsOnly) stopGuestOutputCapture()")
        assertThat(source).contains("diagnosticCapture?.close()")
    }
}
