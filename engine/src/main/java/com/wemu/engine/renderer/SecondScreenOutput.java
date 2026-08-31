package com.wemu.engine.renderer;

import android.util.Log;
import android.view.Surface;

import com.wemu.engine.core.ProcessHelper;
import com.wemu.engine.xserver.Pointer;
import com.wemu.engine.xserver.ScreenLayout;
import com.wemu.engine.xserver.XServer;

import java.nio.ByteBuffer;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * The Thor's bottom panel, as somewhere a guest window can be put.
 *
 * <h2>Why this is not part of a renderer</h2>
 *
 * Neither renderer composites anything here. A window the guest places on the
 * second monitor is full-screen on that monitor and is the only thing on it, so
 * there is no scene to build — its {@code AHardwareBuffer} goes straight to
 * SurfaceFlinger through an {@code ASurfaceControl}, which is what the display
 * controller was going to do with it in any case. That costs no GPU time, which
 * is the whole reason for doing it this way: the alternative for a container on
 * the Vulkan compositor is a second swapchain, a second render pass and a
 * second present every frame, on the part of the device that is already the
 * limit.
 *
 * Because of that it works the same whichever renderer the container uses. The
 * compositor keeps the top panel and never learns this exists.
 *
 * <h2>State is static, and that is deliberate</h2>
 *
 * There is one bottom panel and one session at a time. Both renderers reach
 * this from their own submission paths, and a per-renderer instance would mean
 * the panel belonged to whichever of them happened to be constructed — the
 * native side already keeps a single control for the same reason.
 */
public final class SecondScreenOutput {

    private static final String TAG = "SecondScreenOutput";
    /** Launcher-specific port; full WEMU deliberately retains 7788. */
    private static final int NATIVE_PANEL_INPUT_PORT = 17788;
    private static final ExecutorService NATIVE_PANEL_INPUT =
            Executors.newSingleThreadExecutor(runnable -> {
                Thread thread = new Thread(runnable, "NativePanelControllerInput");
                thread.setDaemon(true);
                return thread;
            });

    static { System.loadLibrary("asurface_renderer"); }

    /**
     * Where on the root window the second monitor is, or null when the
     * container has only one.
     *
     * Read on every frame by the renderers' submission paths, and written from
     * the UI thread when the panel appears or goes, hence volatile.
     */
    private static volatile ScreenLayout.Monitor monitor = null;
    private static volatile XServer xServer = null;
    private static volatile String guestWindowIdentity = "";
    private static volatile String guestWindowClassName = "";
    private static volatile String guestWindowTitle = "";
    private static volatile int guestWindowId = -1;

    private static volatile boolean attached = false;
    private static volatile boolean showing = false;

    /**
     * Told when the panel becomes usable, and when a guest window takes it.
     *
     * Both facts are pushed rather than read, and that is the whole reason this
     * interface exists. The console's window is composed before the session has
     * finished preparing, so a composition that *reads* whether there is a
     * second monitor reads "no" and has nothing to make it look again — which
     * is exactly how the panel came to never attach at all. The claim is pushed
     * for a different reason: it changes on the render thread the moment a
     * frame is accepted, and a poll would either miss it or run every frame.
     *
     * A listener is called once on registration with the current state, so the
     * order the session and the window come up in stops mattering.
     */
    public interface OnStateListener {
        void onSecondScreenState(boolean available, boolean claimed);
    }

    private static volatile OnStateListener stateListener = null;

    private SecondScreenOutput() {}

    public static void setOnStateListener(OnStateListener listener) {
        stateListener = listener;
        if (listener != null) notify(listener);
    }

    private static void notifyState() {
        OnStateListener listener = stateListener;
        if (listener != null) notify(listener);
    }

    private static void notify(OnStateListener listener) {
        try {
            listener.onSecondScreenState(monitor != null, showing);
        } catch (RuntimeException failure) {
            Log.w(TAG, "Second screen state listener failed", failure);
        }
    }

    /** Declares which part of the root window belongs to the second panel. */
    public static void setMonitor(ScreenLayout.Monitor value) {
        monitor = value;
        if (value == null) xServer = null;
        notifyState();
    }

    /** Associates the one active session's X server with the lower output. */
    static void setXServer(XServer value) {
        xServer = value;
    }

    /** Records the guest window which currently owns the physical lower panel. */
    public static void noteGuestWindow(String className, String title) {
        noteGuestWindow(-1, className, title);
    }

