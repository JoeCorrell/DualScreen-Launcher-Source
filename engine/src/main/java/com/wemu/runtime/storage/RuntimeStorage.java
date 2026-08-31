package com.wemu.runtime.storage;

import android.content.Context;

import com.wemu.engine.container.ContainerManager;
import com.wemu.engine.contents.ContentsManager;

/** Opens the persistent catalogs used by the selected runtime implementation. */
public interface RuntimeStorage {
    ContainerManager openContainers(Context context);

    ContentsManager openContents(Context context);
}
