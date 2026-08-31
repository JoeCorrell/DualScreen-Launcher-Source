package com.wemu.engine.core;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;

/**
 * What the GPU is doing, read from sysfs.
 *
 * The counterpart to {@link CPUStatus}, and subject to the same caveat: these
 * files are readable on some devices and blocked by SELinux on others, with no
 * way to tell which without trying. Every reader here answers -1 rather than 0
 * when it cannot see, so the panel can leave the row out instead of reporting a
 * stopped GPU on a device that simply will not say.
 *
 * Qualcomm first, because the AYN Thor is Adreno and so is nearly everything
 * else this runs on, but the Mali and generic devfreq paths cost two stats each
 * and make the difference between a number and a dash on the rest.
 */
public abstract class GPUStatus {

    /** Where Adreno exposes itself. `kgsl-3d0` is the 3D core. */
    private static final String KGSL = "/sys/class/kgsl/kgsl-3d0/";

    /**
     * Load as a percentage, or -1.
     *
     * Two shapes exist and both are checked. `gpu_load` and
     * `gpu_busy_percentage` are already percentages; `gpubusy` is a pair of
     * counters — busy and total since the last read — which has to be divided.
     * The pair is destructive to read, so it is tried last: reading it resets
     * the window, and doing that when a plain percentage was available would
     * make the plain one jitter.
     */
    public static int getLoadPercent() {
        /*
         * AYN's Adreno kernel exposes both nodes, but `devfreq/gpu_load` is a
         * compatibility stub which stays at zero while the GPU is busy.  The
         * live node is `gpu_busy_percentage`, and its value is formatted as
         * "7 %" rather than a bare integer.  Prefer any positive direct reading,
         * retain zero only as a fallback, and verify it against `gpubusy` below.
         */
        int zeroCandidate = -1;
        int direct = readPercent(KGSL + "gpu_busy_percentage");
        if (direct > 0) return Math.min(direct, 100);
        if (direct == 0) zeroCandidate = 0;

        direct = readPercent(KGSL + "devfreq/gpu_load");
        if (direct > 0) return Math.min(direct, 100);
        if (direct == 0) zeroCandidate = 0;

        // Mali, where utilisation is 0-255 rather than a percentage.
        int mali = readInt("/sys/class/misc/mali0/device/utilisation");
        if (mali >= 0) return Math.min(mali * 100 / 255, 100);

        String busy = readLine(KGSL + "gpubusy");
        if (busy != null) {
            String[] parts = busy.trim().split("\\s+");
            if (parts.length >= 2) {
                long used = parseLong(parts[0]);
                long total = parseLong(parts[1]);
                if (total > 0) return (int) Math.min(used * 100 / total, 100);
            }
        }

        return zeroCandidate;
    }

    /** Current core clock in MHz, or -1. */
    public static int getClockMhz() {
        // devfreq reports Hz.
        int hz = readInt(KGSL + "devfreq/cur_freq");
        if (hz > 0) return hz / 1_000_000;

        hz = readInt(KGSL + "gpuclk");
        if (hz > 0) return hz / 1_000_000;

        for (String path : maliFreqPaths()) {
            hz = readInt(path);
            if (hz > 0) return hz / 1_000_000;
        }

        return -1;
    }

    /** The clock the GPU is allowed to reach, in MHz, or -1. */
    public static int getMaxClockMhz() {
        int hz = readInt(KGSL + "devfreq/max_freq");
        if (hz > 0) return hz / 1_000_000;
        return -1;
    }

    /**
     * Mali's devfreq node, whose directory is named after the SoC.
     *
     * Enumerated rather than hardcoded because the prefix differs per vendor —
     * `13000000.mali`, `1c500000.mali` and so on.
     */
    private static String[] maliFreqPaths() {
        File devfreq = new File("/sys/class/devfreq");
        String[] names = devfreq.list();
        if (names == null) return new String[0];

        String[] paths = new String[names.length];
        int count = 0;
        for (String name : names) {
            if (name.endsWith(".mali") || name.endsWith(".gpu")) {
                paths[count++] = "/sys/class/devfreq/" + name + "/cur_freq";
            }
        }

        String[] found = new String[count];
        System.arraycopy(paths, 0, found, 0, count);
        return found;
    }

    private static int readInt(String path) {
        String line = readLine(path);
        if (line == null) return -1;
        try {
            return Integer.parseInt(line.trim());
        } catch (NumberFormatException e) {
            return -1;
        }
    }

    /** Reads either a bare percentage or Qualcomm's `7 %` representation. */
    private static int readPercent(String path) {
        String line = readLine(path);
        if (line == null) return -1;
        String value = line.trim();
        int separator = value.indexOf(' ');
        if (separator >= 0) value = value.substring(0, separator);
        if (value.endsWith("%")) value = value.substring(0, value.length() - 1);
        try {
            return Integer.parseInt(value.trim());
        } catch (NumberFormatException e) {
            return -1;
        }
    }

    private static long parseLong(String value) {
        try {
            return Long.parseLong(value);
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    /**
     * The first line of a sysfs file, or null.
     *
     * Everything is caught. A blocked read throws `FileNotFoundException` with
     * EACCES underneath on some kernels and returns an empty file on others, and
     * neither is worth distinguishing when the answer either way is "no reading".
     */
    private static String readLine(String path) {
        File file = new File(path);
        if (!file.isFile()) return null;
        try (BufferedReader reader = new BufferedReader(new FileReader(file))) {
            String line = reader.readLine();
            return line == null || line.isEmpty() ? null : line;
        } catch (Exception e) {
            return null;
        }
    }
}
