package com.wemu.engine.renderer;

import java.util.Locale;

/** Private-window identity matching used by native second-screen mods. */
final class NativePanelIdentity {
    private NativePanelIdentity() {}

    static boolean isSkyrim(String className, String title) {
        return normalizedClass(className).contains("skyrimsecondscreenpanel")
                || normalizedTitle(title).equals("skyrim dual screen");
    }

    static boolean isFallout(String className, String title) {
        String cls = normalizedClass(className);
        String name = normalizedTitle(title);
        return cls.contains("fallout3secondscreenpanel")
                || cls.contains("fallout4secondscreenpanel")
                || name.equals("fallout 3 dual-screen pip-boy")
                || name.equals("fallout 4 dual-screen pip-boy");
    }

    static boolean isGrimDawn(String className, String title) {
        return normalizedClass(className).equals("grimdawnsecondscreenpanel")
                || normalizedTitle(title).equals("grim dawn dual-screen");
    }

    static boolean isEarlyClaimPanel(String className, String title) {
        return isSkyrim(className, title)
                || isFallout(className, title)
                || isGrimDawn(className, title);
    }

    private static String normalizedClass(String value) {
        return value == null ? "" : value.trim().toLowerCase(Locale.ROOT);
    }

    private static String normalizedTitle(String value) {
        return value == null ? "" : value.trim().toLowerCase(Locale.ROOT);
    }
}
