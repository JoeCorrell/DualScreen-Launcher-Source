package com.wemu.engine.core;

/**
 * Stored Turnip tuning choices shared by the container UI and launcher.
 *
 * Unknown values fail back to Mesa's bandwidth estimator.  Container files and
 * community presets outlive individual app versions, so passing an arbitrary
 * old value into a newer driver is less useful than retaining a known baseline.
 */
public final class TurnipPerformanceOptions {

    public static final String CONFIG_AUTOTUNE = "turnipAutotune";
    public static final String CONFIG_GPU_TRACE = "turnipGpuTrace";

    public static final String AUTOTUNE_BANDWIDTH = "bandwidth";
    public static final String AUTOTUNE_PROFILE_ONCE = "profiled_imm";
    public static final String AUTOTUNE_CONTINUOUS = "profiled";

    private TurnipPerformanceOptions() {}

    public static String resolveAutotune(String stored) {
        if (AUTOTUNE_PROFILE_ONCE.equals(stored)
                || AUTOTUNE_CONTINUOUS.equals(stored)
                || AUTOTUNE_BANDWIDTH.equals(stored)) {
            return stored;
        }
        return AUTOTUNE_BANDWIDTH;
    }

    public static boolean gpuTraceEnabled(String stored) {
        return "1".equals(stored);
    }
}
