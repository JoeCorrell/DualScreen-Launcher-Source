package com.wemu.engine.session;

/**
 * Stable on-disk keys and defaults for container-scoped performance trials.
 *
 * A missing value must reproduce the behavior from before the switch existed.
 * Most experiments therefore default off. Two do not: ADPF hints, because Wemu
 * already used them for every Vulkan session before they became user-visible,
 * and direct scanout, because it is what the SurfaceFlinger renderer is for —
 * see {@code RendererOptions.applyScanout}, where absence reads as on.
 */
public final class ContainerPerformanceOptions {
    public static final String DIRECT_SCANOUT = "perfDirectScanout";
    public static final String ORDERED_PRESENT = "perfOrderedPresent";
    public static final String COMPACT_SCANOUT_BUFFERS = "perfCompactScanoutBuffers";
    public static final String ADAPTIVE_REFRESH_RATE = "perfAdaptiveRefreshRate";
    public static final String ADPF_HINTS = "perfAdpfHints";
    public static final String SUSTAINED_MODE = "perfSustainedMode";
    public static final String BURST_ALIGNED_AUDIO = "perfBurstAlignedAudio";

    /**
     * Whether the guest is told about the Thor's second panel.
     *
     * Not a performance trial, but it lives here for the same reason the others
     * do: it is a per-container switch whose absence has to mean exactly what
     * happened before it existed. Off, so a container that has never been told
     * about it sees the one screen it always had.
     *
     * Turning it on makes the root window span both panels and gives RandR two
     * outputs to report, which is what lets a guest place a window on the
     * second screen at all.
     */
    public static final String SECOND_SCREEN = "displaySecondScreen";

    public static final int COMPAT_SCANOUT_BUFFER_LIMIT = 8;
    public static final int COMPACT_SCANOUT_BUFFER_LIMIT = 3;

    private ContainerPerformanceOptions() {}

    public static boolean storedEnabled(String value, boolean defaultValue) {
        if (value == null || value.isEmpty()) return defaultValue;
        return "1".equals(value);
    }

    public static int scanoutBufferLimit(boolean compact) {
        return compact ? COMPACT_SCANOUT_BUFFER_LIMIT : COMPAT_SCANOUT_BUFFER_LIMIT;
    }
}
