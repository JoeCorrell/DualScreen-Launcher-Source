package com.wemu.engine.renderer;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class WindowFitPolicyTest {
    @Test
    public void launchersAreNeverFitted() {
        assertFalse(WindowFitPolicy.shouldFit(true, 794, 390, 1280, 720,
                "FalloutLauncherSteam.exe"));
        assertFalse(WindowFitPolicy.shouldFit(true, 662, 437, 1280, 720,
                "SkyrimSELauncher.exe"));
    }

    @Test
    public void smallerGameWindowsRemainEligible() {
        assertTrue(WindowFitPolicy.shouldFit(true, 960, 540, 1280, 720,
                "Fallout3.exe"));
        assertFalse(WindowFitPolicy.shouldFit(false, 960, 540, 1280, 720,
                "Fallout3.exe"));
        assertFalse(WindowFitPolicy.shouldFit(true, 1280, 720, 1280, 720,
                "Fallout3.exe"));
    }
}