    /** Records the X window as well, so its removal can release stale output. */
    public static void noteGuestWindow(int windowId, String className, String title) {
        final String normalizedClass = className == null ? "" : className;
        final String normalizedTitle = title == null ? "" : title;
        if (windowId == guestWindowId
                && normalizedClass.equals(guestWindowClassName)
                && normalizedTitle.equals(guestWindowTitle)) return;

        synchronized (LIFECYCLE) {
            if (windowId == guestWindowId
                    && normalizedClass.equals(guestWindowClassName)
                    && normalizedTitle.equals(guestWindowTitle)) return;

            guestWindowId = windowId;
            guestWindowClassName = normalizedClass;
            guestWindowTitle = normalizedTitle;
            guestWindowIdentity = (normalizedClass + " " + normalizedTitle)
                    .toLowerCase(Locale.ROOT);
        }
    }

    private static void clearGuestWindowLocked() {
        guestWindowId = -1;
        guestWindowClassName = "";
        guestWindowTitle = "";
        guestWindowIdentity = "";
    }

    /**
     * A dedicated lower-panel identity which is safe to recognize before Wine
     * has moved the window from its transient primary-monitor coordinates.
     */
    public static boolean isSkyrimPanelWindow(String className, String title) {
        return NativePanelIdentity.isSkyrim(className, title);
    }

    private static boolean isDeusExPanel() {
        String identity = guestWindowIdentity;
        return identity.contains("dxhrsecondscreenpanel")
                || identity.contains("deus ex")
                || identity.contains("dxhrdc");
    }

    private static boolean isSkyrimPanel() {
        return isSkyrimPanelWindow(guestWindowClassName, guestWindowTitle)
                || guestWindowIdentity.contains("skyrimsecondscreenpanel")
                || guestWindowIdentity.contains("skyrim dual screen");
    }

    /** Recognizes either native Fallout Pip-Boy panel before it is repositioned. */
    public static boolean isFalloutPanelWindow(String className, String title) {
        return NativePanelIdentity.isFallout(className, title);
    }

    /** Recognizes Grim Dawn's private panel before Wine applies its final position. */
    public static boolean isGrimDawnPanelWindow(String className, String title) {
        return NativePanelIdentity.isGrimDawn(className, title);
    }

    /** Native panels which are lower-only even at transient primary coordinates. */
    public static boolean isEarlyClaimPanelWindow(String className, String title) {
        return NativePanelIdentity.isEarlyClaimPanel(className, title);
    }

    /** Whether a Fallout 3 or Fallout 4 Pip-Boy owns the lower panel. */
    public static boolean isFalloutPanelActive() {
        return showing && (isFalloutPanelWindow(guestWindowClassName, guestWindowTitle)
                || guestWindowIdentity.contains("fallout3secondscreenpanel")
                || guestWindowIdentity.contains("fallout4secondscreenpanel"));
    }

    /** Whether a native panel supporting WEMU's Select-modified commands is visible. */
    public static boolean isNativePanelControllerActive() {
        return showing && (isFalloutPanelWindow(guestWindowClassName, guestWindowTitle)
                || isGrimDawnPanelWindow(guestWindowClassName, guestWindowTitle));
    }

    /**
     * Sends one main-mode controller step to Fallout's native panel.
     *
     * The guest listener is deliberately the same ordered local protocol used
     * by touch.  Sending this asynchronously keeps a controller event off both
     * the Android UI thread and the game/X-server input path.
     */
    public static boolean sendFalloutPanelPage(boolean next) {
        if (!isFalloutPanelActive()) return false;
        return sendNativePanelPage(next);
    }

    /** Sends a scoped Select-modifier navigation action to the native panel. */
    public static boolean sendFalloutPanelNavigation(String action) {
        if (!isFalloutPanelActive()) return false;
        return sendNativePanelNavigation(action);
    }

    /** Sends one Select-modified page step to the active native panel. */
    public static boolean sendNativePanelPage(boolean next) {
        return sendNativePanelCommand(next ? "page next" : "page previous");
    }

    /** Sends one Select-modified focus action to the active native panel. */
    public static boolean sendNativePanelNavigation(String action) {
        if (action == null || !(action.equals("left") || action.equals("right")
                || action.equals("up") || action.equals("down")
                || action.equals("activate"))) return false;
        return sendNativePanelCommand("nav " + action);
    }

    private static boolean sendNativePanelCommand(String command) {
        if (!isNativePanelControllerActive()) return false;
        final byte[] bytes = command
                .getBytes(java.nio.charset.StandardCharsets.US_ASCII);
        NATIVE_PANEL_INPUT.execute(() -> {
            try (DatagramSocket socket = new DatagramSocket()) {
                InetAddress loopback = InetAddress.getByAddress(
                        new byte[] {127, 0, 0, 1});
                socket.send(new DatagramPacket(
                        bytes, bytes.length, loopback, NATIVE_PANEL_INPUT_PORT));
            } catch (Exception failure) {
                Log.w(TAG, "Could not send native panel controller input", failure);
            }
        });
        return true;
    }

