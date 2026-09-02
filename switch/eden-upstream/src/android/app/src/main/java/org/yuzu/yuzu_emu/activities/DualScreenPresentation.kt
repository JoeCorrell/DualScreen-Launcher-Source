


package org.yuzu.yuzu_emu.activities

import android.app.Presentation
import android.graphics.Color
import android.graphics.PixelFormat
import android.graphics.Rect
import android.graphics.drawable.ColorDrawable
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Display
import android.view.View
import android.view.WindowManager
import android.widget.FrameLayout
import org.yuzu.yuzu_emu.BuildConfig
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.utils.Log
import kotlin.math.min
import kotlin.math.roundToInt


internal class DualScreenPresentation(owner: EmulationActivity, display: Display) :
    Presentation(owner, display) {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window?.apply {





            setFormat(PixelFormat.OPAQUE)
            setBackgroundDrawable(ColorDrawable(Color.BLACK))
            clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND)
            addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
            addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)


            addFlags(
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                    WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
            )
            setWindowAnimations(0)
            decorView.setBackgroundColor(Color.BLACK)
            decorView.systemUiVisibility =
                View.SYSTEM_UI_FLAG_FULLSCREEN or
                    View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
                    View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                    View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE
        }
        setContentView(DualScreenCompanionHost(context))
    }
}


private class DualScreenCompanionHost(context: android.content.Context) : FrameLayout(context) {
    private companion object {
        private const val BOTW_TITLE_ID = 0x01007EF00011E000L
        private const val MK8D_TITLE_ID = 0x0100152000022000L
        private const val ARKHAM_ASYLUM_TITLE_ID = 0x0100E870163CA000L
        private const val PLZA_TITLE_ID = 0x0100F43008C44000L
        private const val SSHD_TITLE_ID = 0x01002DA013484000L
        private const val XCX_TITLE_ID = 0x0100453019AA8000L
        private const val ROUTE_INTERVAL_MS = 500L
        private const val TRANSIENT_TITLE_MISS_SAMPLES = 6
        private const val WAITING_ROUTE_ID = 0L
    }

    private val handler = Handler(Looper.getMainLooper())
    private var routedTitleId = Long.MIN_VALUE
    private var pendingTitleId = Long.MIN_VALUE
    private var pendingTitleSamples = 0
    private val routeRunnable = object : Runnable {
        override fun run() {



            val titleId = runCatching {
                if (NativeLibrary.isRunning()) NativeLibrary.playTimeManagerGetCurrentTitleId()
                else 0L
            }.getOrDefault(0L)
            acceptTitleSample(titleId)
            handler.postDelayed(this, ROUTE_INTERVAL_MS)
        }
    }

    init {
        setBackgroundColor(Color.BLACK)
        route(0L)
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        handler.removeCallbacks(routeRunnable)
        handler.post(routeRunnable)
    }

    override fun onDetachedFromWindow() {
        handler.removeCallbacks(routeRunnable)
        super.onDetachedFromWindow()
    }

    private fun acceptTitleSample(sampledTitleId: Long) {
        val targetTitleId = normalizeRoute(sampledTitleId)
        if (targetTitleId == routedTitleId) {
            pendingTitleId = Long.MIN_VALUE
            pendingTitleSamples = 0
            return
        }

        val leavingActiveGameForWaiting = isGameRoute(routedTitleId) &&
            targetTitleId == WAITING_ROUTE_ID
        if (!leavingActiveGameForWaiting) {
            route(targetTitleId)
            return
        }

        if (pendingTitleId != targetTitleId) {
            pendingTitleId = targetTitleId
            pendingTitleSamples = 1
        } else {
            pendingTitleSamples++
        }
        if (pendingTitleSamples >= TRANSIENT_TITLE_MISS_SAMPLES) {
            route(targetTitleId)
        }
    }

    private fun normalizeRoute(titleId: Long): Long = when (titleId) {
        BOTW_TITLE_ID, MK8D_TITLE_ID, ARKHAM_ASYLUM_TITLE_ID, PLZA_TITLE_ID,
        SSHD_TITLE_ID, XCX_TITLE_ID -> titleId
        else -> WAITING_ROUTE_ID
    }

    private fun isGameRoute(titleId: Long): Boolean =
        titleId == BOTW_TITLE_ID || titleId == MK8D_TITLE_ID ||
            titleId == ARKHAM_ASYLUM_TITLE_ID || titleId == PLZA_TITLE_ID ||
            titleId == SSHD_TITLE_ID || titleId == XCX_TITLE_ID

    private fun route(titleId: Long) {
        if (titleId == routedTitleId) return
        Log.info("[DualScreen] Companion route changed to %016X".format(titleId))
        routedTitleId = titleId
        pendingTitleId = Long.MIN_VALUE
        pendingTitleSamples = 0
        removeAllViews()
        val content = when (titleId) {
            BOTW_TITLE_ID -> if (BuildConfig.BOTW_DUALSCREEN_ENABLED) {
                BotwDualScreenCompanionLayout(context)
            } else {
                WaitingForGameView(context)
            }
            MK8D_TITLE_ID -> Mk8dCompanionView(context)
            ARKHAM_ASYLUM_TITLE_ID -> ArkhamCompanionView(context)
            PLZA_TITLE_ID -> PlzaCompanionView(context)
            SSHD_TITLE_ID -> SshdDualScreenCompanionLayout(context)
            XCX_TITLE_ID -> XcxCompanionView(context)
            else -> WaitingForGameView(context)
        }
        addView(content, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
    }
}

private class WaitingForGameView(context: android.content.Context) : View(context) {
    private val paint = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textAlign = android.graphics.Paint.Align.CENTER
        typeface = android.graphics.Typeface.create("sans-serif", android.graphics.Typeface.NORMAL)
    }

