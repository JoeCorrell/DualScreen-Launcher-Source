package com.wemu.runtime.internal;

import android.content.Context;

import com.wemu.runtime.install.RuntimeInstallListener;
import com.wemu.runtime.install.RuntimeInstallStage;
import com.wemu.runtime.install.RuntimeInstaller;
import com.wemu.engine.xenvironment.ImageFsInstaller;

import java.io.IOException;

/**
 * Bridges Wemu's stable install API to the current image extractor.
 *
 * The stage enums are kept name-for-name on purpose, so the translation is a
 * {@code valueOf} rather than a mapping table that can fall out of step.
 */
public final class WemuRuntimeInstaller implements RuntimeInstaller {

    public static final WemuRuntimeInstaller INSTANCE = new WemuRuntimeInstaller();

    private WemuRuntimeInstaller() { }

    @Override
    public int getLatestImageVersion() {
        return ImageFsInstaller.LATEST_VERSION;
    }

    @Override
    public boolean isInstallNeeded(Context context) {
        return ImageFsInstaller.isInstallNeeded(context);
    }

    @Override
    public boolean install(Context context, RuntimeInstallListener listener) throws IOException {
        return ImageFsInstaller.install(context, listener == null ? null : new ImageFsInstaller.Listener() {
            @Override
            public void onStage(ImageFsInstaller.Stage stage) {
                listener.onStage(RuntimeInstallStage.valueOf(stage.name()));
            }

            @Override
            public void onProgress(int percent) {
                listener.onProgress(percent);
            }
        });
    }
}
