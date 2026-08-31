package com.wemu.engine.renderer;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class NativePanelIdentityTest {
    @Test
    public void grimDawnClassAndTitleAreRecognizedExactly() {
        assertTrue(NativePanelIdentity.isGrimDawn(
                "GrimDawnSecondScreenPanel", ""));
        assertTrue(NativePanelIdentity.isGrimDawn(
                "", "Grim Dawn Dual-Screen"));
        assertTrue(NativePanelIdentity.isGrimDawn(
                "  GRIMDAWNSECONDSCREENPANEL  ", ""));
    }

    @Test
    public void similarWindowsAreNotClaimed() {
        assertFalse(NativePanelIdentity.isGrimDawn(
                "GrimDawnSecondScreenPanelDebug", ""));
        assertFalse(NativePanelIdentity.isGrimDawn(
                "", "Grim Dawn Dual-Screen Settings"));
        assertFalse(NativePanelIdentity.isGrimDawn(
                "Grim Dawn", "Grim Dawn"));
    }

    @Test
    public void grimDawnParticipatesInEarlyLowerOnlyClaim() {
        assertTrue(NativePanelIdentity.isEarlyClaimPanel(
                "GrimDawnSecondScreenPanel", "Grim Dawn Dual-Screen"));
    }

    @Test
    public void fallout4PanelIsRecognizedWithoutClaimingTheGameWindow() {
        assertTrue(NativePanelIdentity.isFallout(
                "Fallout4SecondScreenPanel", "Fallout 4 Dual-Screen Pip-Boy"));
        assertTrue(NativePanelIdentity.isEarlyClaimPanel(
                "Fallout4SecondScreenPanel", "Fallout 4 Dual-Screen Pip-Boy"));
        assertFalse(NativePanelIdentity.isFallout("Fallout4", "Fallout 4"));
    }
}
