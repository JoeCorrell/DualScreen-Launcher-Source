package com.wemu.engine.core;

import java.io.IOException;
import java.util.Map;
import java.util.function.Consumer;

/** A short-lived, testable scheduler scope around process creation, never gameplay. */
public final class CpuAffinityScope implements AutoCloseable {
    public interface Scheduler {
        int get();
        int set(int mask);
    }
    private final Scheduler scheduler;
    private final Consumer<String> diagnostic;
    private int original;
    private int effective;
    private boolean restore;

    private CpuAffinityScope(Scheduler scheduler, Consumer<String> diagnostic) {
        this.scheduler = scheduler;
        this.diagnostic = diagnostic;
    }

    public static CpuAffinityScope open(int mask, Scheduler scheduler, Consumer<String> diagnostic)
            throws IOException {
        CpuAffinityScope scope = new CpuAffinityScope(scheduler, diagnostic);
        if (Integer.bitCount(mask) < 2) return scope;
        try {
            scope.original = scheduler.get();
            if (scope.original == 0 || scope.original == -1)
                throw new IOException("Cannot preserve launch-thread CPU affinity");
            int error = scheduler.set(mask);
            if (error != 0) throw new IOException("Pre-launch affinity refused: " + error);
            scope.restore = true;
            scope.effective = scheduler.get();
            // Linux may silently narrow a successful affinity request. Use its
            // readback for the child topology instead of treating that as a
            // launch failure or advertising CPUs that the child cannot use.
            if (scope.effective == -1 || Integer.bitCount(scope.effective) < 2 ||
                    (scope.effective & ~mask) != 0)
                throw new IOException("Pre-launch affinity has no valid multi-core subset: requested=0x"
                    + Integer.toHexString(mask) + " effective=0x" + Integer.toHexString(scope.effective));
            if (scope.effective != mask)
                scope.emit("CPU affinity restricted by Android: requested=0x" + Integer.toHexString(mask)
                        + " effective=0x" + Integer.toHexString(scope.effective) + "; adapting guest topology");
            scope.emit("CPU affinity established before guest spawn: hostMask=0x"
                    + Integer.toHexString(scope.effective) + " cores=" + CpuAffinityPolicy.list(scope.effective));
            return scope;
        } catch (IOException | RuntimeException | Error failure) {
            scope.close();
            throw failure;
        }
    }

    /** Mutates only this child's ProcessBuilder environment, never shared settings. */
    public void configureEnvironment(Map<String, String> environment) {
        if (effective == 0) return; // Non-opt-in callers keep their existing topology.
        String topology = Integer.bitCount(effective) + ":" + CpuAffinityPolicy.list(effective);
        environment.put("WINE_CPU_TOPOLOGY", topology);
        emit("Guest spawn topology: WINE_CPU_TOPOLOGY=" + topology);
    }

    private void emit(String message) {
        try { diagnostic.accept(message); } catch (RuntimeException ignored) { /* Logging must not leak a child. */ }
    }

    @Override public void close() {
        if (!restore) return;
        restore = false;
        try {
            int error = scheduler.set(original);
            if (error != 0 || scheduler.get() != original)
                emit("WARNING: launch-thread affinity restoration failed or was kernel-restricted; error=" + error);
        } catch (RuntimeException | LinkageError failure) {
            emit("WARNING: launch-thread affinity restoration unavailable: " + failure.getClass().getSimpleName());
        }
    }
}
