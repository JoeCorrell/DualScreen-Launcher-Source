package com.wemu.engine.xserver.extensions;

import static com.wemu.engine.xserver.XClientRequestHandler.RESPONSE_CODE_SUCCESS;

import android.util.SparseArray;

import com.wemu.engine.core.ProcessHelper;
import com.wemu.engine.renderer.GPUImage;
import com.wemu.engine.renderer.NativeTexture;
import com.wemu.engine.renderer.Texture;
import com.wemu.engine.renderer.XServerRenderer;
import com.wemu.engine.xconnector.XInputStream;
import com.wemu.engine.xconnector.XOutputStream;
import com.wemu.engine.xconnector.XStreamLock;
import com.wemu.engine.xserver.Bitmask;
import com.wemu.engine.xserver.Drawable;
import com.wemu.engine.xserver.Pixmap;
import com.wemu.engine.xserver.Window;
import com.wemu.engine.xserver.XClient;
import com.wemu.engine.xserver.XLock;
import com.wemu.engine.xserver.XServer;
import com.wemu.engine.xserver.errors.BadImplementation;
import com.wemu.engine.xserver.errors.BadMatch;
import com.wemu.engine.xserver.errors.BadPixmap;
import com.wemu.engine.xserver.errors.BadWindow;
import com.wemu.engine.xserver.errors.XRequestError;
import com.wemu.engine.xserver.events.PresentCompleteNotify;
import com.wemu.engine.xserver.events.PresentIdleNotify;

import java.io.IOException;
import java.util.ArrayList;
import java.util.PriorityQueue;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

public class PresentExtension implements Extension {
    public static final byte MAJOR_OPCODE = -103;
    public enum Kind { PIXMAP, MSC_NOTIFY }
    public enum Mode { COPY, FLIP, SKIP }

    private final SparseArray<Event> events = new SparseArray<>();
    private SyncExtension syncExtension;

    /**
     * One per presented frame, and deliberately not pooled.
     *
     * Allocating here looks wasteful — a short-lived object per frame, per
     * window, in the hottest path the app has — and it was briefly replaced by
     * one instance per window that got filled in again each frame. That was
     * wrong twice over, and both ways are the kind of wrong that shows up as a
     * hung game rather than as a slow one:
     *
     *  - The Choreographer callback and the CPU pacer read these fields on
     *    other threads. A fresh object per frame is a consistent snapshot; a
     *    mutated one can be read with the pixmap of one frame and the serial of
     *    the next, and the serial is what tells the guest its buffer is free.
     *  - `cpuQueue` is a priority queue ordered by `targetNs`. Handing it the
     *    same object repeatedly, and then changing that field, mutates the key
     *    the heap is ordered by while the heap holds it.
     *
     * One allocation a frame is a price worth paying for a value that stops
     * changing once it has been handed over.
     */
    private static class PendingIdle {
        Window window; Pixmap pixmap; int serial; int idleFence;
        long targetNs;
        int  vsyncSkips;
        PendingIdle(Window w, Pixmap p, int s, int f, long t, int sk) {
            window = w; pixmap = p; serial = s; idleFence = f; targetNs = t; vsyncSkips = sk;
        }
    }

    private final java.util.concurrent.ConcurrentHashMap<Integer, PendingIdle> pendingIdles =
        new java.util.concurrent.ConcurrentHashMap<>();
    private final PriorityQueue<PendingIdle> orderedPendingIdles =
        new PriorityQueue<>(11, java.util.Comparator.comparingLong(p -> p.targetNs));
    private volatile boolean orderedIdleNotifications = false;

    private volatile android.view.Choreographer choreographer = null;
    private volatile boolean choreographerChecked = false;
    private final Object choreographerLock = new Object();

    private final Object cpuPacerLock = new Object();
    private Thread cpuPacerThread = null;
    private final PriorityQueue<PendingIdle> cpuQueue =
        new PriorityQueue<>(11, java.util.Comparator.comparingLong(p -> p.targetNs));
    private final AtomicBoolean closed = new AtomicBoolean(false);

    private static final long HEALTH_INTERVAL_MS = 10_000L;
    private static final long REJECTION_INTERVAL_MS = 5_000L;
    private final AtomicLong presentRequests = new AtomicLong();
    private final AtomicLong rendererAccepted = new AtomicLong();
    private final AtomicLong drawableFallbacks = new AtomicLong();
    private final AtomicLong socketAhbRejected = new AtomicLong();
    private final AtomicLong waitFenceRequests = new AtomicLong();
    private final AtomicLong idleFenceRequests = new AtomicLong();
    private final AtomicLong idleNotifications = new AtomicLong();
    private final AtomicLong lastHealthAtMs = new AtomicLong();
    private final AtomicLong lastRejectionAtMs = new AtomicLong();

