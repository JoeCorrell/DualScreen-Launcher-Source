package com.wemu.engine.core;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class FrameGenerationLaunchPolicyTest {
    @Test
    public void disabledContainerDoesNotRequestGuestLayer() {
        assertFalse(FrameGeneration.isRequested("", false));
        assertFalse(FrameGeneration.isRequested("0", false));
        assertFalse(FrameGeneration.isRequested("false", false));
    }

    @Test
    public void enabledContainerAndDiagnosticMarkerRequestGuestLayer() {
        assertTrue(FrameGeneration.isRequested("1", false));
        assertTrue(FrameGeneration.isRequested("0", true));
    }

    @Test
    public void disabledLaunchRemovesStaleEnableVariable() {
        EnvVars environment = new EnvVars();
        FrameGeneration.applyEnvVars(environment);
        assertTrue(environment.has("BIONIC_FG_ENABLE"));

        FrameGeneration.clearEnvVars(environment);
        assertFalse(environment.has("BIONIC_FG_ENABLE"));
    }
}
