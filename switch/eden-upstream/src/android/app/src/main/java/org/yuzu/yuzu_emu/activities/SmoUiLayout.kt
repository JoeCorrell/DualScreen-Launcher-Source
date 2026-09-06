package org.yuzu.yuzu_emu.activities

/** Physical-reference layout in 1240 x 1080 design units. Gaps are never controls. */
object SmoUiLayout {
    const val PLAYER_LEFT = 875f
    const val PLAYER_TOP = 48f
    const val PLAYER_RIGHT = 1195f
    const val PLAYER_BOTTOM = 548f
    const val PLAYER_RENDER_WIDTH = 320
    const val PLAYER_RENDER_HEIGHT = 500
    fun tabAt(x: Float, y: Float): Int? {
        if (!x.isFinite() || !y.isFinite() || y < 986f || y > 1060f) return null
        return (0..3).firstOrNull { x >= 24f + it * 304f && x <= 308f + it * 304f }
    }
}
