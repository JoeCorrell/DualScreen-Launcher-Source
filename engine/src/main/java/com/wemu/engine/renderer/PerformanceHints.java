package com.wemu.engine.renderer;

import android.content.Context;
import android.os.Build;
import android.os.PerformanceHintManager;
import android.os.Process;
import android.util.Log;

/**
 * Tells the kernel what this app is trying to hit, and how it is doing.
 *
 * <h2>Why it is worth the trouble</h2>
 *
 * Every CPU governor on a phone works the same way: it watches load and raises
 * clocks once it has been busy for a while. That is a reasonable rule for a
 * browser and a poor one for a game, because it is reactive — the frame that
 * caused the boost has already been missed by the time the boost arrives, and on
 * a big.LITTLE chip the scheduler may also decide a thread that spends half its
 * time waiting on the GPU belongs on a little core.
 *
 * ADPF replaces the guess with a statement. You declare the threads that do the
 * frame work and the time a frame is supposed to take, then after each frame you
 * report what it actually took. The governor then has the one thing it could
 * never infer: not how busy you were, but whether you made your deadline.
 *
 * <h2>Nothing here may throw</h2>
 *
 * This is called from the presenting thread on every frame, and an exception on
 * that path does not degrade the HUD — it takes the session down with it. That
 * is not hypothetical: an earlier version guarded this class at API 31 and then
 * called {@code setThreads}, which is API 34, catching {@code Exception}. The
 * result is {@code NoSuchMethodError}, which is an {@code Error} and not an
 * {@code Exception}, escaping through a frame callback on the first frame the
 * guest presented. The container appeared to crash instantly on launch, and the
 * cause was a performance hint.
 *
 * So: every entry point catches {@link Throwable}, every version-gated call is
 * gated at the version that introduced it, and the first failure of any kind
 * switches the whole thing off for the session. A performance hint is worth
 * having and is worth nothing at all compared to the session it is hinting
 * about.
 */
public final class PerformanceHints {

    private static final String TAG = "PerformanceHints";

    /** Null when unavailable, which is most of the interesting cases. */
    private volatile Object session;

    /**
     * Set by the first failure, and never cleared.
     *
     * A vendor implementation that throws once will throw every frame. Retrying
     * sixty times a second to log the same failure is worse than the missing
     * hint.
     */
    private volatile boolean disabled;

    private volatile long targetNanos = DEFAULT_TARGET_NANOS;
    private long frameStartedAtNanos = 0L;

    /** 60 fps, until a container says otherwise. */
    private static final long DEFAULT_TARGET_NANOS = 16_666_667L;

    /** {@code PerformanceHintManager.Session.setThreads} arrived in API 34. */
    private static final int SET_THREADS_SDK = 34;

    /**
     * Held from {@link #start} until the presenting thread turns up.
     *
     * The application context, never the activity's: this outlives a
     * configuration change and a session that keeps an activity alive after it
     * has been finished is a leak of every view in it.
     */
    private volatile Context appContext;

    /**
     * Records what to hint about. The session itself is created later.
     *
     * <h3>Why later</h3>
     *
     * A hint session is a set of <em>threads</em> and a deadline, and the whole
     * value of it is that the governor keeps the named threads on cores that can
     * make that deadline. This is called from the code that configures the
     * renderer, during setup, on whatever thread is doing the setting up — which
     * is emphatically not the thread that will present frames. That thread does
     * not exist yet.
     *
     * The original plan was to correct it afterwards with
     * {@link #attachCurrentThread}, and on API 34 and above that works. Below 34
     * there is no {@code setThreads}, so the session keeps whatever thread
     * created it, for ever. On the hardware this fork targets — an AYN Thor on
     * Android 13, API 33 — that meant every session boosted the setup thread and
     * reported the presenting thread's frame times against it: the governor was
     * being told, accurately, that something was missing its deadline, and given
     * the tid of a thread that was idle.
     *
     * So nothing is created here. The session is created on the first frame, on
     * the thread that drew it, where the right answer is simply
     * {@code Process.myTid()} and no version gate is needed to get it.
     */
    public void start(Context context, int targetFps) {
        setTargetFps(targetFps);
        if (unavailable()) return;
        if (context != null) appContext = context.getApplicationContext();
    }

