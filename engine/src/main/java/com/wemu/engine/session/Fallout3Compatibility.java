package com.wemu.engine.session;

import com.wemu.engine.core.EnvVars;
import com.wemu.engine.core.FileUtils;
import com.wemu.engine.core.WineRegistryEditor;

import java.io.File;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;

/** Executable-scoped compatibility required by Fallout 3 on the Thor Vulkan stack. */
public final class Fallout3Compatibility {
    public static final String PROVISIONING_REVISION = "4";
    public static final String DXVK_OPTION =
            "dxvk.enableGraphicsPipelineLibrary = False; d3d9.maxFrameRate = 60";

    private static final String GAME_EXECUTABLE = "fallout3.exe";
    private static final String[] LAUNCH_CHAIN = {
            GAME_EXECUTABLE,
            "fallout3launcher.exe",
            "falloutlauncher.exe",
            "falloutlaunchersteam.exe",
            "falloutlauncherepic.exe",
            "falloutlaunchergog.exe",
    };
    private static final String FALLOUT_SETTINGS =
            "drive_c/users/xuser/Documents/My Games/Fallout3";

    private Fallout3Compatibility() {}

    public static boolean isGameExecutable(String commandOrPath) {
        return GAME_EXECUTABLE.equals(executableName(commandOrPath));
    }

    public static boolean isLaunchChainExecutable(String commandOrPath) {
        String name = executableName(commandOrPath);
        for (String candidate : LAUNCH_CHAIN) {
            if (candidate.equals(name)) return true;
        }
        return false;
    }

    /** Merges the workaround into a direct Fallout shortcut's DXVK configuration. */
    public static boolean applyDxvkEnvironment(
            EnvVars environment, String commandOrPath) {
        if (environment == null || !isGameExecutable(commandOrPath)) return false;
        environment.put("DXVK_CONFIG", mergeDxvkConfig(environment.get("DXVK_CONFIG")));
        return true;
    }

    /** Adds the same policy to a desktop launch and anything its launcher creates. */
    public static boolean appendBatchEnvironment(
            StringBuilder batch, String commandOrPath) {
        if (batch == null || !isLaunchChainExecutable(commandOrPath)) return false;
        batch.append("set \"DXVK_CONFIG=%DXVK_CONFIG%; ")
                .append(DXVK_OPTION)
                .append("\"\r\n");
        return true;
    }

    /** Selects the game-local PE32 proxy without changing any other executable. */
    public static void applyExecutableOverrides(WineRegistryEditor registryEditor) {
        if (registryEditor == null) return;
        registryEditor.setStringValue(
                "Software\\Wine\\AppDefaults\\Fallout3.exe\\DllOverrides",
                "dinput8",
                "native,builtin");
    }

