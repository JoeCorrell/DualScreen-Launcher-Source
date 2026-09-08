package com.dualscreen.launcher.session

import java.io.File
import java.io.RandomAccessFile
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/** Session-long, bounded crash evidence. Guest pipe readers never do file IO. */
internal class DiagnosticGuestCapture(
    private val directory: File,
    private val gameLog: File,
    private val onError: (Exception) -> Unit,
    private val segmentBytes: Long = 2L * 1024 * 1024,
    startWorker: Boolean = true,
) {
    private val lock = Any()
    private val pending = ArrayDeque<String>()
    private var dropped = 0
    private var closed = false
    private var started = false
    private var lastGameCopyNanos = 0L
    private val worker = Executors.newSingleThreadScheduledExecutor { task ->
        Thread(task, "CrysisDiagnosticCapture").apply { isDaemon = true }
    }

    init {
        if (startWorker) worker.scheduleWithFixedDelay(::flushSafely, 0, 250, TimeUnit.MILLISECONDS)
    }

    fun offer(line: String) = synchronized(lock) {
        if (!closed && line.isNotBlank()) {
            if (pending.size == 4096) { pending.removeFirst(); dropped++ }
            pending.addLast(line.take(16_384))
        }
    }

    // Only the worker (or a single-threaded unit test) may call this.
    internal fun flush() {
        check(directory.isDirectory || directory.mkdirs())
        val tail = File(directory, "guest-tail.log")
        if (!started) {
            if (tail.isFile) tail.copyTo(File(directory, "guest-previous-session.log"), overwrite = true)
            tail.writeText("Crysis session capture started ${java.util.Date()}\n")
            copyGameLog("Game-before-launch.log")
            started = true
        }
        val batch = synchronized(lock) {
            buildList {
                if (dropped > 0) add("[capture] Queue overflow: $dropped older lines omitted")
                dropped = 0
                while (pending.isNotEmpty()) add(pending.removeFirst())
            }
        }
        // Bound each rotation; retain the last two segments, never stop at a cap.
        batch.chunked(128).forEach { lines ->
            if (tail.length() >= segmentBytes) {
                tail.copyTo(File(directory, "guest-tail.1.log"), overwrite = true)
                tail.writeText("[capture] Continued ${java.util.Date()}\n")
            }
            tail.appendText(lines.joinToString("\n", postfix = "\n"))
        }
        val now = System.nanoTime()
        if (now - lastGameCopyNanos >= TimeUnit.SECONDS.toNanos(5) || lastGameCopyNanos == 0L) {
            copyGameLog("Game-latest.log")
            lastGameCopyNanos = now
        }
    }

    private fun copyGameLog(name: String) {
        if (!gameLog.isFile) return
        RandomAccessFile(gameLog, "r").use { input ->
            val fileLength = input.length()
            val length = minOf(fileLength, 2L * 1024 * 1024).toInt()
            input.seek(fileLength - length)
            val bytes = ByteArray(length)
            input.readFully(bytes)
            File(directory, name).writeBytes(bytes)
        }
    }

    private fun flushSafely() {
        try { flush() } catch (failure: Exception) { onError(failure) }
    }

    fun close() {
        synchronized(lock) {
            if (closed) return
            closed = true
        }
        // Drain after the pipe callback is detached. Do not block the UI thread.
        worker.execute {
            flushSafely()
            try { copyGameLog("Game-latest.log") } catch (failure: Exception) { onError(failure) }
        }
        worker.shutdown()
    }

    internal fun awaitClosed() = worker.awaitTermination(5, TimeUnit.SECONDS)
}
