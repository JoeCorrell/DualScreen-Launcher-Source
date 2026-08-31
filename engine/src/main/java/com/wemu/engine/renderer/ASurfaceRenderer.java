package com.wemu.engine.renderer;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Rect;
import android.util.Log;
import android.view.Surface;

import androidx.annotation.Keep;

import com.wemu.engine.R;
import com.wemu.engine.widget.FrameRating;
import com.wemu.engine.widget.XServerView;
import com.wemu.engine.xserver.Bitmask;
import com.wemu.engine.xserver.Cursor;
import com.wemu.engine.xserver.Drawable;
import com.wemu.engine.xserver.Pointer;
import com.wemu.engine.xserver.Property;
import com.wemu.engine.xserver.Window;
import com.wemu.engine.xserver.WindowAttributes;
import com.wemu.engine.xserver.WindowManager;
import com.wemu.engine.xserver.XLock;
import com.wemu.engine.xserver.XServer;

import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

/**
 * A renderer that does not render: it hands each window's buffer to
 * SurfaceFlinger and lets the display controller do the compositing.
 *
 * <h2>What this is for</h2>
 *
 * {@link VulkanRenderer} is a second Vulkan device that does not own the game's
 * frames. Every frame it shows costs an upload or an import, a draw, and a
 * swap, on a GPU that is already the bottleneck in every title measured on this
 * hardware. This path removes all three: each X window gets its own
 * {@code ASurfaceControl}, its {@code AHardwareBuffer} is handed over with
 * {@code ASurfaceTransaction_setBuffer}, and SurfaceFlinger scans it out —
 * often straight from a display overlay, without the GPU touching it at all.
 *
 * <h2>What it gives up, and why that is not a bug list</h2>
 *
 * No shader runs here, so there is no FSR, NIS or SGSR upscaling, no sharpening,
 * no post-processing screen effect, and no filter mode. Those are not features
 * missing from this renderer; they are operations with nowhere to happen in a
 * path whose entire advantage is that nothing composites. Scaling and
 * letterboxing are expressed as source and destination rectangles on the
 * transaction and performed by the display hardware. A container that wants any
 * of the above wants {@link VulkanRenderer}, and the container editor says so
 * rather than offering settings that would quietly do nothing.
 *
 * <h2>Availability</h2>
 *
 * {@code ASurfaceControl} is API 29. The native library resolves every entry
 * point with {@code dlsym} and reports failure rather than refusing to load, so
 * on an older device {@link #onSurfaceCreated} simply returns false and the
 * session falls back. Ported from GameNative, whose implementation this follows
 * closely; the divergences are the ones noted in comments here.
 */
