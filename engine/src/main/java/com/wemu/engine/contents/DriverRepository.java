package com.wemu.engine.contents;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;

import androidx.preference.PreferenceManager;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

/**
 * Where GPU drivers come from, and what is on offer.
 *
 * Turnip builds for Adreno are published as GitHub releases, so a "repository"
 * here is a GitHub releases API URL and a driver is a release asset. Upstream
 * parses that JSON inside a `Fragment`, in the middle of a `RecyclerView`
 * adapter, which is why the list cannot be read anywhere else — including from a
 * settings screen that is not that fragment.
 *
 * The defaults are upstream's own, unchanged. They are the repositories the
 * The original community actually publishes Turnip builds to, and inventing a
 * different list would mean pointing this app at URLs nobody maintains.
 */
public final class DriverRepository {

    private static final String TAG = "DriverRepository";

    private DriverRepository() {}

    /** A place drivers are published. */
    public static final class Repo {
        public final String name;
        public final String apiUrl;

        public Repo(String name, String apiUrl) {
            this.name = name;
            this.apiUrl = apiUrl;
        }
    }

    /** One downloadable driver build. */
    public static final class Release {
        public final String name;
        public final String repoName;
        public final String downloadUrl;
        /** The release's page, for a user who wants to read before installing. */
        public final String pageUrl;

        public Release(String name, String repoName, String downloadUrl, String pageUrl) {
            this.name = name;
            this.repoName = repoName;
            this.downloadUrl = downloadUrl;
            this.pageUrl = pageUrl;
        }
    }

    /**
     * Where GPU drivers come from, out of the box.
     *
     * Two kinds, and both are wanted. Qualcomm's own Adreno blobs are what
     * "a driver for an Adreno 750" usually means and are often the most
     * compatible thing available for a recent part; Turnip is Mesa's open
     * driver, is frequently faster, and is where anything experimental appears
     * first. K11MCH1's repository is the one that publishes the former and was
     * missing here entirely, which is why the list had nothing recognisable as
     * an Adreno driver in it.
     *
     * Five rather than everything findable. Each is a separate network round
     * trip on refresh, and a list nobody scrolls to the bottom of is not made
     * better by being longer.
     */
    public static List<Repo> defaults() {
        List<Repo> result = new ArrayList<>();
        // Qualcomm's Adreno blobs and Turnip builds, per Adreno generation.
        result.add(new Repo(
                "K11MCH1 AdrenoTools",
                "https://api.github.com/repos/K11MCH1/AdrenoToolsDrivers/releases"));
        // Mainline Turnip, rebuilt continuously.
        result.add(new Repo(
                "Whitebelyash AdrenoTools",
                "https://api.github.com/repos/whitebelyash/AdrenoToolsDrivers/releases"));
        // Turnip built per Adreno generation, including a7xx.
        result.add(new Repo(
                "StevenMXZ Turnip",
                "https://api.github.com/repos/StevenMXZ/freedreno_turnip-CI/releases"));
        // Turnip with a patch set of its own; often ahead on new hardware.
        result.add(new Repo(
                "MrPurple Turnip",
                "https://api.github.com/repos/MrPurple666/purple-turnip/releases"));
        /*
         * Kept for its 8 Gen 2 builds, which is the part I got wrong once.
         *
         * This was dropped for being stale and "tuned for 8 Gen 2" — which is
         * precisely the reason to keep it, on a device that is one. Its
         * releases carry assets built against that part specifically, and a
         * driver built for the chip in the device beats a newer generic one
         * often enough to be worth a place in a list of five.
         */
        result.add(new Repo(
                "Weab-chan Turnip",
                "https://api.github.com/repos/Weab-chan/freedreno_turnip-CI/releases"));
        return result;
    }