    override fun onDraw(canvas: android.graphics.Canvas) {
        canvas.drawColor(Color.BLACK)
        paint.textSize = (width.coerceAtMost(height) * .045f).coerceAtLeast(24f)
        canvas.drawText("Waiting for a supported game to load…", width * .5f, height * .5f, paint)
    }
}


private class BotwDualScreenCompanionLayout(context: android.content.Context) : FrameLayout(context) {
    private companion object {
        private const val DESIGN_WIDTH = 1240f
        private const val DESIGN_HEIGHT = 1080f
        private const val PLAYER_LEFT = 813f
        private const val PLAYER_TOP = 368f
        private const val PLAYER_RIGHT = 1104f
        private const val PLAYER_BOTTOM = 954f
    }

    private val playerView = BotwPlayerRenderView(context)
    private val companionView = BotwCompanionView(
        context,
        onPlayerAppearanceChanged = { appearance, visible ->
            playerView.setAppearance(appearance, visible)
        },
        onPlayerItemAction = playerView::playItemAction,
        onPlayerHeldItemCountChanged = playerView::syncHeldItemCount
    )

    init {
        setBackgroundColor(Color.BLACK)
        addView(
            companionView,
            LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT)
        )
        addView(playerView, LayoutParams(1, 1))
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        super.onMeasure(widthMeasureSpec, heightMeasureSpec)
        val bounds = calculatePlayerBounds(measuredWidth, measuredHeight)
        playerView.measure(
            MeasureSpec.makeMeasureSpec(bounds.width(), MeasureSpec.EXACTLY),
            MeasureSpec.makeMeasureSpec(bounds.height(), MeasureSpec.EXACTLY)
        )
    }

    override fun onLayout(changed: Boolean, left: Int, top: Int, right: Int, bottom: Int) {
        val layoutWidth = right - left
        val layoutHeight = bottom - top
        companionView.layout(0, 0, layoutWidth, layoutHeight)
        val bounds = calculatePlayerBounds(layoutWidth, layoutHeight)
        playerView.layout(bounds.left, bounds.top, bounds.right, bounds.bottom)
    }

    private fun calculatePlayerBounds(width: Int, height: Int): Rect {
        if (width <= 0 || height <= 0) return Rect(0, 0, 1, 1)
        val scale = min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT)
        val offsetX = (width - DESIGN_WIDTH * scale) * 0.5f
        val offsetY = (height - DESIGN_HEIGHT * scale) * 0.5f
        val nextWidth = ((PLAYER_RIGHT - PLAYER_LEFT) * scale).roundToInt().coerceAtLeast(1)
        val nextHeight = ((PLAYER_BOTTOM - PLAYER_TOP) * scale).roundToInt().coerceAtLeast(1)
        val nextLeft = (offsetX + PLAYER_LEFT * scale).roundToInt()
        val nextTop = (offsetY + PLAYER_TOP * scale).roundToInt()
        return Rect(nextLeft, nextTop, nextLeft + nextWidth, nextTop + nextHeight)
    }
}

private class SshdDualScreenCompanionLayout(context: android.content.Context) : FrameLayout(context) {
    private companion object {
        private const val DESIGN_WIDTH = 1240f
        private const val DESIGN_HEIGHT = 1080f
        private const val PLAYER_LEFT = 752f
        private const val PLAYER_TOP = 370f
        private const val PLAYER_RIGHT = 1036f
        private const val PLAYER_BOTTOM = 990f
    }

    private val playerView = BotwPlayerRenderView(context, PlayerRenderProfile.Sshd)
    private val companionView = SshdCompanionView(
        context,
        onPlayerVisibilityChanged = playerView::setVisible,
        onPlayerModelChanged = playerView::setSshdOutfit
    )

    init {
        setBackgroundColor(Color.BLACK)
        addView(companionView, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
        addView(playerView, LayoutParams(1, 1))
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        super.onMeasure(widthMeasureSpec, heightMeasureSpec)
        val bounds = calculatePlayerBounds(measuredWidth, measuredHeight)
        playerView.measure(
            MeasureSpec.makeMeasureSpec(bounds.width(), MeasureSpec.EXACTLY),
            MeasureSpec.makeMeasureSpec(bounds.height(), MeasureSpec.EXACTLY)
        )
    }

    override fun onLayout(changed: Boolean, left: Int, top: Int, right: Int, bottom: Int) {
        val layoutWidth = right - left
        val layoutHeight = bottom - top
        companionView.layout(0, 0, layoutWidth, layoutHeight)
        val bounds = calculatePlayerBounds(layoutWidth, layoutHeight)
        playerView.layout(bounds.left, bounds.top, bounds.right, bounds.bottom)
    }

    private fun calculatePlayerBounds(width: Int, height: Int): Rect {
        if (width <= 0 || height <= 0) return Rect(0, 0, 1, 1)
        val scale = min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT)
        val offsetX = (width - DESIGN_WIDTH * scale) * 0.5f
        val offsetY = (height - DESIGN_HEIGHT * scale) * 0.5f
        val nextWidth = ((PLAYER_RIGHT - PLAYER_LEFT) * scale).roundToInt().coerceAtLeast(1)
        val nextHeight = ((PLAYER_BOTTOM - PLAYER_TOP) * scale).roundToInt().coerceAtLeast(1)
        val nextLeft = (offsetX + PLAYER_LEFT * scale).roundToInt()
        val nextTop = (offsetY + PLAYER_TOP * scale).roundToInt()
        return Rect(nextLeft, nextTop, nextLeft + nextWidth, nextTop + nextHeight)
    }
}