public class ASurfaceRenderer implements WindowManager.OnWindowModificationListener,
                                         Pointer.OnPointerMotionListener,
                                         XServerRenderer {

    private static final String TAG = "ASurfaceRenderer";

    /**
     * Per-window, per-frame logging.
     *
     * Off, and a constant rather than a setting: the upstream implementation
     * logs the full geometry of every window on every scene update, which at
     * 60fps is thousands of lines a second and changes the timing of the thing
     * being measured. Worth having when window placement is wrong, worth
     * nothing otherwise.
     */
    private static final boolean TRACE = false;

    static { System.loadLibrary("asurface_renderer"); }

    /**
     * Bumped every time the native context is rebuilt.
     *
     * {@link AHBImage} registers its swapchain with the native side, and a
     * registration made against a destroyed context means nothing to its
     * replacement. The counter is what lets each image re-register exactly once
     * after a surface comes back, instead of on every frame or never.
     */
    private static final AtomicLong NATIVE_CONTEXT_GENERATION = new AtomicLong();

    static long getNativeContextGeneration() {
        return NATIVE_CONTEXT_GENERATION.get();
    }

    public final XServerView xServerView;
    private final XServer xServer;
    public final ViewTransformation viewTransformation = new ViewTransformation();

    public int surfaceWidth;
    public int surfaceHeight;

    private boolean surfaceInitialized = false;
    private volatile Surface currentSurface = null;
    private boolean sfCompatMode = true;
    private int maximumConvertedBuffers = 8;
    private boolean adaptiveRefreshRate = false;
    private String[] unviewableWMClasses = null;
    private boolean containerCursorVisible = true;
    private boolean gameCursorVisible = true;
    private final Drawable rootCursorDrawable;
    private Cursor lastCursor = null;

    /* Foreground-program lifecycle, shared with the host Compose desktop. */
    private volatile XServerRenderer.ProgramWindowListener programWindowListener = null;
    private volatile XServerRenderer.ProgramClosedListener programClosedListener = null;
    private final Object programWindowLock = new Object();
    private final HashSet<Integer> presentedProgramWindows = new HashSet<>();
    private boolean trackingProgramWindows = false;
    private boolean programWindowReportedVisible = false;
    private int programWindowGeneration = 0;
    private int programWindowToIgnore = -1;
    private volatile int lastPresentedWindowId = -1;

    /*
     * The window whose completed transactions currently define the FPS value.
     *
     * Scanout sessions still contain Wine's desktop, launchers and transient
     * dialogs. Counting every SurfaceFlinger transaction adds unrelated frame
     * streams together; pinning one forever makes the counter fall to zero
     * after a launcher-to-game handoff. Follow the active presenter just like
     * VulkanRenderer does: a different window may adopt the counter after the
     * incumbent has been quiet for a short interval.
     */
    private volatile int fpsWindowId = -1;
    private volatile long lastFpsFrameAtMs = 0L;

    private int fpsLimit = 0;
    private int frameGenMultiplier = 1;
    private FrameRating hudRef = null;
    private final AtomicInteger skipFpsCount = new AtomicInteger(0);

    private final HashMap<Integer, WindowSurface> windowSurfaces = new HashMap<>();
    private final ArrayList<RenderableWindow> renderList = new ArrayList<>();
    private int renderListSize = 0;

    private Window desktopWindow = null;
    private Rect cachedDesktopDst = null;
    private int cachedDesktopSrcW = 0;
    private int cachedDesktopSrcH = 0;

    /*
     * A windowed game can render below the Wine desktop resolution. Direct
     * scanout has no compositor transform, so fitting it is expressed in the
     * destination rectangles sent to SurfaceFlinger instead. The lower-panel
     * window is never a candidate: its guest origin is below the primary
     * monitor and SecondScreenOutput owns its presentation.
     */
    private boolean fitGuestWindow = false;
    private int primaryWinX = 0;
    private int primaryWinY = 0;
    private int primaryWinW = 0;
    private int primaryWinH = 0;
    private String primaryWinClassName = "";

    private static class RenderableWindow {
        Drawable content;
        Window window;
        int rootX, rootY;
        boolean isDesktopChild;
        boolean isDesktopWindow;

        void set(Drawable c, Window w, int x, int y, boolean dc, boolean dw) {
            content = c;
            window = w;
            rootX = x;
            rootY = y;
            isDesktopChild = dc;
            isDesktopWindow = dw;
        }
    }

    private static class WindowGeometry {
        final Rect src = new Rect();
        final Rect dst = new Rect();
    }

    /** What was last pushed for a window, so an unchanged frame costs nothing. */
    private static class WindowSurface {
        int width, height;
        int zOrder;
        boolean visible = false;
        /**
         * Whether this window has ever handed over a buffer.
         *
         * A surface control with no buffer is not empty, it is *black*, and it
         * is as opaque as any other layer. The Vulkan compositor has no
         * equivalent problem — a window whose texture was never written blends
         * as nothing and whatever is behind it shows through — so a stray
         * full-screen window that never draws costs nothing there and hides the
         * entire game here. Valheim had one: a classless 1920x1080 sibling that
         * never presented a frame, sitting one z-order above the game.
         *
         * So visibility follows content rather than mapping. A window becomes
         * visible when it first presents, not when it is first collected.
         */
        boolean hasBuffer = false;
        final Rect lastSrc = new Rect();
        final Rect lastDst = new Rect();
    }

    public ASurfaceRenderer(XServerView xServerView, XServer xServer) {
        this.xServerView = xServerView;
        this.xServer = xServer;
        SecondScreenOutput.setXServer(xServer);
        this.rootCursorDrawable = createRootCursorDrawable();
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
        } catch (Exception e) {
            return null;
        }
    }

    /**
     * Whether to route buffers through the compatibility path in native code.
     *
     * Some devices reject a buffer whose format or usage flags SurfaceFlinger
     * did not expect, and answer by showing nothing at all rather than by
     * failing the transaction. The compatibility path converts first and is the
     * default for that reason.
     */
    public void setSfCompatMode(boolean apply) {
        this.sfCompatMode = apply;
    }

    /** Limits compatibility-path AHB pressure without changing direct scanout. */
    public void setMaximumConvertedBuffers(int count) {
        maximumConvertedBuffers = Math.max(2, Math.min(8, count));
    }

    /**
     * The guest rectangle this surface shows, which is the primary monitor and
     * not necessarily the whole root window.
     *
     * The two are the same for every single-screen container, and deliberately
     * are not when the container spans the Thor's two panels: this surface is
     * the top one, and scaling the whole root onto it would shrink the game to
     * half height to leave room for a panel that is not here. A window the
     * guest places on the second monitor falls outside this rectangle and is
     * clipped away — which is what it should do on this screen.
     */
    private int guestWidth() { return xServer.getPrimaryMonitor().width; }

    private int guestHeight() { return xServer.getPrimaryMonitor().height; }

    /**
     * Whether a guest window belongs exclusively to the physical lower panel.
     *
     * Position is the normal authority. Native panels also have stable,
     * private window identities, so recognize them before Wine has applied its
     * final monitor coordinates. Otherwise a first mapped scene can briefly
     * acquire an opaque SurfaceControl over the primary game window.
     */
    private boolean isSecondScreenOnlyWindow(Window window, int rootX, int rootY) {
        return SecondScreenOutput.isSecondMonitorPosition(rootX, rootY)
                || SecondScreenOutput.isEarlyClaimPanelWindow(window);
    }

    private boolean isSecondScreenOnlyWindow(Window window) {
        return window != null && isSecondScreenOnlyWindow(
                window, window.getRootX(), window.getRootY());
    }

    // --- Surface lifecycle ---------------------------------------------------

    public boolean onSurfaceCreated(Surface surface) {
        currentSurface = surface;
        if (surfaceInitialized) {
            if (nativeReattachSurface(surface)) {
                declareFrameRate();
                updateScene();
                return true;
            }
            surfaceInitialized = false;
            nativeDestroy();
        }

        skipFpsCount.set(0);
        surfaceInitialized = nativeInit(surface, guestWidth(), guestHeight());
        if (!surfaceInitialized) {
            currentSurface = null;
            Log.e(TAG, "ASurfaceControl unavailable; this device cannot use the scanout renderer");
            return false;
        }

        NATIVE_CONTEXT_GENERATION.incrementAndGet();
        nativeSetSfCallbackTarget(this);
        updateTransform();
        nativeInitScanout();
        declareFrameRate();
        sendCursorToNative(lastCursor);
        // The scene creates the per-window surface controls; the resubmit fills
        // them, because a window that is not drawing would otherwise stay blank
        // until it next happened to.
        updateScene();
        resubmitAllBuffers();
        return true;
    }

    public void onSurfaceChanged(int width, int height) {
        surfaceWidth = width;
        surfaceHeight = height;
        viewTransformation.update(width, height, guestWidth(), guestHeight());
        updateTransform();

        /*
         * The scene is recomputed, not just the transform.
         *
         * Every destination rectangle is derived from `surfaceWidth` and
         * `surfaceHeight`, and this is the first call that knows what those
         * are — `onSurfaceCreated` runs before the view has been measured, so
         * the scene it builds gives every window a zero-sized destination,
         * `adjustRectLT` rejects it, and `nativeUpdateWindow` is never reached.
         * The surface controls then exist, receive buffers, and stay invisible.
         * That is a window that is running and cannot be seen — which is
         * exactly what a game "launching into the background" looks like.
         */
        updateScene();
    }

    public void onSurfaceDestroyed() {
        currentSurface = null;
        if (surfaceInitialized) {
            nativeDestroyScanout();
            nativeDestroy();
            surfaceInitialized = false;
        }
        skipFpsCount.set(0);
        windowSurfaces.clear();
        cachedDesktopDst = null;
    }

    private void updateTransform() {
        nativeScanoutSetDst(viewTransformation.viewOffsetX,
                viewTransformation.viewOffsetY,
                viewTransformation.viewWidth,
                viewTransformation.viewHeight);
    }

    private void resubmitAllBuffers() {
        try (XLock xl = xServer.lock(XServer.Lockable.WINDOW_MANAGER,
                                     XServer.Lockable.DRAWABLE_MANAGER)) {
            resubmitBuffersForWindow(xServer.windowManager.rootWindow);
        }
    }

    private void resubmitBuffersForWindow(Window window) {
        if (window != xServer.windowManager.rootWindow
                && window.attributes.isMapped()
                && window.getContent() != null
                && windowSurfaces.containsKey(window.id)) {
            pushCpuImageToNative(window.id, window.getContent());
        }
        for (Window child : window.getChildren()) resubmitBuffersForWindow(child);
    }

    // --- Scene ---------------------------------------------------------------

    public void updateScene() {
        renderListSize = 0;
        try (XLock xl = xServer.lock(XServer.Lockable.WINDOW_MANAGER,
                                     XServer.Lockable.DRAWABLE_MANAGER)) {
            collectWindows(xServer.windowManager.rootWindow,
                    xServer.windowManager.rootWindow.getX(),
                    xServer.windowManager.rootWindow.getY());
        }
        pushRenderList();
    }

    /**
     * Walks the window tree, keeping the ones that should be on screen.
     *
     * <h2>Hiding a window means hiding what it contains</h2>
     *
     * Skipping the shell window alone is not enough, and this is the bug that
     * made a game "launch into the background": {@code explorer.exe} owns a
     * full-screen child with no WM_CLASS of its own, so the class filter never
     * matched it. It was collected, given a surface control at a higher z-order
     * than the game, and drawn over the top — an opaque layer with nothing in
     * it. The game was running, presenting and audible behind a black rectangle.
     *
     * {@code disableAllDescendants} is what {@code VulkanRenderer} has always
     * done here, and it is why the same shell never covers anything on that
     * path. Matched rather than reinvented.
     *
     * The size and input-output guards come from the same place: a 1x1 or
     * input-only window has nothing to present and would only take a surface
     * control and a z-order slot.
     */
    private void collectWindows(Window window, int x, int y) {
        if (!window.attributes.isMapped()) return;

        if (window != xServer.windowManager.rootWindow) {
            boolean viewable = true;
            if (unviewableWMClasses != null) {
                String wc = window.getClassName();
                if (wc != null) {
                    for (String cls : unviewableWMClasses) {
                        if (wc.contains(cls)) {
                            // This window only — see the note in VulkanRenderer.
                            // Wine parents every program to the shell, so
                            // disabling the shell's descendants took input away
                            // from the program that was launched.
                            window.attributes.setEnabled(false);
                            viewable = false;
                            break;
                        }
                    }
                }
            }

            if (viewable && window.isInputOutput()
                    && window.getWidth() > 1 && window.getHeight() > 1) {
                RenderableWindow rw;
                if (renderList.size() <= renderListSize) {
                    rw = new RenderableWindow();
                    renderList.add(rw);
                } else {
                    rw = renderList.get(renderListSize);
                }
                renderListSize++;
                rw.set(window.getContent(), window, x, y,
                        isDesktopChild(window), window == findDesktopWindow());
            }
        }

        for (Window child : window.getChildren()) {
            collectWindows(child, child.getX() + x, child.getY() + y);
        }
    }

    private void pushRenderList() {
        notePrimaryWindow();
        nativeBeginTransaction();
        HashSet<Integer> visibleIds = new HashSet<>();

        for (int i = 0; i < renderListSize; i++) {
            RenderableWindow rw = renderList.get(i);
            // Class filtering happens in `collectWindows`, so that a hidden
            // window's descendants go with it and the z-order stays dense.
            if (rw.content == null) continue;

            int contentId = rw.window.id;

            /*
             * A window rooted on the lower monitor belongs only to
             * SecondScreenOutput. Giving it a primary SurfaceControl as well
             * can leave an opaque, bufferless layer over the game -- the white
             * screen seen when Skyrim's GDI panel first mapped.
             */
            if (isSecondScreenOnlyWindow(rw.window, rw.rootX, rw.rootY)) continue;
            visibleIds.add(contentId);

            String debugName = rw.window.getClassName();
            if (debugName == null || debugName.isEmpty()) debugName = "(x11_window)";

            WindowGeometry geom = new WindowGeometry();
            boolean geometryOk = computeWindowRect(rw.rootX, rw.rootY,
                    rw.content.width, rw.content.height,
                    rw.isDesktopWindow, rw.isDesktopChild, geom);

            WindowSurface ws = getOrCreateWindowSurface(contentId,
                    rw.content.width, rw.content.height, debugName);
            if (ws == null) continue;

            // A window that has never presented stays hidden; see WindowSurface.
            boolean shouldShow = ws.hasBuffer;
            boolean needsUpdate = ws.visible != shouldShow || ws.zOrder != i
                    || !geom.dst.equals(ws.lastDst)
                    || !geom.src.equals(ws.lastSrc);

            if (TRACE) {
                Log.d(TAG, String.format(
                        "[%d] id=%d cls=%s root=%d,%d content=%dx%d dst=%s src=%s update=%b ok=%b",
                        i, contentId, debugName, rw.rootX, rw.rootY,
                        rw.content.width, rw.content.height,
                        geom.dst, geom.src, needsUpdate, geometryOk));
            }

            if (geometryOk && needsUpdate) {
                ws.visible = shouldShow;
                ws.zOrder = i;
                ws.lastDst.set(geom.dst);
                ws.lastSrc.set(geom.src);

                nativeUpdateWindow(contentId, shouldShow, i,
                        geom.src.left, geom.src.top, geom.src.right, geom.src.bottom,
                        geom.dst.left, geom.dst.top, geom.dst.right, geom.dst.bottom);
            }
        }

        for (Map.Entry<Integer, WindowSurface> entry : windowSurfaces.entrySet()) {
            if (visibleIds.contains(entry.getKey())) continue;
            WindowSurface ws = entry.getValue();
            if (ws.visible) {
                ws.visible = false;
                nativeUpdateWindow(entry.getKey(), false, ws.zOrder, 0, 0, 0, 0, 0, 0, 0, 0);
            }
        }

        nativeApplyTransaction();
    }

    /** Remembers the largest real, presented window on the primary monitor. */
    private void notePrimaryWindow() {
        int x = 0, y = 0, w = 0, h = 0;
        String className = "";
        long bestArea = -1L;
        final int guestW = guestWidth();
        final int guestH = guestHeight();

        for (int i = 0; i < renderListSize; i++) {
            RenderableWindow rw = renderList.get(i);
            if (rw.content == null || rw.isDesktopWindow) continue;
            if (rw.rootX >= guestW || rw.rootY >= guestH
                    || rw.rootX + rw.content.width <= 0
                    || rw.rootY + rw.content.height <= 0) continue;

            WindowSurface ws = windowSurfaces.get(rw.window.id);
            if (ws == null || !ws.hasBuffer) continue;

            long area = (long) rw.content.width * rw.content.height;
            // A tie goes to the topmost window, matching render-list z-order.
            if (area >= bestArea) {
                bestArea = area;
                x = rw.rootX;
                y = rw.rootY;
                w = rw.content.width;
                h = rw.content.height;
                className = rw.window.getClassName();
                if (className == null) className = "";
            }
        }

        boolean changed = x != primaryWinX || y != primaryWinY
                || w != primaryWinW || h != primaryWinH
                || !primaryWinClassName.equals(className);
        primaryWinX = x;
        primaryWinY = y;
        primaryWinW = w;
        primaryWinH = h;
        primaryWinClassName = className;
        if (changed && fitGuestWindow) {
            Log.i(TAG, "Guest window source class=" + className + " size=" + w + "x" + h
                    + " at " + x + "," + y + " -> surface="
                    + surfaceWidth + "x" + surfaceHeight
                    + " fit=" + fittingPrimaryWindow());
        }
    }

    private boolean fittingPrimaryWindow() {
        return WindowFitPolicy.shouldFit(fitGuestWindow, primaryWinW, primaryWinH,
                guestWidth(), guestHeight(), primaryWinClassName);
    }

    /**
     * Returns the drawable game client inside Wine's decorated top-level
     * window. DXHR's 1280x720 client arrives as a 1286x755 surface: three frame
     * pixels on each side/bottom and a 32-pixel title-bar inset. Scanning that
     * entire surface is what exposed the window controls and changed 16:9 into
     * a taller aspect ratio.
     *
     * The inset is only accepted when removing Wine's standard thin frame
     * produces the panel's aspect ratio and leaves a plausible title bar. An
     * undecorated or differently shaped game therefore remains untouched.
     */
    private void computePrimaryClientRect(Rect out) {
        out.set(0, 0, primaryWinW, primaryWinH);
        if (primaryWinW <= 0 || primaryWinH <= 0
                || surfaceWidth <= 0 || surfaceHeight <= 0) return;

        final int frame = 3;
        final int clientW = primaryWinW - frame * 2;
        if (clientW <= 0) return;
        final float panelAspect = (float) surfaceWidth / surfaceHeight;
        final int clientH = Math.round(clientW / panelAspect);
        final int topInset = primaryWinH - clientH - frame;
        final float clientAspect = clientH > 0 ? (float) clientW / clientH : 0f;
        final boolean aspectMatches = Math.abs(clientAspect - panelAspect) < 0.01f;
        final boolean decorated = topInset >= 20 && topInset <= 64
                && clientH > 0 && clientH < primaryWinH;
        if (aspectMatches && decorated) {
            out.set(frame, topInset, primaryWinW - frame, primaryWinH - frame);
        }
    }

    private WindowSurface getOrCreateWindowSurface(int contentId, int w, int h, String debugName) {
        WindowSurface ws = windowSurfaces.get(contentId);
        if (ws != null && (ws.width != w || ws.height != h)) {
            windowSurfaces.remove(contentId);
            ws = null;
        }
        if (ws == null) {
            try {
                ws = new WindowSurface();
                ws.width = w;
                ws.height = h;
                nativeRegisterWindowSC(contentId, debugName);
                windowSurfaces.put(contentId, ws);
            } catch (Exception e) {
                Log.e(TAG, "Could not create a surface control for window " + contentId, e);
                return null;
            }
        }
        return ws;
    }

    private Window findDesktopWindow() {
        if (desktopWindow != null && desktopWindow.attributes.isMapped()) return desktopWindow;

        desktopWindow = null;
        for (Window child : xServer.windowManager.rootWindow.getChildren()) {
            if (!child.attributes.isOverrideRedirect() && "explorer.exe".equals(child.getClassName())) {
                desktopWindow = child;
                break;
            }
        }
        return desktopWindow;
    }

    private boolean isDesktopChild(Window window) {
        Window desktop = findDesktopWindow();
        if (desktop == null) return false;
        for (Window parent = window.getParent(); parent != null; parent = parent.getParent()) {
            if (parent == desktop) return true;
        }
        return false;
    }

    private boolean isUnviewable(Window window) {
        if (unviewableWMClasses == null) return false;
        String wc = window.getClassName();
        if (wc == null) return false;
        for (String cls : unviewableWMClasses) {
            if (wc.contains(cls)) return true;
        }
        return false;
    }

    // --- Geometry ------------------------------------------------------------

    /**
     * Where a window's pixels come from and where they land, in screen space.
     *
     * The desktop window is letterboxed into the view and its rectangle is
     * remembered, because its children are positioned in *its* coordinates and
     * have to be scaled by the same factor to stay where the guest put them.
     */
    private boolean computeWindowRect(int rootX, int rootY, int w, int h,
                                      boolean isDesktopWindow, boolean isDesktopChild,
                                      WindowGeometry out) {
        out.src.set(0, 0, w, h);

        if (fittingPrimaryWindow() && !isDesktopWindow
                && rootX < guestWidth() && rootY < guestHeight()) {
            /*
             * Map every top-screen game/overlay window through one uniform
             * client-area transform. Independent X/Y scales made a decorated
             * 1286x755 Wine window fill 1920x1080, visibly stretching the game
             * and preserving its title bar. Crop that frame on the owning
             * window and use its 16:9 client as the coordinate origin instead.
             */
            Rect client = new Rect();
            computePrimaryClientRect(client);
            Rect fitted = new Rect();
            computeLetterboxRect(client.width(), client.height(),
                    surfaceWidth, surfaceHeight, fitted);

            boolean primary = rootX == primaryWinX && rootY == primaryWinY
                    && w == primaryWinW && h == primaryWinH;
            if (primary) {
                out.src.set(client);
                out.dst.set(fitted);
            } else {
                float scale = (float) fitted.width() / client.width();
                int dstL = fitted.left + Math.round(
                        (rootX - primaryWinX - client.left) * scale);
                int dstT = fitted.top + Math.round(
                        (rootY - primaryWinY - client.top) * scale);
                out.dst.set(dstL, dstT,
                        dstL + Math.round(w * scale),
                        dstT + Math.round(h * scale));
            }
        } else if (isDesktopWindow && rootX == 0 && rootY == 0) {
            computeLetterboxRect(w, h, surfaceWidth, surfaceHeight, out.dst);
            if (cachedDesktopDst == null) cachedDesktopDst = new Rect();
            cachedDesktopDst.set(out.dst);
            cachedDesktopSrcW = w;
            cachedDesktopSrcH = h;
        } else if (isDesktopChild && cachedDesktopDst != null
                && cachedDesktopSrcW > 0 && cachedDesktopSrcH > 0) {
            float scaleX = (float) cachedDesktopDst.width() / cachedDesktopSrcW;
            float scaleY = (float) cachedDesktopDst.height() / cachedDesktopSrcH;
            int dstL = cachedDesktopDst.left + (int) (rootX * scaleX);
            int dstT = cachedDesktopDst.top + (int) (rootY * scaleY);
            out.dst.set(dstL, dstT, dstL + (int) (w * scaleX), dstT + (int) (h * scaleY));
        } else {
            /*
             * Everything else is placed in guest screen coordinates.
             *
             * This is the path the game itself takes, and it is not a rare one:
             * the desktop shell is on the unviewable list, so it never reaches
             * the render list, `cachedDesktopDst` is never filled, and its
             * children arrive here. Upstream left this case with no destination
             * at all, which `adjustRectLT` then rejects — the surface control
             * exists, receives buffers, and is never made visible. A game that
             * is running and cannot be seen is what "launched into the
             * background" is.
             *
             * The guest screen maps onto the view through the same letterbox
             * the transform already describes, so a window is placed by scaling
             * its root position and size by that ratio. A window covering the
             * guest screen therefore fills the view, which is the common case.
             */
            int guestW = guestWidth();
            int guestH = guestHeight();
            if (guestW <= 0 || guestH <= 0 || viewTransformation.viewWidth <= 0) {
                out.dst.set(0, 0, 0, 0);
            } else {
                float scaleX = (float) viewTransformation.viewWidth / guestW;
                float scaleY = (float) viewTransformation.viewHeight / guestH;
                int dstL = viewTransformation.viewOffsetX + (int) (rootX * scaleX);
                int dstT = viewTransformation.viewOffsetY + (int) (rootY * scaleY);
                out.dst.set(dstL, dstT, dstL + (int) (w * scaleX), dstT + (int) (h * scaleY));
            }
        }

        return adjustRectLT(out.src, out.dst);
    }

    private void computeLetterboxRect(int srcW, int srcH, int dstW, int dstH, Rect outRect) {
        if (srcH <= 0 || dstH <= 0) {
            outRect.set(0, 0, 0, 0);
            return;
        }
        float srcAspect = (float) srcW / srcH;
        float dstAspect = (float) dstW / dstH;

        int scaledW, scaledH;
        if (srcAspect > dstAspect) {
            scaledW = dstW;
            scaledH = (int) (dstW / srcAspect);
        } else {
            scaledH = dstH;
            scaledW = (int) (dstH * srcAspect);
        }
        int left = (dstW - scaledW) / 2;
        int top = (dstH - scaledH) / 2;
        outRect.set(left, top, left + scaledW, top + scaledH);
    }

    /**
     * Clips a destination that starts off-screen, taking the source with it.
     *
     * A transaction will not accept a negative destination, and simply clamping
     * it would stretch the window instead of cropping it — so the same
     * proportion is taken off the source rectangle.
     */
    private boolean adjustRectLT(Rect src, Rect dst) {
        final int originalDstW = dst.width();
        final int originalDstH = dst.height();
        if (originalDstW <= 0 || originalDstH <= 0) return false;

        if (dst.left < 0) {
            int clip = -dst.left;
            src.left += (int) (((long) clip * src.width()) / originalDstW);
            dst.left = 0;
        }
        if (dst.top < 0) {
            int clip = -dst.top;
            src.top += (int) (((long) clip * src.height()) / originalDstH);
            dst.top = 0;
        }

        return src.right > src.left && src.bottom > src.top
                && dst.right > dst.left && dst.bottom > dst.top;
    }

    // --- Frames --------------------------------------------------------------

    /**
     * Records that a window has presented, and reveals it the first time.
     *
     * The scene is only recomputed on that first frame — after that the window
     * is already visible and re-pushing the whole render list on every frame
     * would undo the point of this renderer.
     */
    private void noteBufferPushed(int windowId) {
        WindowSurface ws = windowSurfaces.get(windowId);
        if (ws == null) {
            if (TRACE) Log.d(TAG, "buffer for unregistered window " + windowId);
            return;
        }
        noteProgramWindowPresented(windowId);
        if (ws.hasBuffer) return;
        if (TRACE) Log.d(TAG, "first buffer for window " + windowId + " -> visible");
        ws.hasBuffer = true;
        updateScene();
    }

    /**
     * The CPU path: the guest wrote into its mapped buffer, so a copy into the
     * swapchain is needed before SurfaceFlinger can be given it.
     */
    private void pushCpuImageToNative(int windowId, Drawable drawable) {
        if (drawable == null) return;

        Window window = xServer.windowManager.getWindow(windowId);
        if (window != null && window.attributes.isMapped()
                && submitCpuImageToSecondScreen(window, drawable)) return;
        if (!windowSurfaces.containsKey(windowId)) return;
        syncCursorVisibility();

        synchronized (drawable.renderLock) {
            if (!(drawable.getTexture() instanceof AHBImage)) return;
            AHBImage image = (AHBImage) drawable.getTexture();

            image.prepareScanoutSources();
            long ahbPtr = image.getScanoutHardwareBufferPtr();
            if (ahbPtr == 0) return;

            int acquireFence = image.consumeAcquireFence();
            boolean accepted = nativeSetWindowBuffer(windowId, ahbPtr, acquireFence, windowId,
                    consumePresentSerial(),
                    image, image.getLastUsedSlot(), sfCompatMode, maximumConvertedBuffers);
            if (!accepted) return;
            lastFrameZeroCopy = false;
            noteBufferPushed(windowId);

            /*
             * The HUD updates every other frame on this path.
             *
             * Its own text is drawn through the same X server this renderer is
             * presenting, so updating it once per presented frame makes it a
             * participant in the frame loop it is measuring.
             */
            if (hudRef != null && skipFpsCount.get() >= 1) {
                hudRef.update();
                skipFpsCount.set(0);
            } else {
                skipFpsCount.incrementAndGet();
            }
        }
    }

    /**
     * The GPU path: the guest rendered into the hardware buffer itself, so it
     * can be handed over untouched.
     */
    private boolean pushGpuImageToNative(int windowId, Drawable drawable, int xSerial) {
        if (drawable == null) return false;
        syncCursorVisibility();

        synchronized (drawable.renderLock) {
            if (!(drawable.getTexture() instanceof AHBImage)) {
                if (TRACE) Log.d(TAG, "gpu push for " + windowId + ": texture is "
                        + (drawable.getTexture() == null ? "null"
                           : drawable.getTexture().getClass().getSimpleName()));
                return false;
            }
            AHBImage image = (AHBImage) drawable.getTexture();

            long ahbPtr = image.getHardwareBufferPtr();
            if (ahbPtr == 0) {
                if (TRACE) Log.d(TAG, "gpu push for " + windowId + ": no hardware buffer");
                return false;
            }

            boolean accepted = nativeSetWindowBuffer(windowId, ahbPtr, -1, windowId,
                    xSerial, null, -1, sfCompatMode, maximumConvertedBuffers);
            if (!accepted) return false;
            // GPU-backed means zero-copy only when the container opted out of
            // the compatibility conversion. The old unconditional true made
            // the stats page claim an A/B win even while every frame was copied.
            lastFrameZeroCopy = !sfCompatMode;
            noteBufferPushed(windowId);
            if (hudRef != null) hudRef.update();
            return true;
        }
    }

    private void syncCursorVisibility() {
        Window pw = xServer.inputDeviceManager.getPointWindow();
        Cursor cursor = pw != null ? pw.attributes.getCursor() : null;
        if (cursor != null && gameCursorVisible != cursor.isVisible()) {
            gameCursorVisible = cursor.isVisible();
            nativeScanoutSetCursorVisibility(containerCursorVisible && gameCursorVisible);
        }
    }

    private void sendCursorToNative(Cursor cursor) {
        if (!containerCursorVisible) return;
        Drawable cd = cursor != null ? cursor.cursorImage : rootCursorDrawable;
        if (cursor != null && !cursor.isVisible()) return;
        if (cd == null || cd.getBuffer() == null) return;

        synchronized (cd.renderLock) {
            ByteBuffer buf = cd.getBuffer();
            nativeScanoutSetCursorImage(buf, cd.width, cd.height,
                    (short) (buf.capacity() / (cd.height * 4)));
        }
    }

    // --- Window manager callbacks -------------------------------------------

    @Override
    public void onMapWindow(Window window) {
        if (isUnviewable(window)) return;

        Drawable content = window.getContent();
        if (content != null && content.width > 0 && content.height > 0) {
            if (isSecondScreenOnlyWindow(window)) {
                // The lower output owns its own CPU scanout ring. Do not create
                // or register the primary renderer's per-window scanout sources
                // for a window that can never enter the primary scene.
                content.setOnDrawListener(() -> pushCpuImageToNative(window.id, content));
            } else {
                if (!(content.getTexture() instanceof AHBImage)) {
                    AHBImage image = new AHBImage(content.width, content.height);
                    if (image.getHardwareBufferPtr() != 0) {
                        content.setTexture(image);
                    } else {
                        image.destroy();
                    }
                }
                if (content.getTexture() instanceof AHBImage) {
                    // Registered before the first frame rather than during it: the
                    // first draw would otherwise import three buffers inline.
                    ((AHBImage) content.getTexture()).prepareScanoutSources();
                    content.setOnDrawListener(() -> pushCpuImageToNative(window.id, content));
                }
            }
        }
        updateScene();
    }

    @Override
    public void onUnmapWindow(Window window) {
        noteProgramWindowRemoved(window.id);
        SecondScreenOutput.releaseGuestWindow(window.id);
        Drawable content = window.getContent();
        if (content != null) content.setOnDrawListener(null);
        windowSurfaces.remove(window.id);
        nativeUnregisterWindowSC(window.id);
        updateScene();
    }

    @Override
    public void onDestroyWindow(Window window) {
        noteProgramWindowRemoved(window.id);
        SecondScreenOutput.releaseGuestWindow(window.id);
        if (window == desktopWindow) desktopWindow = null;
        Drawable content = window.getContent();
        if (content != null) content.setOnDrawListener(null);
        windowSurfaces.remove(window.id);
        nativeUnregisterWindowSC(window.id);
        updateScene();
    }

    @Override
    public void onChangeWindowZOrder(Window window) {
        updateScene();
    }

    @Override
    public void onUpdateWindowContent(Window window) {
        Drawable drawable = window.getContent();
        if (drawable == null || !window.attributes.isMapped()) return;
        if (isUnviewable(window)) return;
        if (claimedBySecondScreen(window, drawable)) return;
        pushGpuImageToNative(window.id, drawable, consumePresentSerial());
    }

    @Override
    public void onUpdateWindowGeometry(Window window, boolean resized) {
        if (isUnviewable(window)) return;

        if (resized) {
            windowSurfaces.remove(window.id);
            nativeUnregisterWindowSC(window.id);

            Drawable newContent = window.getContent();
            if (newContent != null && newContent.width > 0 && newContent.height > 0) {
                if (isSecondScreenOnlyWindow(window)) {
                    newContent.setOnDrawListener(
                            () -> pushCpuImageToNative(window.id, newContent));
                } else if (newContent.getTexture() instanceof AHBImage) {
                    ((AHBImage) newContent.getTexture()).prepareScanoutSources();
                    newContent.setOnDrawListener(
                            () -> pushCpuImageToNative(window.id, newContent));
                }
            }
        }
        updateScene();
    }

    @Override
    public void onUpdateWindowAttributes(Window window, Bitmask mask) {
        if (mask.isSet(WindowAttributes.FLAG_CURSOR)) {
            Window pw = xServer.inputDeviceManager.getPointWindow();
            if (pw == window) {
                lastCursor = window.attributes.getCursor();
                sendCursorToNative(lastCursor);
            }
        }
    }

    @Override
    public void onModifyWindowProperty(Window window, Property property) {
        if (!isPanelIdentityProperty(property)
                || !SecondScreenOutput.isEarlyClaimPanelWindow(window)) return;

        /*
         * MapWindow commonly arrives before Wine publishes WM_CLASS/title.
         * Reclassify the already-mapped tree as soon as that identity becomes
         * authoritative; otherwise a one-shot GDI panel keeps the primary
         * scanout listener it received while it was still anonymous and never
         * reaches SecondScreenOutput.
         */
        reclassifyNativePanelTree(window);
        updateScene();
    }

    private void reclassifyNativePanelTree(Window window) {
        if (window == null) return;
        Drawable content = window.getContent();
        if (window.attributes.isMapped() && content != null
                && content.width > 0 && content.height > 0
                && SecondScreenOutput.isEarlyClaimPanelWindow(window)) {
            windowSurfaces.remove(window.id);
            nativeUnregisterWindowSC(window.id);
            content.setOnDrawListener(() -> pushCpuImageToNative(window.id, content));
            onUpdateWindowContent(window);
        }
        for (Window child : window.getChildren()) reclassifyNativePanelTree(child);
    }

    private static boolean isPanelIdentityProperty(Property property) {
        if (property == null) return false;
        String name = property.nameAsString();
        return "WM_CLASS".equals(name) || "WM_NAME".equals(name)
                || "_NET_WM_NAME".equals(name);
    }

    @Override
    public void onPointerMove(short x, short y) {
        Window pw = xServer.inputDeviceManager.getPointWindow();
        Cursor cursor = pw != null ? pw.attributes.getCursor() : null;
        if (cursor == null) return;

        if (cursor != lastCursor) {
            lastCursor = cursor;
            sendCursorToNative(cursor);
        }
        nativeScanoutSetCursorPos(x, y, (short) cursor.hotSpotX, (short) cursor.hotSpotY,
                containerCursorVisible && gameCursorVisible);
    }

    // --- XServerRenderer -----------------------------------------------------

    @Override
    public XServerView getRendererView() { return xServerView; }

    /**
     * Always false.
     *
     * There is no fullscreen *mode* here to be in or out of. Every window is
     * placed by its destination rectangle on the transaction, and a window that
     * covers the screen does so because that is where it was put. The one
     * caller — {@code TouchpadView} — uses this to decide whether pointer
     * coordinates need the letterbox offset removed, and on this path they
     * always do.
     */
    @Override
    public boolean isFullscreen() { return false; }

    @Override
    public void setFitGuestWindow(boolean enabled) {
        if (fitGuestWindow == enabled) return;
        fitGuestWindow = enabled;
        if (surfaceInitialized) updateScene();
    }

    @Override
    public boolean isFitGuestWindow() { return fitGuestWindow; }

    @Override
    public void setCursorVisible(boolean visible) {
        containerCursorVisible = visible;
        if (visible) {
            sendCursorToNative(lastCursor);
        } else {
            nativeScanoutSetCursorVisibility(false);
        }
    }

    @Override
    public boolean isCursorVisible() { return containerCursorVisible; }

    @Override
    public void setUnviewableWMClasses(String... classes) { this.unviewableWMClasses = classes; }

    @Override
    public void armProgramWindowListener(XServerRenderer.ProgramWindowListener listener) {
        synchronized (programWindowLock) {
            programWindowToIgnore = lastPresentedWindowId;
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

    /** Called only after a real buffer has been handed to SurfaceFlinger. */
    private void noteProgramWindowPresented(int windowId) {
        lastPresentedWindowId = windowId;
        XServerRenderer.ProgramWindowListener listener = null;
        synchronized (programWindowLock) {
            if (programWindowListener != null && windowId != programWindowToIgnore) {
                boolean resumedFromEmpty = presentedProgramWindows.isEmpty();
                trackingProgramWindows = true;
                presentedProgramWindows.add(windowId);
                // Invalidates a close timer posted for an earlier empty gap.
                if (resumedFromEmpty) programWindowGeneration++;
                if (!programWindowReportedVisible) {
                    programWindowReportedVisible = true;
                    listener = programWindowListener;
                }
            }
        }
        if (listener != null) {
            Log.d(TAG, "Foreground program window presented: " + windowId);
            listener.onProgramWindowPresented(windowId);
        }
    }

    private void noteProgramWindowRemoved(int windowId) {
        final int emptyGeneration;
        synchronized (programWindowLock) {
            if (!trackingProgramWindows || !presentedProgramWindows.remove(windowId)
                    || !presentedProgramWindows.isEmpty()) return;
            emptyGeneration = programWindowGeneration;
        }

        // Splash windows often disappear just before their main window draws.
        xServerView.postDelayed(() -> {
            XServerRenderer.ProgramClosedListener listener = null;
            int reopenGeneration = -1;
            synchronized (programWindowLock) {
                if (trackingProgramWindows && programWindowReportedVisible &&
                    programWindowGeneration == emptyGeneration &&
                    presentedProgramWindows.isEmpty()) {
                    programWindowReportedVisible = false;
                    reopenGeneration = ++programWindowGeneration;
                    listener = programClosedListener;
                }
            }
            if (listener != null) {
                Log.d(TAG, "Foreground program windows absent; waiting for handoff");
                listener.onProgramClosed();
            }

            if (reopenGeneration != -1) {
                final int expectedGeneration = reopenGeneration;
                xServerView.postDelayed(() -> {
                    synchronized (programWindowLock) {
                        if (trackingProgramWindows && !programWindowReportedVisible &&
                            programWindowGeneration == expectedGeneration &&
                            presentedProgramWindows.isEmpty()) {
                            Log.d(TAG, "Foreground program handoff window expired");
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

    @Override
    public boolean onUpdateWindowContentDirect(Window window, Drawable pixmap, short xOff, short yOff) {
        /*
         * The Present extension's fast path, which here is the ordinary one.
         *
         * The offsets are dropped rather than applied. The Vulkan compositor
         * blits the pixmap into the window at (xOff, yOff) and so has to honour
         * them; a surface control is given a whole buffer for a whole window,
         * and every caller that reaches this with a non-zero offset is
         * presenting a full-window pixmap where the offsets are zero anyway.
         */
        if (pixmap == null || window == null || !window.attributes.isMapped()) return false;
        if (claimedBySecondScreen(window, pixmap)) return true;
        return pushGpuImageToNative(window.id, pixmap, consumePresentSerial());
    }

    /**
     * Sends a second-monitor window's buffer to the bottom panel instead of the
     * scene.
     *
     * This renderer would otherwise give it a surface control of its own under
     * the top panel's surface, at a destination below the visible area, and
     * {@code adjustRectLT} would reject it every frame — a window presenting
     * forever into nothing. Handing the same buffer to the second panel's layer
     * costs no more than the hand-off it was going to get anyway; the two
     * layers differ only in which surface they were created from.
     */
    /**
     * Copies one CPU/GDI window into the lower panel's own three-buffer ring.
     *
     * A true return means the window belongs to the lower monitor, even if the
     * panel is momentarily unable to accept the copy. It must never fall back
     * into the primary scene just because one lower-screen submission raced a
     * Surface recreation.
     */
    private boolean submitCpuImageToSecondScreen(Window window, Drawable source) {
        if (window == null || source == null
                || !isSecondScreenOnlyWindow(window)) {
            return false;
        }

        SecondScreenOutput.noteGuestWindow(window);
        if (SecondScreenOutput.isStardewValleyPanelActive()) {
            // The GDI-backed Stardew panel takes this CPU submission path.
            setFitGuestWindow(true);
        }
        synchronized (source.renderLock) {
            java.nio.ByteBuffer pixels = source.getBuffer();
            int stride = source.height > 0 && pixels != null
                    ? pixels.capacity() / (source.height * 4) : 0;
            if (pixels != null && stride >= source.width) {
                SecondScreenOutput.submitPixels(
                        pixels, source.width, source.height, stride);
            }
        }
        return true;
    }

    private boolean claimedBySecondScreen(Window window, Drawable source) {
        if (!isSecondScreenOnlyWindow(window)) return false;

        if (source.getTexture() instanceof AHBImage
                && ((AHBImage) source.getTexture()).getVirtualData() != null) {
            submitCpuImageToSecondScreen(window, source);
            return true;
        }
        Window deusEx = xServer.windowManager.findWindowByProcessName("DXHRDC.exe");
        if (deusEx != null) {
            // Wine does not consistently publish WM_CLASS/WM_NAME for a
            // WS_EX_TOOLWINDOW. Resolve the owning process from its normal game
            // window so gameplay tab taps do not fall back to the grabbed
            // auxiliary pointer path.
            SecondScreenOutput.noteGuestWindow(
                    window.id, "dxhrdc.exe", deusEx.getName());
            // The game commonly starts as a small Wine window even though the
            // guest desktop is 1280x720. A dedicated lower HUD is conclusive
            // evidence this is the DXHR dual-screen session, so make the game
            // client fill the top panel without requiring a hidden setting.
            setFitGuestWindow(true);
        } else {
            SecondScreenOutput.noteGuestWindow(window);
        }
        if (SecondScreenOutput.isStardewValleyPanelActive()) {
            setFitGuestWindow(true);
        }

        synchronized (source.renderLock) {
            if (!(source.getTexture() instanceof AHBImage)) return false;
            AHBImage image = (AHBImage) source.getTexture();
            long ahbPtr = image.getHardwareBufferPtr();
            if (ahbPtr != 0) {
                SecondScreenOutput.submit(ahbPtr, -1, source.width, source.height);
            }
            // Classification, not transient output attachment, decides
            // ownership. Dropping one lower frame is safer than allowing
            // Present to retry it through the primary SurfaceControl scene.
            return true;
        }
    }

    @Override
    public int getPresentedFpsLimit() {
        return fpsLimit > 0 ? fpsLimit * Math.max(1, frameGenMultiplier) : 0;
    }

    @Override
    public void setFrameRating(Object fr) {
        if (fr instanceof FrameRating) hudRef = (FrameRating) fr;
    }

    @Override
    public int getSurfaceWidth() { return surfaceWidth; }

    @Override
    public int getSurfaceHeight() { return surfaceHeight; }

    public void setFpsLimit(int limit) {
        this.fpsLimit = limit;
        declareFrameRate();
    }

    public void setFrameGenMultiplier(int multiplier) {
        this.frameGenMultiplier = Math.max(1, multiplier);
        declareFrameRate();
    }

    public void setAdaptiveRefreshRate(boolean enabled) {
        adaptiveRefreshRate = enabled;
        declareFrameRate();
    }

    private void declareFrameRate() {
        Surface surface = currentSurface;
        if (surface == null || android.os.Build.VERSION.SDK_INT < 30) return;
        try {
            int cap = adaptiveRefreshRate ? getPresentedFpsLimit() : 0;
            surface.setFrameRate(
                    cap > 0 ? (float) cap : 0f,
                    Surface.FRAME_RATE_COMPATIBILITY_DEFAULT);
        } catch (Throwable failure) {
            Log.w(TAG, "Could not declare scanout frame rate", failure);
        }
    }

    // --- Present serial ------------------------------------------------------

    private int pendingPresentSerial = 0;

    public void setPendingPresentSerial(int serial) { pendingPresentSerial = serial; }

    private int consumePresentSerial() {
        int s = pendingPresentSerial;
        pendingPresentSerial = 0;
        return s;
    }

    /** Told when a frame reached the display, from the transaction callback. */
    public interface ScanoutFrameListener {
        void onFrameLatched(int windowId, int serial, long presentTimeNs);
    }

    private volatile ScanoutFrameListener scanoutFrameListener = null;

    /**
     * Whether the frame just handed over went across without a copy.
     *
     * True on the GPU path, where the guest rendered into the hardware buffer
     * and it is passed straight to SurfaceFlinger. False on the CPU path, where
     * the guest wrote into its own mapping and the frame had to be copied into
     * a swapchain slot first. The HUD says which, exactly as it does for the
     * Vulkan renderer.
     */
    private volatile boolean lastFrameZeroCopy = false;

    public boolean isLastFrameZeroCopy() { return lastFrameZeroCopy; }

    public void setScanoutFrameListener(ScanoutFrameListener listener) {
        this.scanoutFrameListener = listener;
    }

    /**
     * Called from native code on SurfaceFlinger's completion callback.
     *
     * The window id and the serial arrive packed into one long because the
     * callback crosses into the JVM on a binder thread and one primitive is
     * cheaper to pass than two objects.
     */
    @Keep
    public void onScanoutFrameComplete(long packed) {
        int windowId = (int) ((packed >>> 32) & 0xFFFFFFFFL);
        int serial = (int) (packed & 0xFFFFFFFFL);

        final long now = android.os.SystemClock.uptimeMillis();
        final int current = fpsWindowId;
        if (windowId != current) {
            if (current != -1 && (now - lastFpsFrameAtMs) < FPS_HANDOFF_MS) return;
            fpsWindowId = windowId;
        }
        lastFpsFrameAtMs = now;

        ScanoutFrameListener listener = scanoutFrameListener;
        if (listener != null) listener.onFrameLatched(windowId, serial, System.nanoTime());
    }

    private static final long FPS_HANDOFF_MS = 250L;

    // --- Native --------------------------------------------------------------

    private native boolean nativeInit(Surface surface, int screenWidth, int screenHeight);
    private native void nativeDestroy();
    private native void nativeInitScanout();
    private native void nativeDestroyScanout();
    private native boolean nativeReattachSurface(Surface surface);
    private native void nativeSetSfCallbackTarget(Object rendererRef);

    private native void nativeRegisterWindowSC(long contentId, String debugName);
    private native void nativeUnregisterWindowSC(long contentId);

    private native void nativeBeginTransaction();
    private native void nativeApplyTransaction();
    private native void nativeUpdateWindow(long contentId, boolean visible, int zOrder,
                                           int srcL, int srcT, int srcR, int srcB,
                                           int dstL, int dstT, int dstR, int dstB);

    private native boolean nativeSetWindowBuffer(long contentId, long ahbPtr, int fenceFd,
                                              long windowId, long serial, AHBImage ahbImage,
                                              int slot, boolean sfCompatMode,
                                              int maximumConvertedBuffers);

    static native boolean nativePrepareCpuSourceBuffers(long ahb0, long ahb1, long ahb2);
    static native void nativeReleaseCpuSourceBuffers(long ahb0, long ahb1, long ahb2);

    private native void nativeScanoutSetCursorVisibility(boolean visible);
    private native void nativeScanoutSetCursorImage(ByteBuffer pixels, short w, short h, short stride);
    private native void nativeScanoutSetCursorPos(short x, short y, short hotX, short hotY,
                                                  boolean cursorVisible);
    private native void nativeScanoutSetDst(int x, int y, int w, int h);
}
