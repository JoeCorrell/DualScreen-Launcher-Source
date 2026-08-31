package com.wemu.engine.session;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.Arrays;

public class BethesdaLauncherCompatibilityTest {
    @Test
    public void cleanupIsScopedToLauncherExecutables() {
        assertTrue(Arrays.asList(BethesdaLauncherCompatibility.LAUNCHER_EXECUTABLES)
                .contains("FalloutLauncherSteam.exe"));
        assertTrue(Arrays.asList(BethesdaLauncherCompatibility.LAUNCHER_EXECUTABLES)
                .contains("FalloutLauncher.exe"));
        assertTrue(Arrays.asList(BethesdaLauncherCompatibility.LAUNCHER_EXECUTABLES)
                .contains("SkyrimSELauncher.exe"));
        assertFalse(Arrays.asList(BethesdaLauncherCompatibility.LAUNCHER_EXECUTABLES)
                .contains("Fallout3.exe"));
        assertFalse(Arrays.asList(BethesdaLauncherCompatibility.LAUNCHER_EXECUTABLES)
                .contains("SkyrimSE.exe"));
    }
}
