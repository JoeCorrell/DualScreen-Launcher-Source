package com.dualscreen.launcher.session

import android.app.ActivityManager
import android.content.ClipboardManager
import android.content.Context
import android.content.SharedPreferences
import android.os.SystemClock
import android.view.KeyEvent
import android.view.View
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.content.edit
import com.thor.core.common.log.ThorLog
import com.wemu.engine.container.Container
import com.wemu.engine.core.Callback
import com.wemu.engine.core.CPUStatus
import com.wemu.engine.core.GPUStatus
import com.wemu.engine.core.ProcessHelper
import com.wemu.engine.renderer.CompositorRenderer
import com.wemu.engine.renderer.XServerRenderer
import com.wemu.engine.session.RendererOptions
import com.wemu.engine.widget.TouchpadView
import com.wemu.engine.widget.InputControlsView
import com.wemu.engine.winhandler.OnGetProcessInfoListener
import com.wemu.engine.winhandler.ProcessInfo
import com.wemu.engine.winhandler.WinHandler
import kotlinx.coroutines.delay
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.util.concurrent.atomic.AtomicInteger

/** Live emulator controls exposed by Companion's WEMU session console. */
@Stable
class CompanionRuntimeControls(
    context: Context,
    private val compositor: CompositorRenderer?,
    private val activeRenderer: XServerRenderer,
    private val touchpad: TouchpadView,
    private val inputControls: InputControlsView,
    private val winHandler: WinHandler,
    private val container: Container,
    private val preferences: SharedPreferences,
    injectKey: (KeyEvent) -> Unit,
    injectCharacter: (Char) -> Boolean,
) {
    private val appContext = context.applicationContext

    val keyboard = CompanionGuestKeyboard(
        injectKey = injectKey,
        injectCharacter = injectCharacter,
        readClipboard = {
            val clipboard = appContext.getSystemService(ClipboardManager::class.java)
            clipboard?.primaryClip
                ?.takeIf { it.itemCount > 0 }
                ?.getItemAt(0)
                ?.coerceToText(appContext)
                ?.toString()
        },
    )

    val processes = CompanionProcessMonitor(winHandler)

    private val fpsLimitState = mutableIntStateOf(RendererOptions.fpsLimitOf(container))
    var fpsLimit: Int
        get() = fpsLimitState.intValue
        set(value) {
            val safe = value.coerceIn(0, 240)
            fpsLimitState.intValue = safe
            activeRenderer.setFpsLimit(safe)
            persistGraphics()
        }

    private val upscalerState = mutableStateOf(
        (container.rendererFilterMode - RendererOptions.FILTER_UPSCALER_BASE)
            .takeIf { container.rendererFilterMode >= RendererOptions.FILTER_UPSCALER_BASE },
    )
    var upscaler: Int?
        get() = upscalerState.value
        set(value) {
            upscalerState.value = value
            compositor?.setFilterMode(filterMode())
            persistGraphics()
        }

    private val sharpnessState = mutableFloatStateOf(RendererOptions.sharpnessOf(container))
    var sharpness: Float
        get() = sharpnessState.floatValue
        set(value) {
            val safe = value.coerceIn(0f, 1f)
            sharpnessState.floatValue = safe
            compositor?.setSharpness(safe)
            persistGraphics()
        }

    private val postFxState = mutableIntStateOf(
        container.getExtra("graphicsPostFXMode")?.toIntOrNull() ?: 0,
    )
    var postFxMode: Int
        get() = postFxState.intValue
        set(value) {
            val safe = value.coerceIn(0, RendererOptions.POST_FX_MODES.lastIndex)
            postFxState.intValue = safe
            compositor?.setPostFXMode(safe)
            persistGraphics()
        }

    private val presentModeState = mutableStateOf(container.rendererPresentMode ?: "fifo")
    var presentMode: String
        get() = presentModeState.value
        set(value) {
            val safe = value.takeIf { it in setOf("fifo", "mailbox", "immediate") } ?: "fifo"
            presentModeState.value = safe
            compositor?.setVkPresentMode(RendererOptions.toVkPresentMode(safe))
            container.rendererPresentMode = safe
            container.saveData()
        }

    private val swapRedBlueState = mutableStateOf(container.rendererSwapRB)
    var swapRedBlue: Boolean
        get() = swapRedBlueState.value
        set(value) {
            swapRedBlueState.value = value
            compositor?.setSwapRB(value)
            container.rendererSwapRB = value
            container.saveData()
        }

    val shaderControlsAvailable: Boolean get() = compositor != null

    var stretched by mutableStateOf(compositor?.isFullscreen ?: false)
        private set

    fun toggleStretch() {
        compositor?.toggleFullscreen()
        touchpad.toggleFullscreen()
        stretched = compositor?.isFullscreen ?: false
    }

    private val softStretchState = mutableStateOf(false)
    var softStretch: Boolean
        get() = softStretchState.value
        set(value) {
            softStretchState.value = value
            compositor?.setStretchMode(if (value) 1 else 0)
        }

    private val fitWindowState = mutableStateOf(activeRenderer.isFitGuestWindow)
    var fitWindow: Boolean
        get() = fitWindowState.value
        set(value) {
            fitWindowState.value = value
            activeRenderer.setFitGuestWindow(value)
            container.putExtra("fitGuestWindow", if (value) "1" else "0")
            container.saveData()
        }

    private val mouseEnabledState = mutableStateOf(true)
    var mouseEnabled: Boolean
        get() = mouseEnabledState.value
        set(value) {
            mouseEnabledState.value = value
            touchpad.setMouseEnabled(value)
        }

    private val cursorSpeedState = mutableFloatStateOf(preferences.getFloat(CURSOR_SPEED, 1f))
    var cursorSpeed: Float
        get() = cursorSpeedState.floatValue
        set(value) {
            val safe = value.coerceIn(0.25f, 3f)
            cursorSpeedState.floatValue = safe
            touchpad.setSensitivity(safe)
            preferences.edit { putFloat(CURSOR_SPEED, safe) }
        }

    private val pointerCaptureState = mutableStateOf(false)
    var pointerCapture: Boolean
        get() = pointerCaptureState.value
        set(value) {
            pointerCaptureState.value = value
            if (value) {
                touchpad.requestFocus()
                touchpad.requestPointerCapture()
            } else {
                touchpad.releasePointerCapture()
            }
        }

    private val rightClickState = mutableStateOf(touchpad.isPointerButtonRightEnabled)
    var rightClick: Boolean
        get() = rightClickState.value
        set(value) {
            rightClickState.value = value
            touchpad.isPointerButtonRightEnabled = value
        }

    private val xInputState = mutableStateOf(winHandler.inputType == WinHandler.FLAG_INPUT_TYPE_XINPUT)
    var xInput: Boolean
        get() = xInputState.value
        set(value) {
            xInputState.value = value
            winHandler.inputType = if (value) {
                WinHandler.FLAG_INPUT_TYPE_XINPUT
            } else {
                WinHandler.FLAG_INPUT_TYPE_DINPUT
            }
        }

    val vibrationSlots = mutableStateListOf<Boolean>().apply {
        repeat(winHandler.maxControllers) { add(winHandler.isVibrationEnabledForSlot(it)) }
    }

    fun setVibration(slot: Int, enabled: Boolean) {
        if (slot !in vibrationSlots.indices) return
        vibrationSlots[slot] = enabled
        winHandler.setVibrationEnabledForSlot(slot, enabled)
    }

    /** Keeps the hidden touch-control view available without displaying it. */
    fun hideTouchControls() {
        inputControls.visibility = View.GONE
    }

    private fun filterMode(): Int = upscaler
        ?.let { RendererOptions.FILTER_UPSCALER_BASE + it }
        ?: container.rendererFilterMode.takeIf { it < RendererOptions.FILTER_UPSCALER_BASE }
        ?: 0

    private fun persistGraphics() {
        RendererOptions.save(container, fpsLimit, filterMode(), sharpness, postFxMode)
    }

    private companion object {
        const val CURSOR_SPEED = "cursor_speed"
    }
}

