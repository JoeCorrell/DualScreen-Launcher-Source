package com.wemu.runtime;

import android.content.Context;

import com.wemu.runtime.install.RuntimeInstaller;
import com.wemu.runtime.storage.RuntimeStorage;
import com.wemu.engine.XServerHost;
import com.wemu.engine.container.Container;
import com.wemu.engine.container.Shortcut;

/** Creates isolated runtime sessions without exposing their implementation. */
public interface RuntimeEngine {

    String getId();

    RuntimeInstaller getInstaller();

    RuntimeStorage getStorage();

    RuntimeSession createSession(
            Context context,
            XServerHost host,
            Container container,
            Shortcut shortcut);
}
