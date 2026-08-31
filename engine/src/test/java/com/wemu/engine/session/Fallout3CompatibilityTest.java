package com.wemu.engine.session;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.wemu.engine.core.EnvVars;

import org.junit.Test;
import org.junit.Rule;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.LinkedHashMap;

public class Fallout3CompatibilityTest {
    @Rule
    public final TemporaryFolder temporary = new TemporaryFolder();

    @Test
    public void recognizesOnlyTheFalloutLaunchChain() {
        assertTrue(Fallout3Compatibility.isGameExecutable(
                "F:\\Games\\Fallout 3\\Fallout3.exe"));
        assertTrue(Fallout3Compatibility.isLaunchChainExecutable(
                "\"F:\\Games\\Fallout 3\\FalloutLauncherSteam.exe\""));
        assertTrue(Fallout3Compatibility.isLaunchChainExecutable(
                "F:\\Games\\Fallout 3\\FalloutLauncher.exe"));
        assertFalse(Fallout3Compatibility.isGameExecutable(
                "F:\\Games\\Fallout 3\\FalloutLauncher.exe"));
        assertFalse(Fallout3Compatibility.isLaunchChainExecutable(
                "F:\\Games\\Skyrim\\SkyrimSE.exe"));
    }

    @Test
    public void directShortcutKeepsExistingDxvkOptions() {
        EnvVars environment = new EnvVars();
        environment.put("DXVK_CONFIG", "dxvk.numCompilerThreads = 2");

        assertTrue(Fallout3Compatibility.applyDxvkEnvironment(
                environment, "C:\\Games\\Fallout3.exe"));
        assertEquals(
                "dxvk.numCompilerThreads = 2; "
                        + Fallout3Compatibility.DXVK_OPTION,
                environment.get("DXVK_CONFIG"));
        assertFalse(Fallout3Compatibility.applyDxvkEnvironment(
                environment, "C:\\Games\\SkyrimSE.exe"));
    }

    @Test
    public void workaroundIsNotDuplicated() {
        EnvVars environment = new EnvVars();
        environment.put("DXVK_CONFIG", Fallout3Compatibility.DXVK_OPTION);
        Fallout3Compatibility.applyDxvkEnvironment(environment, "Fallout3.exe");
        assertEquals(Fallout3Compatibility.DXVK_OPTION,
                environment.get("DXVK_CONFIG"));
    }

    @Test
    public void existingPipelineWorkaroundStillGainsFrameLimiter() {
        assertEquals(
                "dxvk.enableGraphicsPipelineLibrary=False; d3d9.maxFrameRate = 60",
                Fallout3Compatibility.mergeDxvkConfig(
                        "dxvk.enableGraphicsPipelineLibrary=False"));
    }

    @Test
    public void launcherBatchExportsPolicyToItsChild() {
        StringBuilder batch = new StringBuilder("@echo off\r\n");
        assertTrue(Fallout3Compatibility.appendBatchEnvironment(
                batch, "F:\\Fallout 3\\FalloutLauncher.exe"));
        batch.append("start \"\" FalloutLauncher.exe\r\n");
        String contents = batch.toString();
        assertTrue(contents.contains(
                "set \"DXVK_CONFIG=%DXVK_CONFIG%; "
                        + Fallout3Compatibility.DXVK_OPTION + "\"\r\n"));
        assertTrue(contents.indexOf("set \"DXVK_CONFIG=")
                < contents.indexOf("start \"\""));
    }

    @Test
    public void actorShadowPatchOnlyChangesDisplayActorSettings() {
        String input = "[Display]\r\n"
                + "bDrawShadows=1\r\n"
                + "bDoActorShadows=1\r\n"
                + "iActorShadowCount=4\r\n"
                + "[Other]\r\n"
                + "iActorShadowCount=9\r\n";
        LinkedHashMap<String, String> values = new LinkedHashMap<>();
        values.put("bDoActorShadows", "0");
        values.put("iActorShadowCount", "0");
        values.put("iActorShadowCountInt", "0");

        assertEquals("[Display]\r\n"
                        + "bDrawShadows=1\r\n"
                        + "bDoActorShadows=0\r\n"
                        + "iActorShadowCount=0\r\n"
                        + "iActorShadowCountInt=0\r\n"
                        + "[Other]\r\n"
                        + "iActorShadowCount=9\r\n",
                Fallout3Compatibility.patchDisplaySettings(input, values));
    }

    @Test
    public void actorShadowPatchIsIdempotent() {
        LinkedHashMap<String, String> values = new LinkedHashMap<>();
        values.put("iActorShadowCountInt", "0");
        values.put("iActorShadowCountExt", "0");
        String input = "[Display]\n"
                + "iActorShadowCountInt=0\n"
                + "iActorShadowCountExt=0\n";
        assertEquals(input,
                Fallout3Compatibility.patchDisplaySettings(input, values));
    }

    @Test
    public void genericSectionPatchPreservesOtherSections() {
        LinkedHashMap<String, String> values = new LinkedHashMap<>();
        values.put("bUseAudioDebugInformation", "0");
        values.put("bEnableAudioCache", "1");
        String input = "[Display]\n"
                + "iSize W=1280\n"
                + "[Audio]\n"
                + "bUseAudioDebugInformation=1\n";

        assertEquals("[Display]\n"
                        + "iSize W=1280\n"
                        + "[Audio]\n"
                        + "bUseAudioDebugInformation=0\n"
                        + "bEnableAudioCache=1\n",
                Fallout3Compatibility.patchSectionSettings(input, "Audio", values));
    }

    @Test
    public void runtimeProfileBypassesOnlyTheBrokenLegacyModeCheck() throws Exception {
        File prefix = temporary.newFolder("prefix");
        File settings = new File(
                prefix,
                "drive_c/users/xuser/Documents/My Games/Fallout3");
        assertTrue(settings.mkdirs());
        File falloutIni = new File(settings, "FALLOUT.INI");
        File prefsIni = new File(settings, "FalloutPrefs.ini");
        Files.write(
                falloutIni.toPath(),
                ("[Display]\n"
                        + "bIgnoreResolutionCheck=0\n"
                        + "iSize W=1280\n"
                        + "iSize H=720\n").getBytes(StandardCharsets.UTF_8));
        Files.write(
                prefsIni.toPath(),
                ("[Display]\n"
                        + "iSize W=1280\n"
                        + "iSize H=720\n"
                        + "bFull Screen=1\n").getBytes(StandardCharsets.UTF_8));

        assertTrue(Fallout3Compatibility.applyRuntimeSettings(prefix));

        String engine = new String(
                Files.readAllBytes(falloutIni.toPath()), StandardCharsets.UTF_8);
        String preferences = new String(
                Files.readAllBytes(prefsIni.toPath()), StandardCharsets.UTF_8);
        assertTrue(engine.contains("bIgnoreResolutionCheck=1"));
        assertTrue(engine.contains("iSize W=1280"));
        assertTrue(engine.contains("iSize H=720"));
        assertTrue(preferences.contains("iSize W=1280"));
        assertTrue(preferences.contains("iSize H=720"));
        assertTrue(preferences.contains("bFull Screen=1"));
        assertTrue(engine.contains("[Controls]\nbUse Joystick=1"));
        assertTrue(engine.contains("bGamePadRumble=1"));
        assertTrue(engine.contains("[Interface]\nbDisable360Controller=0"));
        assertTrue(preferences.contains("[Controls]\nbUse Joystick=1"));
        assertTrue(preferences.contains("[Interface]\nbDisable360Controller=0"));
    }
}