    private static final long FIRE_EARLY_NS = 700_000L;

    private android.view.Choreographer tryGetChoreographer(XServerRenderer renderer) {
        if (closed.get()) return null;
        if (choreographerChecked) return choreographer;
        synchronized (choreographerLock) {
            if (closed.get()) return null;
            if (choreographerChecked) return choreographer;
            choreographerChecked = true;
            try {
                if (renderer != null && renderer.getRendererView() != null) {

                    choreographer = android.view.Choreographer.getInstance();
                }
            } catch (Exception ignored) {

                android.util.Log.w("PresentExtension", "Choreographer unavailable, using CPU pacer");
            }
            if (choreographer == null) {
                startCpuPacer();
            }
            return choreographer;
        }
    }

    private void startCpuPacer() {
        final Thread thread;
        synchronized (cpuPacerLock) {
            if (closed.get() || cpuPacerThread != null) return;
            thread = new Thread(this::runCpuPacer, "PresentPacer-CPU");
            cpuPacerThread = thread;
        }
        thread.setDaemon(true);
        thread.setPriority(Thread.NORM_PRIORITY);
        thread.start();
    }

    /**
     * Sleeps until the exact next deadline and is notified when an earlier one
     * is inserted. The old half-millisecond poll plus final two-millisecond
     * yield loop kept a maximum-priority core runnable for a substantial part
     * of every frame, even though the pacer had no work to do yet.
     */
    private void runCpuPacer() {
        try {
            while (!closed.get()) {
                final PendingIdle pending;
                synchronized (cpuPacerLock) {
                    while (true) {
                        if (closed.get()) return;

                        PendingIdle next = cpuQueue.peek();
                        if (next == null) {
                            cpuPacerLock.wait();
                            continue;
                        }

                        long remainingNs = next.targetNs - System.nanoTime();
                        if (remainingNs <= 0) {
                            pending = cpuQueue.poll();
                            break;
                        }

                        long waitMs = remainingNs / 1_000_000L;
                        int waitNs = (int)(remainingNs % 1_000_000L);
                        cpuPacerLock.wait(waitMs, waitNs);
                    }
                }

                if (!closed.get()) {
                    sendIdleNotify(pending.window, pending.pixmap,
                                   pending.serial, pending.idleFence);
                }
            }
        } catch (InterruptedException ignored) {
            // shutdown() interrupts the owned worker to make teardown immediate.
        }
    }

    private boolean choreographerPosted = false;
    private final android.view.Choreographer.FrameCallback vsyncCallback = frameTimeNs -> {
        final ArrayList<PendingIdle> ready = new ArrayList<>();
        final boolean ordered = orderedIdleNotifications;
        synchronized (choreographerLock) {
            choreographerPosted = false;
            if (ordered) {
                while (true) {
                    PendingIdle p = orderedPendingIdles.peek();
                    if (p == null || frameTimeNs < p.targetNs) break;
                    if (p.vsyncSkips > 0) {
                        p.vsyncSkips--;
                        break;
                    }
                    ready.add(orderedPendingIdles.poll());
                }
            }
        }
        if (closed.get()) return;
        boolean anyRemaining;
        if (ordered) {
            for (PendingIdle p : ready) {
                sendIdleNotify(p.window, p.pixmap, p.serial, p.idleFence);
            }
            synchronized (choreographerLock) {
                anyRemaining = !orderedPendingIdles.isEmpty();
            }
        } else {
            anyRemaining = false;
            for (java.util.Iterator<java.util.Map.Entry<Integer, PendingIdle>> it =
                    pendingIdles.entrySet().iterator(); it.hasNext(); ) {
                PendingIdle p = it.next().getValue();
                if (frameTimeNs >= p.targetNs) {
                    if (p.vsyncSkips > 0) {
                        p.vsyncSkips--;
                        anyRemaining = true;
                    } else {
                        it.remove();
                        sendIdleNotify(p.window, p.pixmap, p.serial, p.idleFence);
                    }
                } else {
                    anyRemaining = true;
                }
            }
        }
        if (anyRemaining) postChoreographerCallback();
    };

