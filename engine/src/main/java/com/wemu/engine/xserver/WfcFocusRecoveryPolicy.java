package com.wemu.engine.xserver;

/** Only repair a lost target; never override a live dialog or another app. */
public final class WfcFocusRecoveryPolicy {
    private WfcFocusRecoveryPolicy() {}

    public static boolean shouldRecover(boolean wfcPresented, boolean targetViewable,
            boolean currentIsTargetFamily, boolean currentMissingOrRoot,
            boolean currentViewable, boolean currentIsLowerPanel, boolean grabbed) {
        return wfcPresented && targetViewable && !grabbed && !currentIsTargetFamily
                && (currentMissingOrRoot || !currentViewable || currentIsLowerPanel);
    }
}
