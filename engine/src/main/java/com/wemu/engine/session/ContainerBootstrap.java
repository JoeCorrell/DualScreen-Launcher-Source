package com.wemu.engine.session;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;

import androidx.preference.PreferenceManager;

import com.wemu.engine.container.Container;
import com.wemu.engine.core.FrameGeneration;
import com.wemu.engine.container.Shortcut;
import com.wemu.engine.contents.GuestPayloads;
import com.wemu.engine.contents.AdrenotoolsManager;
import com.wemu.engine.contents.ContentProfile;
import com.wemu.engine.contents.ContentsManager;
import com.wemu.engine.config.DXWrapperConfig;
import com.wemu.engine.core.Callback;
import com.wemu.engine.core.GPUInformation;
import com.wemu.engine.core.DefaultVersion;
import com.wemu.engine.core.EnvVars;
import com.wemu.engine.core.FileUtils;
import com.wemu.engine.core.KeyValueSet;
import com.wemu.engine.core.OnExtractFileListener;
import com.wemu.engine.core.TarCompressorUtils;
import com.wemu.engine.core.TurnipPerformanceOptions;
import com.wemu.engine.core.WineIconTheme;
import com.wemu.engine.core.WineInfo;
import com.wemu.engine.core.WineRegistryEditor;
import com.wemu.engine.core.WineStartMenuCreator;
import com.wemu.engine.core.WineThemeManager;
import com.wemu.engine.core.WineUtils;
import com.wemu.engine.core.WineVisualStyle;
import com.wemu.engine.winhandler.WinHandler;
import com.wemu.engine.xenvironment.ImageFs;
import com.wemu.engine.xserver.XServer;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Prepares a container's filesystem before Wine is started.
 *
 * Unpacking the right DXVK, the right graphics driver and the right Windows
 * components into a container is engine work — it reads the container, writes
 * to its drive and edits its registry, and nothing about it involves a screen.
 * Originally it was seven private methods on XServerDisplayActivity, which is
 * why none of it could be done anywhere else: not from a shortcut, not ahead of
 * time, and not from a test.
 *
 * The bodies are ported as they were, with the activity — which was only ever
 * used as a Context — replaced by one. Behaviour is deliberately unchanged:
 * this fork rewrites the interface, and rewriting the part that decides which
 * DLL lands in system32 at the same time would make any resulting breakage
 * impossible to attribute.
 */
public class ContainerBootstrap {

    private final Context context;
    private final Container container;
    private final ImageFs imageFs;
    private final ContentsManager contentsManager;
    private final XServer xServer;
    private final EnvVars envVars;
    private final SharedPreferences preferences;

    /** Null when launching the container itself rather than a shortcut on it. */
    private final Shortcut shortcut;

    private final String dxwrapper;
    private final KeyValueSet dxwrapperConfig;
    private final String graphicsDriver;
    private final HashMap<String, String> graphicsDriverConfig;
    private final String audioDriver;
    private final WineInfo wineInfo;
    private final boolean skyrimAudioCompatibility;

    private String startupSelection;
    private boolean firstTimeBoot;

    /** Reports extraction progress; may be null. */
    private OnExtractFileListener onExtractFileListener;

    public ContainerBootstrap(
            Context context,
            Container container,
            ImageFs imageFs,
            ContentsManager contentsManager,
            XServer xServer,
            EnvVars envVars,
            Shortcut shortcut,
            String dxwrapper,
            KeyValueSet dxwrapperConfig,
            String graphicsDriver,
            HashMap<String, String> graphicsDriverConfig,
            String audioDriver,
            WineInfo wineInfo,
            boolean firstTimeBoot,
            boolean skyrimAudioCompatibility) {
        this.context = context;
        this.container = container;
        this.imageFs = imageFs;
        this.contentsManager = contentsManager;
        this.xServer = xServer;
        this.envVars = envVars;
        this.shortcut = shortcut;
        this.dxwrapper = dxwrapper;
        this.dxwrapperConfig = dxwrapperConfig;
        this.graphicsDriver = graphicsDriver;
        this.graphicsDriverConfig = graphicsDriverConfig;
        this.audioDriver = audioDriver;
        this.wineInfo = wineInfo;
        this.firstTimeBoot = firstTimeBoot;
        this.skyrimAudioCompatibility = skyrimAudioCompatibility;
        this.preferences = PreferenceManager.getDefaultSharedPreferences(context);
    }

    private static final String TAG = "ContainerBootstrap";

    /**
     * The Vulkan ICD the wrapper installs, relative to the image's share dir.
     *
     * Named once because two things depend on it agreeing: the environment
     * variable the guest loads it through, and the check that decides whether it
     * needs extracting. A drift between those two is a container that reports
     * itself set up and cannot find a GPU.
     */
    private static final String WRAPPER_ICD_PATH = "vulkan/icd.d/wrapper_icd.aarch64.json";

    /**
     * Bumped when a fix changes what a wrapper selection actually unpacks.
     *
     * Revision 2: user-installed DXVK and VKD3D never reached the prefix. The
     * container stored the profile's entry name — `DXVK-3.0-0` — and this class
     * looked it up as `"dxvk-" + version`, so `ContentsManager` parsed the
     * version as "DXVK-3.0" and found nothing. Null reads as "not a user
     * profile", the fallback tried a bundled `dxvk-DXVK-3.0-0.tzst` that does
     * not exist, and the extraction returned false into a discarded value. Every
     * container that selected a downloaded DXVK has been running whichever build
     * happened to be in its prefix — for the one this was found on, the plain
     * x86-64 DXVK 2.3.1 out of the APK.
     */
    private static final String DXWRAPPER_REVISION = "2";

    /**
     * vkBasalt's post-processing config, empty when no screen effect is chosen.
     *
     * A session-level setting rather than a container one — the user picks an
     * effect while playing — so it is supplied after construction.
     */
    private String vkbasaltConfig = "";

    public void setVkBasaltConfig(String config) {
        this.vkbasaltConfig = config == null ? "" : config;
    }

    // --- Version comparison --------------------------------------------------
    //
    // DXVK builds are named things like "dxvk-2.3.1-arm64ec-gplasync", so the
    // comparison has to find a version inside a longer string rather than parse
    // the whole of it. Loose on purpose: anything unparseable sorts as 0.0.0,
    // which selects the conservative code path rather than throwing during a
    // launch.

    private static final Pattern SEMVER_LOOSE =
            Pattern.compile("(\\d+)\\.(\\d+)(?:\\.(\\d+))?");