    private static boolean isGrimDawnPanel() {
        return isGrimDawnPanelWindow(guestWindowClassName, guestWindowTitle);
    }

    public static ScreenLayout.Monitor getMonitor() {
        return monitor;
    }

    /**
     * Injects a normalized lower-panel gesture into the actual guest window.
     *
     * This is intentionally in addition to the native-mod gesture channel.
     * The X event makes the panel's own tab strip reliable without depending
     * on Wine loopback networking; native page content still uses the mod's
     * direct mouse bridge because Scaleform belongs to the top game window.
     */
    public static boolean injectTouch(String action, float x, float y, float value) {
        XServer server = xServer;
        ScreenLayout.Monitor second = monitor;
        if (server == null || second == null || action == null) return false;

        /*
         * DXHR keeps its Scaleform movies on the primary game HWND. Its panel
         * DLL remaps normalized lower-screen gestures to that movie directly.
         * Mirroring those gestures through X as well is actively harmful while
         * Wine owns a gameplay pointer grab: the duplicate click lands in the
         * world (usually firing the held weapon) instead of on the page. The
         * native mod handles both header tabs and page content, including the
         * player's current bindings, so this panel uses that channel alone.
         */
        if (isDeusExPanel() || isSkyrimPanel() || isGrimDawnPanel()) return true;

        float nx = Math.max(0.0f, Math.min(1.0f, x));
        float ny = Math.max(0.0f, Math.min(1.0f, y));
        int guestX = second.x + Math.min(second.width - 1, Math.round(nx * second.width));
        int guestY = second.y + Math.min(second.height - 1, Math.round(ny * second.height));
        server.injectPointerMove(guestX, guestY);

        switch (action) {
            case "tap":
                server.injectPointerButtonPress(Pointer.Button.BUTTON_LEFT);
                server.injectPointerButtonRelease(Pointer.Button.BUTTON_LEFT);
                break;
            case "down":
                server.injectPointerButtonPress(Pointer.Button.BUTTON_LEFT);
                break;
            case "up":
                server.injectPointerButtonRelease(Pointer.Button.BUTTON_LEFT);
                break;
            case "hold":
                server.injectPointerButtonPress(Pointer.Button.BUTTON_RIGHT);
                server.injectPointerButtonRelease(Pointer.Button.BUTTON_RIGHT);
                break;
            case "pinch":
                Pointer.Button wheel = value >= 1.0f
                        ? Pointer.Button.BUTTON_SCROLL_UP
                        : Pointer.Button.BUTTON_SCROLL_DOWN;
                server.injectPointerButtonPress(wheel);
                server.injectPointerButtonRelease(wheel);
                break;
            case "move":
                break;
            default:
                return false;
        }
        return true;
    }

    /** True once the panel has a surface and can be handed frames. */
    public static boolean isAttached() {
        return attached;
    }

    /**
     * Serialises attach, detach, submit and clear against each other.
     *
     * `volatile` on {@link #attached} makes a write visible to the render
     * thread; it does not make "check the flag, then call into native" a single
     * step. A detach arriving between those two enters native teardown while a
     * frame is on its way in. The native side takes its own lock and re-checks
     * the control, so the consequence today is a dropped frame rather than a
     * use-after-free — but that is the native side being careful on this side's
     * behalf, and the ordering belongs here too.
     *
     * Submit holds this while calling native. What it holds it for is one
     * SurfaceFlinger transaction against a control that is already resident, so
     * the render thread is not waiting on anything that can block.
     */
    private static final Object LIFECYCLE = new Object();

    public static boolean attach(Surface surface, int panelWidth, int panelHeight) {
        if (surface == null || !surface.isValid()) return false;
        boolean ready;
        boolean claimChanged;
        synchronized (LIFECYCLE) {
            claimChanged = showing;
            attached = nativeAttach(surface, panelWidth, panelHeight);
            showing = false;
            ready = attached;
        }
        if (ready) {
            ProcessHelper.emitDiagnostic("Second screen output ready: "
                    + panelWidth + "x" + panelHeight);
        } else {
            Log.w(TAG, "Second screen output unavailable; lower guest windows wait for its surface");
        }
        // Native attach replaces the control and therefore drops its old
        // buffer. Publish that claim transition outside the lifecycle lock.
        // notifyState reads the current volatile state, so a new frame racing
        // this notification cannot publish a stale false value.
        if (claimChanged) notifyState();
        return ready;
    }

