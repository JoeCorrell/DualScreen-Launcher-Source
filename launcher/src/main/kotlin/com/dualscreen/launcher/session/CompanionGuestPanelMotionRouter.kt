package com.dualscreen.launcher.session

import android.view.MotionEvent
import com.thor.core.common.log.ThorLog
import com.wemu.session.secondscreen.SecondScreen
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.hypot
import kotlin.math.ln

/**
 * Routes native lower-panel touch without ever creating gameplay mouse input.
 *
 * This is the same ordered gesture protocol used by WEMU. The centered strip at
 * the bottom is deliberately bypassed so Compose can own the console gesture.
 */
internal class CompanionGuestPanelMotionRouter {
    @Volatile var enabled: Boolean = false

    private var width = 0f
    private var height = 0f
    private var active = false
    private var bypass = false
    private var dragging = false
    private var pinching = false
    private var startX = 0f
    private var startY = 0f
    private var lastX = 0f
    private var lastY = 0f
    private var downTime = 0L
    private var lastSpan = 0f
    private var pendingPinchLogScale = 0.0
    private var lastPinchPacketTime = 0L
    private var pinchSamples = 0
    private var pinchPackets = 0
    private var pinchStartSpan = 0f
    private var pinchCenterX = 0f
    private var pinchCenterY = 0f

    fun resize(newWidth: Int, newHeight: Int) {
        width = newWidth.coerceAtLeast(0).toFloat()
        height = newHeight.coerceAtLeast(0).toFloat()
    }

    fun reset() {
        if (active && dragging && width > 0f && height > 0f) {
            SecondScreen.sendGuestGesture("cancel", normalizedX(lastX), normalizedY(lastY))
        }
        active = false
        bypass = false
        dragging = false
        finishPinch("router-reset", flush = false)
        pinching = false
        lastSpan = 0f
    }

    fun dispatch(event: MotionEvent): Boolean {
        if (!enabled || width <= 0f || height <= 0f) {
            if (active) reset()
            return false
        }

        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                startX = event.x
                startY = event.y
                lastX = startX
                lastY = startY
                downTime = event.eventTime
                dragging = false
                pinching = false
                lastSpan = 0f
                pendingPinchLogScale = 0.0
                pinchSamples = 0
                pinchPackets = 0
                pinchStartSpan = 0f
                lastPinchPacketTime = event.eventTime
                bypass = isConsoleGestureStart(normalizedX(startX), normalizedY(startY))
                active = !bypass
                return !bypass
            }

            MotionEvent.ACTION_POINTER_DOWN -> {
                if (bypass || !active) return false
                beginPinch(event)
                return true
            }

            MotionEvent.ACTION_MOVE -> {
                if (bypass || !active) return false
                if (event.pointerCount >= 2) {
                    if (!pinching) beginPinch(event) else accumulatePinch(event)
                    return true
                }
                if (pinching) return true

                lastX = event.x
                lastY = event.y
                val slop = minOf(width, height) * 0.012f
                if (!dragging && hypot(
                        (lastX - startX).toDouble(),
                        (lastY - startY).toDouble(),
                    ).toFloat() >= slop
                ) {
                    dragging = true
                    SecondScreen.sendGuestGesture(
                        "down",
                        normalizedX(startX),
                        normalizedY(startY),
                    )
                }
                if (dragging) {
                    SecondScreen.sendGuestGesture(
                        "move",
                        normalizedX(lastX),
                        normalizedY(lastY),
                    )
                }
                return true
            }

            MotionEvent.ACTION_POINTER_UP -> {
                if (bypass || !active) return false
                if (!pinching) beginPinch(event) else accumulatePinch(event)
                sendPendingPinch(event.eventTime, terminal = true)
                return true
            }

            MotionEvent.ACTION_UP -> {
                if (bypass || !active) {
                    resetWithoutCancel()
                    return false
                }
                lastX = event.x
                lastY = event.y
                if (dragging) {
                    SecondScreen.sendGuestGesture(
                        "up",
                        normalizedX(lastX),
                        normalizedY(lastY),
                    )
                } else if (!pinching) {
                    val heldSeconds =
                        ((event.eventTime - downTime).coerceAtLeast(0L)) / 1_000f
                    SecondScreen.sendGuestGesture(
                        if (heldSeconds >= HOLD_SECONDS) "hold" else "tap",
                        normalizedX(lastX),
                        normalizedY(lastY),
                        heldSeconds,
                    )
                }
                if (pinching) finishPinch("up", flush = true, eventTime = event.eventTime)
                resetWithoutCancel()
                return true
            }

