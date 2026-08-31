package com.wemu.engine.renderer;

import com.wemu.engine.widget.XServerView;
import com.wemu.engine.xserver.Drawable;
import com.wemu.engine.xserver.Window;

/**
 * What the X server needs from whatever is putting pixels on the glass.
 *
 * Deliberately small. {@link CompositorRenderer} has grown around fifty public
 * methods — upscalers, post-processing, present modes, frame generation,
 * performance hints — and almost none of that is the X server's business. The
 * server needs to know where a window's contents went, whether the view is
 * fullscreen, and how to get back to the view's event queue. Everything else is
 * a property of the compositing renderers and is reached by asking for that type.
 *
 * <h2>Why this is not the union of both renderers</h2>
 *
 * {@link ASurfaceRenderer} hands buffers straight to SurfaceFlinger and never
 * runs a shader, so it cannot sharpen, upscale, apply a screen effect, or
 * choose a Vulkan present mode — those are not features it is missing, they are
 * features that have nowhere to happen in a path whose whole point is that
 * nothing composites. Widening this interface to cover them would mean a set of
 * methods that one implementation answers and the other silently ignores, which
 * is the worst of both: the caller cannot tell, and the setting appears to work.
 * Session code that configures those things asks for a {@code CompositorRenderer}
 * and does nothing when it does not have one; see
 * {@code RendererOptions.configureRenderer}.
 */
public interface XServerRenderer {

    /** One-shot notification that a newly launched guest program has drawn. */
    interface ProgramWindowListener {
        void onProgramWindowPresented(int windowId);
    }

    /** Notification that the last presented window of that program has closed. */
    interface ProgramClosedListener {
        void onProgramClosed();
    }

    /** The view this renderer draws into, and the owner of its event queue. */
    XServerView getRendererView();

    /** Whether the guest's output currently fills the view. */
    boolean isFullscreen();

    /**
     * Fits a guest game window that is smaller than the virtual desktop to the
     * host display. Both renderers can do this: compositors use their draw
     * transform, while direct scanout uses SurfaceFlinger destination rects.
     */
    void setFitGuestWindow(boolean enabled);

    boolean isFitGuestWindow();

    void setCursorVisible(boolean visible);

    boolean isCursorVisible();

    /** Window classes never to be shown, by WM_CLASS. */
    void setUnviewableWMClasses(String... classes);

    /** Arms the next newly presenting window as the foreground program. */
    void armProgramWindowListener(ProgramWindowListener listener);

    /** Receives the end of the foreground program tracked by the method above. */
    void setProgramClosedListener(ProgramClosedListener listener);

    /**
     * Takes a window's new contents from a pixmap without going through the
     * window's own drawable.
     *
     * The Present extension's fast path: the client has already rendered into a
     * pixmap and is telling the server to show it, so copying it into the
     * window's drawable first would be a copy for nothing.
     */
    /** Returns true when the renderer accepted the pixmap for presentation. */
    boolean onUpdateWindowContentDirect(Window window, Drawable pixmap, short xOff, short yOff);

    /**
     * The rate the *guest* should be held to, which is not always the rate
     * reaching the screen — frame generation multiplies one into the other.
     * Zero means unlimited.
     */
    int getPresentedFpsLimit();

    /**
     * The cap on frames the guest draws. Zero is uncapped.
     *
     * On the interface because both renderers have always had it and only one
     * of them could be reached. The session held a {@code CompositorRenderer},
     * which is null whenever the container chose scanout, so on that path the
     * console's frame-rate control wrote to nothing at all.
     */
    void setFpsLimit(int limit);

    /**
     * How many frames reach the panel for each one the guest draws.
     *
     * The renderer generates nothing; it declares. A surface told 30 gets a
     * 30 Hz panel, so a layer generating 60 into it has half its work thrown
     * away — which is what "capping the guest to 30 also caps generation to 30"
     * is. Frame generation can be switched on and off while a game runs, so
     * this has to be able to change with it rather than being read once when
     * the renderer was set up.
     */
    void setFrameGenMultiplier(int multiplier);

    /** The classic in-guest FPS readout, when one is attached. */
    void setFrameRating(Object frameRating);

    int getSurfaceWidth();

    int getSurfaceHeight();
}