    /**
     * Creates the session on the calling thread, or adds it to one that exists.
     *
     * Called from the presenting thread on its first frame, which is what makes
     * the thread list right on every API level rather than only on 34 and above.
     * The {@code setThreads} path is kept for the case where a session already
     * exists and the presenting thread has changed under it.
     */
    public void attachCurrentThread() {
        if (unavailable()) return;

        Object current = session;
        if (current == null) {
            createOn(Process.myTid());
            return;
        }

        if (Build.VERSION.SDK_INT < SET_THREADS_SDK) return;
        if (!(current instanceof PerformanceHintManager.Session)) return;

        try {
            ((PerformanceHintManager.Session) current)
                    .setThreads(new int[]{Process.myTid()});
        } catch (Throwable t) {
            // Not fatal to the session, so not a reason to switch hinting off —
            // reporting durations for the wrong thread set is still better than
            // reporting none.
            Log.d(TAG, "Could not update the hint session's threads: " + t);
        }
    }

    /** The one place a session is made, so the tid can only come from a frame. */
    private void createOn(int tid) {
        Context context = appContext;
        if (context == null) {
            // start() was never called, or was called before the service existed.
            // Hinting is an optimisation; there is nothing to recover here.
            disabled = true;
            return;
        }

        try {
            PerformanceHintManager manager =
                    context.getSystemService(PerformanceHintManager.class);
            if (manager == null) {
                disabled = true;
                return;
            }

            PerformanceHintManager.Session created =
                    manager.createHintSession(new int[]{tid}, targetNanos);
            if (created == null) {
                Log.d(TAG, "The device reports the service but declined a session");
                disabled = true;
                return;
            }
            session = created;
            Log.d(TAG, "Hint session started on the presenting thread " + tid
                    + ", target " + (targetNanos / 1_000_000f) + " ms");
        } catch (Throwable t) {
            fail("could not start a hint session", t);
        }
    }

    /** The frame budget, from the container's cap. Zero or less means 60. */
    public void setTargetFps(int fps) {
        long nanos = fps > 0 ? 1_000_000_000L / fps : DEFAULT_TARGET_NANOS;
        if (nanos == targetNanos) return;
        requestedNanos = nanos;
        settledNanos = nanos;
        targetNanos = nanos;

        if (unavailable()) return;
        Object current = session;
        if (!(current instanceof PerformanceHintManager.Session)) return;

        try {
            ((PerformanceHintManager.Session) current).updateTargetWorkDuration(nanos);
        } catch (Throwable t) {
            fail("could not update the target duration", t);
        }
    }

    /**
     * Reports the frame that just finished, and opens the next.
     *
     * Measured present-to-present rather than by bracketing the work, because
     * the work here is spread across the X server thread and the native renderer
     * and there is no single span to wrap. Present-to-present is what the
     * deadline actually is, and it is the figure the console graphs.
     */
    public void reportFrame() {
        long now = System.nanoTime();
        long started = frameStartedAtNanos;
        frameStartedAtNanos = now;
        if (started == 0L) return;

        long actual = now - started;
        // A frame that took a second is a loading screen, not a slow frame, and
        // reporting it teaches the governor nothing except to panic.
        if (actual <= 0L || actual > MAX_PLAUSIBLE_FRAME_NANOS) return;

        settleTarget(actual);

        if (unavailable()) return;
        Object current = session;
        if (!(current instanceof PerformanceHintManager.Session)) return;

        try {
            ((PerformanceHintManager.Session) current).reportActualWorkDuration(actual);
        } catch (Throwable t) {
            fail("could not report a frame duration", t);
        }
    }

