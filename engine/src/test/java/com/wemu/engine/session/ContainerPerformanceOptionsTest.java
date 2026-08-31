package com.wemu.engine.session;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class ContainerPerformanceOptionsTest {
    @Test
    public void missingValuesPreserveTheirDeclaredBaseline() {
        assertFalse(ContainerPerformanceOptions.storedEnabled("", false));
        assertFalse(ContainerPerformanceOptions.storedEnabled(null, false));
        assertTrue(ContainerPerformanceOptions.storedEnabled("", true));
        assertTrue(ContainerPerformanceOptions.storedEnabled(null, true));
    }

    @Test
    public void storedValuesOverrideTheBaseline() {
        assertTrue(ContainerPerformanceOptions.storedEnabled("1", false));
        assertFalse(ContainerPerformanceOptions.storedEnabled("0", true));
        assertFalse(ContainerPerformanceOptions.storedEnabled("garbage", true));
    }

    @Test
    public void compactPoolHasThreeBuffersAndCompatibilityKeepsEight() {
        assertEquals(3, ContainerPerformanceOptions.scanoutBufferLimit(true));
        assertEquals(8, ContainerPerformanceOptions.scanoutBufferLimit(false));
    }
}
