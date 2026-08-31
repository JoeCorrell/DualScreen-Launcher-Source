package com.wemu.runtime.internal;

import android.content.Context;

import com.wemu.runtime.storage.RuntimeStorage;
import com.wemu.engine.container.ContainerManager;
import com.wemu.engine.contents.ContentsManager;

/** Current on-disk Wemu catalog implementation. */
public final class WemuRuntimeStorage implements RuntimeStorage {

    public static final WemuRuntimeStorage INSTANCE = new WemuRuntimeStorage();

    private WemuRuntimeStorage() { }

    @Override
    public ContainerManager openContainers(Context context) {
        return new ContainerManager(context.getApplicationContext());
    }

    @Override
    public ContentsManager openContents(Context context) {
        return new ContentsManager(context.getApplicationContext());
    }
}
