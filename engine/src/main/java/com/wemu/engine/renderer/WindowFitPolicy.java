package com.wemu.engine.renderer;

import java.util.Locale;

/** Decides whether a guest window may replace the desktop as the fitted source. */
final class WindowFitPolicy {
    private WindowFitPolicy() {}

    static boolean shouldFit(boolean enabled, int windowWidth, int windowHeight,
                             int guestWidth, int guestHeight, String windowClassName) {
        if (!enabled || windowWidth <= 0 || windowHeight <= 0) return false;
        if (isLauncherWindow(windowClassName)) return false;
        /*
         * Wine may expose a 1280x720 client as a 1286x755 decorated window and
         * centre it below the primary origin on a stacked desktop. Once fitting
         * is explicitly enabled, map every valid non-launcher game window; the
         * renderers crop a recognized Wine frame and map its client themselves.
         */
        return true;
    }

    static boolean isLauncherWindow(String windowClassName) {
        return windowClassName != null
                && windowClassName.toLowerCase(Locale.ROOT).contains("launcher");
    }
}
