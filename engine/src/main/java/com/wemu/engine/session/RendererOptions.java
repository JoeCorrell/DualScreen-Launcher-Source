package com.wemu.engine.session;

import android.content.Context;
import android.util.Log;

import com.wemu.engine.container.Container;
import com.wemu.engine.core.FrameGeneration;
import com.wemu.engine.container.Shortcut;
import com.wemu.engine.contents.AdrenotoolsManager;
import com.wemu.engine.core.AppUtils;
import com.wemu.engine.renderer.ASurfaceRenderer;
import com.wemu.engine.renderer.CompositorRenderer;
import com.wemu.engine.renderer.VulkanRenderer;
import com.wemu.engine.renderer.XServerRenderer;

import java.util.HashMap;

/**
 * The renderer settings a container carries, and how to put them on a renderer.
 *
 * These lived in `RendererOptionsDialog` and in the middle of the display
 * activity's `setupUI`, which meant the only way to start a container with the
 * driver it was configured for was to inflate the dialog that configures it.
 * Reading a setting and offering to change it are different jobs; this is the
 * first one.
 *
 * The numbers are the native renderer's, not this class's — `filterMode`,
 * `postFXMode` and the present modes are indices the C++ side switches on, so
 * they are named here rather than left as bare integers at the call site.
 */
public final class RendererOptions {

    private static final String TAG = "RendererOptions";

    private RendererOptions() {}

    /** Present without waiting for vblank, dropping frames. Lowest latency. */
    public static final int PRESENT_MODE_MAILBOX = 1;

    /** Wait for vblank. The Vulkan default and the only mode guaranteed present. */
    public static final int PRESENT_MODE_FIFO = 2;

    /** No scaling filter. */
    public static final int FILTER_NEAREST = 0;

    /** Bilinear. */
    public static final int FILTER_LINEAR = 1;

    /**
     * The first upscaler slot.
     *
     * The sidebar computed this as `spinnerPosition + 2`, which is where the two
     * plain filters above stop and the upscalers begin. Named so that adding a
     * second upscaler does not require rediscovering why there was a `+ 2`.
     */
    public static final int FILTER_UPSCALER_BASE = 2;

    /** Post-processing effects, in the order the shader switch expects. */
    public static final String[] POST_FX_MODES = {"None", "DLS", "CRT", "HDR", "Natural"};

    /**
     * Upscalers available in [FILTER_UPSCALER_BASE] order.
     *
     * Both are spatial, which is what makes them usable at all here: this
     * compositor is handed a finished image and knows nothing about how it was
     * made, so anything needing motion vectors or depth — FSR 2 and every
     * temporal upscaler — has nothing to work from.
     *
     * They are kept as a choice rather than one being picked as best because
     * they fail differently. SGSR is cheapest and very clean on text and UI
     * edges. FSR is EASU plus a real RCAS pass over the upscaled image and
     * reconstructs diagonals and fine geometry better. NIS is NVIDIA's
     * NVScaler, which sharpens inside the scaling filter rather than after it,
     * and is the most expensive of the three here — the reference shares its
     * 6x6 luma support across a tile in shared memory, and as a fragment shader
     * there is no tile, so it fetches 36 taps per pixel against EASU's twelve.
     * Which wins is a per-game answer.
     */
    public static final String[] UPSCALERS = {"SGSR", "FSR", "NIS"};

    /** Frame-rate caps the console offers. Zero is uncapped. */
    /**
     * The caps offered as one-tap presets. Any whole number is accepted.
     *
     * These stopped being the permitted values when the cap became a typed
     * field; they are the ones worth reaching without typing. The number itself
     * is what is stored now, so the list can grow or shrink without moving
     * anybody's setting.
     */
    public static final int[] FPS_LIMITS = {0, 30, 40, 45, 50, 60, 72, 90, 120, 144};

    /** The highest cap that can be set, chosen to exceed any panel this runs on. */
    public static final int MAX_FPS_LIMIT = 480;


    public static int toVkPresentMode(String mode) {
        return "mailbox".equals(mode) ? PRESENT_MODE_MAILBOX : PRESENT_MODE_FIFO;
    }

    public static String fromVkPresentMode(int mode) {
        return mode == PRESENT_MODE_MAILBOX ? "mailbox" : "fifo";
    }