    private void postChoreographerCallback() {
        final android.view.Choreographer current;
        synchronized (choreographerLock) {
            if (closed.get() || choreographer == null || choreographerPosted) return;
            choreographerPosted = true;
            current = choreographer;
        }
        try {
            current.postFrameCallback(vsyncCallback);
        } catch (RuntimeException failure) {
            synchronized (choreographerLock) {
                choreographerPosted = false;
            }
            android.util.Log.w("PresentExtension", "Could not post frame callback", failure);
        }
    }

    private static class WindowTiming { long nextIdleNs = 0; }
    private final java.util.concurrent.ConcurrentHashMap<Integer, WindowTiming> windowTimings =
        new java.util.concurrent.ConcurrentHashMap<>();

    private void scheduleIdleNotify(Window window, Pixmap pixmap, int serial,
                                     int idleFence, int targetFps, XServerRenderer renderer) {
        if (closed.get()) return;
        if (targetFps <= 0) {
            sendIdleNotify(window, pixmap, serial, idleFence);
            return;
        }

        final long frameNs = 1_000_000_000L / targetFps;
        long now = System.nanoTime();

        WindowTiming wt = windowTimings.computeIfAbsent(window.id, k -> new WindowTiming());
        final long fireTime;
        synchronized (wt) {
            if (wt.nextIdleNs <= now - frameNs) {
                wt.nextIdleNs = now + frameNs;
            } else {
                wt.nextIdleNs += frameNs;
            }
            fireTime = wt.nextIdleNs - FIRE_EARLY_NS;
        }

        android.view.Choreographer ch = tryGetChoreographer(renderer);
        if (ch != null) {
            PendingIdle pending = new PendingIdle(
                window, pixmap, serial, idleFence, fireTime, 0);
            if (orderedIdleNotifications) {
                synchronized (choreographerLock) {
                    if (!closed.get()) orderedPendingIdles.offer(pending);
                }
            } else {
                pendingIdles.put(window.id, pending);
                if (closed.get()) pendingIdles.remove(window.id, pending);
            }
            if (!closed.get()) postChoreographerCallback();
        } else {
            synchronized (cpuPacerLock) {
                if (closed.get()) return;
                cpuQueue.offer(new PendingIdle(
                    window, pixmap, serial, idleFence, fireTime, 0));
                // Wakes an empty queue and also re-evaluates a newly earlier deadline.
                cpuPacerLock.notifyAll();
            }
        }
    }

    private static abstract class ClientOpcodes {
        static final byte QUERY_VERSION = 0;
        static final byte PRESENT_PIXMAP = 1;
        static final byte SELECT_INPUT = 3;
    }

    /**
     * Preserves every PresentIdleNotify instead of replacing a window's older
     * pending release. Enabled per container before the guest can submit work.
     */
    public void setOrderedIdleNotifications(boolean enabled) {
        orderedIdleNotifications = enabled;
    }

    private static class Event {
        Window window;
        XClient client;
        int id;
        Bitmask mask;
    }

    @Override public String getName() { return "Present"; }
    @Override public byte getMajorOpcode() { return MAJOR_OPCODE; }
    @Override public byte getFirstErrorId() { return 0; }
    @Override public byte getFirstEventId() { return 0; }

    private void sendIdleNotify(Window window, Pixmap pixmap, int serial, int idleFence) {
        idleNotifications.incrementAndGet();
        if (idleFence != 0 && syncExtension != null)
            syncExtension.setTriggered(idleFence);
        synchronized (events) {
            for (int i = 0; i < events.size(); i++) {
                Event e = events.valueAt(i);
                if (e.window == window && e.mask.isSet(PresentIdleNotify.getEventMask()))
                    e.client.sendEvent(new PresentIdleNotify(e.id, window, pixmap, serial, idleFence));
            }
        }
    }

    private void sendCompleteNotify(Window window, int serial, Kind kind, Mode mode, long ust, long msc) {
        synchronized (events) {
            for (int i = 0; i < events.size(); i++) {
                Event e = events.valueAt(i);
                if (e.window == window && e.mask.isSet(PresentCompleteNotify.getEventMask()))
                    e.client.sendEvent(new PresentCompleteNotify(e.id, window, serial, kind, mode, ust, msc));
            }
        }
    }

    private static void queryVersion(XClient client, XInputStream in, XOutputStream out) throws IOException {
        in.skip(8);
        try (XStreamLock lock = out.lock()) {
            out.writeByte(RESPONSE_CODE_SUCCESS);
            out.writeByte((byte)0);
            out.writeShort(client.getSequenceNumber());
            out.writeInt(0);
            out.writeInt(1);
            out.writeInt(0);
            out.writePad(16);
        }
    }

