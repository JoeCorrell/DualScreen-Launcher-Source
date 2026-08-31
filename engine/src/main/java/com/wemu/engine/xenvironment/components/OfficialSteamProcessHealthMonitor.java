package com.wemu.engine.xenvironment.components;

import android.system.Os;
import android.system.OsConstants;

import com.wemu.engine.core.ProcessHelper;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/**
 * Low-rate, credential-free health telemetry for the managed Windows Steam tree.
 *
 * <p>Steam's Chromium subprocesses can remain alive while one renderer consumes
 * a core, several gigabytes of anonymous memory, and stops producing frames.
 * A process-exit callback cannot describe that failure.  This monitor samples
 * only processes whose immutable environment contains the exact managed
 * {@code WINEPREFIX}; it records aggregate roles, RSS, threads and CPU deltas,
 * never arguments, environment values, user names or paths.</p>
 */
final class OfficialSteamProcessHealthMonitor implements Runnable {
    interface Sink { void write(String message); }

    private static final long SAMPLE_MS = 1_000L;
    private static final long SUMMARY_MS = 10_000L;
    private static final long CRITICAL_REPEAT_MS = 10_000L;
    private static final long HIGH_RSS_KB = 6L * 1024L * 1024L;
    private static final double HIGH_CPU_PERCENT = 250.0;

    private final File procRoot;
    private final byte[] expectedPrefix;
    private final Sink sink;
    private final long clockTicksPerSecond;
    private final Map<Integer, CpuPoint> previousCpu = new HashMap<>();

    private volatile boolean stopped;
    private Thread thread;
    private long lastScanAtMs;
    private long lastSummaryAtMs;
    private long lastCriticalAtMs;
    private int peakProcesses;
    private long peakRssKb;
    private int cefGeneration;
    private int lastBrowserPid = -1;
    private boolean sawSteam;

    OfficialSteamProcessHealthMonitor(String winePrefix) {
        this(new File("/proc"), winePrefix, ProcessHelper::emitDiagnostic,
                detectedClockTicks());
    }

    OfficialSteamProcessHealthMonitor(
            File procRoot, String winePrefix, Sink sink, long clockTicksPerSecond) {
        this.procRoot = procRoot;
        this.expectedPrefix = ("WINEPREFIX=" + winePrefix)
                .getBytes(StandardCharsets.UTF_8);
        this.sink = sink;
        this.clockTicksPerSecond = clockTicksPerSecond > 0 ? clockTicksPerSecond : 100L;
    }

    synchronized void start() {
        if (thread != null) return;
        stopped = false;
        thread = new Thread(this, "OfficialSteam-Health");
        thread.setDaemon(true);
        thread.setPriority(Thread.NORM_PRIORITY - 1);
        thread.start();
    }

    synchronized void stop() {
        stopped = true;
        Thread current = thread;
        thread = null;
        if (current != null) current.interrupt();
    }

    @Override public void run() {
        sink.write("Steam health monitor started (1s samples, 10s summaries)");
        try {
            while (!stopped && !Thread.currentThread().isInterrupted()) {
                long now = android.os.SystemClock.uptimeMillis();
                Snapshot snapshot = scan(now);
                if (snapshot.available) publish(snapshot, now);
                Thread.sleep(SAMPLE_MS);
            }
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        } finally {
            sink.write("Steam health monitor stopped");
        }
    }

    private void publish(Snapshot snapshot, long now) {
        peakProcesses = Math.max(peakProcesses, snapshot.processCount);
        peakRssKb = Math.max(peakRssKb, snapshot.rssKb);

        if (snapshot.steamCount > 0) sawSteam = true;
        if (snapshot.browserPid != lastBrowserPid) {
            if (snapshot.browserPid > 0) {
                cefGeneration++;
                sink.write("Steam CEF generation " + cefGeneration + " became active");
            } else if (lastBrowserPid > 0) {
                sink.write("Steam CEF browser left the managed prefix");
            }
            lastBrowserPid = snapshot.browserPid;
        }

        boolean critical = snapshot.rssKb >= HIGH_RSS_KB
                || snapshot.cpuPercent >= HIGH_CPU_PERCENT
                || (sawSteam && snapshot.steamCount == 0);
        if ((now - lastSummaryAtMs) >= SUMMARY_MS
                || (critical && (now - lastCriticalAtMs) >= CRITICAL_REPEAT_MS)) {
            sink.write(snapshot.summary(peakProcesses, peakRssKb, critical));
            lastSummaryAtMs = now;
            if (critical) lastCriticalAtMs = now;
        }
    }