    private static int compareVersion(String varA, String varB) {
        int[] a = parseSemverLoose(varA);
        int[] b = parseSemverLoose(varB);
        if (a[0] != b[0]) return a[0] - b[0];
        if (a[1] != b[1]) return a[1] - b[1];
        return a[2] - b[2];
    }

    private static int[] parseSemverLoose(String s) {
        if (s == null) return new int[]{0, 0, 0};

        Matcher m = SEMVER_LOOSE.matcher(s);
        String g1 = null, g2 = null, g3 = null;
        // Last match wins: the trailing group is the version in names that
        // carry more than one number.
        while (m.find()) {
            g1 = m.group(1);
            g2 = m.group(2);
            g3 = m.group(3);
        }
        if (g1 == null || g2 == null) return new int[]{0, 0, 0};
        return new int[]{safeParseInt(g1), safeParseInt(g2), safeParseInt(g3)};
    }

    private static int safeParseInt(String s) {
        if (s == null || s.isEmpty()) return 0;
        try {
            return Integer.parseInt(s);
        } catch (NumberFormatException ignored) {
            return 0;
        }
    }

    /** Floor and ceiling for the video memory Turnip reports. See the call site. */
    private static final int MIN_REPORTED_HEAP_MB = 1024;

    /**
     * A third of RAM, not a half.
     *
     * The first version of this took half, which on a 16 GB Thor reports 7.6 GB
     * of video memory — and this device demonstrably runs out: a Steam download
     * on it has been observed driving the kernel's low-memory killer to take out
     * the system launcher. Handing DXVK a budget that large on hardware with no
     * dedicated video memory at all invites it to plan a working set that
     * nothing else on the device survives.
     */
    private static final int HEAP_SHARE_OF_RAM = 3;
    private static final int MAX_REPORTED_HEAP_MB = 6144;

    /**
     * A share of physical RAM, in megabytes, clamped to something a driver can use.
     *
     * Falls back to the floor rather than to zero if the device will not say how
     * much memory it has: an unset heap is Turnip's own conservative default and
     * a zero one is an invalid answer that DXVK would treat as "no memory".
     */
    private static int reportedHeapSizeMB(Context context) {
        try {
            android.app.ActivityManager manager = (android.app.ActivityManager)
                    context.getSystemService(Context.ACTIVITY_SERVICE);
            android.app.ActivityManager.MemoryInfo info = new android.app.ActivityManager.MemoryInfo();
            manager.getMemoryInfo(info);

            long shareMB = (info.totalMem / (1024L * 1024L)) / HEAP_SHARE_OF_RAM;
            return (int) Math.max(MIN_REPORTED_HEAP_MB, Math.min(shareMB, MAX_REPORTED_HEAP_MB));
        } catch (Exception e) {
            Log.w(TAG, "Could not read the device's memory size; reporting the minimum heap", e);
            return MIN_REPORTED_HEAP_MB;
        }
    }

    public void setOnExtractFileListener(OnExtractFileListener listener) {
        this.onExtractFileListener = listener;
    }

    /** The startup selection actually in force, resolved from shortcut then container. */
    public String getStartupSelection() {
        return startupSelection;
    }

