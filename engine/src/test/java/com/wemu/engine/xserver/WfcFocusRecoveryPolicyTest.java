package com.wemu.engine.xserver;

import static org.junit.Assert.*;
import org.junit.Test;

public class WfcFocusRecoveryPolicyTest {
    @Test public void remappedGameRecoversRootAndStaleFocus() {
        assertTrue(WfcFocusRecoveryPolicy.shouldRecover(true, true, false, true, true, false, false));
        assertTrue(WfcFocusRecoveryPolicy.shouldRecover(true, true, false, false, false, false, false));
        assertTrue(WfcFocusRecoveryPolicy.shouldRecover(true, true, false, false, true, true, false));
    }
    @Test public void neverStealsDialogsOrChangesOtherGames() {
        assertFalse(WfcFocusRecoveryPolicy.shouldRecover(true, true, false, false, true, false, false));
        assertFalse(WfcFocusRecoveryPolicy.shouldRecover(false, true, false, true, true, false, false));
        assertFalse(WfcFocusRecoveryPolicy.shouldRecover(true, false, false, true, true, false, false));
        assertFalse(WfcFocusRecoveryPolicy.shouldRecover(true, true, false, true, true, false, true));
    }
    @Test public void alreadyFocusedClientDoesNotRepeatForegroundRequests() {
        assertFalse(WfcFocusRecoveryPolicy.shouldRecover(true, true, true, false, true, false, false));
    }
}