            MotionEvent.ACTION_CANCEL -> {
                if (bypass || !active) {
                    resetWithoutCancel()
                    return false
                }
                SecondScreen.sendGuestGesture(
                    "cancel",
                    normalizedX(lastX),
                    normalizedY(lastY),
                )
                if (pinching) finishPinch("cancel", flush = false)
                resetWithoutCancel()
                return true
            }
        }
        return active && !bypass
    }

    private fun resetWithoutCancel() {
        active = false
        bypass = false
        dragging = false
        pinching = false
        lastSpan = 0f
        pendingPinchLogScale = 0.0
    }

    private fun normalizedX(value: Float): Float = (value / width).coerceIn(0f, 1f)
    private fun normalizedY(value: Float): Float = (value / height).coerceIn(0f, 1f)

    private fun beginPinch(event: MotionEvent) {
        if (event.pointerCount < 2) return
        if (dragging) {
            SecondScreen.sendGuestGesture(
                "cancel",
                normalizedX(lastX),
                normalizedY(lastY),
            )
            dragging = false
        }
        pinching = true
        lastSpan = span(event)
        pinchStartSpan = lastSpan
        pendingPinchLogScale = 0.0
        lastPinchPacketTime = event.eventTime
        pinchSamples = 0
        pinchPackets = 0
        updatePinchCenter(event)
    }

    private fun accumulatePinch(event: MotionEvent) {
        if (event.pointerCount < 2) return
        for (history in 0 until event.historySize) {
            updatePinchCenter(event, history)
            accumulatePinchSample(
                span(event, history),
                event.getHistoricalEventTime(history),
            )
        }
        updatePinchCenter(event)
        accumulatePinchSample(span(event), event.eventTime)
    }

    private fun accumulatePinchSample(currentSpan: Float, eventTime: Long) {
        if (!currentSpan.isFinite() || currentSpan <= 0f) return
        val previousSpan = lastSpan
        lastSpan = currentSpan
        if (!previousSpan.isFinite() || previousSpan <= 0f) return
        val ratio = currentSpan.toDouble() / previousSpan.toDouble()
        if (!ratio.isFinite() || ratio <= 0.0) return
        pendingPinchLogScale += ln(ratio)
        pinchSamples++
        sendPendingPinch(eventTime, terminal = false)
    }

    private fun sendPendingPinch(eventTime: Long, terminal: Boolean) {
        val magnitude = abs(pendingPinchLogScale)
        val enoughScale = magnitude >= PINCH_PACKET_LOG_THRESHOLD
        val enoughTime = eventTime - lastPinchPacketTime >= PINCH_PACKET_MAX_DELAY_MS &&
            magnitude >= PINCH_TIMED_LOG_THRESHOLD
        if (!terminal && !enoughScale && !enoughTime) return
        if (terminal && magnitude < PINCH_TERMINAL_LOG_EPSILON) return

        do {
            val chunk = pendingPinchLogScale.coerceIn(
                -PINCH_MAX_PACKET_LOG_SCALE,
                PINCH_MAX_PACKET_LOG_SCALE,
            )
            if (abs(chunk) < PINCH_TERMINAL_LOG_EPSILON) break
            val factor = exp(chunk).toFloat()
            if (!factor.isFinite() || factor <= 0f) break
            if (!SecondScreen.sendGuestGesture(
                    "pinch",
                    normalizedX(pinchCenterX),
                    normalizedY(pinchCenterY),
                    factor,
                )
            ) {
                ThorLog.w(TAG, "Native guest pinch queue rejected a packet")
                return
            }
            pendingPinchLogScale -= chunk
            pinchPackets++
            lastPinchPacketTime = eventTime
            if (!terminal) break
        } while (abs(pendingPinchLogScale) >= PINCH_TERMINAL_LOG_EPSILON)
    }

    private fun finishPinch(
        reason: String,
        flush: Boolean,
        eventTime: Long = android.os.SystemClock.uptimeMillis(),
    ) {
        if (!pinching) return
        if (flush) sendPendingPinch(eventTime, terminal = true)
        if (pinchPackets > 0) {
            ThorLog.i(TAG, "Pinch finished reason=$reason samples=$pinchSamples packets=$pinchPackets")
        }
    }

    private fun updatePinchCenter(event: MotionEvent, history: Int = -1) {
        if (event.pointerCount < 2) return
        val x0 = if (history >= 0) event.getHistoricalX(0, history) else event.getX(0)
        val y0 = if (history >= 0) event.getHistoricalY(0, history) else event.getY(0)
        val x1 = if (history >= 0) event.getHistoricalX(1, history) else event.getX(1)
        val y1 = if (history >= 0) event.getHistoricalY(1, history) else event.getY(1)
        pinchCenterX = (x0 + x1) * 0.5f
        pinchCenterY = (y0 + y1) * 0.5f
    }

    private fun span(event: MotionEvent, history: Int = -1): Float {
        if (event.pointerCount < 2) return 0f
        val x0 = if (history >= 0) event.getHistoricalX(0, history) else event.getX(0)
        val y0 = if (history >= 0) event.getHistoricalY(0, history) else event.getY(0)
        val x1 = if (history >= 0) event.getHistoricalX(1, history) else event.getX(1)
        val y1 = if (history >= 0) event.getHistoricalY(1, history) else event.getY(1)
        return hypot((x1 - x0).toDouble(), (y1 - y0).toDouble()).toFloat()
    }

    private companion object {
        const val TAG = "CompanionGuestInput"
        const val HOLD_SECONDS = 0.25f
        const val PINCH_PACKET_LOG_THRESHOLD = 0.01
        const val PINCH_TIMED_LOG_THRESHOLD = 0.002
        const val PINCH_TERMINAL_LOG_EPSILON = 0.000001
        const val PINCH_PACKET_MAX_DELAY_MS = 50L
        val PINCH_MAX_PACKET_LOG_SCALE = ln(2.0)
    }
}

/** Pure hit-test shared with unit tests; only this strip is withheld from the game mod. */
internal fun isConsoleGestureStart(normalizedX: Float, normalizedY: Float): Boolean =
    normalizedY >= 0.94f && normalizedX in 0.40f..0.60f