        public void setupWineSystemFiles() {
            String imgVersion = String.valueOf(imageFs.getVersion());
            boolean containerDataChanged = false;

            File commonWinhandler = new File(
                    container.getRootDir(), ".wine/drive_c/windows/winhandler.exe");
            boolean commonRuntimeReady = commonWinhandler.isFile();
            if (!container.getExtra("imgVersion").equals(imgVersion) || !commonRuntimeReady) {
                if (!commonRuntimeReady && container.getExtra("imgVersion").equals(imgVersion)) {
                    Log.w(TAG, "Container " + container.id
                            + " recorded image v" + imgVersion
                            + " but its common runtime is absent; repairing the prefix");
                }
                if (!applyGeneralPatches(container)) {
                    throw new IllegalStateException(
                            "Could not provision the container's common runtime files");
                }
                container.putExtra("imgVersion", imgVersion);
                containerDataChanged = true;
            }

            /*
             * The identity of the wrapper set to unpack.
             *
             * Named apart from the field rather than shadowing it, which is what
             * The original did — `String dxwrapper = this.dxwrapper` and then
             * reassigned. For "dxvk" this expands into the three components
             * actually extracted, and that expanded form is what gets recorded
             * on the container so the next launch can tell whether anything
             * changed.
             */
            String wrapperSet = dxwrapper;

            if (wrapperSet.contains("dxvk")) {
                String dxvkWrapper = "dxvk-" + dxwrapperConfig.get("version");
                String vkd3dWrapper = "vkd3d-" + dxwrapperConfig.get("vkd3dVersion");
                String ddrawrapper = dxwrapperConfig.get("ddrawrapper");
                wrapperSet = dxvkWrapper + ";" + vkd3dWrapper + ";" + ddrawrapper;
            }

            /*
             * The revision joins the key, so a fix to *how* a selection unpacks
             * reaches containers that already exist.
             *
             * This guard compares the selection against the one recorded last
             * time and skips the work when they agree — which is right, and
             * which also means a container whose DXVK never installed correctly
             * would go on never installing it, because the wrong outcome was
             * recorded under the right name. Bumping this re-runs the
             * extraction once everywhere and then goes quiet again. Kept out of
             * the string handed to `extractDXWrapperFiles`, which splits on `;`
             * and reads three fields.
             */
            String wrapperKey = wrapperSet + ";r" + DXWRAPPER_REVISION;
            boolean wrapperMaterialized = isSelectedWrapperMaterialized(wrapperSet);
            if (!wrapperKey.equals(container.getExtra("dxwrapper")) || !wrapperMaterialized) {
                if (!wrapperMaterialized && wrapperKey.equals(container.getExtra("dxwrapper"))) {
                    Log.w(TAG, "Container " + container.id + " recorded " + wrapperSet
                            + " but its DLLs do not match; reapplying the selected runtime");
                }
                if (!extractDXWrapperFiles(wrapperSet)) {
                    throw new IllegalStateException(
                            "Could not provision the selected DirectX runtime: " + wrapperSet);
                }
                container.putExtra("dxwrapper", wrapperKey);
                containerDataChanged = true;
            }

            String wincomponents = requestedWinComponents();
            boolean winComponentsMaterialized = areNativeWinComponentsMaterialized(
                    imageFs.getRootDir(), wincomponents);
            if (!wincomponents.equals(container.getExtra("wincomponents"))
                    || !winComponentsMaterialized) {
                if (!winComponentsMaterialized
                        && wincomponents.equals(container.getExtra("wincomponents"))) {
                    Log.w(TAG, "Container " + container.id + " recorded its native Windows "
                            + "components but required DLLs are missing; reapplying them");
                }
                if (!extractWinComponentFiles(!winComponentsMaterialized)) {
                    throw new IllegalStateException(
                            "Could not provision the selected Windows components");
                }
                if (!areNativeWinComponentsMaterialized(imageFs.getRootDir(), wincomponents)) {
                    throw new IllegalStateException(
                            "Selected native Windows components did not materialize completely");
                }
                container.putExtra("wincomponents", wincomponents);
                containerDataChanged = true;
            }

            /*
             * A bare container does not reveal which executable the user will
             * start. Pre-stage XAudio and add executable-scoped overrides so
             * Skyrim's post-logo mixer works even when it is launched from the
             * guest file manager. The archive is installed once; only Skyrim
             * selects these native DLLs, leaving every other title's component
             * policy untouched.
             */
            if (!SkyrimAudioCompatibility.PROVISIONING_REVISION.equals(
                    container.getExtra("skyrimAudioProvisioning"))) {
                if (!provisionSkyrimAudioCompatibility()) {
                    throw new IllegalStateException(
                            "Could not provision Skyrim's XAudio compatibility runtime");
                }
                container.putExtra("skyrimAudioProvisioning",
                        SkyrimAudioCompatibility.PROVISIONING_REVISION);
                containerDataChanged = true;
            }

            /*
             * Revision 1 forced Bethesda launchers onto WineD3D. Device traces
             * disproved that workaround and showed it changed the detected GPU.
             * Remove those executable overrides; launcher input is fixed in the
             * renderer by preserving the launcher's real window geometry.
             */
            if (!BethesdaLauncherCompatibility.PROVISIONING_REVISION.equals(
                    container.getExtra("bethesdaLauncherCompatibility"))) {
                removeObsoleteBethesdaLauncherCompatibility();
                container.putExtra("bethesdaLauncherCompatibility",
                        BethesdaLauncherCompatibility.PROVISIONING_REVISION);
                containerDataChanged = true;
            }

            /*
             * Fallout 3's lower-screen mod is a game-local dinput8 proxy.
             * Wine's built-in DLL otherwise wins before the proxy is examined,
             * so prepare every prefix with an executable-scoped override. This
             * is safe for mixed WEMU containers and also provisions Companion's
             * fresh predefined container without a separate registry step.
             */
            if (!Fallout3Compatibility.PROVISIONING_REVISION.equals(
                    container.getExtra("fallout3Compatibility"))) {
                provisionFallout3Compatibility();
                container.putExtra("fallout3Compatibility",
                        Fallout3Compatibility.PROVISIONING_REVISION);
                containerDataChanged = true;
            }
            // The shared WEMU desktop can launch Fallout through a guest-side
            // shortcut, in which case WemuSession has no Android Shortcut
            // object to identify. Verify the scoped INI fix at every container
            // bootstrap so launcher/MCTF rewrites cannot silently re-enable the
            // pathological combat/actor profile.
            boolean falloutSettingsReady =
                    Fallout3Compatibility.applyRuntimeSettings(new File(
                            imageFs.getRootDir(),
                            ImageFs.WINEPREFIX.substring(1)));
            if (falloutSettingsReady)
                Log.i(TAG, "Verified Fallout 3 performance-profile INIs");

            int bethesdaProfiles = BethesdaDisplayCompatibility.applyExistingProfiles(
                    new File(imageFs.getRootDir(), ImageFs.WINEPREFIX.substring(1)));
            if (bethesdaProfiles > 0) {
                Log.i(TAG, "Verified " + bethesdaProfiles
                        + " Fallout/Skyrim display profile(s)");
            }

            /*
             * The key carries everything that can change the desktop's look.
             *
             * The revision so that replacing the generated background re-themes
             * containers that have already booted, and the rendered wallpaper's
             * timestamp so that changing the launcher's theme reaches the guest
             * desktop too. Without the timestamp the app could redraw its
             * wallpaper all it liked and the container would keep the BMP it
             * baked on first boot — which is exactly how the two came to
             * disagree.
             */
            String desktopTheme = container.getDesktopTheme();
            File rendered = WineThemeManager.getSharedWallpaperFile(context);
            /*
             * The DPI and the style revision join the key.
             *
             * The visual style is generated at the container's DPI — its corner
             * radii, glyphs and caption height are all real pixels — so a
             * container whose resolution changed needs it rebuilt even though
             * nothing about the palette moved.
             */
            String themeKey = desktopTheme + "," + xServer.screenInfo
                    + ",wp" + WineThemeManager.WALLPAPER_REVISION
                    + ",wm" + WineThemeManager.METRICS_REVISION
                    + ",vs" + WineVisualStyle.REVISION
                    + ",ic" + WineIconTheme.REVISION
                    + ",dpi" + container.getDPI()
                    + "," + WineThemeManager.paletteFingerprint(context)
                    + (rendered.isFile() ? "," + rendered.lastModified() : "");
            if (!themeKey.equals(container.getExtra("desktopTheme"))) {
                boolean preserveWineRuntime = "1".equals(
                        container.getExtra("wemuOfficialSteamRuntime"));
                WineThemeManager.apply(context, new WineThemeManager.ThemeInfo(desktopTheme),
                        xServer.screenInfo, container.getDPI(), preserveWineRuntime);
                container.putExtra("desktopTheme", themeKey);
                containerDataChanged = true;
            }

            if (stripGuestHud()) containerDataChanged = true;

            WineStartMenuCreator.create(context, container);
            WineUtils.createDosdevicesSymlinks(container);
            WineUtils.setWindowDecorations(container, false);

            int inputType = container.getInputType();
            if (shortcut != null) {
                String shortcutInputType = shortcut.getExtra("inputType");
                if (!shortcutInputType.isEmpty()) {
                    inputType = Byte.parseByte(shortcutInputType);
                }
            }
            boolean dinputEnabled = (inputType & WinHandler.FLAG_INPUT_TYPE_DINPUT) == WinHandler.FLAG_INPUT_TYPE_DINPUT;

            boolean exclusiveXInput = container.isExclusiveXInput();
            if (shortcut != null) {
                String extra = shortcut.getExtra("exclusiveXInput");
                if (!extra.isEmpty())
                    exclusiveXInput = extra.equals("1");
            }

            WineUtils.setJoystickRegistryKeys(container, dinputEnabled, exclusiveXInput);

            if (shortcut != null)
                startupSelection = shortcut.getExtra("startupSelection", String.valueOf(container.getStartupSelection()));
            else
                startupSelection = String.valueOf(container.getStartupSelection());

            if (!startupSelection.equals(container.getExtra("startupSelection"))) {
                WineUtils.changeServicesStatus(container, startupSelection);
                container.putExtra("startupSelection", startupSelection);
                containerDataChanged = true;
            }
            if (containerDataChanged && !container.saveData()) {
                throw new IllegalStateException("Could not persist container bootstrap state");
            }
        }