    public static void detach() {
        boolean wasShowing;
        synchronized (LIFECYCLE) {
            wasShowing = showing;
            showing = false;
            clearGuestWindowLocked();
            if (attached) {
                attached = false;
                nativeDetach();
            }
        }
        // Outside the lock: a listener runs arbitrary caller code, and holding a
        // lock the render thread wants across that is how a frame stalls.
        if (wasShowing) notifyState();
    }

    /**
     * Whether a window at this root position belongs to the second panel.
     *
     * Decided by the window's origin rather than by overlap, because a window
     * straddling the join is not a thing to be shown on both panels — it is a
     * window that has been placed badly, and picking the monitor its top-left
     * corner is on is the same rule a real window manager applies.
     */
    public static boolean claims(int rootX, int rootY) {
        return attached && isSecondMonitorPosition(rootX, rootY);
    }

    /**
     * Whether this root coordinate belongs to the configured lower monitor,
     * independent of whether its Android Surface is attached at this instant.
     * Renderers use this to keep lower-only windows out of the primary scene
     * during activity/surface recreation.
     */
    public static boolean isSecondMonitorPosition(int rootX, int rootY) {
        ScreenLayout.Monitor second = monitor;
        if (second == null) return false;
        return rootX >= second.x && rootY >= second.y
                && rootX < second.x + second.width
                && rootY < second.y + second.height;
    }

    /** Shows one frame. The fence is handed over and must not be closed here. */
    public static boolean submit(long hardwareBufferPtr, int fenceFd, int width, int height) {
        if (hardwareBufferPtr == 0) return false;
        boolean accepted;
        boolean firstFrame = false;
        synchronized (LIFECYCLE) {
            if (!attached) return false;
            accepted = nativeSetBuffer(hardwareBufferPtr, fenceFd, width, height);
            if (accepted && !showing) {
                showing = true;
                firstFrame = true;
            }
        }
        if (firstFrame) {
            ProcessHelper.emitDiagnostic("Second screen showing a guest window at "
                    + width + "x" + height);
            notifyState();
        }
        return accepted;
    }

    /**
     * Shows an ordinary CPU-backed X11 window on the lower panel.
     *
     * Native code copies into a three-buffer AHardwareBuffer ring. This is an
     * intentional fallback for lightweight GDI interfaces; game frames remain
     * on the existing zero-copy path.
     */
    public static boolean submitPixels(ByteBuffer pixels, int width, int height, int stride) {
        if (pixels == null || !pixels.isDirect() || width <= 0 || height <= 0 ||
                stride < width) return false;
        boolean accepted;
        boolean firstFrame = false;
        synchronized (LIFECYCLE) {
            if (!attached) return false;
            accepted = nativeSetPixels(pixels, width, height, stride);
            if (accepted && !showing) {
                showing = true;
                firstFrame = true;
            }
        }
        if (firstFrame) {
            ProcessHelper.emitDiagnostic("Second screen showing a CPU guest window at "
                    + width + "x" + height);
            notifyState();
        }
        return accepted;
    }

    /**
     * Clears the panel when the window that owned it has gone.
     *
     * Without this the last frame the guest drew stays on the bottom screen for
     * as long as the layer exists, which after a game exits is a frozen HUD over
     * a console that is trying to be visible underneath it.
     */
    public static void clear() {
        synchronized (LIFECYCLE) {
            clearGuestWindowLocked();
            if (!attached || !showing) return;
            showing = false;
            nativeSetVisible(false);
        }
        ProcessHelper.emitDiagnostic("Second screen released back to the console");
        notifyState();
    }

    /** Releases the lower layer only if this is still the window that owns it. */
    public static void releaseGuestWindow(int windowId) {
        boolean released = false;
        synchronized (LIFECYCLE) {
            if (windowId < 0 || guestWindowId != windowId) return;
            clearGuestWindowLocked();
            if (attached && showing) {
                showing = false;
                nativeSetVisible(false);
                released = true;
            }
        }
        if (released) {
            ProcessHelper.emitDiagnostic("Second screen guest window closed");
            notifyState();
        }
    }

    private static native boolean nativeAttach(Surface surface, int panelWidth, int panelHeight);

    private static native void nativeDetach();

    private static native boolean nativeSetBuffer(long hardwareBufferPtr, int fenceFd,
                                                  int width, int height);

    private static native boolean nativeSetPixels(ByteBuffer pixels, int width, int height,
                                                  int stride);

    private static native void nativeSetVisible(boolean visible);
}
