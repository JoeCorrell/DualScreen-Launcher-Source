package com.wemu.engine.xserver;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * How the root window is divided into monitors.
 *
 * The X server has always had exactly one screen, and for a single-panel device
 * that is the whole truth: the root window is the display. The Thor has two
 * panels, and a guest that is to put something on the second one has to be able
 * to see it — which means the root window spans both, and something has to tell
 * the guest where the join is. That something is RandR, and this is what it
 * reports.
 *
 * <h2>Why a separate type from {@link ScreenInfo}</h2>
 *
 * {@link ScreenInfo} is the size of the root window, and half the engine is
 * built on it: the renderer's viewport, the wallpaper, the cursor clamp, the
 * Wine desktop size. A monitor layout is a different question — given that root
 * window, which rectangles of it are physical screens — and answering both with
 * one field is what would make "the guest has two monitors" and "the game
 * renders at twice the height" the same switch. They are not: a container can
 * span two panels and still be shown on one.
 *
 * <h2>The default is one monitor</h2>
 *
 * {@link #single} produces the layout every existing container has implicitly
 * had, so adding RandR changes nothing for them. A guest that asks gets one
 * output, one CRTC at the origin, and the resolution it already knew about.
 */
public final class ScreenLayout {

    /** One physical panel, as a rectangle of the root window. */
    public static final class Monitor {
        /** RandR output name, which is what a guest shows in a display list. */
        public final String name;
        public final int x;
        public final int y;
        public final int width;
        public final int height;

        public Monitor(String name, int x, int y, int width, int height) {
            this.name = name;
            this.x = x;
            this.y = y;
            this.width = width;
            this.height = height;
        }

        /**
         * Physical size, which RandR reports in millimetres and which decides
         * the DPI a guest computes for this monitor.
         *
         * Derived from the pixel count at the same ratio {@link ScreenInfo}
         * uses rather than measured, because the figure only has to be
         * self-consistent: what a guest does with it is divide pixels by it,
         * and both panels dividing by the same constant is what keeps a window
         * the same apparent size when it is moved between them.
         */
        public int widthInMillimeters() { return Math.max(1, width / 10); }

        public int heightInMillimeters() { return Math.max(1, height / 10); }

        @Override
        public String toString() {
            return name + " " + width + "x" + height + "+" + x + "+" + y;
        }
    }

    private final List<Monitor> monitors;

    private ScreenLayout(List<Monitor> monitors) {
        this.monitors = Collections.unmodifiableList(monitors);
    }

    /** The whole root window as one monitor, which is every container today. */
    public static ScreenLayout single(ScreenInfo screenInfo) {
        List<Monitor> monitors = new ArrayList<>(1);
        monitors.add(new Monitor("XWAYLAND0", 0, 0, screenInfo.width, screenInfo.height));
        return new ScreenLayout(monitors);
    }

    /**
     * The Thor's two panels, stacked as they are on the device.
     *
     * Stacked vertically because that is how the hardware is arranged and
     * because it keeps the mapping from a root coordinate to a panel a single
     * comparison: anything at or below {@code topHeight} belongs to the second
     * screen. A guest moving a window there needs no cooperation from us beyond
     * being told the region exists.
     */
    public static ScreenLayout stacked(int width, int topHeight, int bottomWidth, int bottomHeight) {
        List<Monitor> monitors = new ArrayList<>(2);
        monitors.add(new Monitor("THOR-TOP", 0, 0, width, topHeight));
        monitors.add(new Monitor("THOR-BOTTOM", 0, topHeight, bottomWidth, bottomHeight));
        return new ScreenLayout(monitors);
    }

    public List<Monitor> getMonitors() { return monitors; }

    public int size() { return monitors.size(); }

    /**
     * The total extent the monitors occupy, which is what RandR reports as the
     * screen size and must not exceed the root window.
     */
    public int getWidth() {
        int width = 0;
        for (Monitor monitor : monitors) width = Math.max(width, monitor.x + monitor.width);
        return width;
    }

    public int getHeight() {
        int height = 0;
        for (Monitor monitor : monitors) height = Math.max(height, monitor.y + monitor.height);
        return height;
    }

    @Override
    public String toString() {
        StringBuilder text = new StringBuilder();
        for (Monitor monitor : monitors) {
            if (text.length() > 0) text.append(", ");
            text.append(monitor);
        }
        return text.toString();
    }
}