        /** Verifies user-installed DXVK/VKD3D profiles against the active prefix. */
        private boolean isSelectedWrapperMaterialized(String wrapperSet) {
            if (wrapperSet == null || !wrapperSet.contains("dxvk")) return true;
            String[] selected = wrapperSet.split(";", -1);
            if (selected.length < 2) return false;
            ContentProfile dxvkProfile = contentsManager.getProfileByEntryName(selected[0]);
            if (dxvkProfile != null && !contentsManager.isContentApplied(dxvkProfile)) return false;
            if (!selected[1].contains("None")) {
                ContentProfile vkd3dProfile = contentsManager.getProfileByEntryName(selected[1]);
                if (vkd3dProfile != null && !contentsManager.isContentApplied(vkd3dProfile)) return false;
            }
            return true;
        }

        /**
         * Removes the guest-drawn overlays from a container, exactly once.
         *
         * `DXVK_HUD` was in every container's default environment, so it is
         * already saved into every container created before it was dropped from
         * the defaults — changing the constant alone fixes new containers and
         * nothing else.
         *
         * Once, though, and not on every launch: the environment editor is
         * there so people can set what they like, and a session that silently
         * deleted a variable each time it started would make that editor a liar.
         * The marker is what separates "inherited from a default nobody chose"
         * from "typed on purpose".
         *
         * @return whether the container's data changed and needs saving.
         */
        private boolean stripGuestHud() {
            if (!container.getExtra("guestHudStripped").isEmpty()) return false;

            EnvVars stored = new EnvVars();
            stored.putAll(container.getEnvVars());

            boolean removedAny = false;
            for (String name : Container.GUEST_HUD_VARS) {
                if (stored.has(name)) {
                    stored.remove(name);
                    removedAny = true;
                }
            }

            if (removedAny) {
                Log.d(TAG, "Removed the guest overlay variables from container " + container.id);
                container.setEnvVars(stored.toString());
            }

            container.putExtra("guestHudStripped", "1");
            return true;
        }

