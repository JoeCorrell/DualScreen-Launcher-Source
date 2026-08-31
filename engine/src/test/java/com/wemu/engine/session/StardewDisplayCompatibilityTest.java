package com.wemu.engine.session;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

public class StardewDisplayCompatibilityTest {
    @Rule
    public final TemporaryFolder temporary = new TemporaryFolder();

    @Test
    public void repairsPortraitProfileWithoutChangingOtherPreferences() throws Exception {
        File root = temporary.newFolder("container");
        File preferences = preferences(root);
        String original = "\uFEFF<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                + "<StartupPreferences><windowMode>2</windowMode>\r\n"
                + "<fullscreenResolutionX>1050</fullscreenResolutionX>\r\n"
                + "<fullscreenResolutionY>1680</fullscreenResolutionY>\r\n"
                + "<fullscreen>true</fullscreen>\r\n"
                + "<windowedBorderlessFullscreen>false</windowedBorderlessFullscreen>\r\n"
                + "<zoomLevel>0.75</zoomLevel><uiScale>-1</uiScale>\r\n"
                + "<preferredResolutionX>1050</preferredResolutionX>\r\n"
                + "<preferredResolutionY>1680</preferredResolutionY>\r\n"
                + "<gamepadControls><useToolButton>X</useToolButton></gamepadControls>\r\n"
                + "<musicVolume>0.42</musicVolume></StartupPreferences>\r\n";
        write(preferences, original);

        StardewDisplayCompatibility.Result result =
                StardewDisplayCompatibility.ensure(root, 1280, 720);

        assertTrue(result.profileExists);
        assertTrue(result.successful);
        assertTrue(result.changed);
        assertTrue(result.matchedFields == result.expectedFields);
        String repaired = read(preferences);
        assertTrue(repaired.startsWith("\uFEFF<?xml"));
        assertTrue(repaired.contains("<windowMode>2</windowMode>"));
        assertTrue(repaired.contains("<fullscreenResolutionX>1280</fullscreenResolutionX>"));
        assertTrue(repaired.contains("<fullscreenResolutionY>720</fullscreenResolutionY>"));
        assertTrue(repaired.contains("<fullscreen>true</fullscreen>"));
        assertTrue(repaired.contains(
                "<windowedBorderlessFullscreen>false</windowedBorderlessFullscreen>"));
        assertTrue(repaired.contains("<zoomLevel>1</zoomLevel><uiScale>1</uiScale>"));
        assertTrue(repaired.contains("<preferredResolutionX>1280</preferredResolutionX>"));
        assertTrue(repaired.contains("<preferredResolutionY>720</preferredResolutionY>"));
        assertTrue(repaired.contains(
                "<gamepadControls><useToolButton>X</useToolButton></gamepadControls>"));
        assertTrue(repaired.contains("<musicVolume>0.42</musicVolume>"));
    }

    @Test
    public void correctProfileIsIdempotent() throws Exception {
        File root = temporary.newFolder("correct-container");
        File preferences = preferences(root);
        write(preferences, correctProfile());

        StardewDisplayCompatibility.Result result =
                StardewDisplayCompatibility.ensure(root, 1280, 720);

        assertTrue(result.successful);
        assertFalse(result.changed);
        assertTrue(result.matchedFields == result.expectedFields);
        assertTrue(read(preferences).equals(correctProfile()));
    }

    @Test
    public void missingProfileIsNotCreated() throws Exception {
        File root = temporary.newFolder("new-container");
        StardewDisplayCompatibility.Result result =
                StardewDisplayCompatibility.ensure(root, 1280, 720);

        assertFalse(result.profileExists);
        assertTrue(result.successful);
        assertFalse(result.changed);
        assertFalse(preferences(root).exists());
    }

    private static String correctProfile() {
        return "<StartupPreferences><windowMode>2</windowMode>"
                + "<fullscreenResolutionX>1280</fullscreenResolutionX>"
                + "<fullscreenResolutionY>720</fullscreenResolutionY>"
                + "<fullscreen>true</fullscreen>"
                + "<windowedBorderlessFullscreen>false</windowedBorderlessFullscreen>"
                + "<zoomLevel>1</zoomLevel><uiScale>1</uiScale>"
                + "<preferredResolutionX>1280</preferredResolutionX>"
                + "<preferredResolutionY>720</preferredResolutionY></StartupPreferences>";
    }

    private static File preferences(File root) {
        return new File(root,
                ".wine/drive_c/users/xuser/AppData/Roaming/StardewValley/startup_preferences");
    }

    private static void write(File file, String contents) throws Exception {
        assertTrue(file.getParentFile().isDirectory() || file.getParentFile().mkdirs());
        Files.write(file.toPath(), contents.getBytes(StandardCharsets.UTF_8));
    }

    private static String read(File file) throws Exception {
        return new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
    }
}
