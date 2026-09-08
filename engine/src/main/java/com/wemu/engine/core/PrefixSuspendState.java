package com.wemu.engine.core;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.HashMap;
import java.util.Map;

/** Owns only stops issued by this session, with Linux start times guarding PID reuse. */
public final class PrefixSuspendState {
    public interface Signal { void send(int pid, boolean suspend); }
    private final Map<Integer, String> stopped = new HashMap<>();

    public static boolean matchesPrefix(byte[] environment, String prefix) {
        if (prefix == null || prefix.isEmpty()) return false;
        String expected = "WINEPREFIX=" + prefix;
        for (String entry : new String(environment, StandardCharsets.UTF_8).split("\u0000")) {
            if (entry.equals(expected)) return true;
        }
        return false;
    }

    public static String[] identity(String stat) {
        int end = stat.lastIndexOf(')');
        if (end < 0 || end + 2 >= stat.length()) return null;
        String[] fields = stat.substring(end + 2).trim().split("\\s+");
        // Following comm: state is field 3, starttime is field 22.
        return fields.length > 19 ? new String[] {fields[0], fields[19]} : null;
    }

    public synchronized void suspend(File procRoot, String prefix, Signal signal) {
        if (prefix == null || prefix.isEmpty()) return;
        // Once parents are stopped, repeat the bounded scan to include children
        // forked during the first pass. Never use a global Wine-name match.
        for (int pass = 0; pass < 3; pass++) {
            File[] entries = procRoot.listFiles(f -> f.getName().matches("[0-9]+"));
            if (entries == null) return;
            for (File entry : entries) {
                try {
                    int pid = Integer.parseInt(entry.getName());
                    if (stopped.containsKey(pid) || !matchesPrefix(
                            Files.readAllBytes(new File(entry, "environ").toPath()), prefix)) continue;
                    String[] id = identity(new String(Files.readAllBytes(
                            new File(entry, "stat").toPath()), StandardCharsets.UTF_8));
                    if (id == null || id[0].equals("T") || id[0].equals("t") || id[0].equals("Z")) continue;
                    signal.send(pid, true);
                    stopped.put(pid, id[1]);
                } catch (IOException | SecurityException | IllegalArgumentException ignored) {
                    // Processes may exit or belong to another UID during a scan.
                }
            }
        }
    }

    public synchronized void resume(File procRoot, Signal signal) {
        stopped.entrySet().removeIf(entry -> {
            try {
                File stat = new File(new File(procRoot, entry.getKey().toString()), "stat");
                String[] id = identity(new String(Files.readAllBytes(stat.toPath()), StandardCharsets.UTF_8));
                if (id != null && id[1].equals(entry.getValue())) signal.send(entry.getKey(), false);
                return true;
            } catch (IOException ignored) {
                return !new File(procRoot, entry.getKey().toString()).exists();
            } catch (SecurityException | IllegalArgumentException ignored) {
                return false;
            }
        });
    }
}
