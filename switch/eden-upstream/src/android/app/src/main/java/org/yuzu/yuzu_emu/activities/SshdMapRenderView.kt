package org.yuzu.yuzu_emu.activities

import android.content.Context
import android.graphics.SurfaceTexture
import android.view.Surface
import android.view.TextureView
import org.yuzu.yuzu_emu.NativeLibrary

/** Presents the game's resident map texture through Eden's existing Vulkan device. */
internal class SshdMapRenderView(context: Context) : TextureView(context), TextureView.SurfaceTextureListener {
    private var enabled = false
    private var nativeSurface: Surface? = null

    init {
        surfaceTextureListener = this
        isOpaque = false
        visibility = INVISIBLE
    }

    fun setMapVisible(visible: Boolean) {
        if (enabled == visible) return
        enabled = visible
        visibility = if (visible) VISIBLE else INVISIBLE
        org.yuzu.yuzu_emu.utils.Log.info("[SSHD-MAP-SURFACE] visible=$visible size=${width}x$height")
        publishSurface()
    }

    private fun publishSurface() {
        NativeLibrary.setSshdMapSurface(if (enabled) nativeSurface else null, width, height)
    }

    override fun onSurfaceTextureAvailable(texture: SurfaceTexture, width: Int, height: Int) {
        nativeSurface = Surface(texture)
        publishSurface()
    }

    override fun onSurfaceTextureSizeChanged(texture: SurfaceTexture, width: Int, height: Int) {
        publishSurface()
    }

    override fun onSurfaceTextureDestroyed(texture: SurfaceTexture): Boolean {
        NativeLibrary.setSshdMapSurface(null, 0, 0)
        nativeSurface?.release()
        nativeSurface = null
        return true
    }

    override fun onSurfaceTextureUpdated(texture: SurfaceTexture) = Unit

    override fun onDetachedFromWindow() {
        NativeLibrary.setSshdMapSurface(null, 0, 0)
        super.onDetachedFromWindow()
    }
}
