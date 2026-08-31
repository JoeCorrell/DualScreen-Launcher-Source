package com.dualscreen.launcher

import android.content.Context
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

data class LaunchStatus(
    val gameId: String,
    val gameTitle: String,
    val stage: String,
    val running: Boolean,
    val failure: String?,
    val lines: List<String>,
    val logFile: String,
)

/** Persistent, device-visible launch diagnostics shared by both displays. */
class LaunchStatusStore(context: Context) {
    private val lock = Any()
    private val fallbackDirectory = File(context.filesDir, "launch-logs")
    private val mutableStatus = MutableStateFlow<LaunchStatus?>(null)
    val status: StateFlow<LaunchStatus?> = mutableStatus.asStateFlow()
    private var output: File? = null
    private var guestOutputDropped = false

    fun begin(game: SupportedGame): File = synchronized(lock) {
        val file = runCatching { DualscreenStorage().launchLog(game) }.getOrElse {
            check(fallbackDirectory.isDirectory || fallbackDirectory.mkdirs()) {
                "Could not create the launch log directory"
            }
            File(fallbackDirectory, "${game.id}-latest.log")
        }
        file.writeText("")
        output = file
        guestOutputDropped = false
        val first = line("BEGIN", "Launching ${game.title}")
        appendFile(first, always = true)
        mutableStatus.value = LaunchStatus(
            gameId = game.id,
            gameTitle = game.title,
            stage = "BEGIN",
            running = true,
            failure = null,
            lines = listOf(first),
            logFile = file.absolutePath,
        )
        Log.i(TAG, first)
        file
    }

    fun record(stage: String, detail: String) = publish(stage, detail, running = true, failure = null)

    fun ready(detail: String) = publish("RUNNING", detail, running = true, failure = null)

    fun failed(detail: String) = publish("FAILED", detail, running = false, failure = detail)

    fun ended(detail: String) = publish("ENDED", detail, running = false, failure = null)

    /** Guest stdout/stderr is retained in the file without recomposing either UI per line. */
    fun guest(line: String) = synchronized(lock) {
        if (line.isBlank()) return@synchronized
        val file = output ?: return@synchronized
        if (file.length() >= MAX_GUEST_LOG_BYTES) {
            if (!guestOutputDropped) {
                guestOutputDropped = true
                appendFile(line("GUEST", "Output limit reached; further guest lines omitted"), always = true)
            }
            return@synchronized
        }
        appendFile(line("GUEST", line), always = false)
    }

    private fun publish(
        stage: String,
        detail: String,
        running: Boolean,
        failure: String?,
    ) = synchronized(lock) {
        val current = mutableStatus.value ?: return@synchronized
        val nextLine = line(stage, detail)
        appendFile(nextLine, always = true)
        Log.i(TAG, nextLine)
        mutableStatus.value = current.copy(
            stage = stage,
            running = running,
            failure = failure,
            lines = (current.lines + nextLine).takeLast(MAX_VISIBLE_LINES),
        )
    }

    private fun appendFile(value: String, always: Boolean) {
        val file = output ?: return
        if (always || file.length() < MAX_GUEST_LOG_BYTES) {
            file.appendText(value + System.lineSeparator())
        }
    }

    private fun line(stage: String, detail: String): String =
        "[${CLOCK.format(Date())}] [$stage] $detail"

    private companion object {
        const val TAG = "DualScreenLaunch"
        const val MAX_VISIBLE_LINES = 120
        const val MAX_GUEST_LOG_BYTES = 2L * 1024L * 1024L
        val CLOCK = SimpleDateFormat("HH:mm:ss.SSS", Locale.US)
    }
}
