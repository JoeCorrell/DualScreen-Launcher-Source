package com.wemu.engine.session;

import com.wemu.engine.core.FileUtils;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Keeps Stardew Valley's saved window profile aligned with the gameplay monitor.
 *
 * <p>Stardew stores the last physical display size separately from the Wine
 * desktop configuration. Moving a prefix between portrait and dual-screen
 * containers can therefore leave a valid 1280x720 upper monitor rendering a
 * stale 1050x1680 game window. Only the display fields are changed here; input,
 * audio, accessibility, and gameplay preferences remain byte-for-byte intact.</p>
 */
public final class StardewDisplayCompatibility {
    private static final String PREFERENCES_PATH =
            ".wine/drive_c/users/xuser/AppData/Roaming/StardewValley/startup_preferences";
    private static final int EXPECTED_FIELDS = 9;

    private StardewDisplayCompatibility() {}

    /** Repairs an existing profile and deliberately lets Stardew create a missing one. */
    public static Result ensure(File containerRoot, int width, int height) {
        if (containerRoot == null || width <= 0 || height <= 0) {
            return new Result(null, false, false, false, 0, EXPECTED_FIELDS);
        }

        File preferences = new File(containerRoot, PREFERENCES_PATH);
        if (!preferences.isFile()) {
            return new Result(preferences, false, true, false, 0, EXPECTED_FIELDS);
        }

        byte[] bytes = FileUtils.read(preferences);
        if (bytes == null) {
            return new Result(preferences, true, false, false, 0, EXPECTED_FIELDS);
        }
        String original = new String(bytes, StandardCharsets.UTF_8);

        LinkedHashMap<String, String> values = new LinkedHashMap<>();
        /*
         * Stardew's borderless implementation reads Window.ClientBounds.
         * In a dual-screen WEMU session that is the full stacked 1280x1800 X
         * root, so the upper monitor displays only the first 1280x720 slice.
         * Hardware fullscreen uses the explicit preferred resolution instead
         * and keeps the game confined to the gameplay monitor.
         */
        values.put("windowMode", "2");
        values.put("fullscreenResolutionX", Integer.toString(width));
        values.put("fullscreenResolutionY", Integer.toString(height));
        values.put("fullscreen", "true");
        values.put("windowedBorderlessFullscreen", "false");
        values.put("zoomLevel", "1");
        values.put("uiScale", "1");
        values.put("preferredResolutionX", Integer.toString(width));
        values.put("preferredResolutionY", Integer.toString(height));

        String patched = original;
        int matchedFields = 0;
        for (Map.Entry<String, String> entry : values.entrySet()) {
            TagPatch tag = patchTag(patched, entry.getKey(), entry.getValue());
            patched = tag.contents;
            if (tag.matched) matchedFields++;
        }

        if (patched.equals(original)) {
            return new Result(
                    preferences, true, true, false, matchedFields, EXPECTED_FIELDS);
        }
        boolean written = FileUtils.writeStringAtomic(preferences, patched);
        return new Result(
                preferences, true, written, written, matchedFields, EXPECTED_FIELDS);
    }

    private static TagPatch patchTag(String contents, String tag, String value) {
        Pattern pattern = Pattern.compile(
                "(<" + Pattern.quote(tag) + ">)([^<]*)(</" + Pattern.quote(tag) + ">)");
        Matcher matcher = pattern.matcher(contents);
        if (!matcher.find()) return new TagPatch(contents, false);

        StringBuffer patched = new StringBuffer(contents.length());
        do {
            matcher.appendReplacement(
                    patched,
                    Matcher.quoteReplacement(matcher.group(1) + value + matcher.group(3)));
        } while (matcher.find());
        matcher.appendTail(patched);
        return new TagPatch(patched.toString(), true);
    }

    private static final class TagPatch {
        final String contents;
        final boolean matched;

        TagPatch(String contents, boolean matched) {
            this.contents = contents;
            this.matched = matched;
        }
    }

    public static final class Result {
        public final File preferencesFile;
        public final boolean profileExists;
        public final boolean successful;
        public final boolean changed;
        public final int matchedFields;
        public final int expectedFields;

        Result(
                File preferencesFile,
                boolean profileExists,
                boolean successful,
                boolean changed,
                int matchedFields,
                int expectedFields) {
            this.preferencesFile = preferencesFile;
            this.profileExists = profileExists;
            this.successful = successful;
            this.changed = changed;
            this.matchedFields = matchedFields;
            this.expectedFields = expectedFields;
        }
    }
}
