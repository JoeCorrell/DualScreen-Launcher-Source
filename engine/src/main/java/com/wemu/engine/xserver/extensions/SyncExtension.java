package com.wemu.engine.xserver.extensions;

import android.util.SparseBooleanArray;

import com.wemu.engine.xconnector.XInputStream;
import com.wemu.engine.xconnector.XOutputStream;
import com.wemu.engine.xserver.XClient;
import com.wemu.engine.xserver.errors.BadFence;
import com.wemu.engine.xserver.errors.BadIdChoice;
import com.wemu.engine.xserver.errors.BadImplementation;
import com.wemu.engine.xserver.errors.XRequestError;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.IntPredicate;

public class SyncExtension implements Extension {
    public static final byte MAJOR_OPCODE = -104;
    private final SparseBooleanArray fences = new SparseBooleanArray();
    private final Object fenceLock = new Object();
    private final Map<Integer, List<Runnable>> triggerListeners = new HashMap<>();
    private final AtomicBoolean closed = new AtomicBoolean(false);

    private static abstract class ClientOpcodes {
        private static final byte CREATE_FENCE = 14;
        private static final byte TRIGGER_FENCE = 15;
        private static final byte RESET_FENCE = 16;
        private static final byte DESTROY_FENCE = 17;
        private static final byte AWAIT_FENCE = 19;
    }

    @Override public String getName() { return "SYNC"; }
    @Override public byte getMajorOpcode() { return MAJOR_OPCODE; }
    @Override public byte getFirstErrorId() { return 0; }
    @Override public byte getFirstEventId() { return 0; }

    public void setTriggered(int id) {
        List<Runnable> listeners = null;
        synchronized (fenceLock) {
            if (fences.indexOfKey(id) >= 0) {
                fences.put(id, true);
                listeners = triggerListeners.remove(id);
                fenceLock.notifyAll();
            }
        }
        runListeners(listeners);
    }

    /**
     * Runs work once a protocol fence is triggered without parking the X
     * request thread. Returns false only after this extension has shut down.
     */
    public boolean runWhenTriggered(int id, Runnable listener) throws BadFence {
        if (listener == null) return false;
        boolean runNow = false;
        synchronized (fenceLock) {
            if (closed.get()) return false;
            if (fences.indexOfKey(id) < 0) throw new BadFence(id);
            if (fences.get(id)) runNow = true;
            else triggerListeners.computeIfAbsent(id, ignored -> new ArrayList<>()).add(listener);
        }
        if (runNow) runListener(listener);
        return true;
    }

    private static void runListeners(List<Runnable> listeners) {
        if (listeners == null) return;
        for (Runnable listener : listeners) runListener(listener);
    }

    private static void runListener(Runnable listener) {
        try {
            listener.run();
        } catch (RuntimeException failure) {
            android.util.Log.e("SyncExtension", "Deferred fence work failed", failure);
        }
    }

    private void createFence(XClient client, XInputStream inputStream, XOutputStream outputStream) throws IOException, XRequestError {
        int drawableId = inputStream.readInt();
        int id = inputStream.readInt();
        boolean initiallyTriggered = inputStream.readByte() == 1;
        inputStream.skip(3);
        synchronized (fenceLock) {
            if (fences.indexOfKey(id) >= 0) throw new BadIdChoice(id);
            fences.put(id, initiallyTriggered);
        }
    }

    private void triggerFence(XClient client, XInputStream inputStream, XOutputStream outputStream) throws IOException, XRequestError {
        int id = inputStream.readInt();
        synchronized (fenceLock) {
            if (fences.indexOfKey(id) < 0) throw new BadFence(id);
        }
        setTriggered(id);
    }

    private void resetFence(XClient client, XInputStream inputStream, XOutputStream outputStream) throws IOException, XRequestError {
        int id = inputStream.readInt();
        synchronized (fenceLock) {
            if (fences.indexOfKey(id) < 0) throw new BadFence(id);
            fences.put(id, false);
        }
    }

    private void destroyFence(XClient client, XInputStream inputStream, XOutputStream outputStream) throws IOException, XRequestError {
        int id = inputStream.readInt();
        synchronized (fenceLock) {
            if (fences.indexOfKey(id) < 0) throw new BadFence(id);
            fences.delete(id);
            triggerListeners.remove(id);
            fenceLock.notifyAll();
        }
    }

    private void awaitFence(XClient client, XInputStream inputStream, XOutputStream outputStream) throws IOException, XRequestError {
        int remainingBytes = client.getRemainingRequestLength();
        if (remainingBytes < 0) remainingBytes = 0;

        int numIds = remainingBytes / 4;
        int[] ids = new int[numIds];
        for (int i = 0; i < numIds; i++) ids[i] = inputStream.readInt();

        int leftover = remainingBytes - (numIds * 4);
        if (leftover > 0) inputStream.skip(leftover);

        if (ids.length == 0) return;

        awaitFences(ids);
    }

    /**
     * SYNC AwaitFence is an all-of barrier: every named fence must exist and
     * be triggered before the request completes. Keep validating every ID even
     * after finding an untriggered fence so a bad ID is never hidden by list
     * order.
     */
    static boolean areAllFencesTriggered(int[] ids, IntPredicate exists,
                                         IntPredicate triggered) throws BadFence {
        boolean allTriggered = true;
        for (int id : ids) {
            if (!exists.test(id)) throw new BadFence(id);
            if (!triggered.test(id)) allTriggered = false;
        }
        return allTriggered;
    }

    private void awaitFences(int[] ids) throws BadFence {
        boolean interrupted = false;
        try {
            synchronized (fenceLock) {
                while (!closed.get()) {
                    if (areAllFencesTriggered(ids,
                            id -> fences.indexOfKey(id) >= 0,
                            fences::get)) {
                        return;
                    }

                    try {
                        fenceLock.wait();
                    } catch (InterruptedException ignored) {
                        /*
                         * An interrupt is not a fence transition and must not
                         * make the protocol request appear to have completed.
                         * XServerComponent.stop() calls shutdown() first; that
                         * sets closed and notifies this condition, providing a
                         * deterministic teardown path without a guessed render
                         * timeout.
                         */
                        interrupted = true;
                    }
                }
            }
        } finally {
            if (interrupted) Thread.currentThread().interrupt();
        }
    }

    @Override
    public void shutdown() {
        if (!closed.compareAndSet(false, true)) return;
        synchronized (fenceLock) {
            triggerListeners.clear();
            fences.clear();
            fenceLock.notifyAll();
        }
    }

    @Override
    public void handleRequest(XClient client, XInputStream inputStream, XOutputStream outputStream) throws IOException, XRequestError {
        int opcode = client.getRequestData();
        switch (opcode) {
            case ClientOpcodes.CREATE_FENCE:
                createFence(client, inputStream, outputStream);
                break;
            case ClientOpcodes.TRIGGER_FENCE:
                triggerFence(client, inputStream, outputStream);
                break;
            case ClientOpcodes.RESET_FENCE:
                resetFence(client, inputStream, outputStream);
                break;
            case ClientOpcodes.DESTROY_FENCE:
                destroyFence(client, inputStream, outputStream);
                break;
            case ClientOpcodes.AWAIT_FENCE:
                awaitFence(client, inputStream, outputStream);
                break;
            default:
                throw new BadImplementation();
        }
    }
}
