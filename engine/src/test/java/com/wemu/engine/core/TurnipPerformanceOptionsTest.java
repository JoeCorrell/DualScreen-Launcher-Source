package com.wemu.engine.core;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class TurnipPerformanceOptionsTest {

    @Test
    public void resolvesEverySupportedAutotuneMode() {
        assertEquals("bandwidth", TurnipPerformanceOptions.resolveAutotune("bandwidth"));
        assertEquals("profiled_imm", TurnipPerformanceOptions.resolveAutotune("profiled_imm"));
        assertEquals("profiled", TurnipPerformanceOptions.resolveAutotune("profiled"));
    }

    @Test
    public void unknownOrMissingAutotuneFallsBackToBandwidth() {
        assertEquals("bandwidth", TurnipPerformanceOptions.resolveAutotune(null));
        assertEquals("bandwidth", TurnipPerformanceOptions.resolveAutotune(""));
        assertEquals("bandwidth", TurnipPerformanceOptions.resolveAutotune("fastest"));
    }

    @Test
    public void traceIsStrictlyOptIn() {
        assertTrue(TurnipPerformanceOptions.gpuTraceEnabled("1"));
        assertFalse(TurnipPerformanceOptions.gpuTraceEnabled(null));
        assertFalse(TurnipPerformanceOptions.gpuTraceEnabled("0"));
        assertFalse(TurnipPerformanceOptions.gpuTraceEnabled("true"));
    }
}
