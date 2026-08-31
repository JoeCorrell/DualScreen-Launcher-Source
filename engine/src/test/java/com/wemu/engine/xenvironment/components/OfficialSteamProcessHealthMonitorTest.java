package com.wemu.engine.xenvironment.components;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;

public class OfficialSteamProcessHealthMonitorTest {
    private File proc;
    private final List<String> messages = new ArrayList<>();

    @Before public void setUp() throws IOException {
        proc = Files.createTempDirectory("steam-health-proc").toFile();
    }

    @After public void tearDown() {
        deleteRecursively(proc);
    }

    @Test public void scanCountsOnlyExactPrefixAndClassifiesCefRoles() throws Exception {
        String prefix = "/private/prefix";
        process(101, prefix,
                "wine\0C:\\Steam\\steamwebhelper.exe\0--type=renderer\0",
                1_024_000L, 18, 10, 5, 100);
        process(102, prefix,
                "wine\0C:\\Steam\\steamwebhelper.exe\0--type=gpu-process\0",
                512_000L, 9, 4, 1, 200);
        process(103, prefix,
                "wine\0C:\\Steam\\steamwebhelper.exe\0-startcount=2\0",
                256_000L, 7, 2, 1, 300);
        process(104, prefix, "wine\0C:\\Steam\\steam.exe\0", 128_000L,
                6, 1, 1, 400);
        process(105, "/another/prefix", "wine\0secret-token\0", 999_000L,
                99, 50, 50, 500);

        OfficialSteamProcessHealthMonitor monitor = new OfficialSteamProcessHealthMonitor(
                proc, prefix, messages::add, 100L);
        OfficialSteamProcessHealthMonitor.Snapshot snapshot = monitor.scan(1_000L);

        assertTrue(snapshot.available);
        assertEquals(4, snapshot.processCount);
        assertEquals(3, snapshot.cefCount);
        assertEquals(1, snapshot.rendererCount);
        assertEquals(1, snapshot.gpuCount);
        assertEquals(1, snapshot.browserCount);
        assertEquals(1, snapshot.steamCount);
        assertEquals(1_920_000L, snapshot.rssKb);
        assertEquals(40, snapshot.threadCount);

        String summary = snapshot.summary(4, snapshot.rssKb, false);
        assertTrue(summary.contains("largest=renderer/1000MiB"));
        assertFalse(summary.contains(prefix));
        assertFalse(summary.contains("secret"));
        assertFalse(summary.contains("steamid"));
    }

    @Test public void cpuIsDeltaBasedAndPidReuseDoesNotInflateIt() throws Exception {
        String prefix = "/private/prefix";
        process(201, prefix, "wine\0C:\\Steam\\steam.exe\0", 64_000L,
                4, 10, 5, 100);
        OfficialSteamProcessHealthMonitor monitor = new OfficialSteamProcessHealthMonitor(
                proc, prefix, messages::add, 100L);

        monitor.scan(1_000L);
        writeStat(new File(proc, "201"), 110, 5, 100);
        OfficialSteamProcessHealthMonitor.Snapshot active = monitor.scan(2_000L);
        assertEquals(100.0, active.cpuPercent, 0.01);

        // Kernel/system time contributes to the same per-process core usage.
        writeStat(new File(proc, "201"), 110, 105, 100);
        OfficialSteamProcessHealthMonitor.Snapshot systemActive = monitor.scan(3_000L);
        assertEquals(100.0, systemActive.cpuPercent, 0.01);

        // Same numeric pid, different start time: a new process, not 100 old ticks.
        writeStat(new File(proc, "201"), 500, 500, 999);
        OfficialSteamProcessHealthMonitor.Snapshot reused = monitor.scan(4_000L);
        assertEquals(0.0, reused.cpuPercent, 0.01);
    }

    private void process(int pid, String prefix, String command, long rssKb,
                         int threads, long userTicks, long systemTicks, long startTicks)
            throws IOException {
        File directory = new File(proc, Integer.toString(pid));
        assertTrue(directory.mkdirs());
        Files.write(new File(directory, "environ").toPath(),
                ("A=1\0WINEPREFIX=" + prefix + "\0B=2\0")
                        .getBytes(StandardCharsets.UTF_8));
        Files.write(new File(directory, "cmdline").toPath(),
                command.getBytes(StandardCharsets.UTF_8));
        Files.write(new File(directory, "status").toPath(),
                ("Name:\ttest\nVmRSS:\t" + rssKb + " kB\nThreads:\t" + threads + "\n")
                        .getBytes(StandardCharsets.UTF_8));
        writeStat(directory, userTicks, systemTicks, startTicks);
    }

    private void writeStat(File directory, long userTicks, long systemTicks, long startTicks)
            throws IOException {
        // Fields after ')' begin with field 3 (state); utime/stime are 14/15,
        // and starttime is field 22.
        String stat = "1 (guest) R 0 0 0 0 0 0 0 0 0 0 "
                + userTicks + " " + systemTicks + " 0 0 0 0 0 0 " + startTicks + " 0 0\n";
        Files.write(new File(directory, "stat").toPath(),
                stat.getBytes(StandardCharsets.UTF_8));
    }

    private static void deleteRecursively(File file) {
        if (file == null || !file.exists()) return;
        File[] children = file.listFiles();
        if (children != null) for (File child : children) deleteRecursively(child);
        file.delete();
    }
}