        public void extractGraphicsDriverFiles() {
            String adrenoToolsDriverId = graphicsDriverConfig.get("version");

            Log.d("GraphicsDriverExtraction", "Adrenotools DriverID: " + adrenoToolsDriverId);

            File rootDir = imageFs.getRootDir();

            if (dxwrapper.contains("dxvk")) {
                /*
                 * The FPS cap binds the game, not the screen, when frames are
                 * being generated.
                 *
                 * With generation off this changes nothing — the cap was never
                 * enforced here and stays a hint. With it on, the cap has to
                 * hold the *game* down so the layer has room to multiply: cap
                 * 30 at 2x should be a game running at 30 and 60 frames
                 * reaching the panel. Held here because DXVK is the last thing
                 * in the guest that counts presents before the layer sees them.
                 */
                int baseFpsCap = FrameGeneration.isEnabled(container)
                        ? RendererOptions.fpsLimitOf(container)
                        : 0;
                DXWrapperConfig.setDxvkEnvVars(context, dxwrapperConfig, envVars, baseFpsCap);
                String version = dxwrapperConfig.get("version");
                if (version.equals("1.11.1-sarek")) {
                    Log.d("GraphicsDriverExtraction", "Disabling Wrapper PATCH_OPCONSTCOMP SPIR-V pass");
                    envVars.put("WRAPPER_NO_PATCH_OPCONSTCOMP", "1");
                }
            } else {
                DXWrapperConfig.setWineD3DEnvVars(context, dxwrapperConfig, envVars);
            }

            boolean useDRI3 = preferences.getBoolean("use_dri3", true);
            if (!useDRI3) {
                envVars.put("MESA_VK_WSI_DEBUG", "sw");
            }

            envVars.put("VK_ICD_FILENAMES", new File(imageFs.getShareDir(), WRAPPER_ICD_PATH).getPath());
            envVars.put("GALLIUM_DRIVER", "zink");

            /*
             * Extract when told to, and also whenever the result is missing.
             *
             * `firstTimeBoot` alone is a promise about history, and history can
             * be wrong: the container records that it has booted before
             * `extractGraphicsDriverFiles` runs, so a session that failed after
             * that point leaves a container which believes it is set up and an
             * image that never received the wrapper ICD. Every launch after that
             * skips this block, `VK_ICD_FILENAMES` points at a file that does
             * not exist, and Wine exits the moment it asks for a Vulkan device —
             * which looks exactly like a container that unpacks and closes.
             *
             * Checking for the file the environment actually points at makes the
             * condition a fact rather than a belief, and lets an install that was
             * interrupted repair itself on the next launch.
             */
            File wrapperIcd = new File(imageFs.getShareDir(), WRAPPER_ICD_PATH);
            if (firstTimeBoot || !wrapperIcd.isFile()) {
                Log.d(TAG, firstTimeBoot
                        ? "First boot for this container; extracting graphics libraries"
                        : "Wrapper ICD missing at " + wrapperIcd + "; re-extracting graphics libraries");
                GuestPayloads.extractOrFetch(
                        context, "graphics_driver/wrapper.tzst", rootDir, null);
                GuestPayloads.extractOrFetch(context, "layers.tzst", rootDir, null);
                GuestPayloads.extractOrFetch(
                        context, "graphics_driver/extra_libs.tzst", rootDir, null);

                if (!wrapperIcd.isFile()) {
                    // Worth saying out loud: everything downstream will now fail
                    // in a way that points at Wine rather than at this.
                    throw new IllegalStateException(
                            "Graphics runtime provisioning did not produce " + wrapperIcd);
                }
            }

            if (!"System".equals(adrenoToolsDriverId)) {
                AdrenotoolsManager adrenotoolsManager = new AdrenotoolsManager(context);
                if (!adrenotoolsManager.setDriverById(
                        envVars, imageFs, adrenoToolsDriverId)) {
                    throw new IllegalStateException(
                            "The selected GPU driver is missing or incomplete: "
                                    + adrenoToolsDriverId);
                }
            }

            /*
             * What Turnip tells the guest its video memory is.
             *
             * An Adreno has no video memory of its own — it shares the system's
             * — so there is no true answer here, only a useful one. DXVK budgets
             * against whatever it is told: report too little and it evicts and
             * re-uploads resources while the device still has memory free;
             * report more than exists and an allocation fails somewhere the
             * guest handles badly.
             *
             * So it is derived from the device rather than fixed. Half of
             * physical RAM is the share a foreground game can realistically hold
             * without the launcher, the compositor and Android itself being
             * pushed out — and the clamps matter more than the fraction: the
             * floor keeps a small device from reporting a heap so tiny that DXVK
             * thrashes on every texture, and the ceiling exists because the
             * number is a budget rather than a reservation, and a 24 GB handheld
             * reporting 12 GB of video memory would have DXVK plan for a working
             * set nothing else on the device could survive.
             *
             * `MESA_NO_ERROR` drops Mesa's validation of the calls the
             * translation layer makes. That checking exists to find bugs in an
             * application, and DXVK is not the application here — it generates
             * the calls, it is well tested, and nobody reads the complaint.
             * `MESA_DEBUG=silent` keeps what is left off the log.
             *
             * Measured as neutral on an Adreno 740 rather than as a win. It is
             * set because the reasoning holds, not because a frame counter moved.
             */
            envVars.put("TU_OVERRIDE_HEAP_SIZE", String.valueOf(reportedHeapSizeMB(context)));
            envVars.put("MESA_NO_ERROR", "1");
            envVars.put("MESA_DEBUG", "silent");

            String autotune = TurnipPerformanceOptions.resolveAutotune(
                    graphicsDriverConfig.get(TurnipPerformanceOptions.CONFIG_AUTOTUNE));
            envVars.put("TU_AUTOTUNE_ALGO", autotune);

            if (TurnipPerformanceOptions.gpuTraceEnabled(
                    graphicsDriverConfig.get(TurnipPerformanceOptions.CONFIG_GPU_TRACE))) {
                File diagnostics = new File(container.getRootDir(), ".wemu-diagnostics");
                if (diagnostics.isDirectory() || diagnostics.mkdirs()) {
                    File[] older = diagnostics.listFiles((dir, name) ->
                            name.startsWith("turnip-gpu-") && name.endsWith(".json"));
                    if (older != null && older.length >= MAX_TURNIP_TRACES) {
                        Arrays.sort(older, (left, right) -> Long.compare(
                                right.lastModified(), left.lastModified()));
                        for (int index = MAX_TURNIP_TRACES - 1; index < older.length; index++) {
                            if (!older[index].delete()) {
                                Log.w(TAG, "Could not remove old Turnip trace "
                                        + older[index].getAbsolutePath());
                            }
                        }
                    }
                    File trace = new File(diagnostics,
                            "turnip-gpu-" + System.currentTimeMillis() + ".json");
                    envVars.put("MESA_GPU_TRACES", "print_json");
                    envVars.put("MESA_GPU_TRACEFILE", trace.getAbsolutePath());
                    Log.i(TAG, "Turnip GPU timing trace: " + trace.getAbsolutePath());
                } else {
                    Log.w(TAG, "Turnip GPU timing trace disabled: could not create "
                            + diagnostics.getAbsolutePath());
                }
            }

            String vulkanVersion = graphicsDriverConfig.get("vulkanVersion");
            String patch = vulkanPatchLevel(adrenoToolsDriverId);
            envVars.put("WRAPPER_VK_VERSION", vulkanVersion + "." + patch);

            String blacklistedExtensions = graphicsDriverConfig.get("blacklistedExtensions");
            envVars.put("WRAPPER_EXTENSION_BLACKLIST", blacklistedExtensions);

            String gpuName = graphicsDriverConfig.get("gpuName");
            String dxvkVersion = dxwrapperConfig.get("version");
            if (!gpuName.equals("Device") && !dxvkVersion.equals("1.11.1-sarek")) {
                envVars.put("WRAPPER_DEVICE_NAME", gpuName);
                envVars.put("WRAPPER_DEVICE_ID", DXWrapperConfig.getDeviceIdFromGPUName(context, gpuName));
                envVars.put("WRAPPER_VENDOR_ID", DXWrapperConfig.getVendorIdFromGPUName(context, gpuName));
            }

            String maxDeviceMemory = graphicsDriverConfig.get("maxDeviceMemory");
            if (maxDeviceMemory != null && Integer.parseInt(maxDeviceMemory) > 0)
                envVars.put("WRAPPER_VMEM_MAX_SIZE", maxDeviceMemory);

            /*
             * The *guest's* present mode, left as the container set it — frame
             * generation does not touch it.
             *
             * It is worth saying so, because forcing it to FIFO here was tried
             * and it did nothing. Generated frames were being discarded, the
             * guest looked like the place the frames were stuck, and it was the
             * wrong guest: the stall came from this app's own compositor waiting
             * for vblank, one process further out. `RendererOptions
             * .hostPresentMode` is the setting that matters and carries the
             * measurements.
             */
            String presentMode = graphicsDriverConfig.get("presentMode");
            if (presentMode.contains("immediate")) {
                envVars.put("WRAPPER_MAX_IMAGE_COUNT", "1");
            }
            envVars.put("MESA_VK_WSI_PRESENT_MODE", presentMode);

            String resourceType = graphicsDriverConfig.get("resourceType");
            envVars.put("WRAPPER_RESOURCE_TYPE", resourceType);

            String syncFrame = graphicsDriverConfig.get("syncFrame");
            if (syncFrame.equals("1"))
                envVars.put("MESA_VK_WSI_DEBUG", "forcesync");

            String disablePresentWait = graphicsDriverConfig.get("disablePresentWait");
            envVars.put("WRAPPER_DISABLE_PRESENT_WAIT", disablePresentWait);

            String bcnEmulation = graphicsDriverConfig.get("bcnEmulation");
            String bcnEmulationType = graphicsDriverConfig.get("bcnEmulationType");

            switch (bcnEmulation) {
                case "auto" -> {
                    if (bcnEmulationType.equals("compute") && GPUInformation.getVendorID(null, null) != 0x5143) {
                        envVars.put("ENABLE_BCN_COMPUTE", "1");
                        envVars.put("BCN_COMPUTE_AUTO", "1");
                    }
                    envVars.put("WRAPPER_EMULATE_BCN", "3");
                }
                case "full" -> {
                    if (bcnEmulationType.equals("compute") && GPUInformation.getVendorID(null, null) != 0x5143) {
                        envVars.put("ENABLE_BCN_COMPUTE", "1");
                        envVars.put("BCN_COMPUTE_AUTO", "0");
                    }
                    envVars.put("WRAPPER_EMULATE_BCN", "2");
                }
                case "none" -> envVars.put("WRAPPER_EMULATE_BCN", "0");
                default -> envVars.put("WRAPPER_EMULATE_BCN", "1");
            }

            String bcnEmulationCache = graphicsDriverConfig.get("bcnEmulationCache");
            envVars.put("WRAPPER_USE_BCN_CACHE", bcnEmulationCache);

            if (!vkbasaltConfig.isEmpty()) {
                envVars.put("ENABLE_VKBASALT", "1");
                envVars.put("VKBASALT_CONFIG", vkbasaltConfig);
            }
        }

