package com.wemu.engine.widget;

import android.annotation.SuppressLint;
import android.content.Context;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import androidx.annotation.NonNull;

import com.wemu.engine.renderer.ASurfaceRenderer;
import com.wemu.engine.renderer.CompositorRenderer;
import com.wemu.engine.renderer.OpenGLRenderer;
import com.wemu.engine.renderer.VulkanRenderer;
import com.wemu.engine.renderer.XServerRenderer;
import com.wemu.engine.xserver.Drawable;
import com.wemu.engine.xserver.XServer;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

@SuppressLint("ViewConstructor")
public class XServerView extends SurfaceView implements SurfaceHolder.Callback {

    /** The container setting's value for the Vulkan compositor. */
    public static final String RENDERER_VULKAN = "vulkan";

    /** The container setting's value for the EGL/OpenGL ES compositor. */
    public static final String RENDERER_OPENGL = "opengl";

    /** The container setting's value for the SurfaceFlinger scanout path. */
    public static final String RENDERER_SURFACEFLINGER = "surfaceflinger";

    private final XServerRenderer renderer;
    private final XServer xServer;
    private final ExecutorService eventExecutor = Executors.newSingleThreadExecutor();
    private final AtomicBoolean destroyed = new AtomicBoolean(false);
    private final AtomicBoolean cleanupFinished = new AtomicBoolean(false);

    public XServerView(Context context, XServer xServer) {
        this(context, xServer, RENDERER_VULKAN);
    }

    public XServerView(Context context, XServer xServer, String selectedRenderer) {
        super(context);
        this.xServer = xServer;
        setLayoutParams(new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                                                     ViewGroup.LayoutParams.MATCH_PARENT));
        getHolder().addCallback(this);

        /*
         * The mode is set before the renderer, and before any drawable exists.
         *
         * The scanout renderer needs every X drawable to be backed by an
         * AHardwareBuffer from the moment it is created — it has no upload step
         * in which it could fix one up later. `Drawable` reads this static when
         * it is constructed, so setting it after the first window exists would
         * leave that window permanently unpresentable.
         */
        boolean scanout = RENDERER_SURFACEFLINGER.equalsIgnoreCase(selectedRenderer);
        Drawable.DRAWABLE_ASR_MODE(scanout);

        if (scanout) {
            renderer = new ASurfaceRenderer(this, xServer);
        } else if (RENDERER_OPENGL.equalsIgnoreCase(selectedRenderer)) {
            renderer = new OpenGLRenderer(this, xServer);
        } else {
            renderer = new VulkanRenderer(this, xServer);
        }
    }

    public XServerRenderer getRenderer() {
        return renderer;
    }

    /** The Vulkan compositor, or null when the scanout renderer is running. */
    public VulkanRenderer getVulkanRenderer() {
        return renderer instanceof VulkanRenderer ? (VulkanRenderer) renderer : null;
    }

    /** The active compositing renderer, or null for direct SurfaceFlinger scanout. */
    public CompositorRenderer getCompositorRenderer() {
        return renderer instanceof CompositorRenderer ? (CompositorRenderer) renderer : null;
    }

    @Override
    public void surfaceCreated(@NonNull SurfaceHolder holder) {
        if (destroyed.get()) return;
        if (renderer instanceof CompositorRenderer) {
            ((CompositorRenderer) renderer).onSurfaceCreated(holder.getSurface());
        } else if (renderer instanceof ASurfaceRenderer) {
            ((ASurfaceRenderer) renderer).onSurfaceCreated(holder.getSurface());
        }
    }

    @Override
    public void surfaceChanged(@NonNull SurfaceHolder holder, int format, int width, int height) {
        if (destroyed.get()) return;
        if (renderer instanceof CompositorRenderer) {
            ((CompositorRenderer) renderer).onSurfaceChanged(width, height);
        } else if (renderer instanceof ASurfaceRenderer) {
            ((ASurfaceRenderer) renderer).onSurfaceChanged(width, height);
        }
    }

    @Override
    public void surfaceDestroyed(@NonNull SurfaceHolder holder) {
        if (destroyed.get()) return;
        if (renderer instanceof CompositorRenderer) {
            ((CompositorRenderer) renderer).onSurfaceDestroyed();
        } else if (renderer instanceof ASurfaceRenderer) {
            ((ASurfaceRenderer) renderer).onSurfaceDestroyed();
        }
    }

    public void requestRender() {
        if (!destroyed.get() && renderer instanceof CompositorRenderer) {
            ((CompositorRenderer) renderer).requestRender();
        }
    }

    /**
     * Tells the renderer its new size without the surface having been recreated.
     *
     * Picture-in-picture resizes the window in place, so nothing calls
     * {@link #surfaceChanged}, and a renderer that is not told goes on scaling
     * to the size it last heard about.
     */
    public void resize(int width, int height) {
        if (destroyed.get()) return;
        surfaceChanged(getHolder(), 0, width, height);
    }

    public void queueEvent(Runnable r) {
        if (destroyed.get()) return;
        try {
            eventExecutor.execute(r);
        } catch (RejectedExecutionException ignored) {
            // Teardown won the race with an X event that was already in flight.
        }
    }

    /**
     * Permanently releases the renderer and the X-server work owned by this view.
     *
     * This is deliberately separate from {@link #surfaceDestroyed}: Android can
     * recreate a Surface while the same guest remains alive. Session teardown,
     * on the other hand, must destroy native Vulkan/SurfaceControl state and the
     * executor rather than leaving a detached compositor behind.
     */
    public void destroy() {
        detachForDestroy();
        if (!cleanupFinished.compareAndSet(false, true)) return;
        xServer.shutdown();

        eventExecutor.shutdown();
        try {
            if (!eventExecutor.awaitTermination(250, TimeUnit.MILLISECONDS)) {
                eventExecutor.shutdownNow();
            }
        } catch (InterruptedException ignored) {
            eventExecutor.shutdownNow();
            Thread.currentThread().interrupt();
        }

        if (renderer instanceof CompositorRenderer) {
            ((CompositorRenderer) renderer).forceCleanup();
        } else if (renderer instanceof ASurfaceRenderer) {
            ((ASurfaceRenderer) renderer).onSurfaceDestroyed();
        }
        xServer.setRenderer(null);
    }

    /**
     * Stops new Surface/X callbacks without waiting for native renderer work.
     * Call this on the UI thread before removing the view, then call
     * {@link #destroy()} on a worker to perform the potentially blocking joins.
     */
    public void detachForDestroy() {
        if (!destroyed.compareAndSet(false, true)) return;
        getHolder().removeCallback(this);
    }

    public void onPause() {}
    public void onResume() {}
}