    private void presentPixmap(XClient client, XInputStream in, XOutputStream out)
            throws IOException, XRequestError {
        int windowId = in.readInt();
        int pixmapId = in.readInt();
        int serial   = in.readInt();
        in.skip(8);
        short xOff = in.readShort();
        short yOff = in.readShort();
        in.skip(4); // target_crtc
        int waitFence = in.readInt();
        int idleFence = in.readInt();
        in.skip(client.getRemainingRequestLength());

        presentRequests.incrementAndGet();
        notePresentInterval();
        if (waitFence != 0) waitFenceRequests.incrementAndGet();
        if (idleFence != 0) idleFenceRequests.incrementAndGet();

        Window window = client.xServer.windowManager.getWindow(windowId);
        if (window == null) throw new BadWindow(windowId);

        Pixmap pixmap = client.xServer.pixmapManager.getPixmap(pixmapId);
        if (pixmap == null) throw new BadPixmap(pixmapId);

        Drawable content = window.getContent();
        if (content.visual.depth != pixmap.drawable.visual.depth) throw new BadMatch();

        /*
         * Present's wait fence belongs to the producer. Ignoring it lets the
         * renderer sample a pixmap while DXVK is still writing it, which is a
         * direct route to stale/black frames and, for imported AHBs, a GPU
         * fault. Defer only this Present instead of parking the X connector's
         * sole request thread. SyncExtension drops these callbacks at shutdown,
         * so a never-signalled fence cannot retain a dead session.
         */
        Runnable submit = () -> {
            /*
             * A signalled fence may run this callback on a different thread,
             * long after the request handler released its resource locks.
             * Reacquire the complete resource set before resolving the IDs so
             * destroy/free requests cannot race this deferred submission.
             */
            try (XLock lock = client.xServer.lock(
                    XServer.Lockable.WINDOW_MANAGER,
                    XServer.Lockable.PIXMAP_MANAGER,
                    XServer.Lockable.DRAWABLE_MANAGER)) {
                submitPixmap(client, windowId, pixmapId, serial, xOff, yOff, idleFence);
            }
        };
        if (waitFence != 0) {
            if (syncExtension == null) throw new BadImplementation();
            syncExtension.runWhenTriggered(waitFence, submit);
        } else {
            submit.run();
        }
    }

    private void submitPixmap(XClient client, int windowId, int pixmapId, int serial,
                              short xOff, short yOff, int idleFence) {
        if (closed.get()) return;

        /*
         * A deferred fence may outlive either resource. Resolve them again at
         * submission time rather than retaining a freed drawable and native
         * buffer through the callback.
         */
        Window window = client.xServer.windowManager.getWindow(windowId);
        Pixmap pixmap = client.xServer.pixmapManager.getPixmap(pixmapId);
        if (window == null || pixmap == null) return;
        Drawable content = window.getContent();
        if (content == null || content.visual.depth != pixmap.drawable.visual.depth) return;

        XServerRenderer renderer = client.xServer.getRenderer();

        /*
         * The rate frames arrive at, which is not the rate the game runs at.
         *
         * This paces the guest by holding its buffer back — the IdleNotify below
         * is what tells the guest a pixmap is free to draw into again — so the
         * number here has to be every frame that will be presented, generated
         * ones included. Paced at the game's rate instead, a guest generating
         * 2x gets one buffer back for every two it presents, and the frame
         * generation layer discards every frame it makes for want of anywhere to
         * put it. See `VulkanRenderer.getPresentedFpsLimit`.
         */
        int targetFps = renderer != null ? renderer.getPresentedFpsLimit() : 0;

        long ust = System.nanoTime() / 1000;
        long msc = ust / (targetFps > 0 ? (1_000_000L / targetFps) : (1_000_000L / 60));

        boolean accepted = false;
        if (renderer != null && window.attributes.isMapped()) {
            /*
             * Renderer entry takes the renderer/scene lock and may then read a
             * drawable. Never call it while holding content.renderLock: normal
             * damage arrives in the opposite order (renderer, then content),
             * which otherwise deadlocks Present against the render thread.
             */
            accepted = renderer.onUpdateWindowContentDirect(
                    window, pixmap.drawable, xOff, yOff);
        }
        boolean rejectedSocketAhb = false;
        if (accepted) rendererAccepted.incrementAndGet();
        if (!accepted) {
            Texture texture = pixmap.drawable.getTexture();
            rejectedSocketAhb = pixmap.drawable.isDirectScanout()
                    && texture instanceof NativeTexture
                    && ((NativeTexture) texture).getVirtualData() == null;
            if (rejectedSocketAhb) {
                /*
                 * There are no CPU pixels to copy for a socket AHB. Drawable's
                 * constructor buffer is only zero-filled placeholder storage;
                 * copying it reports success while replacing the last good
                 * frame with black. Keep the previous content and make the
                 * import rejection visible in the bounded diagnostics.
                 */
                socketAhbRejected.incrementAndGet();
            } else {
                synchronized (content.renderLock) {
                    // A bounded renderer may reject a frame while its surface
                    // is being replaced or a vendor call is stalled. Preserve
                    // Present semantics through the ordinary drawable path.
                    content.copyArea((short)0, (short)0, xOff, yOff,
                            pixmap.drawable.width, pixmap.drawable.height, pixmap.drawable);
                }
                drawableFallbacks.incrementAndGet();
            }
        }

        // Complete means the server consumed the request. It must not precede
        // the renderer hand-off or fallback copy it describes, and callbacks
        // must never run while a drawable's monitor is held.
        sendCompleteNotify(window, serial, Kind.PIXMAP, Mode.COPY, ust, msc);
        scheduleIdleNotify(window, pixmap, serial, idleFence, targetFps, renderer);
        reportHealth(rejectedSocketAhb);
    }