/** Bounded, rate-limited capture of Wine/Box64/FEX output for the Logs page. */
@Stable
class CompanionSessionLog {
    var lines by mutableStateOf<List<String>>(emptyList())
        private set

    private val buffer = ArrayDeque<String>()
    private var dirty = false
    private val callback = Callback<String> { append(it) }

    fun attach() {
        ProcessHelper.addDebugCallback(callback)
        note("Companion session log attached")
    }

    fun stop() {
        ProcessHelper.removeDebugCallback(callback)
        publish()
    }

    fun note(message: String) {
        append("[companion] $message")
        publish()
    }

    suspend fun pumpForever() {
        while (true) {
            delay(100L)
            publish()
        }
    }

    private fun append(line: String) {
        synchronized(buffer) {
            buffer.addLast(line)
            while (buffer.size > MAX_LINES) buffer.removeFirst()
            dirty = true
        }
    }

    private fun publish() {
        synchronized(buffer) {
            if (!dirty) return
            dirty = false
            lines = buffer.toList()
        }
    }

    private companion object {
        const val MAX_LINES = 500
    }
}

/** Guest process list, polled only while its console page is visible. */
@Stable
class CompanionProcessMonitor(private val winHandler: WinHandler) {
    var processes by mutableStateOf<List<ProcessInfo>>(emptyList())
        private set
    var hasReported by mutableStateOf(false)
        private set

