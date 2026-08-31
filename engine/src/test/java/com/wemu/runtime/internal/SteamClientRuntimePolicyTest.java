package com.wemu.runtime.internal;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

import org.junit.Test;

public class SteamClientRuntimePolicyTest {
    @Test
    public void officialSteamOnX8664UsesDedicatedBox64Profile() {
        assertTrue(SteamClientRuntimePolicy.shouldUseOfficialSteamBox64Profile(
                true, false));
    }

    @Test
    public void arm64EcAndOrdinaryLaunchesDoNotUseBox64Profile() {
        assertFalse(SteamClientRuntimePolicy.shouldUseOfficialSteamBox64Profile(
                true, true));
        assertFalse(SteamClientRuntimePolicy.shouldUseOfficialSteamBox64Profile(
                false, false));
    }

    @Test
    public void officialWindowsSteamUsesItsOwnClientApiDlls() {
        assertTrue(SteamClientRuntimePolicy.shouldDisableProtonLsteamclient(
                true, null));
        assertTrue(SteamClientRuntimePolicy.shouldDisableProtonLsteamclient(
                true, "OFF"));
    }

    @Test
    public void ordinaryGamesKeepTheirSelectedProtonClientPolicy() {
        assertFalse(SteamClientRuntimePolicy.shouldDisableProtonLsteamclient(
                false, null));
        assertFalse(SteamClientRuntimePolicy.shouldDisableProtonLsteamclient(
                false, "OFF"));
        assertTrue(SteamClientRuntimePolicy.shouldDisableProtonLsteamclient(
                false, "COLDCLIENT"));
    }

    @Test
    public void box64ProfileIsPerProcessAndDoesNotWeakenTheBrowserSandbox() {
        String profile = SteamClientRuntimePolicy.officialSteamBox64ProfileContents(true);
        String safeCoreClrProfile = "BOX64_ENV=WINEDLLOVERRIDES=icu=n\n"
                + "BOX64_ENV1=DOTNET_EnableWriteXorExecute=0\n"
                + "BOX64_PROFILE=safest\n"
                + "BOX64_DYNAREC_BIGBLOCK=0\n"
                + "BOX64_DYNAREC_NOHOTPAGE=0\n"
                + "BOX64_DYNAREC_WAIT=1\n"
                + "BOX64_DYNAREC_STRONGMEM=4\n"
                + "BOX64_DYNAREC_SAFEFLAGS=2\n"
                + "BOX64_DYNAREC_CALLRET=0\n"
                + "BOX64_DYNAREC_FORWARD=0\n"
                + "BOX64_DYNAREC_INTERP_SIGNAL=1\n"
                + "BOX64_MAXCPU=4\n"
                + "BOX64_LOG=1\n"
                + "BOX64_SHOWSEGV=1\n"
                + "BOX64_SHOWBT=1\n";

        assertTrue(profile.contains("[steam.exe]\n"));
        assertTrue(profile.contains("[steamwebhelper.exe]\n"));
        assertTrue(profile.contains("[Stardew Valley.exe]\n" + safeCoreClrProfile));
        assertTrue(profile.contains("[StardewModdingAPI.exe]\n" + safeCoreClrProfile));
        assertEquals(2, occurrences(profile, "BOX64_ENV=WINEDLLOVERRIDES=icu=n"));
        assertEquals(2, occurrences(profile, "BOX64_DYNAREC_BIGBLOCK=0"));
        assertEquals(2, occurrences(profile, "BOX64_DYNAREC_NOHOTPAGE=0"));
        assertFalse(profile.contains("BOX64_DYNAREC_NOHOTPAGE=1"));
        assertEquals(2, occurrences(profile,
                "BOX64_ENV1=DOTNET_EnableWriteXorExecute=0"));
        assertEquals(2, occurrences(profile, "BOX64_PROFILE=safest"));
        assertEquals(2, occurrences(profile, "BOX64_DYNAREC_WAIT=1"));
        assertEquals(2, occurrences(profile, "BOX64_DYNAREC_STRONGMEM=4"));
        assertEquals(2, occurrences(profile, "BOX64_DYNAREC_SAFEFLAGS=2"));
        assertEquals(2, occurrences(profile, "BOX64_DYNAREC_CALLRET=0"));
        assertEquals(2, occurrences(profile, "BOX64_DYNAREC_FORWARD=0"));
        assertEquals(2, occurrences(profile, "BOX64_DYNAREC_INTERP_SIGNAL=1"));
        assertEquals(2, occurrences(profile, "BOX64_SHOWSEGV=1"));
        assertEquals(2, occurrences(profile, "BOX64_SHOWBT=1"));
        assertTrue(profile.contains("[steamwebhelper.exe]\n"
                + "BOX64_DYNAREC_STRONGMEM=1\n"
                + "BOX64_DYNAREC_BIGBLOCK=3\n"
                + "BOX64_DYNAREC_CALLRET=1\n"
                + "BOX64_DYNAREC_FASTROUND=0\n"
                + "BOX64_DYNAREC_SAFEFLAGS=0\n"
                + "BOX64_DYNAREC_PURGE=1\n"
                + "BOX64_AVX=0\n"));
        assertFalse(profile.contains("BOX64_NOGTK="));
        assertEquals(1, occurrences(profile, "BOX64_DYNAREC_PURGE=1"));
        assertFalse(profile.contains("BOX64_LIBCEF="));
        assertFalse(profile.contains("MESA_VK_WSI_DEBUG="));
        assertFalse(profile.contains("BOX64_INSERT_ARGS"));
        assertFalse(profile.contains("disable-gpu"));
        assertFalse(profile.contains("swiftshader"));
        assertFalse(profile.toLowerCase().contains("no-sandbox"));
        assertFalse(profile.toLowerCase().contains("disable-gpu-sandbox"));
        assertFalse(profile.toLowerCase().contains("password"));
        assertFalse(profile.toLowerCase().contains("token"));
        assertFalse(SteamClientRuntimePolicy.officialSteamBox64ProfileContents(false)
                .contains("BOX64_INSERT_ARGS"));
    }

