package com.wemu.engine.xenvironment.components;

import java.util.Locale;

/** Pure state machine and process classifier for Valve Steam's bootstrap hand-off. */
final class OfficialSteamBootstrapSupervisor {
    static final long STARTUP_GRACE_MS = 30_000L;
    static final long RESTART_GRACE_MS = 15_000L;
    static final int EMPTY_SAMPLES_TO_FINISH = 3;

    /**
     * A prefix is active while it contains any session-owned payload, not only
     * Steam itself. Steam can temporarily leave no client process behind while
     * it runs a prerequisite or hands control to the game.
     */
    enum PrefixSnapshot { SESSION_ACTIVITY, INACTIVE, UNAVAILABLE }

    enum ProcessKind { SESSION_ACTIVITY, RESIDENT_INFRASTRUCTURE, UNAVAILABLE }

    /*
     * These processes are the Wine prefix rather than the Steam session. They
     * can remain alive after the client, installers, and game have all exited,
     * so counting one would make the session immortal. Everything else in the
     * exact WINEPREFIX is activity by default: the executable name of a Steam
     * game is not knowable here, and prerequisite names are not exhaustive.
     */
    private static final String[] RESIDENT_PROCESS_IMAGES = {
            "wineserver",
            "wineserver64",
            "explorer.exe",
            "services.exe",
            "winedevice.exe",
            "winhandler.exe",
            "conhost.exe",
            "plugplay.exe",
            "rpcss.exe",
            "svchost.exe",
            "spoolsv.exe",
            "winemenubuilder.exe",
            "steamservice.exe",
            "steammonitor.exe",
            "steamerrorreporter.exe",
            "steamerrorreporter64.exe"
    };

    static final class Decision {
        private static final Decision KEEP = new Decision(false, 0);
        final boolean shouldFinish;
        final int exitStatus;

        private Decision(boolean shouldFinish, int exitStatus) {
            this.shouldFinish = shouldFinish;
            this.exitStatus = exitStatus;
        }

        static Decision keepAlive() { return KEEP; }
        static Decision finish(int status) { return new Decision(true, status); }
    }

    private boolean launcherExited;
    private boolean sessionActivitySeen;
    private boolean cancelled;
    private boolean finished;
    private long launcherExitAtMs;
    private long lastSessionActivityAtMs;
    private int launcherStatus;
    private int consecutiveEmptySamples;

    synchronized Decision onLauncherExit(int status, long nowMs) {
        if (cancelled || finished) return Decision.keepAlive();
        if (!launcherExited) {
            launcherExited = true;
            launcherStatus = status;
            launcherExitAtMs = nowMs;
        }
        return Decision.keepAlive();
    }

    synchronized Decision onPrefixSnapshot(long nowMs, PrefixSnapshot snapshot) {
        if (cancelled || finished || !launcherExited) return Decision.keepAlive();

        if (snapshot == PrefixSnapshot.SESSION_ACTIVITY) {
            sessionActivitySeen = true;
            lastSessionActivityAtMs = nowMs;
            consecutiveEmptySamples = 0;
            return Decision.keepAlive();
        }
        // An unreadable /proc observation is not evidence that the session stopped.
        if (snapshot == PrefixSnapshot.UNAVAILABLE) {
            consecutiveEmptySamples = 0;
            return Decision.keepAlive();
        }

        if (!sessionActivitySeen) {
            return nowMs - launcherExitAtMs < STARTUP_GRACE_MS
                    ? Decision.keepAlive()
                    : finishOnce();
        }

        // Steam replaces itself during updates and may hand off to an installer
        // or game. A short inactive period is not completion of the session.
        if (nowMs - lastSessionActivityAtMs < RESTART_GRACE_MS) {
            consecutiveEmptySamples = 0;
            return Decision.keepAlive();
        }
        consecutiveEmptySamples++;
        return consecutiveEmptySamples >= EMPTY_SAMPLES_TO_FINISH
                ? finishOnce()
                : Decision.keepAlive();
    }

    synchronized void cancel() { cancelled = true; }

    /**
     * Classifies one command line after its process has already been proven to
     * belong to the exact official Steam WINEPREFIX.
     */
    static ProcessKind classifyPrefixProcess(String commandLine) {
        if (commandLine == null) return ProcessKind.UNAVAILABLE;
        String normalized = commandLine
                .replace('\0', ' ')
                .toLowerCase(Locale.ROOT)
                .trim();
        if (normalized.isEmpty()) return ProcessKind.UNAVAILABLE;

        for (String image : RESIDENT_PROCESS_IMAGES) {
            if (containsProcessImage(normalized, image)) {
                return ProcessKind.RESIDENT_INFRASTRUCTURE;
            }
        }
        return ProcessKind.SESSION_ACTIVITY;
    }

    private static boolean containsProcessImage(String commandLine, String image) {
        int fromIndex = 0;
        while (fromIndex < commandLine.length()) {
            int index = commandLine.indexOf(image, fromIndex);
            if (index < 0) return false;
            int end = index + image.length();
            boolean leftBoundary = index == 0 || !isImageNameCharacter(commandLine.charAt(index - 1));
            boolean rightBoundary = end == commandLine.length()
                    || !isImageNameCharacter(commandLine.charAt(end));
            if (leftBoundary && rightBoundary) return true;
            fromIndex = index + 1;
        }
        return false;
    }

    private static boolean isImageNameCharacter(char value) {
        return (value >= 'a' && value <= 'z')
                || (value >= '0' && value <= '9')
                || value == '_'
                || value == '-'
                || value == '.';
    }

    private Decision finishOnce() {
        if (finished || cancelled) return Decision.keepAlive();
        finished = true;
        return Decision.finish(launcherStatus);
    }
}