    /**
     * The present mode this app's compositor should actually use.
     *
     * Mailbox while frames are being generated, whatever the container says, and
     * this one setting is the difference between frame generation working and
     * appearing to do nothing at all.
     *
     * The layer inserts its frames into the *guest's* swapchain, and to do that
     * it has to acquire an image without blocking — blocking would stall the
     * game. Under FIFO this compositor waits for vblank before presenting, and
     * that wait is backpressure: it propagates back through the X11 present path
     * to the guest, whose swapchain images are then never released. The layer's
     * acquire returns `VK_NOT_READY` and the frame is thrown away. Measured on an
     * AYN Thor: exactly 30 discards a second against a 30 fps game, which is
     * every generated frame, and neither a bigger guest swapchain
     * (`dxgi.numBackBuffers`) nor forcing the guest itself to FIFO moved it —
     * because the stall was never in the guest.
     *
     * Under mailbox the compositor never waits, so guest images come back and
     * the acquire finds one. The cost is the usual mailbox cost, and it is the
     * right trade: generation exists to put more frames on the panel.
     */
    public static int hostPresentMode(Container container, Shortcut shortcut) {
        String chosen = shortcut != null ? shortcut.getRendererPresentMode()
                : container != null ? container.getRendererPresentMode() : "fifo";

        if (container != null && FrameGeneration.isEnabled(container)
                && !"mailbox".equals(chosen)) {
            Log.d(TAG, "Frame generation is on; compositing with mailbox (was "
                    + chosen + ") so generated frames can reach the panel");
            return PRESENT_MODE_MAILBOX;
        }
        return toVkPresentMode(chosen);
    }

    /**
     * Puts a container's stored renderer settings onto a live renderer.
     *
     * Call once, after the surface exists and before the guest starts drawing
     * into it. A shortcut overrides the container for any setting it pins.
     *
     * @param graphicsDriverConfig the parsed driver config, consulted only for
     *        the driver version when neither the container nor the shortcut names
     *        one — the case for a container created before per-container drivers
     *        existed.
     */
    /**
     * The settings that belong to any renderer, not just the Vulkan one.
     *
     * These were inside {@link #apply}, which takes a {@code VulkanRenderer},
     * and that is where they were lost when a second renderer arrived: a
     * container on the scanout path never had its shell hidden, so the built-in
     * Wine desktop and its taskbar were drawn along with the game and the game
     * appeared to launch behind them. Neither of these is a compositor feature
     * — one is which windows exist as far as the display is concerned, the
     * other is whose cursor is drawn — so both belong to the interface both
     * renderers implement.
     */
    public static void applyCommon(XServerRenderer renderer) {
        // The guest draws its own cursor. Showing the host's as well is the
        // double-cursor bug, and it is the default because the same renderer
        // serves the desktop shell, which has no cursor of its own.
        renderer.setCursorVisible(false);

        /*
         * The desktop shell is hidden, not stopped.
         *
         * `explorer.exe` still runs — `winhandler.exe` is launched with it as
         * its argument and every launch goes through that bridge — but this app
         * supplies its own desktop, so the guest's is a second one drawn over
         * the first. `wfm.exe` is the file manager, replaced by the console's
         * Programs page.
         */
        renderer.setUnviewableWMClasses("explorer.exe", "wfm.exe");
    }

    /**
     * The scanout renderer's share of a container's settings.
     *
     * Short by nature: everything else {@link #apply} does is a shader or
     * swapchain setting and `ASurfaceRenderer` has neither. The frame rate
     * still matters because the Present extension paces the guest against it.
     */
    public static void applyScanout(ASurfaceRenderer renderer, Container container,
                                    Shortcut shortcut) {
        if (container == null) return;
        renderer.setFpsLimit(fpsLimitOf(container));
        renderer.setAdaptiveRefreshRate(
                enabled(container, ContainerPerformanceOptions.ADAPTIVE_REFRESH_RATE));
        renderer.setFitGuestWindow("1".equals(container.getExtra("fitGuestWindow")));
        /*
         * Direct scanout is on unless the container has switched it off.
         *
         * It defaulted off, and with it off this renderer does the one thing it
         * exists not to do: every guest frame is blit-converted into a second
         * buffer before SurfaceFlinger will scan it out. That costs a
         * full-screen copy of GPU time that is not the game, and worse, it
         * serialises -- the guest cannot have its buffer back until the copy
         * finishes, so it stalls, and the GPU sits idle for the half of each
         * frame it is waiting. A GPU at fifty percent while the game misses its
         * frame rate is that stall, not a shortage of work.
         *
         * Choosing the SurfaceFlinger renderer is choosing zero copy; there is
         * no reason to pick it and then pay for a copy. It degrades safely on a
         * driver that will not import a buffer: the import is counted as
         * rejected and the previous content is kept rather than shown black.
         *
         * A container that explicitly turned it off keeps it off.
         */
        renderer.setSfCompatMode(!ContainerPerformanceOptions.storedEnabled(
                container.getExtra(ContainerPerformanceOptions.DIRECT_SCANOUT), true));
        renderer.setMaximumConvertedBuffers(
                ContainerPerformanceOptions.scanoutBufferLimit(
                        enabled(container, ContainerPerformanceOptions.COMPACT_SCANOUT_BUFFERS)));

        String multiplier = container.getExtra("frameGenerationMultiplier");
        if (multiplier != null && !multiplier.isEmpty()) {
            try {
                renderer.setFrameGenMultiplier(Integer.parseInt(multiplier));
            } catch (NumberFormatException ignored) {
                // Left at 1; a malformed value is not worth failing a launch for.
            }
        }
    }

