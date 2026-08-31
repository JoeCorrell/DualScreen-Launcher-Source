package com.wemu.engine.core;

import com.wemu.engine.math.Mathf;
import com.wemu.engine.xserver.XServer;

import java.util.Timer;
import java.util.TimerTask;

public class CursorLocker extends TimerTask {
    private final XServer xServer;
    private float damping = 0.25f;
    private short maxDistance;
    private boolean enabled = true;
    private final Object pauseLock = new Object();
    private final Timer timer;
    private volatile boolean closed = false;

    public CursorLocker(XServer xServer) {
        this.xServer = xServer;
        maxDistance = (short)(xServer.screenInfo.width * 0.05f);
        timer = new Timer("CursorLocker", true);
        timer.scheduleAtFixedRate(this, 0, 1000 / 60);
    }

    public short getMaxDistance() {
        return maxDistance;
    }

    public void setMaxDistance(short maxDistance) {
        this.maxDistance = maxDistance;
    }

    public float getDamping() {
        return damping;
    }

    public void setDamping(float damping) {
        this.damping = damping;
    }

    public boolean isEnabled() {
        return enabled;
    }

    public void setEnabled(boolean enabled) {
        if (closed) return;
        if (enabled) {
            synchronized (pauseLock) {
                this.enabled = true;
                pauseLock.notifyAll();
            }
        }
        else this.enabled = enabled;
    }

    @Override
    public void run() {
        synchronized (pauseLock) {
            while (!enabled && !closed) {
                try {
                    pauseLock.wait();
                }
                catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    return;
                }
            }
        }
        if (closed) return;

        short x = (short)Mathf.clamp(xServer.pointer.getX(), -maxDistance, xServer.screenInfo.width + maxDistance);
        short y = (short)Mathf.clamp(xServer.pointer.getY(), -maxDistance, xServer.screenInfo.height + maxDistance);

        if (x < 0) {
            xServer.pointer.setX((short)Math.ceil(x * damping));
        }
        else if (x >= xServer.screenInfo.width) {
            xServer.pointer.setX((short)Math.floor(xServer.screenInfo.width + (x - xServer.screenInfo.width) * damping));
        }
        if (y < 0) {
            xServer.pointer.setY((short)Math.ceil(y * damping));
        }
        else if (y >= xServer.screenInfo.height) {
            xServer.pointer.setY((short)Math.floor(xServer.screenInfo.height + (y - xServer.screenInfo.height) * damping));
        }
    }

    /** Stops the per-session timer and releases a task paused inside run(). */
    public void close() {
        closed = true;
        synchronized (pauseLock) {
            pauseLock.notifyAll();
        }
        cancel();
        timer.cancel();
        timer.purge();
    }
}
