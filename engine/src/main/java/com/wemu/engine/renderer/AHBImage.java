package com.wemu.engine.renderer;

import androidx.annotation.Keep;

import java.nio.ByteBuffer;

/**
 * The scanout path's backing store: one buffer the guest draws into, and three
 * more for SurfaceFlinger to read.
 *
 * <h2>Why a swapchain, when {@link GPUImage} needs only one buffer</h2>
 *
 * The Vulkan compositor reads the guest's buffer and draws from it, so there is
 * exactly one reader and it is this process. The scanout path has no such
 * moment — the buffer is handed to SurfaceFlinger and stays latched until the
 * display is finished with it, which is at least one refresh and may be more.
 * Letting the guest keep writing into that same buffer is tearing by
 * construction.
 *
 * So the guest still writes into one buffer it owns, and each present copies
 * that into the next of three that rotate. Three rather than two because the
 * compositor can hold a buffer while the next is already queued, and with two
 * the guest would block on the display every frame.
 *
 * <h2>The fences</h2>
 *
 * Each slot carries a release fence handed back by SurfaceFlinger, saying when
 * it has finished reading that slot. The copy into a slot waits on that fence
 * rather than on the CPU, so a slot still on screen delays only the copy that
 * would have overwritten it. {@code setSwapchainFence} is called from native
 * code on the transaction-complete callback, which is why it is {@link Keep}.
 *
 * <h2>Why this is not {@code GPUImage} with extra fields</h2>
 *
 * Merging them would put the swapchain, its fences and its copy in the path of
 * every container running the Vulkan renderer, which is all of them today, to
 * serve a renderer they are not using. They share {@link NativeTexture}, which
 * is the part {@code Drawable} actually cares about.
 */
public class AHBImage extends NativeTexture {

    private long hardwareBufferPtr;
    private ByteBuffer virtualData;
    private short stride;
    private int width = -1;
    private int height = -1;

    private final Object fenceLock = new Object();
    private final int[] swapchainFences = new int[]{-1, -1, -1};
    private final long[] swapchainAhbs = new long[3];
    private int swapchainIndex = 0;
    private int lastUsedSlot = 0;
    private int lastAcquireFence = -1;

    /**
     * Which native context these buffers were registered against.
     *
     * The renderer's native context is torn down and rebuilt when the surface
     * goes away, and buffers registered with the old one mean nothing to the
     * new one. Comparing generations re-registers exactly once per rebuild
     * instead of on every frame.
     */
    private long preparedRendererGeneration = -1;

    private static boolean supported = false;

    static {
        System.loadLibrary("ahbimage");
    }

    public AHBImage(short width, short height) {
        hardwareBufferPtr = createHardwareBuffer(width, height);
        if (hardwareBufferPtr == 0) return;

        virtualData = lockHardwareBuffer(hardwareBufferPtr);
        this.width = nativeGetWidth(hardwareBufferPtr);
        this.height = nativeGetHeight(hardwareBufferPtr);

        if (virtualData == null) {
            destroyHardwareBuffer(hardwareBufferPtr);
            hardwareBufferPtr = 0;
            return;
        }

    }

    public AHBImage(int socketFd) {
        hardwareBufferPtr = hardwareBufferFromSocket(socketFd);
        if (hardwareBufferPtr != 0) {
            width = nativeGetWidth(hardwareBufferPtr);
            height = nativeGetHeight(hardwareBufferPtr);
        }
    }

    @Override
    public short getStride() {
        return stride;
    }

    @Override
    public ByteBuffer getVirtualData() {
        return virtualData;
    }

    @Override
    public void allocateTexture(short width, short height, ByteBuffer data) {}

    @Override
    public void updateFromDrawable(com.wemu.engine.xserver.Drawable drawable) {
        needsUpdate = false;
    }

    @Override
    public long getHardwareBufferPtr() {
        return hardwareBufferPtr;
    }

    public int getWidth() {
        return width;
    }

    public int getHeight() {
        return height;
    }

    private boolean hasCompleteScanoutSwapchain() {
        for (long ahb : swapchainAhbs) {
            if (ahb == 0) return false;
        }
        return true;
    }

    /**
     * Allocates display-owned copies only once this drawable is actually mapped
     * into the primary scanout scene.
     *
     * X11 creates many pixmaps which never become visible windows. The lower
     * panel also owns a separate synchronized CPU ring. Eagerly giving every
     * one of those drawables three full-size scanout buffers consumed memory
     * and allocator time for buffers which could never be submitted.
     */
    private boolean ensureScanoutSwapchain() {
        synchronized (fenceLock) {
            if (hasCompleteScanoutSwapchain()) return true;
            if (hardwareBufferPtr == 0 || virtualData == null || width <= 0 || height <= 0) {
                return false;
            }

            boolean swapchainOk = true;
            for (int i = 0; i < swapchainAhbs.length; i++) {
                if (swapchainAhbs[i] == 0) {
                    swapchainAhbs[i] = createHardwareBuffer((short) width, (short) height);
                }
                if (swapchainAhbs[i] == 0) swapchainOk = false;
            }

            if (!swapchainOk) {
                // Never retain a partial set: it cannot be registered or used.
                for (int i = 0; i < swapchainAhbs.length; i++) {
                    if (swapchainAhbs[i] != 0) {
                        destroyHardwareBuffer(swapchainAhbs[i]);
                        swapchainAhbs[i] = 0;
                    }
                }
            }
            return swapchainOk;
        }
    }