    Snapshot scan(long nowMs) {
        File[] entries = procRoot.listFiles(file -> file.isDirectory()
                && isNumeric(file.getName()));
        if (entries == null) return Snapshot.unavailable();

        Snapshot result = new Snapshot();
        Map<Integer, CpuPoint> nextCpu = new HashMap<>();
        boolean readAnyEnvironment = false;

        for (File entry : entries) {
            final int pid;
            try {
                pid = Integer.parseInt(entry.getName());
            } catch (NumberFormatException ignored) {
                continue;
            }

            byte[] environment;
            try {
                environment = Files.readAllBytes(new File(entry, "environ").toPath());
                readAnyEnvironment = true;
            } catch (IOException | SecurityException ignored) {
                continue;
            }
            if (!containsEntry(environment, expectedPrefix)) continue;

            String commandLine;
            try {
                byte[] raw = Files.readAllBytes(new File(entry, "cmdline").toPath());
                for (int i = 0; i < raw.length; i++) if (raw[i] == 0) raw[i] = ' ';
                commandLine = new String(raw, StandardCharsets.UTF_8)
                        .toLowerCase(Locale.ROOT);
            } catch (IOException | SecurityException ignored) {
                commandLine = "";
                result.unclassifiedCount++;
            }

            Status status = readStatus(new File(entry, "status"));
            CpuPoint cpu = readCpuPoint(new File(entry, "stat"));
            double processCpuPercent = 0.0;
            if (cpu != null) {
                nextCpu.put(pid, cpu);
                CpuPoint before = previousCpu.get(pid);
                if (before != null && before.startTicks == cpu.startTicks
                        && lastScanAtMs > 0 && nowMs > lastScanAtMs) {
                    long delta = cpu.totalTicks - before.totalTicks;
                    if (delta > 0) {
                        processCpuPercent = delta * 100_000.0
                                / (clockTicksPerSecond * (nowMs - lastScanAtMs));
                        result.cpuPercent += processCpuPercent;
                    }
                }
            }

            result.processCount++;
            result.rssKb += status.rssKb;
            result.threadCount += status.threads;
            String role = commandLine.isEmpty()
                    ? "unclassified"
                    : classify(commandLine, pid, result);
            if (status.rssKb > result.largestRssKb) {
                result.largestRssKb = status.rssKb;
                result.largestRole = role;
            }
            if (processCpuPercent > result.hottestCpuPercent) {
                result.hottestCpuPercent = processCpuPercent;
                result.hottestRole = role;
            }
        }

        previousCpu.clear();
        previousCpu.putAll(nextCpu);
        lastScanAtMs = nowMs;
        result.available = readAnyEnvironment;
        return result;
    }

    private static String classify(String command, int pid, Snapshot snapshot) {
        if (command.contains("steamwebhelper.exe")) {
            snapshot.cefCount++;
            if (command.contains("--type=renderer")) {
                snapshot.rendererCount++;
                return "renderer";
            } else if (command.contains("--type=gpu-process")) {
                snapshot.gpuCount++;
                return "gpu";
            } else if (command.contains("--type=utility")) {
                snapshot.utilityCount++;
                return "utility";
            } else if (command.contains("crashpad-handler")) {
                snapshot.crashpadCount++;
                return "crashpad";
            }
            else {
                snapshot.browserCount++;
                snapshot.browserPid = pid;
                return "browser";
            }
        }
        if (command.contains("steam.exe")) {
            snapshot.steamCount++;
            return "steam";
        } else if (command.contains("wineserver")) {
            snapshot.wineserverCount++;
            return "wineserver";
        } else if (containsAny(command, "msiexec", "setup.exe", "dxsetup.exe",
                "dotnetfx", "installscript")) {
            snapshot.installerCount++;
            return "installer";
        } else if (containsAny(command, "services.exe", "explorer.exe", "winhandler.exe",
                "tabtip.exe", "rpcss.exe", "svchost.exe", "plugplay.exe",
                "winedevice.exe", "start.exe")) {
            snapshot.infrastructureCount++;
            return "infrastructure";
        }
        snapshot.otherCount++;
        return "other";
    }

