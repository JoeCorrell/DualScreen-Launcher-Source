package com.wemu.runtime.install;

import com.wemu.engine.R;

/** User-visible phases of preparing the Windows runtime image. */
public enum RuntimeInstallStage {
    DOWNLOADING_SYSTEM_FILES(R.string.downloading_system_files),
    DOWNLOADING_WINE_FILES(R.string.downloading_wine_files),
    DOWNLOADING_CONTAINER_FILES(R.string.downloading_container_files),
    SYSTEM_FILES(R.string.installing_system_files),
    WINE_FILES(R.string.installing_wine_files),
    GRAPHICS_DRIVERS(R.string.installing_wine_files);

    public final int labelResId;

    RuntimeInstallStage(int labelResId) {
        this.labelResId = labelResId;
    }
}