    @Test
    public void profileIsAtomicallyInstalledAndRepairsStaleContent() throws IOException {
        File directory = Files.createTempDirectory("steam-box64-profile").toFile();
        try {
            File profile = SteamClientRuntimePolicy.ensureOfficialSteamBox64Profile(directory, true);
            assertEquals(
                    SteamClientRuntimePolicy.officialSteamBox64ProfileContents(true),
                    new String(Files.readAllBytes(profile.toPath()), StandardCharsets.UTF_8));

            Files.write(profile.toPath(), "stale".getBytes(StandardCharsets.UTF_8));
            File repaired = SteamClientRuntimePolicy.ensureOfficialSteamBox64Profile(directory, true);
            assertEquals(profile.getCanonicalFile(), repaired.getCanonicalFile());
            assertEquals(
                    SteamClientRuntimePolicy.officialSteamBox64ProfileContents(true),
                    new String(Files.readAllBytes(repaired.toPath()), StandardCharsets.UTF_8));

            File[] leftovers = directory.listFiles(
                    file -> file.getName().endsWith(".tmp"));
            assertTrue(leftovers != null && leftovers.length == 0);
        } finally {
            deleteRecursively(directory);
        }
    }

    @Test
    public void officialSteamDescriptorAlwaysDisablesMultiblock() {
        assertTrue(SteamClientRuntimePolicy.shouldDisableFexMultiblock(
                true, false, new File("missing-prefix")));
    }

    @Test
    public void bareDesktopWithSteamInItsPrefixDisablesMultiblock() throws IOException {
        File prefix = Files.createTempDirectory("steam-prefix").toFile();
        try {
            File client = new File(
                    prefix, "drive_c/Program Files (x86)/Steam/steam.exe");
            assertTrue(client.getParentFile().mkdirs());
            assertTrue(client.createNewFile());

            assertTrue(SteamClientRuntimePolicy.shouldDisableFexMultiblock(
                    false, true, prefix));
        } finally {
            deleteRecursively(prefix);
        }
    }

    @Test
    public void bareDesktopWithoutSteamRetainsPreset() throws IOException {
        File prefix = Files.createTempDirectory("empty-prefix").toFile();
        try {
            assertFalse(SteamClientRuntimePolicy.shouldDisableFexMultiblock(
                    false, true, prefix));
        } finally {
            deleteRecursively(prefix);
        }
    }

    @Test
    public void directGameShortcutRetainsPresetEvenWhenSteamIsInstalled() throws IOException {
        File prefix = Files.createTempDirectory("game-prefix").toFile();
        try {
            File client = new File(prefix, "drive_c/Program Files/Steam/steam.exe");
            assertTrue(client.getParentFile().mkdirs());
            assertTrue(client.createNewFile());

            assertFalse(SteamClientRuntimePolicy.shouldDisableFexMultiblock(
                    false, false, prefix));
        } finally {
            deleteRecursively(prefix);
        }
    }

    private static void deleteRecursively(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) deleteRecursively(child);
        }
        if (!file.delete()) file.deleteOnExit();
    }

    private static int occurrences(String value, String needle) {
        int count = 0;
        int start = 0;
        while ((start = value.indexOf(needle, start)) >= 0) {
            count++;
            start += needle.length();
        }
        return count;
    }
}
