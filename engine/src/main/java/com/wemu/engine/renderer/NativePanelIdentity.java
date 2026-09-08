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

    static boolean isStardewValley(String className, String title) {
        return normalizedClass(className).equals("stardewvalleysecondscreenpanel")
                || normalizedTitle(title).equals("stardew valley dual screen");
    }

    static boolean isTransformersWfc(String className, String title) {
        return normalizedClass(className).equals("transformerswfcsecondscreenpanel")
                || normalizedTitle(title).equals(
                        "transformers: war for cybertron dual-screen");
    }

    /**
     * Wine sometimes exposes WFC's private GDI child with only the executable
     * WM_CLASS.  Accept that fallback only when the window exactly fills the
     * configured lower monitor; the ordinary game window must never be claimed
     * merely because it belongs to the same process.
     */
    static boolean isTransformersWfcLowerPanelFallback(
            String className, int width, int height,
            int monitorWidth, int monitorHeight) {
        return normalizedClass(className).equals("twfc.exe")
                && width > 0 && height > 0
                && width == monitorWidth && height == monitorHeight;
    }

    static boolean isIsaac(String className, String title) {
        return normalizedClass(className).equals("isaacsecondscreenpanel")
                || normalizedTitle(title).equals(
                        "the binding of isaac: repentance+ dual-screen");
    }

    static boolean isEarlyClaimPanel(String className, String title) {
        return isSkyrim(className, title)
                || isFallout(className, title)
                || isGrimDawn(className, title)
                || isTransformersWfc(className, title)
                || isIsaac(className, title)
                || isAssassinsCreedII(className, title)
                || isDishonored(className, title)
                || isCrysis3(className, title)
                || isStardewValley(className, title);
    }

    static boolean isAssassinsCreedII(String className, String title) {
        return normalizedClass(className).equals("ac2secondscreenpanel")
                || normalizedTitle(title).equals("assassin's creed ii dual-screen animus");
    }

    static boolean isDishonored(String className, String title) {
        return normalizedClass(className).equals("dishonoredsecondscreenpanel")
                || normalizedTitle(title).equals("dishonored dual-screen journal");
    }

    static boolean isCrysis3(String className, String title) {
        return normalizedClass(className).equals("crysis3secondscreenpanel")
                || normalizedTitle(title).equals("crysis 3 dual-screen tactical interface");
    }

    private static String normalizedClass(String value) {
        return value == null ? "" : value.trim().toLowerCase(Locale.ROOT);
    }

    private static String normalizedTitle(String value) {
        return value == null ? "" : value.trim().toLowerCase(Locale.ROOT);
    }
}
