package com.wemu.engine.renderer;

import android.view.Surface;

import com.wemu.engine.widget.XServerView;
import com.wemu.engine.xserver.XServer;

import dalvik.annotation.optimization.FastNative;

/**
 * EGL/OpenGL ES 3 implementation of WEMU's X11 compositor.
 *
 * It consumes the same CPU and AHardwareBuffer drawables as the Vulkan
 * compositor.  The guest graphics API is independent: a Direct3D game may
 * still use DXVK/VKD3D while this final host-side composition step uses GLES.
 */
public final class OpenGLRenderer extends CompositorRenderer {
    static {
        System.loadLibrary("opengl_renderer");
    }

    public OpenGLRenderer(XServerView xServerView, XServer xServer) {
        super(xServerView, xServer);
    }

    @Override protected native long nativeInit(Surface surface, int screenWidth, int screenHeight,
        String ignoredDriverPath, String ignoredLibraryName, String ignoredNativeLibDir);
    @Override protected native void nativeResize(long handle, int width, int height);
    @Override protected native void nativeDestroy(long handle);
    @Override protected native void nativeDetachSurface(long handle);
    @Override protected native boolean nativeReattachSurface(long handle, Surface surface);
    @Override protected native int[] nativeGetSwapchainSize(long handle);

    @Override @FastNative protected native boolean nativeUpdateWindowContent(long handle, long id,
        java.nio.ByteBuffer pixels, short width, short height, short stride, int x, int y);
    @Override @FastNative protected native boolean nativeUpdateWindowContentAHB(long handle, long id,
        long ahbPtr, java.nio.ByteBuffer fallbackPixels, short width, short height,
        short fallbackStride, int x, int y);
    @Override @FastNative protected native void nativeSetTransformAndScissor(long handle,
        float ox, float oy, float sx, float sy,
        boolean hasScissor, int scX, int scY, int scW, int scH);
    @Override @FastNative protected native void nativeSetPointerPos(long handle, short x, short y);
    @Override @FastNative protected native void nativeSetCursorVisible(long handle, boolean visible);
    @Override @FastNative protected native void nativeSetRootColor(long handle, int argb);
    @Override @FastNative protected native void nativeUpdateCursorImage(long handle,
        java.nio.ByteBuffer pixels, short width, short height, short hotX, short hotY);
    @Override @FastNative protected native void nativeSetRenderList(long handle,
        long[] ids, int[] xs, int[] ys, int count);
    @Override @FastNative protected native void nativeRemoveWindow(long handle, long id);
    @Override @FastNative protected native void nativeSetVerboseLog(long handle, boolean value);
    @Override @FastNative protected native void nativeSetSharpness(long handle, float sharpness);

    @Override protected native void nativeDumpRendererInfo(long handle);
    @Override protected native void nativeSetFilterMode(long handle, int mode);
    @Override protected native void nativeSetStretchMode(long handle, int mode);
    @Override protected native void nativeSetPostFXMode(long handle, int mode);
    @Override protected native void nativeSetSwapRB(long handle, boolean enabled);
    @Override protected native void nativeSetPresentMode(long handle, int mode);
    @Override protected native int[] nativeGetSupportedPresentModes(long handle);
}