    private val incoming = mutableListOf<ProcessInfo>()
    private val listener = OnGetProcessInfoListener { index, count, process ->
        synchronized(incoming) {
            if (index == 0) incoming.clear()
            if (process != null) incoming += process
            if (index == count - 1) {
                processes = incoming.sortedBy { it.name.lowercase() }
                hasReported = true
                incoming.clear()
            }
        }
    }

    suspend fun pollWhileVisible() {
        winHandler.onGetProcessInfoListener = listener
        try {
            while (true) {
                winHandler.listProcesses()
                delay(2_000L)
            }
        } finally {
            if (winHandler.onGetProcessInfoListener === listener) {
                winHandler.onGetProcessInfoListener = null
            }
        }
    }

    fun kill(process: ProcessInfo) {
        winHandler.killProcess(process.name)
        processes = processes.filterNot { it.pid == process.pid }
    }
}

/** Lightweight HUD sampling; expensive Android reads run only while console is open. */
@Stable
class CompanionSessionMetrics(context: Context) {
    private val activityManager =
        context.applicationContext.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
    private val startedAt = SystemClock.elapsedRealtime()
    private var lastSample = startedAt
    private val frameCount = AtomicInteger(0)

    var consoleVisible by mutableStateOf(false)
    var fps by mutableIntStateOf(0)
        private set
    var frameTimeMs by mutableFloatStateOf(0f)
        private set
    var onePercentLowFps by mutableIntStateOf(0)
        private set
    var fpsHistory by mutableStateOf<List<Int>>(emptyList())
        private set
    var uptimeSeconds by mutableIntStateOf(0)
        private set
    var usedMemoryMb by mutableIntStateOf(0)
        private set
    var totalMemoryMb by mutableIntStateOf(0)
        private set
    var memoryHistory by mutableStateOf<List<Int>>(emptyList())
        private set
    var cpuUsagePercent by mutableIntStateOf(-1)
        private set
    var cpuClocks by mutableStateOf<List<Int>>(emptyList())
        private set
    var cpuMaxClock by mutableIntStateOf(0)
        private set
    val cpuCoreCount: Int = Runtime.getRuntime().availableProcessors()
    var cpuHistory by mutableStateOf<List<Int>>(emptyList())
        private set
    var gpuLoadPercent by mutableIntStateOf(-1)
        private set
    var gpuClockMhz by mutableIntStateOf(-1)
        private set
    var gpuMaxClockMhz by mutableIntStateOf(-1)
        private set
    var gpuHistory by mutableStateOf<List<Int>>(emptyList())
        private set
    var zeroCopy by mutableStateOf<Boolean?>(null)
        private set

    private var latestZeroCopy = false
    private val fpsSamples = ArrayDeque<Int>()
    private val cpuSamples = ArrayDeque<Int>()
    private val memorySamples = ArrayDeque<Int>()
    private val gpuSamples = ArrayDeque<Int>()
    private var previousCpuTotal = 0L
    private var previousCpuIdle = 0L

    val frameListener = CompositorRenderer.FrameListener { shared ->
        latestZeroCopy = shared
        frameCount.incrementAndGet()
    }

    fun onFramePresented(shared: Boolean) {
        latestZeroCopy = shared
        frameCount.incrementAndGet()
    }