        private static final int MAX_TURNIP_TRACES = 8;

        /**
         * Unpacks a bundled archive, and says so when there is not one.
         *
         * {@link TarCompressorUtils#extract} answers a missing asset with
         * `false` — the `IOException` from `AssetManager.open` never leaves it —
         * and every call site here discarded that answer. The visible result was
         * a container that reported the wrapper the user chose while running
         * whichever one happened to be in the prefix already, because "extract
         * nothing" and "extract the right thing" looked identical from the
         * outside. Anything reaching this branch has already failed to resolve
         * as a user profile, so a miss here means the selection matched nothing
         * at all and is worth an error rather than a shrug.
         */
        private boolean extractBundled(String assetPath, File destination) {
            boolean extracted = GuestPayloads.extractOrFetch(context, assetPath,
                    destination, onExtractFileListener);
            if (!extracted) {
                Log.e(TAG, "No bundled archive at assets/" + assetPath
                        + " — the selection it came from will have no effect");
            }
            return extracted;
        }

        private boolean extractDXWrapperFiles(String dxwrapper) {
            final String[] dlls = { "d3d10.dll", "d3d10_1.dll", "d3d10core.dll", "d3d11.dll", "d3d12.dll", "d3d12core.dll",
                    "d3d8.dll", "d3d9.dll", "dxgi.dll", "ddraw.dll", "d3dimm.dll" };

            File rootDir = imageFs.getRootDir();
            File windowsDir = new File(rootDir, ImageFs.WINEPREFIX + "/drive_c/windows");
            boolean success = true;

            if (dxwrapper.contains("dxvk")) {
                Log.d(TAG, "Extracting DXVK wrapper files, version: " + dxwrapper);

                String dxvkWrapper = dxwrapper.split(";")[0];
                String vkd3dWrapper = dxwrapper.split(";")[1];
                String ddrawrapper = dxwrapper.split(";")[2];

                ContentProfile dxvkProfile = contentsManager.getProfileByEntryName(dxvkWrapper);
                if (dxvkProfile != null) {
                    Log.d(TAG, "Applying user-defined DXVK content profile: " + dxvkWrapper);
                    success &= contentsManager.applyContent(dxvkProfile);
                } else {
                    Log.d(TAG, "Extracting fallback DXVK .tzst archive: " + dxvkWrapper);
                    success &= extractBundled(
                            "dxwrapper/" + dxvkWrapper + ".tzst", windowsDir);

                    if (compareVersion(dxvkWrapper, "2.4") < 0) {
                        Log.d(TAG, "Extracting d8vk as part of DXVK version " + dxvkWrapper);
                        success &= extractBundled(
                                "dxwrapper/d8vk-" + DefaultVersion.D8VK + ".tzst", windowsDir);
                    }
                }

                if (vkd3dWrapper.contains("None")) {
                    Log.d(TAG, "No VKD3D has been selected, restoring original d3d12");
                    success &= restoreOriginalDllFiles(
                            new String[] { "d3d12.dll", "d3d12core.dll" });
                } else {
                    ContentProfile vkd3dProfile = contentsManager.getProfileByEntryName(vkd3dWrapper);
                    if (vkd3dProfile != null) {
                        Log.d(TAG, "Applying user-defined VKD3D content profile: " + vkd3dWrapper);
                        success &= contentsManager.applyContent(vkd3dProfile);
                    } else {
                        Log.d(TAG, "Extracting fallback VKD3D .tzst archive: " + vkd3dWrapper);
                        success &= extractBundled(
                                "dxwrapper/" + vkd3dWrapper + ".tzst", windowsDir);
                    }
                }

                Log.d(TAG, "Extracting nglide wrapper");
                success &= extractBundled("ddrawrapper/nglide.tzst", windowsDir);

                /*
                 * Case-insensitive, because the value that reaches here is
                 * lower case. `Container.DEFAULT_DDRAWRAPPER` is the string
                 * "none", and this asked whether it contained "None" — so the
                 * default never took this branch. Every container instead fell
                 * through to extracting `ddrawrapper/none.tzst`, which does not
                 * exist, and the original ddraw files were never restored. The
                 * miss was invisible until `extractBundled` started reporting
                 * archives that are not there.
                 */
                if (ddrawrapper.equalsIgnoreCase("none")) {
                    Log.d(TAG, "No DDRaw wrapper has been selected, restoring original ddraw files");
                    success &= restoreOriginalDllFiles(
                            new String[] { "ddraw.dll", "d3dimm.dll" });
                } else {
                    if (ddrawrapper.equals("cnc-ddraw"))
                        envVars.put("CNC_DDRAW_CONFIG_FILE", "C:\\windows\\syswow64\\ddraw.ini");

                    Log.d(TAG, "Extracting ddrawrapper " + ddrawrapper);
                    success &= extractBundled(
                            "ddrawrapper/" + ddrawrapper + ".tzst", windowsDir);
                }

                Log.d(TAG, "Finished extraction of DXVK wrapper files, version: " + dxwrapper);
            } else if (dxwrapper.contains("wined3d")) {
                Log.d(TAG, "Restoring original DLL files for wined3d.");
                success &= restoreOriginalDllFiles(dlls);
            }
            return success;
        }

        private boolean extractWinComponentFiles(boolean forceRefresh) {
            Log.d("XServerDisplayActivity", "Extracting WinComponents");
            File rootDir = imageFs.getRootDir();
            File windowsDir = new File(rootDir, ImageFs.WINEPREFIX + "/drive_c/windows");
            File systemRegFile = new File(rootDir, ImageFs.WINEPREFIX + "/system.reg");
            boolean success = true;

            try {
                JSONObject wincomponentsJSONObject = new JSONObject(
                        FileUtils.readString(context, "wincomponents/wincomponents.json"));
                ArrayList<String> dlls = new ArrayList<>();
                String wincomponents = requestedWinComponents();

                KeyValueSet oldWinComponents = new KeyValueSet(
                        container.getExtra("wincomponents", Container.FALLBACK_WINCOMPONENTS));

                for (String[] wincomponent : new KeyValueSet(wincomponents)) {
                    String identifier = wincomponent[0];
                    if (!forceRefresh
                            && wincomponent[1].equals(oldWinComponents.get(identifier))
                            && !firstTimeBoot)
                        continue;
                    boolean useNative = wincomponent[1].equals("1");

                    if (useNative) {
                        boolean extracted = TarCompressorUtils.extract(
                                TarCompressorUtils.Type.ZSTD, context,
                                "wincomponents/" + identifier + ".tzst", windowsDir,
                                onExtractFileListener);
                        if (!extracted) {
                            Log.e(TAG, "Could not provision native Windows component "
                                    + identifier);
                            success = false;
                            continue;
                        }
                    } else {
                        JSONArray dlnames = wincomponentsJSONObject.getJSONArray(identifier);
                        for (int i = 0; i < dlnames.length(); i++) {
                            String dlname = dlnames.getString(i);
                            dlls.add(!dlname.endsWith(".exe") ? dlname + ".dll" : dlname);
                        }
                    }
                    Log.d("XServerDisplayActivity",
                            "Setting wincomponent " + identifier + " to " + String.valueOf(useNative));
                    WineUtils.overrideWinComponentDlls(context, container, identifier, useNative);
                    WineUtils.setWinComponentRegistryKeys(systemRegFile, identifier, useNative, context);
                }

                if (!dlls.isEmpty())
                    success &= restoreOriginalDllFiles(dlls.toArray(new String[0]));
            } catch (JSONException e) {
                Log.e(TAG, "Could not read Windows component manifest", e);
                return false;
            }
            return success;
        }

