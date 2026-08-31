package com.wemu.runtime.install;

/** Receives install progress on the thread performing the installation. */
public interface RuntimeInstallListener {
    void onStage(RuntimeInstallStage stage);

    /** Progress from 0 through 100 within the current stage. */
    void onProgress(int percent);
}