    public static void apply(
            Context context,
            CompositorRenderer renderer,
            Container container,
            Shortcut shortcut,
            HashMap<String, String> graphicsDriverConfig) {

        // The guest draws its own cursor. Showing the host's as well is the
        // double-cursor bug, and it is the default because the same renderer
        // serves the desktop shell, which has no cursor of its own.
        renderer.setCursorVisible(false);

        applyRenderer(context, renderer, container, shortcut, graphicsDriverConfig);
    }

    private static void applyRenderer(Context context, CompositorRenderer renderer,
            Container container, Shortcut shortcut,
            HashMap<String, String> graphicsDriverConfig) {

        // Adrenotools opens a Vulkan ICD. The OpenGL compositor deliberately
        // uses Android's EGL/GLES implementation, while the selected driver is
        // still passed to the guest for DXVK/VKD3D by WemuSession.
        if (renderer instanceof VulkanRenderer) {
            applyDriver(context, (VulkanRenderer) renderer,
                    container, shortcut, graphicsDriverConfig);
        }

        renderer.setVkPresentMode(hostPresentMode(container, shortcut));

        /*
         * What the X root is, when no guest window covers it.
         *
         * It was black, and black is only right for a game that fills the panel.
         * Anything windowed — an installer, a launcher, a game at a resolution
         * below the panel's — sat in a void with a hard edge, and the container
         * desktop the user had a moment earlier looked as though it had been
         * replaced by nothing rather than covered by something.
         *
         * The launcher's own background colour, so the two planes agree. It
         * cannot be the desktop itself: that is a Compose layer above this
         * surface and there is no way to put it behind a window drawn inside it.
         * Matching the colour is what makes the seam stop reading as a hole.
         *
         * Falls back to black when the app has never published a palette, which
         * is exactly the behaviour this replaces.
         */
        renderer.setRootColor(rootColorOf(context));

        renderer.setFilterMode(shortcut != null ? shortcut.getRendererFilterMode()
                : container != null ? container.getRendererFilterMode() : FILTER_NEAREST);

        renderer.setSwapRB(shortcut != null ? shortcut.getRendererSwapRB()
                : container != null && container.getRendererSwapRB());

        if (container != null) {
            renderer.setFpsLimit(fpsLimitOf(container));
            renderer.setSharpness(sharpnessOf(container));
            renderer.setAdaptiveRefreshRate(
                    enabled(container, ContainerPerformanceOptions.ADAPTIVE_REFRESH_RATE));

            /*
             * A game that runs below the desktop resolution is stretched to fill
             * it, if the container asked for that.
             *
             * The guest runs inside a virtual desktop, so Wine's own fullscreen
             * hack has nothing to scale — see `VulkanRenderer.setFitGuestWindow`
             * for what was measured. Lowering the resolution in a game's own
             * options is the only reliable way to make it draw fewer pixels, and
             * this is what stops the result being a small window in a corner.
             */
            renderer.setFitGuestWindow("1".equals(container.getExtra("fitGuestWindow")));

            /*
             * The renderer generates nothing, but it declares the frame rate.
             *
             * A surface told 30 on a 120 Hz panel gets a 30 Hz panel, and the
             * layer's extra frames have nowhere to land. It has to be told what
             * will actually arrive.
             */
            renderer.setFrameGenMultiplier(
                    FrameGeneration.isEnabled(container)
                            ? FrameGeneration.multiplierOf(container)
                            : 1);

            /*
             * Nothing else here for frame generation.
             *
             * It is a Vulkan layer inside the guest, set up by
             * `FrameGeneration.prepare` from the container's own extras. The
             * compositor briefly did the work itself and the code for it is in
             * this file's history, along with what it cost.
             */

            /*
             * The scheduler hint, aimed at the cap the user actually chose.
             *
             * A target is only useful if it is the deadline being worked to: a
             * session that reports against 16.7 ms while the container is
             * capped at 30 will report every frame as twice over budget and ask
             * for a boost that is not needed. Started here because this is the
             * one place that knows both the renderer and the container.
             */
            if (ContainerPerformanceOptions.storedEnabled(
                    container.getExtra(ContainerPerformanceOptions.ADPF_HINTS), true)) {
                renderer.getPerformanceHints().start(context, fpsLimitOf(container));
            } else {
                renderer.getPerformanceHints().close();
            }

            int postFX = intExtra(container, "graphicsPostFXMode", 0);
            if (postFX > 0) renderer.setPostFXMode(postFX);
        }

        /*
         * Wine's shell is never drawn, whether or not a shortcut was launched.
         *
         * It still *runs* — installers want shell folders, a tray and DDE to
         * exist, and killing it to hide it would break them — but the renderer
         * composites none of it. With a shortcut the desktop behind the game was
         * always scenery, and hiding it is also what stops the game being
         * letterboxed inside a container whose screen size differs from the
         * panel's.
         *
         * Without a shortcut the desktop used to *be* the picture, and that is
         * the branch that put a 1998 interface on a 2026 handheld. It was left
         * that way for a good reason: `wfm.exe` was hidden here once, which
         * removed the one window a desktop is opened for and left an empty
         * screen. The answer was never a better-themed Win32 shell — a menu bar,
         * a text-labelled toolbar and a tree pane are compiled into a binary and
         * no visual style reaches them. It was a shell that is not Win32 at all,
         * and `session.desktop.ContainerDesktop` is now that: wallpaper, drives
         * and a file explorer, drawn on the host in the launcher's own
         * interface. Proton, CrossOver, Bottles and Whisky all arrived here too.
         *
         * `wfm.exe` is named separately because it is a separate window with a
         * class of its own, and hiding the shell never touched it. Opening a
         * container without a shortcut starts it deliberately — see
         * `WemuSession.wineStartCommand` — so it kept appearing over the
         * new desktop with the old one nowhere in sight, which read as the
         * replacement half-working rather than as a second program.
         *
         * Hidden rather than not started: `winhandler.exe` is given it as its
         * argument, and that bridge is what every launch from the desktop goes
         * through. Leaving the argument alone keeps startup exactly as it was.
         */
        renderer.setUnviewableWMClasses("explorer.exe", "wfm.exe");
    }

