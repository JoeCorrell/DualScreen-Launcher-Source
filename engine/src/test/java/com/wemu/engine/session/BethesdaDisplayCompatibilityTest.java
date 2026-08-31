package com.wemu.engine.session;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

public class BethesdaDisplayCompatibilityTest {
    @Rule
    public final TemporaryFolder temporary = new TemporaryFolder();

    @Test
    public void recognizesFalloutAndSkyrimLaunchChains() {
        assertTrue(BethesdaDisplayCompatibility.isSupportedExecutable(
                "F:\\Games\\Fallout New Vegas\\FalloutNV.exe -nolauncher"));
        assertTrue(BethesdaDisplayCompatibility.isSupportedExecutable(
                "F:\\Games\\Fallout 4\\Fallout4Launcher.exe"));
        assertTrue(BethesdaDisplayCompatibility.isSupportedExecutable(
                "\"F:\\Games\\Skyrim Special Edition\\SkyrimSE.exe\""));
        assertTrue(BethesdaDisplayCompatibility.isSupportedExecutable(
                "F:\\Games\\Skyrim VR\\SkyrimVR.exe"));
        assertFalse(BethesdaDisplayCompatibility.isSupportedExecutable(
                "F:\\Games\\Oblivion\\Oblivion.exe"));
    }

    @Test
    public void patchesEveryExistingProfileWithoutChangingResolution() throws Exception {
        File prefix = temporary.newFolder("prefix");
        File root = new File(prefix,
                "drive_c/users/xuser/Documents/My Games");
        File falloutNv = new File(root, "FalloutNV/Fallout.ini");
        File fallout4 = new File(root, "Fallout4/Fallout4Prefs.ini");
        File skyrimSe = new File(root,
                "Skyrim Special Edition/SkyrimPrefs.ini");
        write(falloutNv, "[Display]\n"
                + "bIgnoreResolutionCheck=0\n"
                + "iSize W=1920\n"
                + "iSize H=1080\n");
        write(fallout4, "[Display]\n"
                + "bFull Screen=1\n"
                + "bBorderless=0\n"
                + "iSize W=1600\n"
                + "iSize H=900\n");
        write(skyrimSe, "[Display]\n"
                + "bFull Screen=1\n"
                + "bBorderless=0\n"
                + "iSize W=1280\n"
                + "iSize H=720\n");

        assertEquals(3, BethesdaDisplayCompatibility.applyExistingProfiles(prefix));

        String legacy = read(falloutNv);
        assertTrue(legacy.contains("bIgnoreResolutionCheck=1"));
        assertTrue(legacy.contains("iSize W=1920"));
        assertTrue(legacy.contains("iSize H=1080"));
        assertBorderlessAtOriginalSize(fallout4, 1600, 900);
        assertBorderlessAtOriginalSize(skyrimSe, 1280, 720);
    }

    @Test
    public void missingProfilesAreNotCreated() throws Exception {
        File prefix = temporary.newFolder("empty-prefix");
        assertEquals(0, BethesdaDisplayCompatibility.applyExistingProfiles(prefix));
        assertFalse(new File(prefix, "drive_c/users/xuser/Documents/My Games").exists());
    }

    private static void assertBorderlessAtOriginalSize(
            File file, int width, int height) throws Exception {
        String contents = read(file);
        assertTrue(contents.contains("bFull Screen=0"));
        assertTrue(contents.contains("bBorderless=1"));
        assertTrue(contents.contains("iSize W=" + width));
        assertTrue(contents.contains("iSize H=" + height));
    }

    private static void write(File file, String contents) throws Exception {
        assertTrue(file.getParentFile().isDirectory() || file.getParentFile().mkdirs());
        Files.write(file.toPath(), contents.getBytes(StandardCharsets.UTF_8));
    }

    private static String read(File file) throws Exception {
        return new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
    }
}
