package com.wemu.engine.renderer;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.view.Surface;
import android.widget.Toast;

import com.wemu.engine.R;
import com.wemu.engine.core.ProcessHelper;
import com.wemu.engine.widget.FrameRating;
import com.wemu.engine.widget.XServerView;
import com.wemu.engine.xserver.Bitmask;
import com.wemu.engine.xserver.Cursor;
import com.wemu.engine.xserver.Drawable;
import com.wemu.engine.xserver.Pointer;
import com.wemu.engine.xserver.Property;
import com.wemu.engine.xserver.ScreenLayout;
import com.wemu.engine.xserver.Window;
import com.wemu.engine.xserver.WindowAttributes;
import com.wemu.engine.xserver.WindowManager;
import com.wemu.engine.xserver.XLock;
import com.wemu.engine.xserver.XServer;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;

import dalvik.annotation.optimization.FastNative;

public abstract class CompositorRenderer implements WindowManager.OnWindowModificationListener,
                                       Pointer.OnPointerMotionListener,
                                       XServerRenderer {

    public final XServerView xServerView;
    private final XServer xServer;
    private long nativeHandle = 0;
    private final Object lock = new Object();

    public final ViewTransformation viewTransformation = new ViewTransformation();
    private boolean fullscreen = false;
    private float magnifierZoom = 1.0f;
    private boolean screenOffsetYRelativeToCursor = false;
    public int surfaceWidth;
    public int surfaceHeight;
    private String[] unviewableWMClasses = null;
    private volatile XServerRenderer.ProgramWindowListener programWindowListener = null;
    private volatile int programWindowToIgnore = -1;
    private volatile XServerRenderer.ProgramClosedListener programClosedListener = null;
    private final Object programWindowLock = new Object();
    private final HashSet<Integer> presentedProgramWindows = new HashSet<>();
    private boolean trackingProgramWindows = false;
    private boolean programWindowReportedVisible = false;
    private int programWindowGeneration = 0;
    private boolean cursorVisible = false;
    private String driverPath = null;
    private java.util.concurrent.ExecutorService initExecutor = null;
    private volatile boolean initComplete = false;
    private volatile boolean inPipMode = false;
    private long nextWfcFocusCheckAtMs;
    private String driverLibraryName = null;
    private String nativeLibDir = null;
    private Drawable rootCursorDrawable;
    private Cursor lastCursor = null;

    private volatile ArrayList<RenderableWindow> renderableWindows = new ArrayList<>();

    /**
     * The drawable each mapped window has actually handed to the native
     * compositor. Guarded by {@link #lock}.
     *
     * Every InputOutput drawable receives a CPU buffer when it is created, so
     * {@code Drawable.getBuffer() != null} says only that a window could draw.
     * Wine creates screen-sized helper windows that never do. Treating one of
     * those as an opaque fullscreen occluder cuts the real game out of the
     * render list, while the native side skips the helper because it has no
     * texture: a perfectly black screen.
     *
     * Keying the value by drawable identity also handles resize. WindowManager
     * replaces the drawable in-place, and the replacement must earn visibility
     * with its own first frame rather than inheriting it from the old buffer.
     */
    private final HashMap<Integer, Long> presentedDrawableByWindow = new HashMap<>();

    protected CompositorRenderer(XServerView xServerView, XServer xServer) {
        this.xServerView = xServerView;
        this.xServer = xServer;
        SecondScreenOutput.setXServer(xServer);
        rootCursorDrawable = createRootCursorDrawable();
        xServer.windowManager.addOnWindowModificationListener(this);
        xServer.pointer.addOnPointerMotionListener(this);
    }

    private Drawable createRootCursorDrawable() {
        try {
            Context context = xServerView.getContext();
            BitmapFactory.Options options = new BitmapFactory.Options();
            options.inScaled = false;
            Bitmap bitmap = BitmapFactory.decodeResource(context.getResources(), R.drawable.cursor, options);
            return Drawable.fromBitmap(bitmap);
        } catch (Exception e) { return null; }
    }

    protected abstract long nativeInit(Surface surface, int screenWidth, int screenHeight,
        String driverPath, String libraryName, String nativeLibDir);
    protected abstract void nativeResize(long handle, int width, int height);
    protected abstract void nativeDestroy(long handle);
    protected abstract void nativeDetachSurface(long handle);
    protected abstract boolean nativeReattachSurface(long handle, Surface surface);
    protected abstract int[] nativeGetSwapchainSize(long handle);

    @FastNative protected abstract boolean nativeUpdateWindowContent(long handle, long id,
        java.nio.ByteBuffer pixels, short width, short height, short stride, int x, int y);
    @FastNative protected abstract boolean nativeUpdateWindowContentAHB(long handle, long id,
        long ahbPtr, java.nio.ByteBuffer fallbackPixels, short width, short height,
        short fallbackStride, int x, int y);
    @FastNative protected abstract void nativeSetTransformAndScissor(long handle,
        float ox, float oy, float sx, float sy,
        boolean hasScissor, int scX, int scY, int scW, int scH);
    @FastNative protected abstract void nativeSetPointerPos(long handle, short x, short y);
    @FastNative protected abstract void nativeSetCursorVisible(long handle, boolean visible);
    @FastNative protected abstract void nativeSetRootColor(long handle, int argb);
    @FastNative protected abstract void nativeUpdateCursorImage(long handle,
        java.nio.ByteBuffer pixels, short width, short height, short hotX, short hotY);
    @FastNative protected abstract void nativeSetRenderList(long handle,
        long[] ids, int[] xs, int[] ys, int count);
    @FastNative protected abstract void nativeRemoveWindow(long handle, long id);
    @FastNative protected abstract void nativeSetVerboseLog(long handle, boolean v);
    @FastNative protected abstract void nativeSetSharpness(long handle, float sharpness);

    protected abstract void nativeDumpRendererInfo(long handle);
    protected abstract void nativeSetFilterMode(long handle, int mode);
    protected abstract void nativeSetStretchMode(long handle, int mode);
    protected abstract void nativeSetPostFXMode(long handle, int mode);
    protected abstract void nativeSetSwapRB(long handle, boolean enabled);
    protected abstract void nativeSetPresentMode(long handle, int mode);
    protected abstract int[] nativeGetSupportedPresentModes(long handle);


    private static volatile boolean gpuImageChecked = false;

    private static long did(Drawable d) {
        return (long) System.identityHashCode(d);
    }

    /** Both helpers are called only while {@link #lock} is held. */
    private void markDrawablePresented(Window window, Drawable drawable) {
        presentedDrawableByWindow.put(window.id, did(drawable));
    }

    private boolean hasPresented(RenderableWindow rw) {
        Long drawableId = presentedDrawableByWindow.get(rw.window.id);
        return drawableId != null && drawableId.longValue() == did(rw.content);
    }


    /**
     * The guest rectangle this surface shows, which is the primary monitor and
     * not necessarily the whole root window.
     *
     * The two are the same for every single-screen container, and deliberately
     * are not when the container spans the Thor's two panels: this surface is
     * the top one, and scaling the whole root onto it would shrink the game to
     * half height to leave room for a panel that is not here. It is also the
     * coordinate space the compositor is initialised with, so a window the
     * guest places on the second monitor falls outside the scene and is not
     * drawn — which is what it should do on this screen.
     */
    private int guestWidth() { return xServer.getPrimaryMonitor().width; }

    private int guestHeight() { return xServer.getPrimaryMonitor().height; }

    public void onSurfaceCreated(Surface surface) {
        if (!gpuImageChecked) { GPUImage.checkIsSupported(); gpuImageChecked = true; }
        currentSurface = surface;
        declareFrameRate(surface);
        if (initExecutor != null) {
            initExecutor.shutdownNow();
            try { initExecutor.awaitTermination(3, java.util.concurrent.TimeUnit.SECONDS); }
            catch (InterruptedException ignored) { Thread.currentThread().interrupt(); }
        }
        initExecutor = java.util.concurrent.Executors.newSingleThreadExecutor();
        initExecutor.execute(() -> {
            synchronized (lock) {
                if (nativeHandle != 0) {
                    boolean ok = nativeReattachSurface(nativeHandle, surface);
                    if (!ok) {
                        nativeDestroy(nativeHandle);
                        nativeHandle = 0;
                    } else {
                        int[] sc = nativeGetSwapchainSize(nativeHandle);
                        if (sc != null && sc[0] > 0 && sc[1] > 0) {
                            surfaceWidth = sc[0]; surfaceHeight = sc[1];
                            viewTransformation.update(surfaceWidth, surfaceHeight,
                                guestWidth(), guestHeight());
                        }
                        nativeSetPresentMode(nativeHandle, pendingPresentMode);
                        nativeSetFilterMode(nativeHandle, pendingFilterMode);
                        nativeSetSwapRB(nativeHandle, pendingSwapRB);
                        nativeSetStretchMode(nativeHandle, pendingStretchMode);
                        nativeSetPostFXMode(nativeHandle, pendingPostFXMode);
                        nativeSetSharpness(nativeHandle, pendingSharpness);
                        updateTransform();
                        nativeSetCursorVisible(nativeHandle, cursorVisible);
                        nativeSetRootColor(nativeHandle, pendingRootColor);
                        initComplete = true;
                        xServerView.queueEvent(this::updateScene);
                        return;
                    }
                }
                nativeHandle = nativeInit(surface,
                    guestWidth(), guestHeight(),
                    driverPath, driverLibraryName, nativeLibDir);
                if (nativeHandle != 0) {
                    nativeSetPresentMode(nativeHandle, pendingPresentMode);
                    nativeSetFilterMode(nativeHandle, pendingFilterMode);
                    nativeSetSwapRB(nativeHandle, pendingSwapRB);
                    nativeSetPostFXMode(nativeHandle, pendingPostFXMode);
                    nativeSetSharpness(nativeHandle, pendingSharpness);
                    updateTransform();
                    nativeSetCursorVisible(nativeHandle, cursorVisible);
                    nativeSetRootColor(nativeHandle, pendingRootColor);
                }
            }
            synchronized (lock) {
                if (nativeHandle != 0) {
                    nativeSetVerboseLog(nativeHandle, true);
                    nativeDumpRendererInfo(nativeHandle);
                }
            }
            initComplete = nativeHandle != 0;
            if (initComplete) {
                xServerView.queueEvent(this::updateScene);
            } else {
                final String rendererName = getClass().getSimpleName().replace("Renderer", "");
                android.util.Log.e("Wemu_Renderer", rendererName + " compositor initialization failed");
                xServerView.post(() -> Toast.makeText(
                        xServerView.getContext(),
                        rendererName + " renderer could not start. Choose another display renderer.",
                        Toast.LENGTH_LONG).show());
            }
        });
    }

    public void onSurfaceChanged(int width, int height) {
        if (inPipMode) return;
        surfaceWidth = width; surfaceHeight = height;
        viewTransformation.update(width, height, guestWidth(), guestHeight());
        synchronized (lock) {
            if (nativeHandle != 0) { nativeResize(nativeHandle, width, height); updateTransform(); }
        }
    }

    public void onSurfaceDestroyed() {
        initComplete = false;
        currentSurface = null;
        if (initExecutor != null) {
            initExecutor.shutdownNow();
            try { initExecutor.awaitTermination(3, java.util.concurrent.TimeUnit.SECONDS); }
            catch (InterruptedException ignored) { Thread.currentThread().interrupt(); }
            initExecutor = null;
        }
        synchronized (lock) {
            if (nativeHandle != 0) nativeDetachSurface(nativeHandle);
        }
    }

    public void forceCleanup() {
        initComplete = false;
        currentSurface = null;
        /*
         * The hint session ends with the renderer.
         *
         * It was never closed, so a finished game left a session behind holding
         * a frame deadline that nothing was working to any more — telling the
         * governor to keep meeting a target for a workload that no longer
         * exists.
         */
        performanceHints.close();
        if (initExecutor != null) {
            initExecutor.shutdownNow();
            try { initExecutor.awaitTermination(3, java.util.concurrent.TimeUnit.SECONDS); }
            catch (InterruptedException ignored) { Thread.currentThread().interrupt(); }
            initExecutor = null;
        }
        synchronized (lock) {
            if (nativeHandle != 0) {
                nativeDestroy(nativeHandle);
                nativeHandle = 0;
            }
        }
    }



    /*
     * The game window, in guest coordinates, as of the last scene change.
     * Only read while `fitGuestWindow` is on; kept up to date regardless so
     * that turning it on mid-session does not have to wait for a redraw.
     */
    private int primaryWinX, primaryWinY, primaryWinW, primaryWinH;
    private String primaryWinClassName = "";
    private boolean fitGuestWindow = false;
    private boolean transformersFitIdentityApplied = false;

    /**
     * Remembers the window the session is really showing, for [fitGuestWindow].
     *
     * The largest of the windows actually being drawn, ties going to the topmost
     * — which is the game, because a game's window is the biggest thing on a
     * desktop whose shell is not composited at all. Cheap enough to redo on
     * every scene change, and it has to be: the window a game finally settles on
     * is rarely the first one it maps.
     */
    /** Returns the WFC process class inherited by a drawable child, if any. */
    private String transformersWfcProcessClass(Window window) {
        int remaining = 64;
        for (Window candidate = window; candidate != null && remaining-- > 0;
             candidate = candidate.getParent()) {
            String className = candidate.getClassName();
            if (className != null && className.trim().equalsIgnoreCase("twfc.exe")) {
                return className;
            }
        }
        return "";
    }

    /** Records one fit target and updates the transform only when it changed. */
    private void updatePrimaryWindowLocked(int x, int y, int w, int h,
                                           String className,
                                           boolean transformersFamily,
                                           boolean gpuBacked) {
        if (className == null) className = "";
        if (x == primaryWinX && y == primaryWinY && w == primaryWinW
                && h == primaryWinH && primaryWinClassName.equals(className)) return;
        primaryWinX = x;
        primaryWinY = y;
        primaryWinW = w;
        primaryWinH = h;
        primaryWinClassName = className;
        android.util.Log.i("Wemu_Diagnostics", "Fit primary selected: class=\""
                + className + "\" position=" + x + "," + y + " size=" + w
                + "x" + h + " transformersFamily=" + transformersFamily
                + " gpuBacked=" + gpuBacked);
        if (fitGuestWindow) updateTransform();
    }

    private void notePrimaryWindow(ArrayList<RenderableWindow> list, int start) {
        int x = 0, y = 0, w = 0, h = 0;
        String className = "";
        long best = -1;
        final boolean preferTransformers =
                SecondScreenOutput.isTransformersWfcPanelActive();
        boolean foundTransformers = false;
        boolean foundTransformersGpu = false;
        for (int i = start; i < list.size(); i++) {
            RenderableWindow rw = list.get(i);
            if (rw.content == null) continue;
            if (preferTransformers && SecondScreenOutput.isSecondMonitorPosition(
                    rw.rootX, rw.rootY)) continue;
            final boolean transformers = preferTransformers &&
                    !transformersWfcProcessClass(rw.window).isEmpty();
            final boolean transformersGpu = transformers &&
                    rw.content.getTexture() instanceof GPUImage;
            if (preferTransformers) {
                if (foundTransformersGpu && !transformersGpu) continue;
                if (transformersGpu && !foundTransformersGpu) {
                    foundTransformers = true;
                    foundTransformersGpu = true;
                    best = -1;
                } else if (!foundTransformersGpu && foundTransformers
                        && !transformers) {
                    continue;
                } else if (!foundTransformersGpu && transformers
                        && !foundTransformers) {
                    foundTransformers = true;
                    best = -1;
                }
            }
            long area = (long) rw.content.width * rw.content.height;
            if (area >= best) {
                best = area;
                x = rw.rootX; y = rw.rootY;
                w = rw.content.width; h = rw.content.height;
                className = transformers
                        ? transformersWfcProcessClass(rw.window)
                        : rw.window.getClassName();
                if (className == null) className = "";
            }
        }
        updatePrimaryWindowLocked(x, y, w, h, className,
                foundTransformers, foundTransformersGpu);
    }

    /**
     * WFC creates its final DRI3 client after the last scene/configure event.
     * Follow the accepted GPU producer itself so a stale CPU Wine helper can
     * never remain the fitted top-screen window.
     */
    private void noteTransformersPrimaryGpuFrameLocked(
            Window window, Drawable source, int rootX, int rootY) {
        if (!SecondScreenOutput.isTransformersWfcPanelActive()
                || source == null || !(source.getTexture() instanceof GPUImage)
                || source.width <= 1 || source.height <= 1
                || SecondScreenOutput.isSecondMonitorPosition(rootX, rootY)) return;
        String className = transformersWfcProcessClass(window);
        if (className.isEmpty()) return;
        updatePrimaryWindowLocked(rootX, rootY, source.width, source.height,
                className, true, true);
        long now = android.os.SystemClock.uptimeMillis();
        if (now >= nextWfcFocusCheckAtMs) {
            nextWfcFocusCheckAtMs = now + 2000;
            xServerView.post(() -> {
                if (!inPipMode && xServerView.isAttachedToWindow()
                        && xServerView.hasWindowFocus()) {
                    com.wemu.engine.xserver.DesktopHelper.recoverWfcInputFocus(xServer, window);
                }
            });
        }
    }

    /**
     * Whether a game smaller than the desktop is blown up to fill the screen.
     *
     * Off, because it changes what "the screen" means and a container that is
     * happy windowed should stay that way.
     *
     * <h2>Why this exists at all</h2>
     *
     * Wine has its own answer to this — the fullscreen hack, which fakes the
     * mode change a game asks for and scales the result, with FSR if
     * `WINE_FULLSCREEN_FSR` is set. It does not apply here. The original starts Wine
     * as `explorer /desktop=shell,WxH`, so the guest is running inside a virtual
     * desktop, and inside one the desktop *is* a window: a game that asks to go
     * fullscreen at less than its size gets a smaller window placed in the
     * corner, and there is nothing for the hack to scale. Verified on the
     * device, with the environment confirmed on the running guest —
     * `WINE_FULLSCREEN_FSR=1`, `WINE_DISABLE_FULLSCREEN_HACK=0`, and the game
     * still in the corner.
     *
     * So it is done on this side instead, which is the better place for it
     * anyway. The compositor already maps guest coordinates onto the panel; this
     * changes what it maps from — the game's window rather than the whole
     * desktop — and everything downstream, including the upscaler, follows.
     *
     * <h2>What it is for</h2>
     *
     * It is the setting that makes the upscalers worth having. Nothing here can
     * make the guest draw a frame faster, so the only way to gain frames is for
     * the game to draw fewer pixels, and the only reliable way to arrange that
     * is to lower the resolution in the game's own options — where it will be
     * obeyed, unlike a desktop shrunk underneath it. Do that and the game lands
     * in the corner at its new size, which is why lowering it has never been
     * worth doing. With this on it fills the screen, EASU and RCAS have a
     * genuinely smaller source to reconstruct from, and the frames are real.
     */
    @Override
    public void setFitGuestWindow(boolean enabled) {
        if (fitGuestWindow == enabled) return;
        fitGuestWindow = enabled;
        synchronized (lock) { updateTransform(); }
    }

    @Override
    public boolean isFitGuestWindow() { return fitGuestWindow; }

    /**
     * True when there is a game window worth stretching to the screen.
     *
     * A window at least as large as the desktop already fills it, and one with
     * no area at all is a window that has not been drawn into yet. Both take the
     * ordinary path, so turning the setting on for a game that was already
     * fullscreen changes nothing.
     */
    private boolean fittingPrimaryWindow() {
        return WindowFitPolicy.shouldFit(fitGuestWindow, primaryWinW, primaryWinH,
                guestWidth(), guestHeight(), primaryWinClassName);
    }

    private void updateTransform() {
        if (nativeHandle == 0) return;
        final float zoom = magnifierZoom;
        final float ptrX = xServer.pointer.getX();
        final float ptrY = xServer.pointer.getY();

        /*
         * Fitting the game window replaces the transform rather than adjusting
         * it, so it is resolved first and returns.
         *
         * The compositor turns a guest coordinate into a clip coordinate as
         * `(offset + X * scale) / screenWidth * 2 - 1`, so mapping the desktop
         * onto the panel is offset 0, scale 1. Mapping a window instead is the
         * same equation solved for that window's edges: a scale of
         * `screenWidth / windowWidth` puts its far edge on the right of the
         * screen, and an offset of `-windowX * scale` puts its near edge on the
         * left.
         *
         * One scale is used for both axes, and centred, so a game that picks
         * 4:3 on a 16:9 panel is pillarboxed rather than distorted. The
         * magnifier is left alone: it is a transform of its own and two of them
         * composed is not useful here.
         */
        if (fittingPrimaryWindow() && zoom == 1.0f) {
            final int screenW = guestWidth(), screenH = guestHeight();
            int sourceX = primaryWinX;
            int sourceY = primaryWinY;
            int sourceW = primaryWinW;
            int sourceH = primaryWinH;

            /*
             * Wine may expose a nominal 1280x720 DXHR client as a 1286x755
             * decorated surface. Scaling that outer rectangle preserves the
             * title bar and turns the actual game image into a slightly tall,
             * visibly squeezed frame. Detect only the standard thin-frame case
             * whose inner client exactly matches the host panel aspect; shift
             * the decoration outside the clip and fit the client itself. This
             * mirrors the direct-scanout path and leaves genuinely windowed or
             * differently shaped games alone.
             */
            if (surfaceWidth > 0 && surfaceHeight > 0) {
                final int frame = 3;
                final int clientW = primaryWinW - frame * 2;
                final float panelAspect = (float) surfaceWidth / surfaceHeight;
                final int clientH = clientW > 0
                        ? Math.round(clientW / panelAspect) : 0;
                final int topInset = primaryWinH - clientH - frame;
                final boolean decorated = clientH > 0
                        && topInset >= 20 && topInset <= 64
                        && Math.abs((float) clientW / clientH - panelAspect) < 0.01f;
                if (decorated) {
                    sourceX += frame;
                    sourceY += topInset;
                    sourceW = clientW;
                    sourceH = clientH;
                }
            }

            final float scale = Math.min((float) screenW / sourceW,
                    (float) screenH / sourceH);
            final float fx = (screenW - sourceW * scale) * 0.5f
                    - sourceX * scale;
            final float fy = (screenH - sourceH * scale) * 0.5f
                    - sourceY * scale;
            nativeSetTransformAndScissor(nativeHandle, fx, fy, scale, scale,
                    false, 0, 0, 0, 0);
            return;
        }

        final float ox, oy, sx, sy;
        if (fullscreen) {
            viewTransformation.update(surfaceWidth, surfaceHeight,
                guestWidth(), guestHeight());
            if (zoom != 1.0f) {
                ox = viewTransformation.sceneOffsetX + ptrX * viewTransformation.sceneScaleX * (1f - zoom);
                oy = viewTransformation.sceneOffsetY + ptrY * viewTransformation.sceneScaleY * (1f - zoom);
                sx = viewTransformation.sceneScaleX * zoom;
                sy = viewTransformation.sceneScaleY * zoom;
            } else {
                ox = 0f; oy = 0f; sx = 1f; sy = 1f;
            }
        } else {
            float py = 0;
            if (screenOffsetYRelativeToCursor) {
                short halfH = (short)(guestHeight() / 2);
                py = Math.max(0, Math.min(ptrY - halfH / 2.0f, halfH));
            }
            ox = viewTransformation.sceneOffsetX + ptrX * viewTransformation.sceneScaleX * (1f - zoom);
            oy = viewTransformation.sceneOffsetY - py + ptrY * viewTransformation.sceneScaleY * (1f - zoom);
            sx = viewTransformation.sceneScaleX * zoom;
            sy = viewTransformation.sceneScaleY * zoom;
        }

        final boolean needScissor = !(fullscreen && zoom == 1.0f);
        nativeSetTransformAndScissor(nativeHandle, ox, oy, sx, sy,
            needScissor,
            viewTransformation.viewOffsetX, viewTransformation.viewOffsetY,
            viewTransformation.viewWidth,   viewTransformation.viewHeight);
    }

    public void updateScene() {
        ArrayList<RenderableWindow> newList = new ArrayList<>();
        try (XLock xl = xServer.lock(XServer.Lockable.WINDOW_MANAGER, XServer.Lockable.DRAWABLE_MANAGER)) {
            collectWindows(newList, xServer.windowManager.rootWindow,
                xServer.windowManager.rootWindow.getX(),
                xServer.windowManager.rootWindow.getY());
        }
        synchronized (lock) {
            renderableWindows = newList;
            pushRenderList(newList);
        }
    }

    private void collectWindows(ArrayList<RenderableWindow> list, Window window, int x, int y) {
        if (!window.attributes.isMapped()) return;
        if (window != xServer.windowManager.rootWindow) {
            boolean viewable = true;
            if (unviewableWMClasses != null) {
                String wc = window.getClassName();
                for (String cls : unviewableWMClasses) {
                    if (wc.contains(cls)) {
                        /*
                         * This window, and not everything inside it.
                         *
                         * `attributes.isEnabled` is what decides whether the X
                         * server delivers a pointer event to a window at all —
                         * see `InputDeviceManager.sendEvent`. Disabling the
                         * shell is right: it is not drawn, so it must not take
                         * clicks aimed at what is drawn over it.
                         *
                         * Disabling its *descendants* was not. Wine runs every
                         * program inside `explorer /desktop=`, so in the window
                         * tree the shell is the parent of the thing that was
                         * launched — and a sweep of its descendants therefore
                         * turned off input for the game as well as for the
                         * desktop behind it.
                         *
                         * That went unnoticed for as long as it did because a
                         * game does not use this path. Gamepads and the touch
                         * controls arrive through fake evdev inside the guest,
                         * and a full-screen game grabs the pointer outright. It
                         * is an ordinary window that suffers: a launcher, an
                         * installer, a settings dialog — visible, correct, and
                         * completely unclickable.
                         *
                         * Nothing is lost by narrowing it. The walk below
                         * visits every descendant and tests each against these
                         * same classes, so a shell window nested inside the
                         * shell is still disabled when its turn comes.
                         */
                        window.attributes.setEnabled(false);
                        viewable = false; break;
                    }
                }
            }
            /*
             * The same guards `isViewableWindow` and the scanout renderer apply.
             *
             * This list used to take every mapped window whatever it was, which
             * put input-only and 1x1 windows — of which Wine creates a steady
             * supply for message handling — into the render list beside real
             * ones. They have nothing to present, so on their own they only cost
             * a slot; the damage is done in `pushRenderList`, where the size of
             * a listed window decides what occludes what.
             */
            if (viewable && window.isInputOutput()
                    && window.getWidth() > 1 && window.getHeight() > 1
                    && !isSecondScreenOnlyWindow(window, x, y)) {
                list.add(new RenderableWindow(window.getContent(), window, x, y));
            }
        }
        for (Window child : window.getChildren())
            collectWindows(list, child, child.getX() + x, child.getY() + y);
    }

    private void pushRenderList(ArrayList<RenderableWindow> list) {
        if (nativeHandle == 0) return;
        final int screenW = guestWidth(), screenH = guestHeight();
        /*
         * Everything below a full-screen window is hidden by it, so the list is
         * cut there. The cut is worth being careful about, because getting it
         * wrong does not degrade the picture — it removes the game from it.
         *
         * The test used to be size alone: the topmost listed window at least as
         * large as the screen won, and everything under it was dropped. That is
         * only sound if such a window is genuinely painting over what it covers,
         * and a screen-sized entry in this list need not be. A game that keeps a
         * full-screen wrapper or an overlay above its own swapchain window has
         * two of them, and if the one on top is not the one being drawn, the cut
         * discards the window that *is* — the game keeps running, keeps
         * presenting, and stops appearing. Which is exactly what it looked like:
         * a game that launched and then went behind the desktop.
         *
         * So an occluder now has to be something that can actually occlude:
         * still mapped, able to hold pixels, and having presented at least one
         * frame. A window that has never drawn anything covers nothing.
         */
        int start = 0;
        for (int i = list.size() - 1; i >= 0; i--) {
            RenderableWindow rw = list.get(i);
            if (rw.content == null || !hasPresented(rw)) continue;
            if (rw.window != null
                    && (!rw.window.attributes.isMapped() || !rw.window.isInputOutput())) continue;
            if (rw.content.width >= screenW && rw.content.height >= screenH) {
                start = i; break;
            }
        }
        notePrimaryWindow(list, start);
        final int n = list.size() - start;
        if (n <= 0) { nativeSetRenderList(nativeHandle, new long[0], new int[0], new int[0], 0); return; }
        long[] ids = new long[n]; int[] xs = new int[n]; int[] ys = new int[n];
        for (int i = 0; i < n; i++) {
            RenderableWindow rw = list.get(start + i);
            ids[i] = did(rw.content); xs[i] = rw.rootX; ys[i] = rw.rootY;
        }
        nativeSetRenderList(nativeHandle, ids, xs, ys, n);
    }


    private void sendCursorToNative(Cursor cursor) {
        if (nativeHandle == 0) return;
        Drawable cd; short hotX = 0, hotY = 0;
        boolean effVis = cursorVisible;
        if (cursor != null) {
            if (!cursor.isVisible()) effVis = false;
            cd = cursor.cursorImage; hotX = (short)cursor.hotSpotX; hotY = (short)cursor.hotSpotY;
        } else { cd = rootCursorDrawable; }
        nativeSetCursorVisible(nativeHandle, effVis);
        if (effVis && cd != null && cd.getBuffer() != null) {
            synchronized (cd.renderLock) {
                nativeUpdateCursorImage(nativeHandle, cd.getBuffer(), cd.width, cd.height, hotX, hotY);
            }
        }
    }

    /**
     * Records that a frame reached the screen, following whatever is drawing.
     *
     * Called from both content paths, which is half the point. Frames arrive
     * either through the Present extension — `onUpdateWindowContentDirect`, the
     * DRI3 route — or through the plain `onUpdateWindowContent` that everything
     * else uses, and only the first of the two ever reported.
     *
     * The other half is which window to believe. A fixed nomination is why the
     * counter worked on the desktop and died the moment a game opened: nothing
     * on the desktop satisfied the nomination test, so everything was counted
     * and the number was right by accident. A game window then qualified, was
     * pinned, and the moment the guest drew through a different window — a
     * launcher handing off, a fullscreen window replacing a windowed one,
     * anything with a splash screen — every frame belonged to a window nobody
     * was counting, and the reading fell to zero while the game ran perfectly.
     *
     * So the nomination follows the work. Whoever presents adopts the counter if
     * the incumbent has gone quiet, which makes a handoff cost one sampling
     * window instead of the whole session, and makes a stale nomination
     * impossible to get stuck on. The stickiness is what keeps a desktop shell
     * repainting a clock from stealing the count from a running game.
     */
    private void notePresentedFrame(Window window) {
        if (!isViewableWindow(window)) return;

        /*
         * Program launches are handed back to the host shell only once.  The
         * callback is posted by the activity, so by the time Compose removes its
         * desktop layer this presentation call has already put a real guest
         * frame into the renderer.  Keeping this in the shared presentation path
         * covers both ordinary X11 drawing and DRI3 games.
         */
        XServerRenderer.ProgramWindowListener programListener = null;
        synchronized (programWindowLock) {
            if (programWindowListener != null && window.id != programWindowToIgnore) {
                boolean resumedFromEmpty = presentedProgramWindows.isEmpty();
                trackingProgramWindows = true;
                presentedProgramWindows.add(window.id);
                // Invalidates a close timer posted for an earlier empty gap.
                if (resumedFromEmpty) programWindowGeneration++;
                if (!programWindowReportedVisible) {
                    programWindowReportedVisible = true;
                    programListener = programWindowListener;
                }
            }
        }
        if (programListener != null) {
            android.util.Log.d("Wemu_Compositor", "Foreground program window presented: " + window.id);
            programListener.onProgramWindowPresented(window.id);
        }

        final long now = android.os.SystemClock.uptimeMillis();
        final int current = fpsWindowId;

        if (window.id != current) {
            if (current != -1 && (now - lastFrameAtMs) < FPS_HANDOFF_MS) return;
            fpsWindowId = window.id;
        }
        lastFrameAtMs = now;

        raisePresentingThread();
        try {
            performanceHints.reportFrame();
        } catch (Throwable t) {
            // Belt and braces. PerformanceHints swallows its own failures, and
            // this is still the one call site where a mistake costs a session.
            android.util.Log.w("Wemu_Compositor", "Frame hint failed", t);
        }

        final FrameListener listener = frameListener;
        if (listener != null) listener.onFramePresented(zeroCopyPath);

        /*
         * The overlay is redrawn four times a second, not a hundred and twenty.
         *
         * This runs on the presenting thread, in the path every frame takes.
         * The number it draws is read by a human, and a human cannot read more
         * than a few updates a second — so the other 116 were pure cost in the
         * one place in the app where cost is measured in frames.
         */
        if (classicHudRef != null && (now - lastHudUpdateAtMs) >= HUD_INTERVAL_MS) {
            lastHudUpdateAtMs = now;
            classicHudRef.update();
        }
    }

    private boolean isViewableWindow(Window window) {
        if (window == null || window == xServer.windowManager.rootWindow ||
            !window.attributes.isMapped() || !window.isInputOutput() ||
            window.getWidth() <= 1 || window.getHeight() <= 1) return false;

        if (unviewableWMClasses != null) {
            final String windowClass = window.getClassName();
            if (windowClass != null) {
                for (String hiddenClass : unviewableWMClasses) {
                    if (hiddenClass != null && windowClass.contains(hiddenClass)) return false;
                }
            }
        }
        return true;
    }

    /**
     * Tells the scheduler this thread is presenting.
     *
     * Once, on the first frame, because the thread does not change and
     * `setThreadPriority` is a syscall. Without it the thread that hands frames
     * to the compositor runs at the default priority and competes on equal terms
     * with every background thread in the process — which is how a game that has
     * the GPU headroom still stutters.
     */
    private void raisePresentingThread() {
        if (presentingThreadRaised) return;
        presentingThreadRaised = true;
        try {
            android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_URGENT_DISPLAY);
            performanceHints.attachCurrentThread();
        } catch (Throwable t) {
            /*
             * Throwable, not RuntimeException.
             *
             * This is the frame path: anything that escapes here does not spoil
             * a hint, it ends the session. A missing platform method arrives as
             * NoSuchMethodError, which is an Error, and an Error thrown through
             * a frame callback is indistinguishable from the container crashing
             * on launch — which is exactly how it was reported.
             */
            android.util.Log.w("Wemu_Compositor", "Could not raise the presenting thread", t);
        }
    }

    private volatile boolean presentingThreadRaised = false;
    private volatile long lastHudUpdateAtMs = 0L;

    /** Four times a second: faster than a reader, slower than a frame. */
    private static final long HUD_INTERVAL_MS = 250L;

    /**
     * The scheduler hint session, which is a no-op below API 31.
     *
     * Held rather than created per frame: the whole point is that it persists
     * across frames so the governor can learn what this workload costs.
     */
    private final PerformanceHints performanceHints = new PerformanceHints();

    /**
     * Whether the guest's frames arrive as a shared buffer or as a copy.
     *
     * The difference is a whole framebuffer memcpy per frame — at 1080p and 60
     * fps, around 500 MB/s of pure overhead — and until now there was no way to
     * tell which path a game was on. It depends on whether the guest handed us
     * a DRI3 buffer, which depends on the driver, the container and the game,
     * so it cannot be answered anywhere but at run time.
     */
    private volatile boolean zeroCopyPath = false;

    /** @return the hint session, so the session can set its target from the cap. */
    public PerformanceHints getPerformanceHints() { return performanceHints; }

    /**
     * Tells the compositor how often this surface will actually change.
     *
     * Without it the panel runs at whatever rate it likes and picks a cadence
     * for the app by observation. A game at a steady 60 on a 120 Hz panel is the
     * bad case: the compositor cannot tell 60-with-every-other-frame-repeated
     * from 120-with-half-the-frames-identical, and what a player sees is
     * intermittent judder that looks exactly like a frame rate problem and is
     * not one.
     *
     * `FRAME_RATE_COMPATIBILITY_FIXED_SOURCE` is the important half: it says
     * this content has one true rate and would rather the display changed mode
     * than have frames duplicated unevenly.
     */
    private void declareFrameRate(Surface surface) {
        if (surface == null) return;
        if (android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.R) return;

        /*
         * The rate that reaches the surface, not the rate the game draws at.
         *
         * These differ once frame generation is on, and getting it wrong is not
         * a hint being ignored — it is a hard cap. `FIXED_SOURCE` asks
         * SurfaceFlinger to change display mode rather than duplicate frames,
         * so declaring 30 on a 120 Hz panel puts the panel into 30 Hz and the
         * generated frames have nowhere to go. Capping to 30 with 2x on
         * produced 30 frames a second in total, which is the feature doing
         * nothing at full cost.
         */
        int cap = getPresentedFpsLimit();

        /*
         * Uncapped declares nothing, and this used to declare 60.
         *
         * The reasoning for 60 was that zero means "no preference" and returns
         * the compositor to guessing, while most of what runs here targets 60 —
         * so 60 was the better guess. That is sound for a *description* and
         * wrong for this call, because the paragraph above is the reason why:
         * FIXED_SOURCE does not describe the content, it asks SurfaceFlinger to
         * put the panel in that mode. On a 120 Hz panel the guess came true by
         * force. A container set to unlimited — the setting somebody picks when
         * they want every frame the device can produce — was pinning the display
         * to 60 and throwing away half of them.
         *
         * Zero with DEFAULT is the documented way to say "no request", which
         * leaves the panel at whatever mode it was already in. That is the right
         * answer for content with no known rate: the guess belongs to the
         * compositor, which can see the frames arriving, rather than to us.
         *
         * A cap still uses FIXED_SOURCE, because there the rate genuinely is
         * known and fixed, and mode-matching is what stops it juddering.
         */
        try {
            if (cap > 0) {
                surface.setFrameRate(
                        (float) cap,
                        adaptiveRefreshRate
                                ? Surface.FRAME_RATE_COMPATIBILITY_DEFAULT
                                : Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
            } else {
                surface.setFrameRate(0f, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT);
            }
        } catch (Throwable t) {
            // Same rule as the frame path: a refused hint must not stop a draw.
            android.util.Log.w("Wemu_Compositor", "Could not declare a frame rate", t);
        }
    }

    /**
     * How long the counted window keeps its claim after its last frame.
     *
     * Long enough that a game running at 10fps is not interrupted by a shell
     * repaint, short enough that a handoff is invisible at a one-second sample.
     */
    private static final long FPS_HANDOFF_MS = 250L;

    private volatile long lastFrameAtMs = 0L;

    /*
     * Bounded presentation telemetry.  These counters live on the renderer
     * lock's submission path, so they need no per-frame allocation or atomics.
     * A ten-second summary is enough to distinguish "the guest stopped
     * presenting" from "AHB import is being rejected" without turning the log
     * itself into a frame-time problem.
     */
    private static final long RENDER_HEALTH_INTERVAL_MS = 10_000L;
    private static final long RENDER_REJECTION_INTERVAL_MS = 5_000L;
    private long directSubmissionCount;
    private long ahbAcceptedCount;
    private long ahbRejectedCount;
    private long cpuAcceptedCount;
    private long cpuRejectedCount;
    private long lastAcceptedAtMs;
    private long lastRenderHealthAtMs;
    private long lastRenderRejectionAtMs;

    private void reportRenderHealthLocked(boolean rejected, Drawable source) {
        long now = android.os.SystemClock.uptimeMillis();
        boolean due = now - lastRenderHealthAtMs >= RENDER_HEALTH_INTERVAL_MS;
        boolean rejectionDue = rejected
                && now - lastRenderRejectionAtMs >= RENDER_REJECTION_INTERVAL_MS;
        if (!due && !rejectionDue) return;

        long acceptedAgo = lastAcceptedAtMs > 0 ? now - lastAcceptedAtMs : -1L;
        String message = "Compositor health: direct=" + directSubmissionCount
                + " ahb=" + ahbAcceptedCount + "/" + ahbRejectedCount
                + " cpu=" + cpuAcceptedCount + "/" + cpuRejectedCount
                + " lastAcceptedAgoMs=" + acceptedAgo
                + " source=" + source.width + "x" + source.height
                + (source.isDirectScanout() ? " socket-ahb" : " buffered");
        ProcessHelper.emitDiagnostic(message);
        lastRenderHealthAtMs = now;
        if (rejected) lastRenderRejectionAtMs = now;
    }

    public boolean onUpdateWindowContentDirect(Window window, Drawable pixmap, short xOff, short yOff) {
        Drawable target = window != null ? window.getContent() : null;
        if (pixmap == null || target == null || !isViewableWindow(window)) return false;

        final boolean accepted;
        synchronized (lock) {
            if (nativeHandle == 0) return false;
            directSubmissionCount++;
            final int rx = window.getRootX() + xOff, ry = window.getRootY() + yOff;
            accepted = submitDrawableLocked(window, target, pixmap, rx, ry);
        }
        // Program-window callbacks may re-enter the renderer, so keep them out
        // of both the renderer mutex and the drawable's render lock.
        if (accepted) notePresentedFrame(window);
        return accepted;
    }

    @Override
    public void onUpdateWindowContent(Window window) {
        Drawable drawable = window != null ? window.getContent() : null;
        if (drawable == null || !isViewableWindow(window)) return;

        final boolean accepted;
        synchronized (lock) {
            if (nativeHandle == 0) return;
            final int rx = window.getRootX(), ry = window.getRootY();
            accepted = submitDrawableLocked(window, drawable, drawable, rx, ry);
        }
        if (accepted) notePresentedFrame(window);
    }

    private boolean isSecondScreenOnlyWindow(Window window, int rootX, int rootY) {
        return SecondScreenOutput.isSecondMonitorPosition(rootX, rootY)
                || SecondScreenOutput.isEarlyClaimPanelWindow(window);
    }

    /**
     * Hands a lower-only window's buffer to the bottom panel.
     *
     * GPU-backed panels retain the direct AHardwareBuffer route. Lightweight
     * GDI panels use SecondScreenOutput's bounded CPU ring. A failed submission
     * drops only that lower frame; classification remains authoritative so the
     * panel never falls through into the primary scene.
     */
    private boolean submitToSecondScreen(Window window, Drawable target, Drawable source) {
        Window deusEx = xServer.windowManager.findWindowByProcessName("DXHRDC.exe");
        if (deusEx != null) {
            SecondScreenOutput.noteGuestWindow(
                    window.id, "dxhrdc.exe", deusEx.getName());
            setFitGuestWindow(true);
        } else {
            ScreenLayout.Monitor second = SecondScreenOutput.getMonitor();
            String processClass = transformersWfcProcessClass(window);
            if (second != null && NativePanelIdentity.isTransformersWfcLowerPanelFallback(
                    processClass, source.width, source.height,
                    second.width, second.height)) {
                // Wine drops the private Win32 class/title on WFC's drawable
                // child.  Its process identity plus an exact lower-monitor fit
                // uniquely identifies the panel without claiming the game.
                SecondScreenOutput.noteGuestWindow(
                        window.id, "TransformersWfcSecondScreenPanel",
                        "Transformers: War for Cybertron Dual-Screen");
            } else {
                SecondScreenOutput.noteGuestWindow(window);
            }
        }
        if (SecondScreenOutput.isStardewValleyPanelActive()) {
            // Stardew centres its 1280x720 Wine window in the full stacked
            // desktop. Fit the game client itself to the top panel.
            setFitGuestWindow(true);
        }
        if (SecondScreenOutput.isTransformersWfcPanelActive()) {
            // WFC changes from its full-size startup movie to a smaller DRI3
            // game client after the panel appears. Older shortcuts may not
            // carry fitGuestWindow, so assert it at the observed transition.
            setFitGuestWindow(true);
            if (!transformersFitIdentityApplied) {
                transformersFitIdentityApplied = true;
                // The scene was normally collected before the lower window's
                // first frame established its identity. Re-select immediately
                // from that scene so the stale Wine desktop does not remain the
                // fit target until an unrelated map/configure event occurs.
                notePrimaryWindow(renderableWindows, 0);
            }
        }
        final boolean submitted;
        if (source.getTexture() instanceof GPUImage) {
            // Existing native panels retain their zero-copy GPU path.
            GPUImage image = (GPUImage) source.getTexture();
            long ahbPtr = image.getHardwareBufferPtr();
            submitted = ahbPtr != 0 && SecondScreenOutput.submit(
                    ahbPtr, -1, source.width, source.height);
        } else {
            /*
             * Skyrim uses a lightweight GDI panel so it never creates the
             * second DXGI/Vulkan device that disturbs its primary framegen
             * swapchain. Only that changed UI window takes this bounded copy.
             */
            java.nio.ByteBuffer pixels = source.getBuffer();
            int stride = source.height > 0 && pixels != null
                    ? pixels.capacity() / (source.height * 4) : 0;
            submitted = pixels != null && stride >= source.width &&
                    SecondScreenOutput.submitPixels(
                            pixels, source.width, source.height, stride);
        }
        if (!submitted) return false;

        markDrawablePresented(window, target);
        return true;
    }

    /** Called with {@link #lock} held; lower-only ownership survives output recreation. */
    private boolean submitDrawableLocked(Window window, Drawable target, Drawable source,
                                         int rootX, int rootY) {
        synchronized (source.renderLock) {
            /*
             * A window the guest put on the second monitor never enters the
             * scene. It goes straight to the bottom panel's own layer, and
             * returning accepted here is what stops it also being placed in
             * this compositor's coordinate space — where it sits below the
             * primary monitor and would be clipped away frame after frame,
             * with the guest still paying to draw it.
             */
            if (isSecondScreenOnlyWindow(window, rootX, rootY)) {
                submitToSecondScreen(window, target, source);
                // Classification, not a transient Surface attachment or frame
                // acceptance, decides ownership. Never leak a native panel
                // into the primary scene while the lower Surface is recreated.
                return true;
            }

            noteTransformersPrimaryGpuFrameLocked(window, source, rootX, rootY);

            if (source.getTexture() instanceof GPUImage) {
                GPUImage image = (GPUImage) source.getTexture();
                java.nio.ByteBuffer fallback = image.getVirtualData();
                short stride = image.getStride() > 0 ? image.getStride() : source.width;
                long ahbPtr = image.getHardwareBufferPtr();
                if (ahbPtr != 0 && nativeUpdateWindowContentAHB(nativeHandle, did(target),
                        ahbPtr, fallback, source.width, source.height, stride, rootX, rootY)) {
                    ahbAcceptedCount++;
                    lastAcceptedAtMs = android.os.SystemClock.uptimeMillis();
                    zeroCopyPath = true;
                    markDrawablePresented(window, target);
                    reportRenderHealthLocked(false, source);
                    return true;
                }
                if (ahbPtr != 0) ahbRejectedCount++;
                zeroCopyPath = false;
                if (fallback != null && nativeUpdateWindowContent(nativeHandle, did(target),
                        fallback, source.width, source.height, stride, rootX, rootY)) {
                    cpuAcceptedCount++;
                    lastAcceptedAtMs = android.os.SystemClock.uptimeMillis();
                    markDrawablePresented(window, target);
                    reportRenderHealthLocked(false, source);
                    return true;
                }

                /*
                 * A socket-imported AHB has no CPU mapping. Drawable retains
                 * the zero-filled allocation made by its constructor, but that
                 * buffer has never contained the producer's pixels. Falling
                 * through and uploading it turns an import error into a
                 * successful black frame and hides the actual fault. Preserve
                 * the previous valid frame and let Present report the rejected
                 * hand-off instead.
                 */
                if (source.isDirectScanout() && fallback == null) {
                    cpuRejectedCount++;
                    reportRenderHealthLocked(true, source);
                    return false;
                }
            }

            java.nio.ByteBuffer pixels = source.getBuffer();
            if (pixels == null || source.height <= 0) {
                cpuRejectedCount++;
                reportRenderHealthLocked(true, source);
                return false;
            }
            zeroCopyPath = false;
            short stride = (short)(pixels.capacity() / (source.height * 4));
            if (!nativeUpdateWindowContent(nativeHandle, did(target), pixels,
                    source.width, source.height, stride, rootX, rootY)) {
                cpuRejectedCount++;
                reportRenderHealthLocked(true, source);
                return false;
            }
            cpuAcceptedCount++;
            lastAcceptedAtMs = android.os.SystemClock.uptimeMillis();
            markDrawablePresented(window, target);
            reportRenderHealthLocked(false, source);
            return true;
        }
    }

    @Override
    public void onPointerMove(short x, short y) {
        synchronized (lock) {
            if (nativeHandle == 0) return;
            nativeSetPointerPos(nativeHandle, x, y);
            Window pw = xServer.inputDeviceManager.getPointWindow();
            Cursor cursor = pw != null ? pw.attributes.getCursor() : null;
            if (cursor != lastCursor) { lastCursor = cursor; sendCursorToNative(cursor); }
            if (screenOffsetYRelativeToCursor || magnifierZoom != 1.0f) updateTransform();
        }
    }


    @Override
    public void onDestroyWindow(Window window) {
        noteProgramWindowRemoved(window.id);
        SecondScreenOutput.releaseGuestWindow(window.id);
        synchronized (lock) {
            presentedDrawableByWindow.remove(window.id);
        }
        final long id = did(window.getContent());
        xServerView.queueEvent(() -> {
            synchronized (lock) {
                if (nativeHandle != 0) nativeRemoveWindow(nativeHandle, id);
            }
            updateScene();
        });
    }

    @Override
    public void onMapWindow(Window window) {
        xServerView.queueEvent(() -> {
            /*
             * Native GDI panels commonly finish their first paint before
             * ShowWindow. Mapping exposes retained pixels but does not produce
             * another damage event, so the ordinary content callback never
             * reaches SecondScreenOutput. Replay that one retained frame at
             * the mapped transition; classification is checked again on the
             * renderer worker after Wine has finished the topology change.
             */
            Drawable content = window != null ? window.getContent() : null;
            if (content != null && isViewableWindow(window)
                    && isSecondScreenOnlyWindow(
                            window, window.getRootX(), window.getRootY())) {
                onUpdateWindowContent(window);
            }
            updateScene();
        });
    }

    @Override
    public void onUnmapWindow(Window window) {
        noteProgramWindowRemoved(window.id);
        SecondScreenOutput.releaseGuestWindow(window.id);
        synchronized (lock) {
            presentedDrawableByWindow.remove(window.id);
        }
        final long id = did(window.getContent());
        xServerView.queueEvent(() -> {
            synchronized (lock) {
                if (nativeHandle != 0) nativeRemoveWindow(nativeHandle, id);
            }
            updateScene();
        });
    }

    @Override public void onChangeWindowZOrder(Window window) { xServerView.queueEvent(this::updateScene); }

    @Override
    public void onUpdateWindowGeometry(Window window, boolean resized) {
        final Long staleDrawableId;
        synchronized (lock) {
            staleDrawableId = resized ? presentedDrawableByWindow.remove(window.id) : null;
        }
        xServerView.queueEvent(() -> {
            if (staleDrawableId != null) {
                synchronized (lock) {
                    if (nativeHandle != 0) nativeRemoveWindow(nativeHandle, staleDrawableId);
                }
            }
            updateScene();
        });
    }

    @Override
    public void onUpdateWindowAttributes(Window window, Bitmask mask) {
        if (mask.isSet(WindowAttributes.FLAG_CURSOR)) {
            synchronized (lock) {
                Window pw = xServer.inputDeviceManager.getPointWindow();
                if (pw == window) { lastCursor = window.attributes.getCursor(); sendCursorToNative(lastCursor); }
            }
        }
    }

    @Override
    public void onModifyWindowProperty(Window window, Property property) {
        if (!isPanelIdentityProperty(property)
                || !SecondScreenOutput.isEarlyClaimPanelWindow(window)) return;

        /* See ASurfaceRenderer: Wine may map and paint a GDI panel before its
         * EWMH title is visible. Replay the retained drawable at the moment the
         * private identity arrives instead of waiting for another game-state
         * change to produce a second paint. */
        submitNativePanelTree(window);
        xServerView.queueEvent(this::updateScene);
    }

    private void submitNativePanelTree(Window window) {
        if (window == null) return;
        if (SecondScreenOutput.isEarlyClaimPanelWindow(window)) {
            onUpdateWindowContent(window);
        }
        for (Window child : window.getChildren()) submitNativePanelTree(child);
    }

    private static boolean isPanelIdentityProperty(Property property) {
        if (property == null) return false;
        String name = property.nameAsString();
        return "WM_CLASS".equals(name) || "WM_NAME".equals(name)
                || "_NET_WM_NAME".equals(name);
    }

    /**
     * The colour the X root is cleared to.
     *
     * Held as a field as well as pushed, because the surface is torn down and
     * rebuilt on every rotation and panel move, and a setting that only lives in
     * native memory comes back as whatever the constructor chose -- which for
     * this is black. Every other renderer setting on this class is restored the
     * same way in `surfaceChanged`; this one joins them.
     */
    public void setRootColor(int argb) {
        pendingRootColor = argb;
        synchronized (lock) { if (nativeHandle != 0) nativeSetRootColor(nativeHandle, argb); }
    }

    public void setCursorVisible(boolean visible) {
        cursorVisible = visible;
        synchronized (lock) {
            if (nativeHandle != 0) { nativeSetCursorVisible(nativeHandle, visible); if (visible) sendCursorToNative(lastCursor); }
        }
    }

    public boolean isCursorVisible() { return cursorVisible; }

    public void setDriverInfo(String driverPath, String libraryName, String nativeLibDir) {
        this.driverPath = driverPath;
        this.driverLibraryName = libraryName;
        this.nativeLibDir = nativeLibDir;
        android.util.Log.d("Wemu_Renderer",
            "setDriverInfo: path=" + driverPath + " lib=" + libraryName);
    }

    public void setVerboseLog(boolean v) {
        synchronized (lock) { if (nativeHandle != 0) nativeSetVerboseLog(nativeHandle, v); }
    }

    public void dumpRendererInfo() {
        synchronized (lock) { if (nativeHandle != 0) nativeDumpRendererInfo(nativeHandle); }
    }

    public void setStretchMode(int mode) {
        pendingStretchMode = mode;
        synchronized (lock) { if (nativeHandle != 0) nativeSetStretchMode(nativeHandle, mode); }
    }

    public void setPostFXMode(int mode) {
        pendingPostFXMode = mode;
        synchronized (lock) { if (nativeHandle != 0) nativeSetPostFXMode(nativeHandle, mode); }
    }

    public void setSharpness(float s) {
        pendingSharpness = s;
        synchronized (lock) { if (nativeHandle != 0) nativeSetSharpness(nativeHandle, s); }
    }

    public void setFilterMode(int mode) {
        pendingFilterMode = mode;
        synchronized (lock) { if (nativeHandle != 0) nativeSetFilterMode(nativeHandle, mode); }
    }

    public void setSwapRB(boolean enabled) {
        pendingSwapRB = enabled;
        synchronized (lock) { if (nativeHandle != 0) nativeSetSwapRB(nativeHandle, enabled); }
    }

    public void setVkPresentMode(int mode) {
        pendingPresentMode = mode;
        synchronized (lock) { if (nativeHandle != 0) nativeSetPresentMode(nativeHandle, mode); }
    }

    public int[] getSupportedPresentModes() {
        synchronized (lock) {
            if (nativeHandle != 0) return nativeGetSupportedPresentModes(nativeHandle);
        }
        return new int[0];
    }

    public boolean isFullscreen() { return fullscreen; }
    public void toggleFullscreen() {
        fullscreen = !fullscreen;
        synchronized (lock) { updateTransform(); }
        xServerView.queueEvent(this::updateScene);
    }
    public void setScreenOffsetYRelativeToCursor(boolean b) {
        screenOffsetYRelativeToCursor = b;
        synchronized (lock) { updateTransform(); }
    }
    public boolean isScreenOffsetYRelativeToCursor() { return screenOffsetYRelativeToCursor; }
    public void setMagnifierZoom(float zoom) {
        magnifierZoom = zoom;
        synchronized (lock) { updateTransform(); }
    }
    public float getMagnifierZoom() { return magnifierZoom; }
    public void setUnviewableWMClasses(String... classes) { this.unviewableWMClasses = classes; }

    private int     pendingRootColor      = 0;
    private int     pendingPresentMode    = 2;
    private int     pendingStretchMode    = 0;
    private int     pendingFilterMode     = 0;
    private int     pendingPostFXMode     = 0;
    private float   pendingSharpness      = 0.5f;
    private boolean pendingSwapRB         = false;

    private FrameRating classicHudRef = null;
    /**
     * Told when a frame has been presented.
     *
     * The original's own overlay is gone — the HUD is a Compose surface on the other
     * panel now — so the renderer no longer holds a view to poke. It reports the
     * event and lets whoever is listening decide what to draw and where, which is
     * what makes the same signal usable from a Presentation on a second display.
     */
    private volatile FrameListener frameListener = null;
    private volatile int fpsWindowId = -1;
    private int fpsLimit = 0;

    /**
     * Frames presented per frame the guest draws, or 1 when nothing is
     * generating. Only affects what rate this surface declares; the cap itself
     * still means the rate the *game* is held to.
     */
    private int frameGenMultiplier = 1;

    /** Notified on the render thread each time the frame-timing window updates. */
    public interface FrameListener {
        /**
         * @param zeroCopy whether the last frame was handed over as a shared
         *   buffer rather than copied. Reported one frame late, which is
         *   irrelevant for a status line and keeps this off the hot path.
         */
        void onFramePresented(boolean zeroCopy);
    }

    @Override
    public void armProgramWindowListener(XServerRenderer.ProgramWindowListener listener) {
        // If the desktop was restored over a running game, its next frame is not
        // evidence that the newly requested program is ready. Wait for a window
        // handoff instead of immediately uncovering the old one.
        synchronized (programWindowLock) {
            programWindowToIgnore = fpsWindowId;
            programWindowListener = listener;
            trackingProgramWindows = false;
            programWindowReportedVisible = false;
            programWindowGeneration++;
            presentedProgramWindows.clear();
        }
    }

    @Override
    public void setProgramClosedListener(XServerRenderer.ProgramClosedListener listener) {
        programClosedListener = listener;
    }

    private void noteProgramWindowRemoved(int windowId) {
        final int emptyGeneration;
        synchronized (programWindowLock) {
            if (!trackingProgramWindows || !presentedProgramWindows.remove(windowId) ||
                !presentedProgramWindows.isEmpty()) return;
            emptyGeneration = programWindowGeneration;
        }

        // A launcher can unmap its splash before the game maps its main window.
        // Give that handoff time to present before declaring the program closed.
        xServerView.postDelayed(() -> {
            XServerRenderer.ProgramClosedListener listener = null;
            int reopenGeneration = -1;
            synchronized (programWindowLock) {
                if (trackingProgramWindows && programWindowReportedVisible &&
                    programWindowGeneration == emptyGeneration &&
                    presentedProgramWindows.isEmpty()) {
                    /*
                     * Report the gap so the host desktop is available if the
                     * program really ended, but retain the launch listener for
                     * a while. Some games tear down a splash process and do not
                     * present their main window until several seconds later.
                     * The old one-shot listener forgot the launch here, leaving
                     * that later window permanently behind the host desktop.
                     */
                    programWindowReportedVisible = false;
                    reopenGeneration = ++programWindowGeneration;
                    listener = programClosedListener;
                }
            }
            if (listener != null) {
                android.util.Log.d("Wemu_Compositor",
                        "Foreground program windows absent; waiting for handoff");
                listener.onProgramClosed();
            }

            if (reopenGeneration != -1) {
                final int expectedGeneration = reopenGeneration;
                xServerView.postDelayed(() -> {
                    synchronized (programWindowLock) {
                        if (trackingProgramWindows && !programWindowReportedVisible &&
                            programWindowGeneration == expectedGeneration &&
                            presentedProgramWindows.isEmpty()) {
                            android.util.Log.d("Wemu_Compositor",
                                    "Foreground program handoff window expired");
                            trackingProgramWindows = false;
                            programWindowToIgnore = -1;
                            programWindowListener = null;
                            programWindowGeneration++;
                        }
                    }
                }, PROGRAM_REOPEN_GRACE_MS);
            }
        }, PROGRAM_CLOSE_GRACE_MS);
    }

    private static final long PROGRAM_CLOSE_GRACE_MS = 1200L;
    private static final long PROGRAM_REOPEN_GRACE_MS = 15000L;

    public void setFpsWindowId(int id) { fpsWindowId = id; }

    /** Frees the counter if it was following this window. */
    public void releaseFpsWindow(int id) {
        if (fpsWindowId == id) fpsWindowId = -1;
    }

    public void setFrameListener(FrameListener listener) { this.frameListener = listener; }

    public void setFrameRating(Object fr) {
        if (fr instanceof FrameRating) classicHudRef = (FrameRating) fr;
    }
    public int getFpsLimit() { return fpsLimit; }

    /**
     * Frames a second that should actually reach the panel. Zero is uncapped.
     *
     * The cap means the rate the *game* is held to. Once a layer in the guest is
     * generating, more frames than that arrive, and anything pacing the stream
     * has to work to the larger number or it throttles the generated frames back
     * out of existence.
     *
     * The X server's present pacer is the one that matters, because it releases
     * the guest's buffers. Held to the game's rate it hands back one buffer per
     * two presents, the guest's swapchain drains, and the layer — which acquires
     * without blocking, because blocking would stall the game — finds nothing
     * free and drops the frame it just paid to generate. Measured before this
     * existed: exactly 30 `generated frame acquire skipped` a second against a
     * 30 fps game at 2x, which is all of them.
     */
    public int getPresentedFpsLimit() {
        return fpsLimit > 0 ? fpsLimit * Math.max(1, frameGenMultiplier) : 0;
    }
    /**
     * The cap, and the two things downstream that have to agree with it.
     *
     * Both used to be told once at start-up. The console can change the cap
     * mid-session — that is the point of having it there — and a compositor
     * still pacing for 60 while the game is capped at 30, or a governor still
     * working to a 16.7 ms deadline the game is no longer trying to hit, is
     * worse than never having told them.
     */

    /**
     * How many frames reach the screen per frame the guest draws.
     *
     * Set from the container's frame-generation settings. The renderer does no
     * generating itself — a Vulkan layer in the guest does — but it is the
     * thing that declares a frame rate to the surface, so it has to know.
     */
    public void setFrameGenMultiplier(int multiplier) {
        this.frameGenMultiplier = Math.max(1, multiplier);
        declareFrameRate(currentSurface);
    }

    public void setFpsLimit(int limit) {
        this.fpsLimit = limit;
        performanceHints.setTargetFps(limit);
        declareFrameRate(currentSurface);
    }

    /**
     * The surface currently being drawn to, or null between attachments.
     *
     * Held only so the frame rate can be re-declared; the renderer's real
     * reference to it lives on the native side.
     */
    private volatile Surface currentSurface = null;
    private volatile boolean adaptiveRefreshRate = false;

    /** Lets SurfaceFlinger choose a compatible Thor mode instead of requiring an exact one. */
    public void setAdaptiveRefreshRate(boolean enabled) {
        adaptiveRefreshRate = enabled;
        declareFrameRate(currentSurface);
    }

    public void setPipMode(boolean pip) { inPipMode = pip; }
    public int getSurfaceWidth() { return surfaceWidth; }
    public int getSurfaceHeight() { return surfaceHeight; }

    /**
     * The view, by method as well as by field.
     *
     * `xServerView` is public and call sites reach it directly; this exists so
     * that code holding an {@link XServerRenderer} can get there too, without
     * every such call site having to know which renderer it has.
     */
    @Override
    public XServerView getRendererView() { return xServerView; }
    public void requestRender() {}


    private static class RenderableWindow {
        public final Drawable content; public final Window window; public int rootX, rootY;
        public RenderableWindow(Drawable c, Window w, int x, int y) {
            content = c; window = w; rootX = x; rootY = y;
        }
    }
}
