package com.wemu.engine.renderer;

import com.wemu.engine.xserver.Drawable;

import java.nio.ByteBuffer;

/**
 * A texture whose pixels live in an {@code AHardwareBuffer} the guest writes
 * into directly, rather than in memory the renderer has to upload from.
 *
 * There are two of these and they exist for two different renderers.
 * {@link GPUImage} backs the Vulkan compositor; {@link AHBImage} backs the
 * scanout path and adds a swapchain, because a buffer being read by
 * SurfaceFlinger cannot also be the buffer the guest is drawing into.
 *
 * The base exists so that {@code Drawable} can ask "are these pixels already on
 * the GPU, and what is their stride" without asking which renderer is running.
 * It used to test for {@code GPUImage} by name, which was the same question
 * with only one possible answer.
 */
public abstract class NativeTexture extends Texture {

    /** Row length in pixels, which the driver may pad beyond the width. */
    public abstract short getStride();

    /** The mapping the guest writes into, or null once unlocked. */
    public abstract ByteBuffer getVirtualData();

    /** The {@code AHardwareBuffer} behind this texture, or 0 if it has none. */
    public abstract long getHardwareBufferPtr();

    /**
     * Allocates whichever of the two the running renderer can present.
     *
     * The X server creates textures from several places — the Present
     * extension when a window first presents, DRI3 when the guest hands over a
     * buffer it already owns — and none of them has any business knowing which
     * renderer is attached. They used to name {@link GPUImage} directly, which
     * was correct while it was the only one; with the scanout renderer it
     * silently produced a texture that renderer cannot hand to SurfaceFlinger,
     * so the window presented every frame into something nothing would read and
     * the game rendered, at full GPU load, onto a black screen.
     */
    public static NativeTexture create(short width, short height) {
        return Drawable.IS_ASR() ? new AHBImage(width, height) : new GPUImage(width, height);
    }

    /** The same choice, for a buffer the guest passed over a socket. */
    public static NativeTexture fromSocket(int fd) {
        return Drawable.IS_ASR() ? new AHBImage(fd) : new GPUImage(fd);
    }
}