        /**
         * A component receipt is only useful when the files it represents are
         * still in this prefix. Interrupted first boots and copied WEMU preset
         * receipts can otherwise make bootstrap skip extraction forever.
         *
         * Requiring both system32 and syswow64 is intentional: ARM64EC Proton
         * uses the first for its 64-bit side while 32-bit games such as
         * Transformers WFC load native components from the second through the
         * selected HODLL translator.
         */
        static boolean areNativeWinComponentsMaterialized(
                File imageRoot, String wincomponents) {
            File windowsDir = new File(imageRoot, ImageFs.WINEPREFIX + "/drive_c/windows");
            File system32 = new File(windowsDir, "system32");
            File syswow64 = new File(windowsDir, "syswow64");
            KeyValueSet selected = new KeyValueSet(wincomponents);

            for (String[] component : NATIVE_WINCOMPONENT_SENTINELS) {
                if (!selected.getBoolean(component[0], false)) continue;
                for (int i = 1; i < component.length; i++) {
                    if (!new File(system32, component[i]).isFile()
                            || !new File(syswow64, component[i]).isFile()) {
                        return false;
                    }
                }
            }
            return true;
        }

        private static final String[][] NATIVE_WINCOMPONENT_SENTINELS = {
                {"direct3d", "d3dx9_42.dll"},
                {"directsound", "dsound.dll"},
                {"directmusic", "dmusic.dll"},
                {"directshow", "quartz.dll"},
                {"directplay", "dplayx.dll"},
                {"xaudio", "xactengine3_0.dll", "xaudio2_7.dll"},
                {"vcrun2010", "msvcr100.dll"},
        };

        /**
         * The user's component selection, plus compatibility WEMU can prove the
         * current title needs. The container setting itself is not rewritten:
         * another game launched from the same prefix returns to its configured
         * component set on its next bootstrap.
         */
        private String requestedWinComponents() {
            String configured = shortcut != null
                    ? shortcut.getExtra("wincomponents", container.getWinComponents())
                    : container.getWinComponents();
            if (!skyrimAudioCompatibility) return configured;

            String compatible = SkyrimAudioCompatibility.enableRequiredComponents(configured);
            if (!compatible.equals(configured)) {
                Log.i(TAG, "Skyrim audio compatibility provisioning native XAudio/XACT");
            }
            return compatible;
        }

        private boolean provisionSkyrimAudioCompatibility() {
            File rootDir = imageFs.getRootDir();
            File windowsDir = new File(rootDir, ImageFs.WINEPREFIX + "/drive_c/windows");
            boolean extracted = TarCompressorUtils.extract(
                    TarCompressorUtils.Type.ZSTD, context,
                    "wincomponents/xaudio.tzst", windowsDir,
                    onExtractFileListener);
            if (!extracted) {
                Log.e(TAG, "Could not pre-stage Skyrim's native XAudio files");
                return false;
            }

            File userRegFile = new File(rootDir, ImageFs.WINEPREFIX + "/user.reg");
            try (WineRegistryEditor registryEditor = new WineRegistryEditor(userRegFile)) {
                SkyrimAudioCompatibility.applyExecutableOverrides(registryEditor);
            }
            Log.i(TAG, "Pre-staged Skyrim XAudio with executable-scoped DLL overrides");
            return true;
        }

        private void removeObsoleteBethesdaLauncherCompatibility() {
            File userRegFile = new File(imageFs.getRootDir(), ImageFs.WINEPREFIX + "/user.reg");
            try (WineRegistryEditor registryEditor = new WineRegistryEditor(userRegFile)) {
                BethesdaLauncherCompatibility.removeObsoleteExecutableOverrides(registryEditor);
            }
            Log.i(TAG, "Removed obsolete Bethesda launcher D3D9 overrides");
        }

        private void provisionFallout3Compatibility() {
            File userRegFile = new File(imageFs.getRootDir(), ImageFs.WINEPREFIX + "/user.reg");
            try (WineRegistryEditor registryEditor = new WineRegistryEditor(userRegFile)) {
                Fallout3Compatibility.applyExecutableOverrides(registryEditor);
            }
            Log.i(TAG, "Provisioned Fallout 3 executable-scoped dinput8 override");
        }

        private boolean restoreOriginalDllFiles(final String... dlls) {
            File rootDir = imageFs.getRootDir();
            File windowsDir = new File(rootDir, ImageFs.WINEPREFIX + "/drive_c/windows");
            File system32dlls = null;
            File syswow64dlls = null;

            if (wineInfo.isArm64EC())
                system32dlls = new File(imageFs.getWinePath() + "/lib/wine/aarch64-windows");
            else
                system32dlls = new File(imageFs.getWinePath() + "/lib/wine/x86_64-windows");

            syswow64dlls = new File(imageFs.getWinePath() + "/lib/wine/i386-windows");
            boolean success = true;

            for (String dll : dlls) {
                File srcFile = new File(system32dlls, dll);
                File dstFile = new File(windowsDir, "system32/" + dll);
                success &= restoreOriginalDllFile(srcFile, dstFile);
                srcFile = new File(syswow64dlls, dll);
                dstFile = new File(windowsDir, "syswow64/" + dll);
                success &= restoreOriginalDllFile(srcFile, dstFile);
            }
            if (!success) Log.e(TAG, "Could not restore one or more Wine runtime DLLs");
            return success;
        }