    /**
     * Eases the frame budget towards one the device can actually hit.
     *
     * <h3>Why an unreachable target is worse than a slower one</h3>
     *
     * <p>ADPF is a closed loop: the governor raises clocks while the reported
     * duration exceeds the target. Hand it a 16.6ms budget on a frame that
     * genuinely takes 25ms and the error never closes, so it holds the highest
     * clocks it has, indefinitely, for a frame rate it was never going to
     * reach. On a passively cooled handheld that ends one way — the SoC heats,
     * thermal throttling cuts the clocks hard, the frame time gets worse, and
     * the loop asks for more. That is the sawtooth: not the game varying, but
     * the governor and the thermal limit taking turns.</p>
     *
     * <p>Given a budget it can meet, the same loop settles. Clocks find a level
     * that holds the frame rate, the die stays under the limit, and the frame
     * time stops moving — which is what "stable" means. A steady 40 is worth
     * more than an average of 45 that swings between 55 and 30.</p>
     *
     * <h3>Only ever slower, and only slowly</h3>
     *
     * <p>The target is allowed to relax towards the measured frame time and to
     * snap back the moment frames come in under budget, so a scene that gets
     * cheaper is not held at the pace of the last hard one. It moves a sixteenth
     * of the gap per frame: fast enough to settle within a second, slow enough
     * that one expensive frame does not lower the bar.</p>
     *
     * <p>It never relaxes past {@link #SLOWEST_TARGET_NANOS}. Below that the
     * hint has stopped describing an interactive workload, and a governor told
     * a game is content with 20fps will happily deliver exactly that.</p>
     */
    private void settleTarget(long actual) {
        long ceiling = Math.min(SLOWEST_TARGET_NANOS, Math.max(requestedNanos, actual));
        long settled = settledNanos == 0L ? requestedNanos : settledNanos;

        if (actual <= requestedNanos) {
            // Meeting the real budget: go back to asking for it outright.
            settled = requestedNanos;
        } else {
            settled += (ceiling - settled) / TARGET_SETTLE_DIVISOR;
        }

        if (settled < requestedNanos) settled = requestedNanos;
        settledNanos = settled;

        // Nothing is sent for a change too small to alter a scheduling decision;
        // this runs on every frame and the call crosses into system_server.
        if (Math.abs(settled - targetNanos) < TARGET_EPSILON_NANOS) return;
        targetNanos = settled;

        Object current = session;
        if (!(current instanceof PerformanceHintManager.Session)) return;
        try {
            ((PerformanceHintManager.Session) current).updateTargetWorkDuration(settled);
        } catch (Throwable t) {
            fail("could not settle the target duration", t);
        }
    }

    /** What the container asked for, before any settling. */
    private volatile long requestedNanos = DEFAULT_TARGET_NANOS;
    private volatile long settledNanos = 0L;

    /** 20 fps. Below this the hint stops describing an interactive workload. */
    private static final long SLOWEST_TARGET_NANOS = 50_000_000L;

    /** A sixteenth of the gap per frame: settles in about a second at 60fps. */
    private static final int TARGET_SETTLE_DIVISOR = 16;

    /** Half a millisecond, below which an update cannot change a decision. */
    private static final long TARGET_EPSILON_NANOS = 500_000L;

    public void close() {
        Object current = session;
        session = null;
        // Cleared so a frame arriving after the session has ended cannot make
        // the lazy path build a second one. createOn treats a missing context as
        // "switch hinting off", which is the right answer for a dead session.
        appContext = null;
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) return;
        if (!(current instanceof PerformanceHintManager.Session)) return;

        try {
            ((PerformanceHintManager.Session) current).close();
        } catch (Throwable ignored) {
        }
    }

    private boolean unavailable() {
        return disabled || Build.VERSION.SDK_INT < Build.VERSION_CODES.S;
    }

    private void fail(String what, Throwable t) {
        disabled = true;
        session = null;
        Log.w(TAG, "Performance hints off for this session: " + what, t);
    }

    /** Quarter of a second. Anything slower is not a frame. */
    private static final long MAX_PLAUSIBLE_FRAME_NANOS = 250_000_000L;
}
