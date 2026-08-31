package com.thor.core.display

import android.content.Context
import android.hardware.display.DisplayManager
import android.os.Handler
import android.os.Looper
import android.view.Display
import com.thor.core.common.log.ThorLog
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.flow.distinctUntilChanged

/**
 * Watches the attached displays.
 *
 * Emits a fresh snapshot whenever a display is added, removed or reconfigured
 * (a resolution or refresh-rate change counts), so the app can move the session
 * console between panels without the user restarting anything.
 *
 * Loki injects this; here it is constructed by hand from the application
 * container. Hilt earns its keep across fifteen modules and a dozen
 * repositories, and this app has neither — adding KSP and an annotation
 * processor to supply four singletons would cost more build time than it saves
 * in wiring.
 */
class ThorDisplayMonitor(
    context: Context,
) {

    private val displayManager: DisplayManager =
        context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager

    /** All displays, primary first, deduplicated and sorted by id. */
    val displays: Flow<List<ThorDisplayInfo>> = callbackFlow {
        fun emitSnapshot() {
            trySend(snapshot())
        }

        val listener = object : DisplayManager.DisplayListener {
            override fun onDisplayAdded(displayId: Int) {
                ThorLog.i("Display", "Display added: $displayId")
                emitSnapshot()
            }

            override fun onDisplayRemoved(displayId: Int) {
                ThorLog.i("Display", "Display removed: $displayId")
                emitSnapshot()
            }

            override fun onDisplayChanged(displayId: Int) = emitSnapshot()
        }

        // The listener must be registered against a Looper thread; the flow
        // itself may be collected from anywhere.
        displayManager.registerDisplayListener(listener, Handler(Looper.getMainLooper()))
        emitSnapshot()

        awaitClose { displayManager.unregisterDisplayListener(listener) }
    }.distinctUntilChanged()

    /** Immediate, non-observing read. */
    fun snapshot(): List<ThorDisplayInfo> =
        displayManager.displays
            .distinctBy { it.displayId }
            .sortedBy { it.displayId }
            .map { it.toInfo() }

    /**
     * The display the info panel should occupy, or `null` when the device only
     * has one usable screen.
     */
    fun findSecondaryDisplay(): Display? =
        displayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)
            .firstOrNull(DisplayTopology::isUsableSecondary)
            ?: displayManager.displays.firstOrNull(DisplayTopology::isUsableSecondary)

    fun displayById(displayId: Int): Display? = displayManager.getDisplay(displayId)

    private fun Display.toInfo(): ThorDisplayInfo {
        @Suppress("DEPRECATION")
        val metrics = android.util.DisplayMetrics().also { getRealMetrics(it) }
        return ThorDisplayInfo(
            displayId = displayId,
            name = name ?: "Display $displayId",
            widthPx = metrics.widthPixels,
            heightPx = metrics.heightPixels,
            densityDpi = metrics.densityDpi,
            refreshRate = refreshRate,
            isPrimary = displayId == Display.DEFAULT_DISPLAY,
            isPresentationCapable = DisplayTopology.isUsableSecondary(this),
        )
    }
}
