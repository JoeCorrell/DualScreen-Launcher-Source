package com.wemu.engine.session;

import com.wemu.engine.core.WineRegistryEditor;

/** Removes an obsolete launcher-only D3D9 experiment from existing containers. */
public final class BethesdaLauncherCompatibility {
    public static final String PROVISIONING_REVISION = "3";
    static final String[] LAUNCHER_EXECUTABLES = {
            "Fallout3Launcher.exe",
            "FalloutLauncherSteam.exe",
            "FalloutLauncher.exe",
            "FalloutLauncherEpic.exe",
            "FalloutLauncherGOG.exe",
            "SkyrimSELauncher.exe"
    };

    private BethesdaLauncherCompatibility() {}

    public static void removeObsoleteExecutableOverrides(WineRegistryEditor editor) {
        for (String executable : LAUNCHER_EXECUTABLES) {
            editor.removeValue("AppDefaults\\" + executable + "\\DllOverrides", "d3d9");
        }
    }
}