    /**
     * How the frame intervals were distributed since the last report.
     *
     * <p>The counters above say how many frames arrived; they cannot say
     * whether they arrived evenly, and that is the difference between a game
     * that is slow and a game that is stalling. Both look identical as an
     * average, and the average is what a frame-rate counter shows.</p>
     *
     * <p>Read it as a shape rather than as numbers:</p>
     * <ul>
     *   <li><b>p50 close to p95</b> — evenly paced. The frame rate is the frame
     *       rate; whatever is limiting it is limiting every frame equally, which
     *       means a genuine throughput limit.</li>
     *   <li><b>p95 near double p50</b> — frames are missing a display interval
     *       and waiting for the next one. That is a vsync cliff: the work only
     *       just overruns the budget, and the cost of overrunning is a whole
     *       extra interval rather than the microsecond it went over by.</li>
     *   <li><b>max far above p95</b> — something blocks occasionally. Shader
     *       compilation, a texture upload, a garbage collection in the guest.</li>
     * </ul>
     *
     * <p>Sampled into a small ring rather than accumulated, because the useful
     * question is what the last few seconds looked like, and a mean since launch
     * answers a question nobody has.</p>
     */
    private static final int INTERVAL_SAMPLES = 256;
    private final long[] intervalsUs = new long[INTERVAL_SAMPLES];
    private int intervalCount = 0;
    private int intervalIndex = 0;
    private long lastPresentNs = 0;

    private synchronized void notePresentInterval() {
        long now = System.nanoTime();
        long previous = lastPresentNs;
        lastPresentNs = now;
        if (previous == 0) return;

        long deltaUs = (now - previous) / 1000L;
        // A gap of a second or more is the game being paused, loading or
        // alt-tabbed, and folding it in would swamp everything real.
        if (deltaUs <= 0 || deltaUs > 1_000_000L) return;

        intervalsUs[intervalIndex] = deltaUs;
        intervalIndex = (intervalIndex + 1) % INTERVAL_SAMPLES;
        if (intervalCount < INTERVAL_SAMPLES) intervalCount++;
    }

    private synchronized String describeIntervals() {
        if (intervalCount < 8) return "";
        long[] sorted = new long[intervalCount];
        System.arraycopy(intervalsUs, 0, sorted, 0, intervalCount);
        java.util.Arrays.sort(sorted);

        long p50 = sorted[intervalCount / 2];
        long p95 = sorted[Math.min(intervalCount - 1, (intervalCount * 95) / 100)];
        long max = sorted[intervalCount - 1];
        return " frameMs=" + (p50 / 1000f) + "/" + (p95 / 1000f) + "/" + (max / 1000f);
    }