    /**
     * Merges the handheld profile into the INIs Fallout actually reads.  The
     * third-party "multicore" package in this installation leaves several
     * actor/effect paths synchronous, enables debug audio, and lets the launcher
     * restore MSAA/shadows.  Those settings correlate with the hostile-actor
     * main-thread collapse and are not a performance profile on this device.
     *
     * Width, height, fullscreen mode, controls, draw distance, difficulty, and
     * gameplay settings are deliberately preserved. The one display override
     * disables Fallout's legacy pre-D3D9 mode-list rejection: a dual-screen Wine
     * desktop can validly create 1280x720 even when that mode is absent from the
     * adapter enumeration. This is idempotent so launcher-created files are
     * repaired on every WEMU and Launcher launch.
     */
    public static boolean applyRuntimeSettings(File winePrefix) {
        if (winePrefix == null) return false;
        File settings = new File(winePrefix, FALLOUT_SETTINGS);

        LinkedHashMap<String, LinkedHashMap<String, String>> engineValues =
                new LinkedHashMap<>();
        engineValues.put("General", values(
                "bUseThreadedAI", "1",
                "iNumHWThreads", "2",
                "bUseThreadedBlood", "1",
                "bUseThreadedMorpher", "1",
                "bUseThreadedTempEffects", "1",
                "bUseThreadedParticleSystem", "1"));
        engineValues.put("MAIN", values("bCloneModelsInBackground", "1"));
        engineValues.put("BackgroundLoad", values(
                "bUseBackgroundFileLoader", "1",
                "bBackgroundCellLoads", "1",
                "bBackgroundPathing", "1",
                "bBackgroundNavmeshUpdate", "1",
                "bBackgroundLoadLipFiles", "1",
                "bLoadBackgroundFaceGen", "1",
                "bLoadHelmetsInBackground", "1",
                "bCloneModelsInBackground", "1",
                "bUseMultiThreadedFaceGen", "1",
                "bUseMultiThreadedTrees", "1",
                "iBackgroundLoadExtraMaxFPS", "60",
                "iBackgroundLoadExtraMinFPS", "30"));
        engineValues.put("HAVOK", values(
                "iNumHavokThreads", "2", "bHavokDebug", "0"));
        // These are the exact setting names embedded in Fallout3.exe:
        // bUse Joystick:Controls, bGamePadRumble:Controls and
        // bDisable360Controller:Interface. Keep the controller enabled even
        // when a launcher-generated INI starts from keyboard/mouse defaults.
        engineValues.put("Controls", values(
                "bUse Joystick", "1",
                "bGamePadRumble", "1"));
        engineValues.put("Interface", values(
                "bDisable360Controller", "0"));
        engineValues.put("Audio", values(
                "bMultiThreadAudio", "0",
                "bUseAudioDebugInformation", "0",
                "bEnableAudioCache", "1",
                "iAudioCacheSize", "2048",
                "iMaxSizeForCachedSound", "512"));
        engineValues.put("Display", values(
                "bIgnoreResolutionCheck", "1",
                "bDisplayBoundingVolumes", "0",
                "bDrawShadows", "0",
                "bDoActorShadows", "0",
                "bDoStaticAndArchShadows", "0",
                "bDoCanopyShadowPass", "0",
                "bActorSelfShadowing", "0",
                "bShadowsOnGrass", "0",
                "iActorShadowCount", "0",
                "iActorShadowCountInt", "0",
                "iActorShadowCountExt", "0",
                "iMultiSample", "0",
                "iPresentInterval", "0"));

        LinkedHashMap<String, LinkedHashMap<String, String>> preferenceValues =
                new LinkedHashMap<>();
        preferenceValues.put("Display", values(
                "bShowAllResolutions", "1",
                "bDrawShadows", "0",
                "iActorShadowCountInt", "0",
                "iActorShadowCountExt", "0",
                "iShadowFilter", "0",
                "iShadowMapResolution", "256",
                "bShadowsOnGrass", "0",
                "iMultiSample", "0",
                "iWaterMultisamples", "0",
                "iPresentInterval", "0"));
        preferenceValues.put("Controls", values(
                "bUse Joystick", "1",
                "bGamePadRumble", "1"));
        preferenceValues.put("Interface", values(
                "bDisable360Controller", "0"));

        boolean found = false;
        boolean success = true;
        File falloutIni = new File(settings, "FALLOUT.INI");
        if (falloutIni.isFile()) {
            found = true;
            success &= patchSettings(falloutIni, engineValues);
        }
        File prefsIni = new File(settings, "FalloutPrefs.ini");
        if (prefsIni.isFile()) {
            found = true;
            success &= patchSettings(prefsIni, preferenceValues);
        }
        return found && success;
    }

    /**
     * Keeps Fallout 3's exclusive-fullscreen request aligned with the mode
     * exposed by the active WEMU container. Fallout 3 validates this pair
     * before creating D3D9 and otherwise shows its unsupported-resolution
     * message before the main menu.
     */
    public static boolean applyDisplayResolution(
            File preferencesFile, int width, int height) {
        if (preferencesFile == null || !preferencesFile.isFile()
                || width <= 0 || height <= 0) return false;
        String contents = FileUtils.readString(preferencesFile);
        if (contents == null) return false;
        LinkedHashMap<String, String> values = values(
                "iAdapter", "0",
                "iSize W", Integer.toString(width),
                "iSize H", Integer.toString(height),
                "bFull Screen", "1",
                "bShowAllResolutions", "1");
        String patched = patchDisplaySettings(contents, values);
        return contents.equals(patched)
                || FileUtils.writeStringAtomic(preferencesFile, patched);
    }

    static String patchDisplaySettings(
            String contents, LinkedHashMap<String, String> replacements) {
        return patchSectionSettings(contents, "Display", replacements);
    }

