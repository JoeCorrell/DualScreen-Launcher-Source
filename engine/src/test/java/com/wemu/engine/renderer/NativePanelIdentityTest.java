package com.wemu.engine.renderer;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class NativePanelIdentityTest {
    @Test
    public void ac2ClaimsOnlyThePrivateAnimusWindow() {
        assertTrue(NativePanelIdentity.isEarlyClaimPanel("Ac2SecondScreenPanel", ""));
        assertTrue(NativePanelIdentity.isAssassinsCreedII(
                "assassinscreediigame.exe", "Assassin's Creed II Dual-Screen Animus"));
        assertFalse(NativePanelIdentity.isEarlyClaimPanel(
                "assassinscreediigame.exe", "Assassin's Creed II"));
        assertFalse(NativePanelIdentity.isAssassinsCreedII("Ac2SecondScreenPanelDebug", ""));
        assertFalse(NativePanelIdentity.isAssassinsCreedII(
                "", "Assassin's Creed II Dual-Screen Animus Settings"));
    }

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

    @Test
    public void fallout3PanelUsesItsExactPrivateTitle() {
        assertTrue(NativePanelIdentity.isFallout(
                "fallout3.exe", "Fallout 3 Dual-Screen Pip-Boy"));
        assertTrue(NativePanelIdentity.isEarlyClaimPanel(
                "", "  FALLOUT 3 DUAL-SCREEN PIP-BOY  "));
        assertFalse(NativePanelIdentity.isFallout(
                "fallout3.exe", "Fallout 3"));
    }

    @Test
    public void stardewPanelUsesOnlyItsPrivateClassOrExactTitle() {
        assertTrue(NativePanelIdentity.isStardewValley(
                "StardewValleySecondScreenPanel", ""));
        assertTrue(NativePanelIdentity.isStardewValley(
                "", "Stardew Valley Dual Screen"));
        assertTrue(NativePanelIdentity.isEarlyClaimPanel(
                "StardewValleySecondScreenPanel", "Stardew Valley Dual Screen"));
        assertFalse(NativePanelIdentity.isStardewValley(
                "StardewValleySecondScreenPanelDebug", ""));
        assertFalse(NativePanelIdentity.isStardewValley(
                "", "Stardew Valley Dual Screen Settings"));
        assertFalse(NativePanelIdentity.isStardewValley(
                "Stardew Valley", "Stardew Valley"));
    }

    @Test
    public void transformersPanelUsesExactPrivateIdentityAndEarlyClaim() {
        assertTrue(NativePanelIdentity.isTransformersWfc(
                "TransformersWfcSecondScreenPanel", ""));
        assertTrue(NativePanelIdentity.isTransformersWfc(
                "", "Transformers: War for Cybertron Dual-Screen"));
        assertTrue(NativePanelIdentity.isEarlyClaimPanel(
                "TransformersWfcSecondScreenPanel", ""));
        assertFalse(NativePanelIdentity.isTransformersWfc(
                "TransformersWfcSecondScreenPanelDebug", ""));
        assertFalse(NativePanelIdentity.isTransformersWfc(
                "TWFC", "Transformers: War for Cybertron"));
    }

    @Test
    public void transformersExecutableFallbackRequiresExactLowerMonitorSize() {
        assertTrue(NativePanelIdentity.isTransformersWfcLowerPanelFallback(
                "twfc.exe", 1240, 1080, 1240, 1080));
        assertTrue(NativePanelIdentity.isTransformersWfcLowerPanelFallback(
                "  TWFC.EXE  ", 1240, 1080, 1240, 1080));
        assertFalse(NativePanelIdentity.isTransformersWfcLowerPanelFallback(
                "twfc.exe", 640, 480, 1240, 1080));
        assertFalse(NativePanelIdentity.isTransformersWfcLowerPanelFallback(
                "explorer.exe", 1240, 1080, 1240, 1080));
        assertFalse(NativePanelIdentity.isTransformersWfcLowerPanelFallback(
                "twfc.exe", 0, 0, 0, 0));
    }

    @Test
    public void isaacPanelUsesExactPrivateIdentityAndEarlyClaim() {
        assertTrue(NativePanelIdentity.isIsaac(
                "IsaacSecondScreenPanel", ""));
        assertTrue(NativePanelIdentity.isIsaac(
                "", "The Binding of Isaac: Repentance+ Dual-Screen"));
        assertTrue(NativePanelIdentity.isEarlyClaimPanel(
                "IsaacSecondScreenPanel", ""));
        assertFalse(NativePanelIdentity.isIsaac(
                "IsaacSecondScreenPanelDebug", ""));
        assertFalse(NativePanelIdentity.isIsaac(
                "isaac-ng", "The Binding of Isaac: Repentance+"));
    }
}