    private void reportHealth(boolean rejected) {
        long now = android.os.SystemClock.uptimeMillis();
        long previous = lastHealthAtMs.get();
        boolean due = now - previous >= HEALTH_INTERVAL_MS;
        long previousRejection = lastRejectionAtMs.get();
        boolean rejectionDue = rejected
                && now - previousRejection >= REJECTION_INTERVAL_MS;
        if (!due && !rejectionDue) return;
        if (due && !lastHealthAtMs.compareAndSet(previous, now)) return;
        if (rejectionDue) lastRejectionAtMs.compareAndSet(previousRejection, now);

        ProcessHelper.emitDiagnostic("Present health: requests=" + presentRequests.get()
                + " rendererAccepted=" + rendererAccepted.get()
                + " cpuFallback=" + drawableFallbacks.get()
                + " socketAhbRejected=" + socketAhbRejected.get()
                + " waitFence=" + waitFenceRequests.get()
                + " idleFence=" + idleFenceRequests.get()
                + " idleSent=" + idleNotifications.get()
                + describeIntervals());
    }

    private void selectInput(XClient client, XInputStream in, XOutputStream out)
            throws IOException, XRequestError {
        int eventId  = in.readInt();
        int windowId = in.readInt();
        Bitmask mask = new Bitmask(in.readInt());

        Window window = client.xServer.windowManager.getWindow(windowId);
        if (window == null) throw new BadWindow(windowId);

        if (GPUImage.isSupported() && !mask.isEmpty()) {
            Drawable content = window.getContent();
            final Texture oldTexture = content.getTexture();
            if (oldTexture != null && !(oldTexture instanceof GPUImage)) {
                XServerRenderer r = client.xServer.getRenderer();
                if (r != null)
                    r.getRendererView().queueEvent(oldTexture::destroy);
            }
            /*
             * Whatever the attached renderer can actually present.
             *
             * Naming GPUImage here is what left the scanout renderer with a
             * texture it cannot hand to SurfaceFlinger — the window presented
             * every frame and none of them reached the display.
             */
            if (!(content.getTexture() instanceof NativeTexture))
                content.setTexture(NativeTexture.create(content.width, content.height));
        }

        synchronized (events) {
            Event event = events.get(eventId);
            if (event != null) {
                if (event.window != window || event.client != client) throw new BadMatch();
                if (!mask.isEmpty()) event.mask = mask;
                else events.remove(eventId);
            } else {
                event = new Event();
                event.id     = eventId;
                event.window = window;
                event.client = client;
                event.mask   = mask;
                events.put(eventId, event);
            }
        }
    }

    /** Stops both pacing paths and drops references to the retired X clients. */
    @Override
    public void shutdown() {
        if (!closed.compareAndSet(false, true)) return;

        final android.view.Choreographer currentChoreographer;
        synchronized (choreographerLock) {
            currentChoreographer = choreographer;
            choreographer = null;
            choreographerPosted = false;
        }
        if (currentChoreographer != null) {
            try {
                currentChoreographer.removeFrameCallback(vsyncCallback);
            } catch (RuntimeException failure) {
                android.util.Log.w("PresentExtension", "Could not remove frame callback", failure);
            }
        }

        final Thread pacer;
        synchronized (cpuPacerLock) {
            cpuQueue.clear();
            pacer = cpuPacerThread;
            cpuPacerThread = null;
            cpuPacerLock.notifyAll();
        }
        if (pacer != null) {
            pacer.interrupt();
            if (pacer != Thread.currentThread()) {
                try {
                    pacer.join(250);
                } catch (InterruptedException ignored) {
                    Thread.currentThread().interrupt();
                }
            }
        }

        pendingIdles.clear();
        synchronized (choreographerLock) {
            orderedPendingIdles.clear();
        }
        windowTimings.clear();
        synchronized (events) {
            events.clear();
        }
        syncExtension = null;
    }

    @Override
    public void handleRequest(XClient client, XInputStream in, XOutputStream out)
            throws IOException, XRequestError {
        int opcode = client.getRequestData();
        if (syncExtension == null)
            syncExtension = client.xServer.getExtension(SyncExtension.MAJOR_OPCODE);

        switch (opcode) {
            case ClientOpcodes.QUERY_VERSION:
                queryVersion(client, in, out);
                break;
            case ClientOpcodes.PRESENT_PIXMAP:
                try (XLock lock = client.xServer.lock(
                        XServer.Lockable.WINDOW_MANAGER,
                        XServer.Lockable.PIXMAP_MANAGER,
                        XServer.Lockable.DRAWABLE_MANAGER)) {
                    presentPixmap(client, in, out);
                }
                break;
            case ClientOpcodes.SELECT_INPUT:
                try (XLock lock = client.xServer.lock(XServer.Lockable.WINDOW_MANAGER)) {
                    selectInput(client, in, out);
                }
                break;
            default:
                throw new BadImplementation();
        }
    }
}