    static String patchSectionSettings(
            String contents, String section,
            LinkedHashMap<String, String> replacements) {
        if (contents == null || replacements == null || replacements.isEmpty())
            return contents;
        final String newline = contents.contains("\r\n") ? "\r\n" : "\n";
        final String[] lines = contents.split("\\r?\\n", -1);
        final boolean trailingNewline = contents.endsWith("\n");
        final int lineCount = trailingNewline && lines.length > 0
                && lines[lines.length - 1].isEmpty() ? lines.length - 1 : lines.length;
        final LinkedHashMap<String, String> remaining =
                new LinkedHashMap<>(replacements);
        StringBuilder result = new StringBuilder(contents.length() + 96);
        boolean inDisplay = false;
        boolean sawDisplay = false;

        for (int lineIndex = 0; lineIndex < lineCount; ++lineIndex) {
            String line = lines[lineIndex];
            String trimmed = line.trim();
            if (trimmed.startsWith("[") && trimmed.endsWith("]")) {
                if (inDisplay) appendMissing(result, remaining, newline);
                inDisplay = ("[" + section + "]").equalsIgnoreCase(trimmed);
                sawDisplay |= inDisplay;
            }

            String output = line;
            if (inDisplay) {
                int equals = trimmed.indexOf('=');
                if (equals > 0) {
                    String key = trimmed.substring(0, equals).trim();
                    String canonical = findKey(remaining, key);
                    if (canonical != null) {
                        output = canonical + "=" + remaining.remove(canonical);
                    }
                }
            }
            result.append(output);
            if (lineIndex + 1 < lineCount || trailingNewline)
                result.append(newline);
        }
        if (inDisplay) {
            ensureLineBreak(result, newline);
            appendMissing(result, remaining, newline);
        }
        if (!sawDisplay) {
            ensureLineBreak(result, newline);
            result.append('[').append(section).append(']').append(newline);
            appendMissing(result, remaining, newline);
        }
        return result.toString();
    }

    private static boolean patchSettings(
            File file,
            LinkedHashMap<String, LinkedHashMap<String, String>> sections) {
        String contents = FileUtils.readString(file);
        if (contents == null) return false;
        String patched = contents;
        for (Map.Entry<String, LinkedHashMap<String, String>> entry
                : sections.entrySet()) {
            patched = patchSectionSettings(patched, entry.getKey(), entry.getValue());
        }
        return contents.equals(patched) || FileUtils.writeStringAtomic(file, patched);
    }

    private static LinkedHashMap<String, String> values(String... pairs) {
        if ((pairs.length & 1) != 0) throw new IllegalArgumentException("pairs");
        LinkedHashMap<String, String> result = new LinkedHashMap<>();
        for (int index = 0; index < pairs.length; index += 2)
            result.put(pairs[index], pairs[index + 1]);
        return result;
    }

    private static String findKey(Map<String, String> values, String candidate) {
        for (String key : values.keySet())
            if (key.equalsIgnoreCase(candidate)) return key;
        return null;
    }

    private static void appendMissing(
            StringBuilder output, LinkedHashMap<String, String> remaining,
            String newline) {
        for (Map.Entry<String, String> entry : remaining.entrySet())
            output.append(entry.getKey()).append('=').append(entry.getValue())
                    .append(newline);
        remaining.clear();
    }

    private static void ensureLineBreak(StringBuilder output, String newline) {
        if (output.length() == 0) return;
        if (output.length() < newline.length()
                || !output.substring(output.length() - newline.length()).equals(newline))
            output.append(newline);
    }

    static String mergeDxvkConfig(String existing) {
        String value = existing == null ? "" : existing.trim();
        String lower = value.toLowerCase(Locale.ROOT);
        if (!lower.contains("dxvk.enablegraphicspipelinelibrary"))
            value = appendOption(value, "dxvk.enableGraphicsPipelineLibrary = False");
        if (!lower.contains("d3d9.maxframerate"))
            value = appendOption(value, "d3d9.maxFrameRate = 60");
        return value;
    }

    private static String appendOption(String existing, String option) {
        return existing == null || existing.isEmpty()
                ? option : existing + "; " + option;
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
        int space = name.toLowerCase(Locale.ROOT).indexOf(".exe ");
        if (space >= 0) name = name.substring(0, space + 4);
        return name.toLowerCase(Locale.ROOT);
    }
}