    suspend fun sampleForever() {
        while (true) {
            delay(1_000L)
            val now = SystemClock.elapsedRealtime()
            val elapsed = (now - lastSample).coerceAtLeast(1L)
            lastSample = now
            val frames = frameCount.getAndSet(0)
            fps = ((frames * 1_000L) / elapsed).toInt()
            frameTimeMs = if (fps > 0) 1_000f / fps else 0f
            push(fpsSamples, fps)
            fpsHistory = fpsSamples.toList()
            onePercentLowFps = fpsSamples.sorted().let { samples ->
                if (samples.size < 5) 0 else samples[(samples.lastIndex * 0.01f).toInt()]
            }
            uptimeSeconds = ((now - startedAt) / 1_000L).toInt()
            if (frames > 0) zeroCopy = latestZeroCopy

            if (consoleVisible) {
                val reading = withContext(Dispatchers.IO) { sampleResources() }
                usedMemoryMb = reading.usedMemoryMb
                totalMemoryMb = reading.totalMemoryMb
                cpuUsagePercent = reading.cpuUsagePercent
                cpuClocks = reading.cpuClocks
                cpuMaxClock = reading.cpuMaxClock
                gpuLoadPercent = reading.gpuLoadPercent
                gpuClockMhz = reading.gpuClockMhz
                gpuMaxClockMhz = reading.gpuMaxClockMhz
                if (reading.cpuUsagePercent >= 0) push(cpuSamples, reading.cpuUsagePercent)
                if (reading.totalMemoryMb > 0) {
                    push(memorySamples, reading.usedMemoryMb * 100 / reading.totalMemoryMb)
                }
                if (reading.gpuLoadPercent >= 0) push(gpuSamples, reading.gpuLoadPercent)
                cpuHistory = cpuSamples.toList()
                memoryHistory = memorySamples.toList()
                gpuHistory = gpuSamples.toList()
            }
        }
    }

    private fun sampleResources(): ResourceReading {
        val memory = ActivityManager.MemoryInfo().also(activityManager::getMemoryInfo)
        val clocks = runCatching { CPUStatus.getCurrentClockSpeeds().map(Short::toInt) }
            .getOrDefault(emptyList())
            .filter { it > 0 }
        val maximum = runCatching {
            (0 until cpuCoreCount).maxOfOrNull { CPUStatus.getMaxClockSpeed(it).toInt() } ?: 0
        }.getOrDefault(0)
        return ResourceReading(
            usedMemoryMb = ((memory.totalMem - memory.availMem) / BYTES_PER_MB).toInt(),
            totalMemoryMb = (memory.totalMem / BYTES_PER_MB).toInt(),
            cpuUsagePercent = readCpuUsage(),
            cpuClocks = clocks,
            cpuMaxClock = maximum,
            gpuLoadPercent = runCatching { GPUStatus.getLoadPercent() }.getOrDefault(-1),
            gpuClockMhz = runCatching { GPUStatus.getClockMhz() }.getOrDefault(-1),
            gpuMaxClockMhz = runCatching { GPUStatus.getMaxClockMhz() }.getOrDefault(-1),
        )
    }

    private fun readCpuUsage(): Int {
        val values = runCatching {
            File("/proc/stat").useLines { lines ->
                lines.firstOrNull { it.startsWith("cpu ") }
            }?.trim()?.split(Regex("\\s+"))?.drop(1)?.map(String::toLong)
        }.getOrNull() ?: return -1
        if (values.size < 4) return -1
        val total = values.sum()
        val idle = values.getOrElse(3) { 0L } + values.getOrElse(4) { 0L }
        val deltaTotal = total - previousCpuTotal
        val deltaIdle = idle - previousCpuIdle
        previousCpuTotal = total
        previousCpuIdle = idle
        if (deltaTotal <= 0L) return -1
        return (((deltaTotal - deltaIdle) * 100L) / deltaTotal).toInt().coerceIn(0, 100)
    }

    private fun push(samples: ArrayDeque<Int>, value: Int) {
        samples.addLast(value)
        while (samples.size > HISTORY_SAMPLES) samples.removeFirst()
    }

    private data class ResourceReading(
        val usedMemoryMb: Int,
        val totalMemoryMb: Int,
        val cpuUsagePercent: Int,
        val cpuClocks: List<Int>,
        val cpuMaxClock: Int,
        val gpuLoadPercent: Int,
        val gpuClockMhz: Int,
        val gpuMaxClockMhz: Int,
    )

    private companion object {
        const val BYTES_PER_MB = 1_048_576L
        const val HISTORY_SAMPLES = 60
    }
}