        /** Restores the runtime's file state, including an intentionally absent DLL. */
        private boolean restoreOriginalDllFile(File source, File destination) {
            if (source.isFile()) {
                return FileUtils.copy(source, destination) && destination.isFile();
            }
            if (!destination.exists() && !FileUtils.isSymlink(destination)) return true;
            return FileUtils.delete(destination);
        }

        /**
         * Writes the container's audio choice into Wine's registry.
         *
         * Public because the session calls it — upstream ran this as the third
         * step of its startup sequence and it was left private and unreferenced
         * when these methods were lifted off the activity. Without it the
         * registry keeps whatever driver it was last given, so changing a
         * container from ALSA to PulseAudio silently does nothing.
         */
        /**
         * The patch component of the device's Vulkan version, or zero.
         *
         * `GPUInformation.getVulkanVersion` is a JNI call that answers with a
         * `major.minor.patch` string when it can and an empty one when it cannot
         * — an unknown driver id, a query the loader refuses. The original took
         * `split(".")[2]` of the result, so an empty answer became
         * `ArrayIndexOutOfBoundsException: length=1; index=2` and the container
         * failed to open with nothing to suggest a version string was involved.
         *
         * Zero is a safe patch level: `WRAPPER_VK_VERSION` is a floor the
         * wrapper checks against, and claiming less than the device has costs
         * nothing.
         */
        private String vulkanPatchLevel(String driverId) {
            String reported = GPUInformation.getVulkanVersion(driverId, context);
            if (reported == null) return "0";

            String[] parts = reported.split("\\.");
            if (parts.length < 3) {
                Log.w("ContainerBootstrap",
                        "Vulkan version came back as \"" + reported + "\" for driver \""
                                + driverId + "\"; assuming patch 0");
                return "0";
            }
            return parts[2];
        }

        /**
         * Tells Windows how big a pixel is.
         *
         * Wine defaults to 96 DPI and nothing upstream ever changed it, which
         * is why a container set to a high resolution renders every pixel
         * correctly and is still unusable: the desktop, the file manager, every
         * dialog and every caption is laid out for a monitor, then shrunk to the
         * size of a phone. Scaling the resolution down instead costs the game
         * the resolution, which is not the same trade.
         *
         * Written to both places Wine looks. `Control Panel\Desktop\LogPixels`
         * in user.reg is what `winecfg` sets and what current Wine reads; the
         * `Fonts` key in system.reg is the older location, still consulted by
         * some paths and by a few installers that read it directly.
         */
        public void changeWineDPI() {
            int dpi = container.getDPI();
            if (String.valueOf(dpi).equals(container.getExtra("dpi"))) return;

            File rootDir = imageFs.getRootDir();

            try (WineRegistryEditor registryEditor =
                         new WineRegistryEditor(new File(rootDir, ImageFs.WINEPREFIX + "/user.reg"))) {
                registryEditor.setDwordValue("Control Panel\\Desktop", "LogPixels", dpi);
            }

            try (WineRegistryEditor registryEditor =
                         new WineRegistryEditor(new File(rootDir, ImageFs.WINEPREFIX + "/system.reg"))) {
                registryEditor.setDwordValue(
                        "System\\CurrentControlSet\\Hardware Profiles\\Current\\Software\\Fonts",
                        "LogPixels", dpi);
            }

            Log.d(TAG, "Set guest DPI to " + dpi);
            container.putExtra("dpi", String.valueOf(dpi));
            if (!container.saveData()) {
                throw new IllegalStateException("Could not persist the guest DPI state");
            }
        }

        public void changeWineAudioDriver() {
            if (!audioDriver.equals(container.getExtra("audioDriver"))) {
                File rootDir = imageFs.getRootDir();
                File userRegFile = new File(rootDir, ImageFs.WINEPREFIX + "/user.reg");
                try (WineRegistryEditor registryEditor = new WineRegistryEditor(userRegFile)) {
                    if (audioDriver.equals("alsa")) {
                        registryEditor.setStringValue("Software\\Wine\\Drivers", "Audio", "alsa");
                    } else if (audioDriver.equals("pulseaudio")) {
                        registryEditor.setStringValue("Software\\Wine\\Drivers", "Audio", "pulse");
                    }
                }
                container.putExtra("audioDriver", audioDriver);
                if (!container.saveData()) {
                    throw new IllegalStateException("Could not persist the guest audio state");
                }
            }
        }

        /**
         * Maps the common pattern's fixed {@code home/xuser} prefix into one
         * real container directory.
         *
         * <p>{@code imagefs/home/xuser} is the active-container symlink by the
         * time startup reaches this code.  Extracting through it would defeat
         * the archive policy's guarantee that no archive output traverses a
         * link.  The payload contains only {@code home/},
         * {@code home/xuser/}, and descendants, so strip that fixed archive
         * prefix while keeping the extraction root narrowed to xuser-N.  An
         * unexpected top-level entry is an invalid payload, not permission to
         * write elsewhere in imagefs.</p>
         */
        static File mapCommonPatternOutput(File containerRoot, File archivedOutput) {
            if (containerRoot == null || archivedOutput == null) {
                throw new IllegalArgumentException("Common container pattern output is missing");
            }
            try {
                Path root = containerRoot.getCanonicalFile().toPath();
                Path output = archivedOutput.toPath().toAbsolutePath().normalize();
                Path archiveHome = root.resolve("home");
                Path archiveUser = archiveHome.resolve(ImageFs.USER);

                // The archive's parent directory entry carries no content.
                if (output.equals(archiveHome)) return null;
                if (!output.startsWith(archiveUser)) {
                    throw new IllegalArgumentException(
                            "Unexpected common container pattern output: " + archivedOutput);
                }
                return root.resolve(archiveUser.relativize(output)).toFile();
            } catch (IOException e) {
                throw new IllegalArgumentException(
                        "Could not resolve the common container pattern output", e);
            }
        }

        private boolean applyGeneralPatches(Container container) {
            File containerRoot = container.getRootDir();
            boolean commonReady = GuestPayloads.extractOrFetch(
                    context, "container_pattern_common.tzst", containerRoot,
                    (output, size) -> mapCommonPatternOutput(containerRoot, output));
            boolean pulseReady = GuestPayloads.extractOrFetch(context, "pulseaudio.tzst",
                    new File(context.getFilesDir(), "pulseaudio"), null);
            WineUtils.applySystemTweaks(context, wineInfo);
            container.putExtra("graphicsDriver", null);
            container.putExtra("desktopTheme", null);
            if (!commonReady || !pulseReady) {
                Log.e(TAG, "Common runtime payload provisioning failed: common="
                        + commonReady + ", pulseaudio=" + pulseReady);
            }
            return commonReady && pulseReady;
        }
}
