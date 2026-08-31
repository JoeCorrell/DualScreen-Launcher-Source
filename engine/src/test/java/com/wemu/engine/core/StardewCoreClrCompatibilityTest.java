package com.wemu.engine.core;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

import org.junit.Test;

public class StardewCoreClrCompatibilityTest {
    @Test
    public void recognizesVanillaAndSmapiPathsWithoutMatchingOtherGames() {
        assertTrue(StardewCoreClrCompatibility.isTargetExecutable(
                "F:\\Games\\Stardew Valley\\Stardew Valley.exe"));
        assertTrue(StardewCoreClrCompatibility.isTargetExecutable(
                "\"F:\\Games\\Stardew Valley\\StardewModdingAPI.exe\""));
        assertTrue(StardewCoreClrCompatibility.isTargetExecutable(
                "/games/STArdew valley.EXE"));
        assertTrue(StardewCoreClrCompatibility.isTargetExecutable(
                "F:\\Games\\Stardew Valley\\Stardew Valley.exe --skip-terminal"));
        assertFalse(StardewCoreClrCompatibility.isTargetExecutable(
                "F:\\Games\\Skyrim Special Edition\\SkyrimSE.exe"));
        assertFalse(StardewCoreClrCompatibility.isTargetExecutable(
                "F:\\Games\\Stardew Valley\\Stardew Valley.dll"));
        assertFalse(StardewCoreClrCompatibility.isTargetExecutable(null));
    }

    @Test
    public void directEnvironmentOverridesUnsafeFexPerformanceValues() {
        EnvVars environment = new EnvVars();
        environment.put("FEX_TSOENABLED", "0");
        environment.put("FEX_MULTIBLOCK", "1");

        assertTrue(StardewCoreClrCompatibility.applyTo(
                environment, "F:\\Games\\Stardew Valley\\Stardew Valley.exe"));
        assertEquals("0", environment.get("DOTNET_EnableWriteXorExecute"));
        assertEquals("1", environment.get("COREHOST_TRACE"));
        assertEquals("C:\\wemu-stardew-corehost.log", environment.get("COREHOST_TRACEFILE"));
        assertEquals("4", environment.get("COREHOST_TRACE_VERBOSITY"));
        assertEquals("1", environment.get("FEX_TSOENABLED"));
        assertEquals("0", environment.get("FEX_VECTORTSOENABLED"));
        assertEquals("0", environment.get("FEX_MEMCPYSETTSOENABLED"));
        assertEquals("0", environment.get("FEX_MULTIBLOCK"));
        assertEquals("1", environment.get("FEX_SMCCHECKS"));
        assertEquals("safest", environment.get("BOX64_PROFILE"));
        assertEquals("0", environment.get("BOX64_DYNAREC_BIGBLOCK"));
        assertEquals("4", environment.get("BOX64_DYNAREC_STRONGMEM"));
    }

    @Test
    public void unrelatedProgramKeepsItsEnvironment() {
        EnvVars environment = new EnvVars();
        environment.put("FEX_TSOENABLED", "0");

        assertFalse(StardewCoreClrCompatibility.applyTo(
                environment, "F:\\Games\\Skyrim\\SkyrimSE.exe"));
        assertEquals("0", environment.get("FEX_TSOENABLED"));
        assertFalse(environment.has("DOTNET_EnableWriteXorExecute"));
    }

    @Test
    public void batchEnvironmentPrecedesTheStartCommandAndIsQuoted() {
        StringBuilder batch = new StringBuilder("@echo off\r\n");
        assertTrue(StardewCoreClrCompatibility.appendBatchEnvironment(
                batch, "F:\\Games\\Stardew Valley\\StardewModdingAPI.exe"));
        batch.append("start \"\" game.exe\r\n");

        String contents = batch.toString();
        assertTrue(contents.contains("set \"DOTNET_EnableWriteXorExecute=0\"\r\n"));
        assertTrue(contents.contains("set \"FEX_TSOENABLED=1\"\r\n"));
        assertTrue(contents.contains("set \"FEX_VECTORTSOENABLED=0\"\r\n"));
        assertTrue(contents.contains("set \"FEX_SMCCHECKS=1\"\r\n"));
        assertTrue(contents.contains("set \"COREHOST_TRACE=1\"\r\n"));
        assertTrue(contents.indexOf("set \"DOTNET_EnableWriteXorExecute=0\"")
                < contents.indexOf("start \"\""));
    }

    @Test
    public void hiddenLauncherWaitsForTheChildAndRecordsItsExitCode() {
        String contents = StardewCoreClrCompatibility.hiddenLauncherScriptContents(
                "F:\\Games\\Stardew Valley\\Stardew Valley.exe",
                "--skip-terminal");

        assertTrue(contents.contains("Set shell = CreateObject(\"WScript.Shell\")"));
        assertTrue(contents.contains("environment(\"COREHOST_TRACE\") = \"1\""));
        assertTrue(contents.contains("shell.CurrentDirectory = \"F:\\Games\\Stardew Valley\""));
        assertTrue(contents.contains(
                "exitCode = shell.Run(\"\"\"F:\\Games\\Stardew Valley\\Stardew Valley.exe\"\" --skip-terminal\", 1, True)"));
        assertTrue(contents.contains("Stardew child exit code \" & CStr(exitCode)"));
        assertFalse(contents.toLowerCase().contains("cmd.exe"));
        assertEquals(null, StardewCoreClrCompatibility.hiddenLauncherScriptContents(
                "F:\\Games\\Skyrim\\SkyrimSE.exe", ""));
    }

    @Test
    public void fexApplicationLayerIsInstalledForGenericAndSteamNames() throws IOException {
        File imageFs = Files.createTempDirectory("stardew-fex-imagefs").toFile();
        File home = new File(imageFs, "home/xuser-2");
        try {
            assertEquals(18, StardewCoreClrCompatibility.installFexAppConfigs(imageFs, home));

            String expected = StardewCoreClrCompatibility.fexAppConfigContents();
            File generic = new File(home, ".fex-emu/AppConfig/Stardew Valley.exe.json");
            File steam = new File(
                    imageFs, "AppConfig/Steam_413150_StardewModdingAPI.exe.json");
            assertEquals(expected, new String(
                    Files.readAllBytes(generic.toPath()), StandardCharsets.UTF_8));
            assertEquals(expected, new String(
                    Files.readAllBytes(steam.toPath()), StandardCharsets.UTF_8));
            assertTrue(expected.contains("\"TSOEnabled\": \"1\""));
            assertTrue(expected.contains("\"Multiblock\": \"0\""));
            assertTrue(expected.contains("\"SMCChecks\": \"1\""));
            assertTrue(expected.contains("\"OutputLog\": \"stderr\""));
        } finally {
            deleteRecursively(imageFs);
        }
    }

    private static void deleteRecursively(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) deleteRecursively(child);
        }
        if (!file.delete()) file.deleteOnExit();
    }
}
