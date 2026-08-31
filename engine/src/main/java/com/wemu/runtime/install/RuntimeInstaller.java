package com.wemu.runtime.install;

import android.content.Context;

import java.io.IOException;

/**
 * Installs and validates the files shared by Wemu runtime sessions.
 *
 * This interface deliberately says nothing about <em>where</em> the runtime is
 * laid down, and callers must not assume it lands somewhere they can execute.
 * Today it unpacks into the app's data directory and the guest binaries are
 * {@code execve}'d from there, which is the single reason {@code targetSdk} is
 * pinned at 28 — from API 29 the platform refuses to execute anything under
 * {@code /data/data}. Raising it means changing how the runtime is staged and
 * loaded, and this is the seam where that decision belongs, so it can be swapped
 * for one implementation rather than chased through the whole engine.
 */
public interface RuntimeInstaller {
    int getLatestImageVersion();

    boolean isInstallNeeded(Context context);

    /** Performs a blocking install on the calling thread. */
    /**
     * Runs the install on the calling thread.
     *
     *  IOException when a payload that is not in the APK could not be
     *   fetched. Declared rather than folded into the boolean because the reason
     *   decides what the user is told to do about it -- see
     *   {@code PayloadUnavailableException}.
     */
    boolean install(Context context, RuntimeInstallListener listener) throws IOException;
}