    /**
     * Points the renderer at a bundled Adreno driver, if the container names one.
     *
     * Silent on failure and deliberately so: the fallback is the system driver,
     * which works. Turning a missing custom driver into a failed launch would
     * strand a container whose driver was uninstalled, and the user's next step
     * either way is to pick a different one.
     */
    private static void applyDriver(
            Context context,
            VulkanRenderer renderer,
            Container container,
            Shortcut shortcut,
            HashMap<String, String> graphicsDriverConfig) {

        /*
         * Read only an explicit shortcut override here. Shortcut#getRendererDriverId
         * falls back to the shortcut's owning container, which is not necessarily
         * the container selected by "Run with". The resolved config already
         * contains the effective launch container's driver and WemuSession folds
         * an explicit game override into that same map.
         */
        String driverId = shortcut != null
                ? shortcut.getExtra("rendererDriverId", null) : null;
        if (driverId == null || driverId.isEmpty()) {
            driverId = graphicsDriverConfig != null ? graphicsDriverConfig.get("version") : null;
        }
        if ((driverId == null || driverId.isEmpty()) && container != null) {
            driverId = container.getRendererDriverId();
        }
        if (driverId == null || driverId.isEmpty() || driverId.equalsIgnoreCase("system")) return;

        try {
            String libraryName = new AdrenotoolsManager(context).getLibraryName(driverId);
            if (libraryName.isEmpty()) return;

            String driverPath = context.getFilesDir().getAbsolutePath()
                    + "/contents/adrenotools/" + driverId + "/";
            renderer.setDriverInfo(driverPath, libraryName, AppUtils.getNativeLibDir(context));
        } catch (Exception e) {
            Log.w(TAG, "Falling back to the system Vulkan driver; " + driverId + " is unusable", e);
        }
    }

    /**
     * The launcher's background colour, as the engine can see it.
     *
     * Read from the same shared preference `WineThemeManager.GuestPalette` uses
     * — the app flattens its OKLCH theme into plain ints there because the
     * engine cannot resolve one. Read directly rather than through that class
     * because it is package-private to `core` and this needs one value out of
     * it, not the whole palette.
     */
    private static int rootColorOf(Context context) {
        if (context == null) return 0;
        try {
            return androidx.preference.PreferenceManager
                    .getDefaultSharedPreferences(context)
                    .getInt("guest_theme_background", 0);
        } catch (Exception e) {
            return 0;
        }
    }

