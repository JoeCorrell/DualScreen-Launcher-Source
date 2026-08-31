package com.wemu.engine.contents;

import android.content.Context;
import android.content.res.AssetManager;
import android.util.Log;

import com.wemu.engine.R;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * What ships inside the APK.
 *
 * Every one of these is an asset the container bootstrap already unpacks by
 * name, so a container can be configured to use it without anything being
 * downloaded first — but nothing could *list* them. `ContentsManager` knows only
 * about profiles the user installed, and `AdrenotoolsManager.enumarateInstalledDrivers`
 * goes out of its way to exclude the bundled drivers (`if (!fromResources)`).
 *
 * The result was a container editor whose DXVK and VKD3D rows simply vanished on
 * a fresh install, and whose driver list offered nothing but "System" while two
 * Turnip builds sat unused in the assets directory.
 */
public final class BundledContents {

    private static final String TAG = "BundledContents";

    private BundledContents() {}

    /**
     * The GPU drivers this build carries, "System" first.
     *
     * From the resource array rather than the assets directory: the array is
     * what `ImageFsInstaller` extracts from, so a driver listed here is
     * guaranteed to have been unpacked, and one present as an asset but absent
     * from the array never was.
     */
    public static List<String> drivers(Context context) {
        return new ArrayList<>(Arrays.asList(
                context.getResources().getStringArray(R.array.wrapper_graphics_driver_version_entries)));
    }

    /**
     * The DXVK builds this app offers, newest last.
     *
     * From the resource array, not from listing `assets/dxwrapper`. Listing the
     * directory was correct for exactly as long as these shipped in the APK, and
     * broke silently the moment they moved to the payload release — the
     * directory became empty, so the container editor offered no DXVK and no
     * VKD3D at all, which is the same disappearing-row failure the class comment
     * above describes it having been written to fix.
     *
     * "Bundled" now means "this build knows about it and can get it", which is
     * the question the editor was always really asking.
     */
    public static List<String> dxvkVersions(Context context) {
        return new ArrayList<>(Arrays.asList(
                context.getResources().getStringArray(R.array.dxvk_version_entries)));
    }

    /** The VKD3D builds this app offers, with "None" first. */
    public static List<String> vkd3dVersions(Context context) {
        List<String> versions = new ArrayList<>(Arrays.asList(
                context.getResources().getStringArray(R.array.vkd3d_version_entries)));
        // "None" is a real choice: a container that never asks for Direct3D 12
        // has no reason to carry VKD3D, and the container default is exactly
        // this string.
        versions.add(0, "None");
        return versions;
    }

    /** The Box64 builds in `assets/box64`. */
    public static List<String> box64Versions(Context context) {
        return versionsIn(context, "box64", "box64-");
    }

    /**
     * Version strings from an asset directory, by stripping a prefix and the
     * archive extension.
     *
     * The engine names these files `<component>-<version>.tzst` and reads the
     * version back out the same way when it extracts them, so parsing the
     * filename is not a guess — it is the same convention, read from the other
     * end.
     */
    private static List<String> versionsIn(Context context, String dir, String prefix) {
        List<String> versions = new ArrayList<>();
        AssetManager assets = context.getResources().getAssets();

        try {
            String[] files = assets.list(dir);
            if (files == null) return versions;

            for (String file : files) {
                if (!file.startsWith(prefix)) continue;
                int dot = file.lastIndexOf('.');
                versions.add(file.substring(prefix.length(), dot != -1 ? dot : file.length()));
            }
        } catch (IOException e) {
            Log.w(TAG, "Could not list assets/" + dir, e);
        }
        return versions;
    }
}
