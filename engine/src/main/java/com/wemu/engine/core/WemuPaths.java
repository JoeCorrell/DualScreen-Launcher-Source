package com.wemu.engine.core;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Environment;

import androidx.preference.PreferenceManager;

import java.io.File;

/**
 * Where the engine keeps things the user is meant to be able to reach.
 *
 * Containers, shortcuts and profiles live inside the app's private storage, but
 * exported presets, control profiles and shortcuts have to land somewhere a file
 * manager can see. That location was a constant on `SettingsFragment` — a screen
 * — so the preset managers, the input-control manager and the driver manager all
 * imported a fragment in order to find out where to write a file.
 *
 * The path is unchanged, so an existing the original install's exports are found
 * exactly where they were left.
 */
public final class WemuPaths {

    private WemuPaths() {}

    /**
     * The user-visible root, `/sdcard/Wemu`.
     *
     * Named for the app: this is the folder
     * a user already has presets and control profiles in, and renaming it would
     * silently orphan all of them.
     */
    public static final String DEFAULT_SHARED_PATH =
            Environment.getExternalStorageDirectory().getPath() + "/Wemu";

    public static final String DEFAULT_SHORTCUT_EXPORT_PATH =
            DEFAULT_SHARED_PATH + "/Shortcuts";

    public static File presetsDir() {
        return new File(DEFAULT_SHARED_PATH, "Presets");
    }

    public static File profilesDir() {
        return new File(DEFAULT_SHARED_PATH, "profiles");
    }

    /**
     * Forgets which emulator builds are installed.
     *
     * Called after the image filesystem is reinstalled or upgraded: the versions
     * recorded here describe what was unpacked into the *previous* image, and
     * leaving them behind makes the installer skip work it now needs to redo.
     */
    public static void resetEmulatorsVersion(Context context) {
        SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(context);
        SharedPreferences.Editor editor = preferences.edit();
        editor.remove("current_box64_version");
        editor.remove("current_wowbox64_version");
        editor.remove("current_fexcore_version");
        editor.apply();
    }
}
