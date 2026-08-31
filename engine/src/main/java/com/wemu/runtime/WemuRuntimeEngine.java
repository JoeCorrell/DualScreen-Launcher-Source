package com.wemu.runtime;

import android.content.Context;

import com.wemu.runtime.install.RuntimeInstaller;
import com.wemu.runtime.internal.WemuRuntimeInstaller;
import com.wemu.runtime.internal.WemuRuntimeStorage;
import com.wemu.runtime.internal.WemuSession;
import com.wemu.runtime.storage.RuntimeStorage;
import com.wemu.engine.XServerHost;
import com.wemu.engine.container.Container;
import com.wemu.engine.container.Shortcut;

/**
 * Entry point for Wemu's runtime.
 *
 * This singleton is deliberately tiny: backend selection belongs here rather
 * than in an Activity, so a second implementation can be benchmarked without
 * changing application code or container data.
 */
public final class WemuRuntimeEngine implements RuntimeEngine {

    public static final WemuRuntimeEngine INSTANCE = new WemuRuntimeEngine();

    private WemuRuntimeEngine() { }

    @Override
    public String getId() {
        return "wemu";
    }

    @Override
    public RuntimeInstaller getInstaller() {
        return WemuRuntimeInstaller.INSTANCE;
    }

    @Override
    public RuntimeStorage getStorage() {
        return WemuRuntimeStorage.INSTANCE;
    }

    @Override
    public RuntimeSession createSession(
            Context context,
            XServerHost host,
            Container container,
            Shortcut shortcut) {
        return new WemuSession(context, host, container, shortcut);
    }
}