    private static Status readStatus(File file) {
        Status status = new Status();
        try {
            for (String line : Files.readAllLines(file.toPath(), StandardCharsets.UTF_8)) {
                if (line.startsWith("VmRSS:")) status.rssKb = firstNumber(line);
                else if (line.startsWith("Threads:")) status.threads = (int) firstNumber(line);
            }
        } catch (IOException | SecurityException ignored) {}
        return status;
    }

    private static CpuPoint readCpuPoint(File file) {
        try {
            String stat = new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
            int close = stat.lastIndexOf(')');
            if (close < 0 || close + 2 >= stat.length()) return null;
            String[] fields = stat.substring(close + 2).trim().split("\\s+");
            // fields[0] is /proc stat field 3 (state).
            if (fields.length <= 19) return null;
            long user = Long.parseLong(fields[11]);
            long system = Long.parseLong(fields[12]);
            long start = Long.parseLong(fields[19]);
            return new CpuPoint(user + system, start);
        } catch (IOException | SecurityException | NumberFormatException ignored) {
            return null;
        }
    }

    private static long firstNumber(String value) {
        long result = 0;
        boolean found = false;
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            if (c >= '0' && c <= '9') {
                found = true;
                result = result * 10 + (c - '0');
            } else if (found) break;
        }
        return result;
    }

    private static boolean containsEntry(byte[] environment, byte[] expected) {
        for (int start = 0; start < environment.length; ) {
            int end = start;
            while (end < environment.length && environment[end] != 0) end++;
            if (end - start == expected.length) {
                boolean match = true;
                for (int i = 0; i < expected.length; i++) {
                    if (environment[start + i] != expected[i]) {
                        match = false;
                        break;
                    }
                }
                if (match) return true;
            }
            start = end + 1;
        }
        return false;
    }

    private static boolean containsAny(String value, String... needles) {
        for (String needle : needles) if (value.contains(needle)) return true;
        return false;
    }

    private static boolean isNumeric(String value) {
        if (value.isEmpty()) return false;
        for (int i = 0; i < value.length(); i++) {
            if (value.charAt(i) < '0' || value.charAt(i) > '9') return false;
        }
        return true;
    }

    private static long detectedClockTicks() {
        try {
            long ticks = Os.sysconf(OsConstants._SC_CLK_TCK);
            return ticks > 0 ? ticks : 100L;
        } catch (Throwable ignored) {
            return 100L;
        }
    }

    private static final class CpuPoint {
        final long totalTicks;
        final long startTicks;
        CpuPoint(long totalTicks, long startTicks) {
            this.totalTicks = totalTicks;
            this.startTicks = startTicks;
        }
    }

    private static final class Status {
        long rssKb;
        int threads;
    }

    static final class Snapshot {
        boolean available;
        int processCount;
        int threadCount;
        int steamCount;
        int wineserverCount;
        int cefCount;
        int browserCount;
        int rendererCount;
        int gpuCount;
        int utilityCount;
        int crashpadCount;
        int installerCount;
        int infrastructureCount;
        int otherCount;
        int unclassifiedCount;
        int browserPid = -1;
        long rssKb;
        double cpuPercent;
        String largestRole = "none";
        long largestRssKb;
        String hottestRole = "none";
        double hottestCpuPercent;

        static Snapshot unavailable() { return new Snapshot(); }

        String summary(int peakProcesses, long peakRssKb, boolean critical) {
            return String.format(Locale.US,
                    "Steam health%s: procs=%d/%dpeak threads=%d rss=%d/%dpeak MiB "
                            + "cpu=%.0f%%; cef=%d browser=%d renderer=%d gpu=%d utility=%d "
                            + "crashpad=%d; largest=%s/%dMiB hottest=%s/%.0f%%; "
                            + "steam=%d wineserver=%d installers=%d infra=%d other=%d",
                    critical ? " PRESSURE" : "",
                    processCount, peakProcesses, threadCount,
                    rssKb / 1024L, peakRssKb / 1024L, cpuPercent,
                    cefCount, browserCount, rendererCount, gpuCount, utilityCount,
                    crashpadCount, largestRole, largestRssKb / 1024L,
                    hottestRole, hottestCpuPercent,
                    steamCount, wineserverCount, installerCount,
                    infrastructureCount, otherCount + unclassifiedCount);
        }
    }
}