    /**
     * The saved cap in frames per second, or zero for uncapped.
     *
     * <p>Read from {@code graphicsFpsLimit}, which holds the number itself. A
     * container saved before the cap could be typed has only
     * {@code graphicsFpsPreset}, an index into whatever {@link #FPS_LIMITS} was
     * that day — so it is read as a fallback and translated through the list as
     * it stands. That is why the presets keep 0, 30, 60, 90 and 120 in their
     * original positions: those indices are written into containers on the
     * device and moving them would silently change somebody's cap.</p>
     */
    public static int fpsLimitOf(Container container) {
        int stored = intExtra(container, "graphicsFpsLimit", -1);
        if (stored >= 0) return clampFpsLimit(stored);

        int index = intExtra(container, "graphicsFpsPreset", 0);
        return index >= 0 && index < FPS_LIMITS.length ? FPS_LIMITS[index] : 0;
    }

    /**
     * Brings a typed cap into the range the renderer can pace.
     *
     * <p>Zero is uncapped and is the only value below the floor that means
     * anything: a cap of one or two frames a second is not a setting, it is a
     * way to make a container look broken. The ceiling exists because the value
     * becomes a frame interval in nanoseconds, and a cap higher than any panel
     * can show is indistinguishable from uncapped while still costing the pacer
     * arithmetic.</p>
     */
    public static int clampFpsLimit(int fps) {
        if (fps <= 0) return 0;
        if (fps < MIN_FPS_LIMIT) return MIN_FPS_LIMIT;
        return Math.min(fps, MAX_FPS_LIMIT);
    }

    /** Below this a cap is a fault report rather than a setting. */
    public static final int MIN_FPS_LIMIT = 10;

    /** Sharpness as the renderer wants it: 0..1, not the stored 0..100. */
    public static float sharpnessOf(Container container) {
        String saved = container.getExtra("graphicsSharpness");
        if (saved.isEmpty()) return 0.5f;
        try {
            return Float.parseFloat(saved) / 100f;
        } catch (NumberFormatException e) {
            return 0.5f;
        }
    }

    /**
     * Writes the console's graphics choices back onto the container.
     *
     * The upstream sidebar had a "save preset" button because its controls were
     * transient and the container was only written when you pressed it. Here the
     * console writes through on every change, so this is called from the setters
     * rather than from a button.
     */
    /** The preset closest to a typed cap, for the legacy key. */
    private static int nearestPresetIndex(int fpsLimit) {
        int best = 0;
        int bestDistance = Integer.MAX_VALUE;
        for (int i = 0; i < FPS_LIMITS.length; i++) {
            int distance = Math.abs(FPS_LIMITS[i] - fpsLimit);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = i;
            }
        }
        return best;
    }

    public static void save(Container container, int fpsLimit, int filterMode, float sharpness, int postFXMode) {
        if (container == null) return;

        /*
         * The number, not an index into a list of allowed numbers.
         *
         * The old key is written too, and kept in step, because an older build
         * of the app reads only that one -- a container edited here and then
         * launched from a previous install should not come back uncapped. It
         * gets the nearest preset, which is the best that key can express.
         */
        container.putExtra("graphicsFpsLimit", String.valueOf(clampFpsLimit(fpsLimit)));
        container.putExtra("graphicsFpsPreset", String.valueOf(nearestPresetIndex(fpsLimit)));

        /*
         * The filter mode is a field on the container, not an extra.
         *
         * This wrote `graphicsFilterMode` as an extra, and nothing in the app
         * has ever read it — every reader takes the `rendererFilterMode` field:
         * `applyRenderer` at launch, the container editor, and the console's
         * own state when it opens. So choosing an upscaler worked for exactly
         * as long as the session lasted, and came back Off, because it was
         * being written to one place and read from another.
         *
         * That is the whole of "I set FSR and it does nothing next time".
         */
        container.setRendererFilterMode(filterMode);

        container.putExtra("graphicsSharpness", String.valueOf(sharpness * 100f));
        container.putExtra("graphicsPostFXMode", String.valueOf(postFXMode));
        container.saveData();
    }

    private static int intExtra(Container container, String key, int fallback) {
        String value = container.getExtra(key);
        if (value.isEmpty()) return fallback;
        try {
            return Integer.parseInt(value);
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    private static boolean enabled(Container container, String key) {
        return container != null && "1".equals(container.getExtra(key));
    }
}