    /**
     * Whether a stored list is one this app wrote, rather than one a user made.
     *
     * Changing a default does nothing to a value already written down, and
     * upstream the original writes this key on first run — so without this, a
     * better list would reach nobody who had ever opened the app. Matched by
     * exact URL set: a list somebody has actually edited differs from the old
     * defaults in at least one entry and is left alone.
     */
    private static boolean isLegacyDefaults(String json) {
        try {
            JSONArray array = new JSONArray(json);
            if (array.length() != LEGACY_DEFAULT_URLS.length) return false;

            List<String> stored = new ArrayList<>();
            for (int i = 0; i < array.length(); i++) {
                stored.add(array.getJSONObject(i).optString("url", ""));
            }
            for (String url : LEGACY_DEFAULT_URLS) {
                if (!stored.contains(url)) return false;
            }
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    /** The set this app used to ship, for {@link #isLegacyDefaults}. */
    private static final String[] LEGACY_DEFAULT_URLS = {
            "https://api.github.com/repos/StevenMXZ/freedreno_turnip-CI/releases",
            "https://api.github.com/repos/whitebelyash/AdrenoToolsDrivers/releases",
            "https://api.github.com/repos/Weab-chan/freedreno_turnip-CI/releases",
    };

    /**
     * The repositories to search, the user's own if they have set any.
     *
     * Stored under upstream's key so a list curated in stock the original Ludashi
     * carries over.
     */
    public static List<Repo> repos(Context context) {
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(context);
        String json = prefs.getString(CUSTOM_REPOS_KEY, "");

        List<Repo> result = new ArrayList<>();
        if (json == null || json.isEmpty() || isLegacyDefaults(json)) {
            result.addAll(defaults());
            return result;
        }

        try {
            JSONArray array = new JSONArray(json);
            for (int i = 0; i < array.length(); i++) {
                JSONObject entry = array.getJSONObject(i);
                String url = entry.optString("url", "");
                if (!url.isEmpty()) {
                    result.add(new Repo(entry.optString("name", "Repository"), url));
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "Custom driver repository list is unreadable; using the defaults", e);
        }
        return result;
    }

    /**
     * Turns a repository's browser URL into its API URL.
     *
     * So a user can paste what is in their address bar rather than having to
     * know that the releases endpoint lives on a different host.
     */
    public static String toApiUrl(String url) {
        String trimmed = url.trim();
        if (trimmed.startsWith("https://github.com/") && !trimmed.contains("api.github.com")) {
            trimmed = trimmed.replace("https://github.com/", "https://api.github.com/repos/");
            if (!trimmed.endsWith("/releases")) {
                trimmed = trimmed.replaceAll("/+$", "") + "/releases";
            }
        }
        return trimmed;
    }

    /**
     * Reads the driver builds a repository is offering.
     *
     * Blocking. Returns an empty list rather than throwing when the network is
     * away or the response is not what was expected — this is a list the user
     * asked to see, not a step something else depends on.
     *
     * @param limit how many releases to take, newest first. GitHub returns
     *   thirty per page and a CI repository can have hundreds; the older ones
     *   are of no interest to anyone who is not bisecting a regression.
     */
    public static List<Release> fetch(Repo repo, int limit) {
        List<Release> releases = new ArrayList<>();

        String json = Downloader.downloadString(repo.apiUrl);
        if (json == null) {
            Log.w(TAG, "Could not reach " + repo.apiUrl);
            return releases;
        }

        try {
            JSONArray array = new JSONArray(json);
            for (int i = 0; i < array.length() && releases.size() < limit; i++) {
                JSONObject release = array.getJSONObject(i);
                JSONArray assets = release.optJSONArray("assets");
                if (assets == null) continue;

                for (int j = 0; j < assets.length(); j++) {
                    JSONObject asset = assets.getJSONObject(j);
                    String downloadUrl = asset.optString("browser_download_url", "");

                    // The first archive in the release, and only the first: a
                    // release with several assets is one driver plus its
                    // checksums and notes, not several drivers.
                    if (downloadUrl.endsWith(".zip") || downloadUrl.endsWith(".tzst")) {
                        String name = release.optString(
                                "name",
                                release.optString("tag_name", asset.optString("name", "Driver")));
                        releases.add(new Release(
                                name, repo.name, downloadUrl, release.optString("html_url", "")));
                        break;
                    }
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "Could not read releases from " + repo.apiUrl, e);
        }
        return releases;
    }

    /** Upstream's key, so a curated list carries over from stock the original. */
    private static final String CUSTOM_REPOS_KEY = "custom_driver_repos";
}
