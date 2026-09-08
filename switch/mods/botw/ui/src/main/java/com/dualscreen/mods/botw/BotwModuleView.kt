package com.dualscreen.mods.botw
import android.graphics.Color
import android.graphics.Rect
import android.widget.FrameLayout
import com.dualscreen.companion.api.CompanionHost
import com.dualscreen.companion.api.CompanionHostV2
import kotlin.math.min
import kotlin.math.roundToInt
class BotwModuleView(context: android.content.Context, api: CompanionHost) : FrameLayout(context) {
    private companion object {
        private const val DESIGN_WIDTH = 1240f
        private const val DESIGN_HEIGHT = 1080f
        private const val PLAYER_LEFT = 813f
        private const val PLAYER_TOP = 368f
        private const val PLAYER_RIGHT = 1104f
        private const val PLAYER_BOTTOM = 954f
    }

    private val host = api as CompanionHostV2
    private val playerView = BotwPlayerRenderView(context, host)
    private val companionView = BotwCompanionView(
        context, host,
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

