package com.wemu.engine.session;

import android.util.Log;

import com.wemu.engine.core.FileUtils;

import java.io.File;
import java.util.LinkedHashMap;
import java.util.Locale;

/**
 * Repairs Bethesda display profiles that otherwise depend on Wine exposing a
 * physical fullscreen mode for the upper half of a dual-screen desktop.
 *
 * <p>The selected iSize width/height are intentionally never written here.
 * Legacy Gamebryo games retain exclusive fullscreen and bypass only their
 * obsolete adapter-mode validation. Newer Creation Engine games retain their
 * configured size in a borderless window, which WEMU places on the gameplay
 * display without requiring that sub-display size to be a host display mode.</p>
 */
public final class BethesdaDisplayCompatibility {
    private static final String TAG = "BethesdaDisplay";
    private static final String SETTINGS_ROOT =
            "drive_c/users/xuser/Documents/My Games";

    private static final String[] SUPPORTED_EXECUTABLES = {
            "fallout3.exe", "falloutlauncher.exe", "falloutlaunchersteam.exe",
            "falloutnv.exe", "falloutnvlauncher.exe",
            "fallout4.exe", "fallout4launcher.exe",
            "fallout76.exe", "fallout76launcher.exe",
            "tesv.exe", "skyrimlauncher.exe",
            "skyrimse.exe", "skyrimselauncher.exe",
            "skyrimvr.exe", "skyrimvrlauncher.exe",
    };

    private static final Profile[] PROFILES = {
            legacy("Fallout3/FALLOUT.INI", true),
            legacy("Fallout3/FalloutPrefs.ini", false),
            legacy("FalloutNV/Fallout.ini", true),
            legacy("FalloutNV/FalloutPrefs.ini", false),
            borderless("Fallout4/Fallout4Prefs.ini"),
            borderless("Fallout 76/Fallout76Prefs.ini"),
            borderless("Skyrim/SkyrimPrefs.ini"),
            borderless("Skyrim Special Edition/SkyrimPrefs.ini"),
            borderless("Skyrim VR/SkyrimPrefs.ini"),
    };

    private BethesdaDisplayCompatibility() {}

    public static boolean isSupportedExecutable(String commandOrPath) {
        String executable = executableName(commandOrPath);
        for (String candidate : SUPPORTED_EXECUTABLES) {
            if (candidate.equals(executable)) return true;
        }
        return false;
    }

    /**
     * Patches every existing supported profile in the prefix. The operation is
     * idempotent and deliberately does not create profiles before the game has.
     *
     * @return the number of existing profiles successfully verified or patched
     */
    public static int applyExistingProfiles(File winePrefix) {
        if (winePrefix == null) return 0;
        File root = new File(winePrefix, SETTINGS_ROOT);
        int applied = 0;
        for (Profile profile : PROFILES) {
            File file = new File(root, profile.relativePath);
            if (!file.isFile()) continue;
            if (patchDisplay(file, profile.values)) {
                applied++;
            } else {
                Log.e(TAG, "Could not patch " + file.getAbsolutePath());
            }
        }
        return applied;
    }

    private static Profile legacy(String path, boolean engineIni) {
        LinkedHashMap<String, String> values = new LinkedHashMap<>();
        if (engineIni) values.put("bIgnoreResolutionCheck", "1");
        else values.put("bShowAllResolutions", "1");
        return new Profile(path, values);
    }

    private static Profile borderless(String path) {
        LinkedHashMap<String, String> values = new LinkedHashMap<>();
        values.put("bFull Screen", "0");
        values.put("bBorderless", "1");
        return new Profile(path, values);
    }

    private static boolean patchDisplay(
            File file, LinkedHashMap<String, String> replacements) {
        String contents = FileUtils.readString(file);
        if (contents == null) return false;
        String patched = Fallout3Compatibility.patchSectionSettings(
                contents, "Display", replacements);
        return contents.equals(patched) || FileUtils.writeStringAtomic(file, patched);
    }

    private static String executableName(String commandOrPath) {
        if (commandOrPath == null) return "";
        String value = commandOrPath.trim();
        if (value.startsWith("\"")) {
            int closing = value.indexOf('"', 1);
            value = closing >= 0 ? value.substring(1, closing) : value.substring(1);
        }
        int slash = Math.max(value.lastIndexOf('/'), value.lastIndexOf('\\'));
        String name = (slash >= 0 ? value.substring(slash + 1) : value).trim();
        int arguments = name.toLowerCase(Locale.ROOT).indexOf(".exe ");
        if (arguments >= 0) name = name.substring(0, arguments + 4);
        return name.toLowerCase(Locale.ROOT);
    }

    private static final class Profile {
        final String relativePath;
        final LinkedHashMap<String, String> values;

        Profile(String relativePath, LinkedHashMap<String, String> values) {
            this.relativePath = relativePath;
            this.values = values;
        }
    }
}