    /** Registers the swapchain with the renderer's current native context. */
    void prepareScanoutSources() {
        final long generation = ASurfaceRenderer.getNativeContextGeneration();
        if (generation <= 0 || preparedRendererGeneration == generation) return;
        if (!ensureScanoutSwapchain()) return;

        try {
            if (ASurfaceRenderer.nativePrepareCpuSourceBuffers(
                    swapchainAhbs[0], swapchainAhbs[1], swapchainAhbs[2])) {
                preparedRendererGeneration = generation;
            }
        } catch (UnsatisfiedLinkError ignored) {
            // The scanout library is not loaded; the Vulkan renderer is running.
        }
    }

    private void releaseScanoutSources() {
        if (!hasCompleteScanoutSwapchain()) return;
        try {
            ASurfaceRenderer.nativeReleaseCpuSourceBuffers(
                    swapchainAhbs[0], swapchainAhbs[1], swapchainAhbs[2]);
        } catch (UnsatisfiedLinkError ignored) {
            // The context may already be gone; the buffers are freed below anyway.
        } finally {
            preparedRendererGeneration = -1;
        }
    }

    /**
     * Copies this frame into the next swapchain slot and returns it, or 0 when
     * there is no usable swapchain.
     */
    public long getScanoutHardwareBufferPtr() {
        if (!hasCompleteScanoutSwapchain() || virtualData == null) return 0;

        long targetAhb = swapchainAhbs[swapchainIndex];

        int waitFence;
        synchronized (fenceLock) {
            waitFence = swapchainFences[swapchainIndex];
            swapchainFences[swapchainIndex] = -1;
        }

        if (lastAcquireFence >= 0) nativeCloseFd(lastAcquireFence);

        lastAcquireFence = copyHardwareBuffer(
                virtualData, targetAhb, (short) width, (short) height, stride, waitFence);

        lastUsedSlot = swapchainIndex;
        swapchainIndex = (swapchainIndex + 1) % swapchainAhbs.length;
        return targetAhb;
    }

    public int getLastUsedSlot() {
        return lastUsedSlot;
    }

    /** Hands the copy's completion fence to the caller, which then owns it. */
    public int consumeAcquireFence() {
        int fence = lastAcquireFence;
        lastAcquireFence = -1;
        return fence;
    }

    @Keep
    public void setSwapchainFence(int slot, int fence) {
        if (slot < 0 || slot >= swapchainFences.length) {
            if (fence >= 0) nativeCloseFd(fence);
            return;
        }
        synchronized (fenceLock) {
            if (swapchainFences[slot] >= 0) nativeCloseFd(swapchainFences[slot]);
            swapchainFences[slot] = fence;
        }
    }

    @Keep
    private void setStride(short stride) {
        this.stride = stride;
    }

    @Override
    public void destroy() {
        releaseScanoutSources();

        if (lastAcquireFence >= 0) {
            nativeCloseFd(lastAcquireFence);
            lastAcquireFence = -1;
        }
        synchronized (fenceLock) {
            for (int i = 0; i < swapchainFences.length; i++) {
                if (swapchainFences[i] >= 0) {
                    nativeCloseFd(swapchainFences[i]);
                    swapchainFences[i] = -1;
                }
            }
        }
        for (int i = 0; i < swapchainAhbs.length; i++) {
            if (swapchainAhbs[i] != 0) {
                destroyHardwareBuffer(swapchainAhbs[i]);
                swapchainAhbs[i] = 0;
            }
        }
        if (hardwareBufferPtr != 0) {
            destroyHardwareBuffer(hardwareBufferPtr);
            hardwareBufferPtr = 0;
        }
        virtualData = null;
        super.destroy();
    }

    public static boolean isSupported() {
        return supported;
    }

    /**
     * Allocates a token buffer to find out whether this device can do it at all.
     *
     * Mirrors {@code GPUImage.checkIsSupported}: a device that cannot hand out
     * a CPU-mappable {@code AHardwareBuffer} cannot run either path, and
     * finding out at container start is much better than finding out at the
     * first frame.
     */
    public static void checkIsSupported() {
        final short size = 8;
        AHBImage image = new AHBImage(size, size);
        supported = image.hardwareBufferPtr != 0 && image.virtualData != null;
        android.util.Log.d("AHBImage", "checkIsSupported: supported=" + supported);
        image.destroy();
    }

    private native long hardwareBufferFromSocket(int fd);

    private native long createHardwareBuffer(short width, short height);

    private native void destroyHardwareBuffer(long hardwareBufferPtr);

    private native ByteBuffer lockHardwareBuffer(long hardwareBufferPtr);

    private native int unlockHardwareBuffer(long hardwareBufferPtr);

    private native int copyHardwareBuffer(ByteBuffer srcBuffer, long dstPtr, short width,
                                          short height, short srcStride, int waitFence);

    public static native short nativeGetWidth(long ptr);

    public static native short nativeGetHeight(long ptr);

    private static native void nativeCloseFd(int fd);
}
