package com.wemu.engine.renderer;

import java.util.Locale;

/** Decides whether a guest window may replace the desktop as the fitted source. */
final class WindowFitPolicy {
    private WindowFitPolicy() {}

    static boolean shouldFit(boolean enabled, int windowWidth, int windowHeight,
                             int guestWidth, int guestHeight, String windowClassName) {
        if (!enabled || windowWidth <= 0 || windowHeight <= 0) return false;
        if (isLauncherWindow(windowClassName)) return false;
        return windowWidth < guestWidth || windowHeight < guestHeight;
    }

    static boolean isLauncherWindow(String windowClassName) {
        return windowClassName != null
                && windowClassName.toLowerCase(Locale.ROOT).contains("launcher");
    }
}
