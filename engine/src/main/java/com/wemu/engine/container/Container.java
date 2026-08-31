package com.wemu.engine.container;

import android.os.Environment;
import android.util.Log;

import com.wemu.engine.box64.Box64Preset;
import com.wemu.engine.core.DefaultVersion;
import com.wemu.engine.core.FileUtils;
import com.wemu.engine.core.KeyValueSet;
import com.wemu.engine.core.WineInfo;
import com.wemu.engine.core.WineThemeManager;
import com.wemu.engine.fexcore.FEXCorePreset;
import com.wemu.engine.winhandler.WinHandler;
import com.wemu.engine.xenvironment.ImageFs;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.util.Iterator;

public class Container {
    public enum XrControllerMapping {
        BUTTON_A, BUTTON_B, BUTTON_X, BUTTON_Y, BUTTON_GRIP, BUTTON_TRIGGER,
        THUMBSTICK_UP, THUMBSTICK_DOWN, THUMBSTICK_LEFT, THUMBSTICK_RIGHT
    }
    /**
     * No `DXVK_HUD` here, and that is the fork's whole premise applied.
     *
     * Upstream shipped `DXVK_HUD=devinfo,version,gpuload,fps`, which draws
     * DXVK's own overlay *inside the game's swapchain* — device name, driver
     * version, GPU load and frame rate, in the corner of the picture, on every
     * container, always. The original had a reason: it had nowhere else to put them.
     * This fork has a whole second panel showing the same readings and more, so
     * the overlay is pure cost — it covers part of the game, it is rendered by
     * the guest at the guest's resolution, and it says what the console already
     * says better.
     *
     * Anyone who wants it back can add it in the container's environment
     * editor; see `stripGuestHud` for why doing so survives.
     */
    /*
     * Two of these used to cost a third of the frame rate.
     *
     * `TU_DEBUG` carried `sysmem` as well as `noconform`, which tells Turnip to
     * render straight to system memory and skip the binning pass. That is a
     * compatibility escape hatch for a handful of titles and a large, permanent
     * loss on every other one, because bypassing the tiler is bypassing the
     * thing an Adreno is built around. `DXVK_DISABLE_TIMELINE_SEMAPHORES` is the
     * same shape of mistake one layer up: it forces DXVK back onto the legacy
     * fence path for a driver bug that current Turnip does not have.
     *
     * Nothing replaces them here. The obvious third addition would be
     * `MESA_VK_WSI_PRESENT_MODE`, and it does not belong in this string:
     * `ContainerBootstrap` already sets it from the container's own
     * `graphicsDriverConfig.presentMode`, which has a control in the editor and
     * runs later, so a copy here is both dead and misleading about where the
     * setting lives.
     *
     * `PULSE_LATENCY_MSEC` is not about latency you can hear — it is the size of
     * the buffer the audio thread has to keep fed, and a small one wakes that
     * thread often enough to matter on a device this size.
     */
    public static final String DEFAULT_ENV_VARS = "WRAPPER_MAX_IMAGE_COUNT=0 VKD3D_SHADER_MODEL=6_6 ZINK_DESCRIPTORS=lazy ZINK_DEBUG=compact MESA_SHADER_CACHE_DISABLE=false MESA_SHADER_CACHE_MAX_SIZE=512MB mesa_glthread=true WINEESYNC=1 TU_DEBUG=noconform PULSE_LATENCY_MSEC=144";

    /**
     * Overlays the guest draws over the game, removed once from old containers.
     *
     * `GALLIUM_HUD` and `MANGOHUD` are here as well as DXVK's because all three
     * are the same idea and a container imported from stock the original may carry
     * any of them.
     */
    public static final String[] GUEST_HUD_VARS = {"DXVK_HUD", "GALLIUM_HUD", "MANGOHUD", "MANGOHUD_CONFIG"};
    public static final String DEFAULT_SCREEN_SIZE = "1280x720";

    /**
     * Windows' own default, and the one Wine assumes.
     *
     * Kept as the default here so an existing container behaves exactly as it
     * did. New containers get a scale chosen from their resolution instead —
     * see `Container.suggestedDPI`.
     */
    public static final int DEFAULT_DPI = 96;
    public static final String DEFAULT_GRAPHICS_DRIVER = "wrapper";
    public static final String DEFAULT_AUDIO_DRIVER = "alsa";
    public static final String DEFAULT_EMULATOR = "FEXCore";
    public static final String DEFAULT_DXWRAPPER = "dxvk+vkd3d";
    /*
     * `async=1`, which costs nothing on a build that cannot do it.
     *
     * It becomes `DXVK_ASYNC=1`, which vanilla DXVK ignores and a gplasync build
     * acts on by compiling pipelines off the render thread instead of stalling
     * for them. Shader compilation stalls are most of what a Unity game's first
     * hour feels like, so this is on by default and the build that honours it is
     * the container's choice.
     *
     * `asyncCache` stays off. It persists the async pipelines across runs, and
     * a stale cache produces corruption that looks like a driver fault.
     */
    public static final String DEFAULT_DXWRAPPERCONFIG = "version=" + DefaultVersion.DXVK + ",framerate=0,async=1,asyncCache=0,maxFrameLatency=0" + ",vkd3dVersion=" + DefaultVersion.VKD3D + ",vkd3dLevel=12_1" + ",ddrawrapper=" + Container.DEFAULT_DDRAWRAPPER + ",csmt=3" + ",gpuName=NVIDIA GeForce GTX 480" + ",videoMemorySize=2048" + ",strict_shader_math=1" + ",OffscreenRenderingMode=fbo" + ",renderer=gl";
    public static final String DEFAULT_GRAPHICSDRIVERCONFIG =
            "vulkanVersion=1.3" + ";version=" + ";blacklistedExtensions=" + ";maxDeviceMemory=0" + ";presentMode=mailbox" + ";syncFrame=0" + ";disablePresentWait=0" + ";resourceType=auto" + ";bcnEmulation=auto" + ";bcnEmulationType=compute" + ";bcnEmulationCache=1" + ";gpuName=Device";
    public static final String DEFAULT_DDRAWRAPPER = "none";

    /**
     * Which renderer puts the guest's windows on the glass.
     *
     * "vulkan" is the compositor that has always run here and stays the
     * default. "surfaceflinger" hands each window's buffer straight to
     * SurfaceFlinger and skips compositing entirely, which is faster where it
     * works and cannot do any of the shader-based screen features. See
     * `ASurfaceRenderer`.
     */
    public static final String DEFAULT_DISPLAY_RENDERER = "vulkan";
    public static final String DEFAULT_WINCOMPONENTS = "direct3d=1,directsound=0,directmusic=0,directshow=0,directplay=0,xaudio=0,vcrun2010=1";
    public static final String FALLBACK_WINCOMPONENTS = "direct3d=1,directsound=1,directmusic=1,directshow=1,directplay=1,xaudio=1,vcrun2010=1";
    public static final String DEFAULT_DRIVES = "F:"+Environment.getExternalStorageDirectory().getAbsolutePath()+"D:"+Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
    public static final byte STARTUP_SELECTION_NORMAL = 0;
    public static final byte STARTUP_SELECTION_ESSENTIAL = 1;
    public static final byte STARTUP_SELECTION_AGGRESSIVE = 2;
    public static final byte MAX_DRIVE_LETTERS = 26;
    public final int id;
    private String name;
    private String screenSize = DEFAULT_SCREEN_SIZE;
    private String envVars = DEFAULT_ENV_VARS;
    private String graphicsDriver = DEFAULT_GRAPHICS_DRIVER;
    private String graphicsDriverConfig = DEFAULT_GRAPHICSDRIVERCONFIG;
    private String dxwrapper = DEFAULT_DXWRAPPER;
    private String dxwrapperConfig = "";
    private String wincomponents = DEFAULT_WINCOMPONENTS;
    private String audioDriver = DEFAULT_AUDIO_DRIVER;
    private String displayRenderer = DEFAULT_DISPLAY_RENDERER;
    private String drives = DEFAULT_DRIVES;
    private String wineVersion = WineInfo.MAIN_WINE_VERSION.identifier();
    private boolean showFPS;
    private boolean rendererNative = false;
    private String rendererPresentMode = "fifo";
    private String rendererDriverId = "system";
    private int rendererFilterMode = 0;
    private boolean rendererSwapRB = false;
    private boolean fullscreenStretched;
    private byte startupSelection = STARTUP_SELECTION_ESSENTIAL;
    private String cpuList;
    private String cpuListWoW64;
    private boolean syncCpuTopology = false;
    private String desktopTheme = WineThemeManager.DEFAULT_DESKTOP_THEME;
    private int dpi = DEFAULT_DPI;
    private String fexcoreVersion = DefaultVersion.FEXCORE;
    private String fexcorePreset = FEXCorePreset.INTERMEDIATE;
    private String box64Preset = Box64Preset.COMPATIBILITY;
    private File rootDir;
    private JSONObject extraData;
    private String midiSoundFont = "";
    private int inputType = WinHandler.DEFAULT_INPUT_TYPE;
    private String lc_all = "";
    private int primaryController = 1;
    private String controllerMapping = new String(new char[XrControllerMapping.values().length]);
    private String box64Version = DefaultVersion.BOX64;
    private String emulator = DEFAULT_EMULATOR;
    private boolean exclusiveXInput = true;
    private ContainerManager containerManager;



    public Container(int id) {
        this.id = id;
        this.name = "Container-"+id;
    }

    public Container(int id, ContainerManager containerManager) {
        this.id = id;
        this.name = "Container-"+id;
        this.containerManager = containerManager;
    }

    public ContainerManager getManager() {
        return containerManager;
    }

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public String getScreenSize() {
        return screenSize;
    }

    public void setScreenSize(String screenSize) {
        this.screenSize = screenSize;
    }

    public String getEnvVars() {
        return envVars;
    }

    public void setEnvVars(String envVars) {
        this.envVars = envVars != null ? envVars : "";
    }

    public String getGraphicsDriver() {
        return graphicsDriver;
    }

    public void setGraphicsDriver(String graphicsDriver) {
        this.graphicsDriver = graphicsDriver;
    }

    public String getGraphicsDriverConfig() { return this.graphicsDriverConfig; }

    public void setGraphicsDriverConfig(String graphicsDriverConfig) { this.graphicsDriverConfig = graphicsDriverConfig; }

    public boolean isRendererNative() { return rendererNative; }
    public void setRendererNative(boolean v) { this.rendererNative = v; }
    public String getRendererPresentMode() { return rendererPresentMode; }
    public void setRendererPresentMode(String v) { this.rendererPresentMode = v != null ? v : "fifo"; }
    public String getRendererDriverId() { return rendererDriverId; }
    public void setRendererDriverId(String v) { this.rendererDriverId = v != null ? v : ""; }
    public int getRendererFilterMode() { return rendererFilterMode; }
    public void setRendererFilterMode(int v) { this.rendererFilterMode = v; }
    public boolean getRendererSwapRB() { return rendererSwapRB; }
    public void setRendererSwapRB(boolean v) { this.rendererSwapRB = v; }

    public String getDXWrapper() {
        return dxwrapper;
    }

    public void setDXWrapper(String dxwrapper) {
        this.dxwrapper = dxwrapper;
    }

    public String getDXWrapperConfig() {
        return dxwrapperConfig;
    }

    public void setDXWrapperConfig(String dxwrapperConfig) {
        this.dxwrapperConfig = dxwrapperConfig != null ? dxwrapperConfig : "";
    }

    public String getAudioDriver() {
        return audioDriver;
    }

    public void setAudioDriver(String audioDriver) {
        this.audioDriver = audioDriver;
    }

    public String getDisplayRenderer() {
        return displayRenderer;
    }

    public void setDisplayRenderer(String displayRenderer) {
        this.displayRenderer = displayRenderer != null && !displayRenderer.isEmpty()
                ? displayRenderer : DEFAULT_DISPLAY_RENDERER;
    }

    public String getWinComponents() {
        return wincomponents;
    }

    public void setWinComponents(String wincomponents) {
        this.wincomponents = wincomponents;
    }

    public String getDrives() {
        return drives;
    }

    public void setDrives(String drives) {
        this.drives = drives;
    }

    public String getLC_ALL() {
        return lc_all;
    }

    public void setLC_ALL(String lc_all) {
        this.lc_all = lc_all;
    }

    public int getPrimaryController() {
        return primaryController;
    }

    public void setPrimaryController(int primaryController) {
        this.primaryController = primaryController;
    }

    public byte getControllerMapping(XrControllerMapping input) {
        return (byte) controllerMapping.charAt(input.ordinal());
    }

    public void setControllerMapping(String controllerMapping) {
        this.controllerMapping = controllerMapping;
    }

    public boolean isFullscreenStretched() { return fullscreenStretched; }

    public boolean isShowFPS() {
        return showFPS;
    }

    public void setFullscreenStretched(boolean fullscreenStretched) { this.fullscreenStretched = fullscreenStretched; }

    public void setShowFPS(boolean showFPS) {
        this.showFPS = showFPS;
    }

    public byte getStartupSelection() {
        return startupSelection;
    }

    public void setStartupSelection(byte startupSelection) {
        this.startupSelection = startupSelection;
    }

    public String getCPUList() {
        return getCPUList(false);
    }

    public String getCPUList(boolean allowFallback) {
        return cpuList != null ? cpuList : (allowFallback ? getFallbackCPUList() : null);
    }

    public void setCPUList(String cpuList) {
        this.cpuList = cpuList != null && !cpuList.isEmpty() ? cpuList : null;
    }

    public String getCPUListWoW64() {
        return getCPUListWoW64(false);
    }

    public String getCPUListWoW64(boolean allowFallback) {
        return cpuListWoW64 != null ? cpuListWoW64 : (allowFallback ? getFallbackCPUListWoW64() : null);
    }

    public void setCPUListWoW64(String cpuListWoW64) {
        this.cpuListWoW64 = cpuListWoW64 != null && !cpuListWoW64.isEmpty() ? cpuListWoW64 : null;
    }

    public boolean isSyncCpuTopology() {
        return syncCpuTopology;
    }

    public void setSyncCpuTopology(boolean syncCpuTopology) {
        this.syncCpuTopology = syncCpuTopology;
    }

    public void setFEXCoreVersion(String version) {
        this.fexcoreVersion = version;
    }

    public String getFEXCoreVersion() {
        return this.fexcoreVersion;
    }

    public void setFEXCorePreset(String preset) {
        this.fexcorePreset = preset;
    }

    public String getFEXCorePreset() {
        return fexcorePreset;
    }

    public String getBox64Preset() {
        return box64Preset;
    }

    public void setBox64Preset(String box64Preset) {
        this.box64Preset = box64Preset;
    }

    public String getBox64Version() { return box64Version; }

    public void setBox64Version(String version) { this.box64Version = version; }

    public void setEmulator(String emulator) {
        this.emulator = emulator;
    }

    public String getEmulator() {
        return this.emulator;
    }

    public File getRootDir() {
        return rootDir;
    }

    public void setRootDir(File rootDir) {
        this.rootDir = rootDir;
    }

    public void setExtraData(JSONObject extraData) {
        this.extraData = extraData;
    }

    /** A detached copy suitable for backup, duplication and editor round trips. */
    public JSONObject getExtraDataCopy() {
        if (extraData == null) return new JSONObject();
        try {
            return new JSONObject(extraData.toString());
        } catch (JSONException invalid) {
            return new JSONObject();
        }
    }

    public String getExtra(String name) {
        return getExtra(name, "");
    }

    public String getExtra(String name, String fallback) {
        try {
            return extraData != null && extraData.has(name) ? extraData.getString(name) : fallback;
        }
        catch (JSONException e) {
            return fallback;
        }
    }

    public void putExtra(String name, Object value) {
        if (extraData == null) extraData = new JSONObject();
        try {
            if (value != null) {
                extraData.put(name, value);
            }
            else extraData.remove(name);
        }
        catch (JSONException e) {
            // A dropped extra is a setting the player changed and did not get.
            // It cannot be recovered here, but it must not be invisible.
            Log.w("Container", "Could not store container extra \"" + name + "\"", e);
        }
    }

    public String getWineVersion() {
        return wineVersion;
    }

    public void setWineVersion(String wineVersion) {
        this.wineVersion = wineVersion;
    }

    public File getConfigFile() {
        return new File(rootDir, ".container");
    }

    public File getDesktopDir() {
        return new File(rootDir, ".wine/drive_c/users/"+ImageFs.USER+"/Desktop/");
    }

    public File getStartMenuDir() {
        return new File(rootDir, ".wine/drive_c/ProgramData/Microsoft/Windows/Start Menu/");
    }

    public File getIconsDir(int size) {
        return new File(rootDir, ".local/share/icons/hicolor/"+size+"x"+size+"/apps/");
    }

    /**
     * What Windows is told one inch is, in pixels.
     *
     * 96 is the Windows default and what Wine uses when nothing says otherwise,
     * which is why a container set to 1920x1080 renders correctly and is still
     * unusable on a handheld: every dialog, menu and caption is laid out for a
     * monitor two feet away. Raising this scales the whole interface without
     * touching the resolution a game renders at, which is the distinction the
     * screen-size setting cannot express on its own.
     */
    public int getDPI() {
        return dpi;
    }

    /**
     * A sensible scale for a resolution, on a screen you hold.
     *
     * 1280x720 at 96 is about right in the hand, so everything above it is
     * scaled in proportion and snapped to a multiple of 24 — Windows' own scale
     * steps, and the ones whose bitmap assets are least likely to be resampled.
     * 1920x1080 lands on 144, which is the 150% that makes a 1080p container
     * legible rather than technically correct.
     */
    public static int suggestedDPI(String screenSize) {
        int height;
        try {
            String[] parts = screenSize.split("x");
            height = Integer.parseInt(parts[1].trim());
        } catch (Exception e) {
            return DEFAULT_DPI;
        }

        if (height <= 0) return DEFAULT_DPI;

        int scaled = Math.round(DEFAULT_DPI * (height / (float) DPI_REFERENCE_HEIGHT));
        int snapped = Math.round(scaled / (float) DPI_STEP) * DPI_STEP;
        return Math.max(DEFAULT_DPI, Math.min(snapped, MAX_DPI));
    }

    /** The resolution 96 DPI is comfortable at, held at arm's length. */
    private static final int DPI_REFERENCE_HEIGHT = 720;
    private static final int DPI_STEP = 24;
    private static final int MAX_DPI = 240;

    public void setDPI(int dpi) {
        this.dpi = dpi > 0 ? dpi : DEFAULT_DPI;
    }

    public String getDesktopTheme() {
        return desktopTheme;
    }

    public void setDesktopTheme(String desktopTheme) {
        this.desktopTheme = desktopTheme;
    }

    public String getMIDISoundFont() {
        return midiSoundFont;
    }

    public void setMidiSoundFont(String fileName) {
        midiSoundFont = fileName;
    }

    public int getInputType() {
        return inputType;
    }

    public void setInputType(int inputType) {
        this.inputType = inputType;
    }


    public boolean isExclusiveXInput() {
        return exclusiveXInput;
    }

    public void setExclusiveXInput(boolean exclusiveXInput) {
        this.exclusiveXInput = exclusiveXInput;
    }


    public Iterable<String[]> drivesIterator() {
        return drivesIterator(drives);
    }

    public static Iterable<String[]> drivesIterator(final String drives) {
        final int[] index = {drives.indexOf(":")};
        final String[] item = new String[2];
        return () -> new Iterator<String[]>() {
            @Override
            public boolean hasNext() {
                return index[0] != -1;
            }

            @Override
            public String[] next() {
                item[0] = String.valueOf(drives.charAt(index[0]-1));
                int nextIndex = drives.indexOf(":", index[0]+1);
                item[1] = drives.substring(index[0]+1, nextIndex != -1 ? nextIndex-1 : drives.length());
                index[0] = nextIndex;
                return item;
            }
        };
    }

    /** Serializes every persisted field without writing it. */
    public JSONObject toJson() throws JSONException {
        JSONObject data = new JSONObject();
        data.put("id", id);
        data.put("name", name);
        data.put("screenSize", screenSize);
        data.put("envVars", envVars);
        data.put("cpuList", cpuList);
        data.put("cpuListWoW64", cpuListWoW64);
        if (syncCpuTopology) data.put("syncCpuTopology", true);
        data.put("graphicsDriver", graphicsDriver);
        data.put("graphicsDriverConfig", graphicsDriverConfig);
        data.put("rendererNative", rendererNative);
        data.put("rendererPresentMode", rendererPresentMode);
        if (!rendererDriverId.isEmpty()) data.put("rendererDriverId", rendererDriverId);
        if (rendererFilterMode != 0) data.put("rendererFilterMode", rendererFilterMode);
        if (rendererSwapRB) data.put("rendererSwapRB", true);
        data.put("emulator", emulator);
        data.put("dxwrapper", dxwrapper);
        if (!dxwrapperConfig.isEmpty()) data.put("dxwrapperConfig", dxwrapperConfig);
        data.put("audioDriver", audioDriver);
        data.put("displayRenderer", displayRenderer);
        data.put("wincomponents", wincomponents);
        data.put("drives", drives);
        data.put("showFPS", showFPS);
        data.put("fullscreenStretched", fullscreenStretched);
        data.put("inputType", inputType);
        data.put("startupSelection", startupSelection);
        data.put("box64Version", box64Version);
        data.put("fexcorePreset", fexcorePreset);
        data.put("fexcoreVersion", fexcoreVersion);
        data.put("box64Preset", box64Preset);
        data.put("desktopTheme", desktopTheme);
        data.put("dpi", dpi);
        data.put("extraData", getExtraDataCopy());
        data.put("midiSoundFont", midiSoundFont);
        data.put("lc_all", lc_all);
        data.put("primaryController", primaryController);
        data.put("controllerMapping", controllerMapping);
        data.put("exclusiveXInput", exclusiveXInput);
        if (!WineInfo.isMainWineVersion(wineVersion)) data.put("wineVersion", wineVersion);
        return data;
    }

    /** Writes a complete config through FileUtils' atomic publication path. */
    public synchronized boolean saveData() {
        try {
            return FileUtils.writeStringAtomic(getConfigFile(), toJson().toString());
        }
        catch (JSONException e) {
            return false;
        }
    }


    public void loadData(JSONObject data) throws JSONException {
        wineVersion = WineInfo.MAIN_WINE_VERSION.identifier();
        dxwrapperConfig = "";
        checkObsoleteOrMissingProperties(data);

        for (Iterator<String> it = data.keys(); it.hasNext(); ) {
            String key = it.next();
            switch (key) {
                case "name" :
                    setName(data.getString(key));
                    break;
                case "screenSize" :
                    setScreenSize(data.getString(key));
                    break;
                case "dpi" :
                    setDPI(data.getInt(key));
                    break;
                case "envVars" :
                    setEnvVars(data.getString(key));
                    break;
                case "cpuList" :
                    setCPUList(data.getString(key));
                    break;
                case "cpuListWoW64" :
                    setCPUListWoW64(data.getString(key));
                    break;
                case "syncCpuTopology" :
                    setSyncCpuTopology(data.getBoolean(key));
                    break;
                case "graphicsDriver" :
                    setGraphicsDriver(data.getString(key));
                    break;
                case "graphicsDriverConfig" :
                    setGraphicsDriverConfig(data.getString(key));
                    break;
                case "rendererNative" :
                    rendererNative = data.getBoolean(key);
                    break;
                case "rendererPresentMode" :
                    rendererPresentMode = data.getString(key);
                    break;
                case "rendererDriverId":
                    rendererDriverId = data.getString(key);
                    break;
                case "rendererFilterMode" :
                    rendererFilterMode = data.getInt(key);
                    break;
                case "rendererSwapRB":
                    rendererSwapRB = data.getBoolean(key);
                    break;
                case "emulator":
                    setEmulator(data.getString(key));
                    break;
                case "wincomponents" :
                    setWinComponents(data.getString(key));
                    break;
                case "dxwrapper" :
                    setDXWrapper(data.getString(key));
                    break;
                case "dxwrapperConfig" :
                    setDXWrapperConfig(data.getString(key));
                    break;
                case "drives" :
                    setDrives(data.getString(key));
                    break;
                case "showFPS" :
                    setShowFPS(data.getBoolean(key));
                    break;
                case "fullscreenStretched" :
                    setFullscreenStretched(data.getBoolean(key));
                    break;
                case "inputType" :
                    setInputType(data.getInt(key));
                    break;
                case "startupSelection" :
                    setStartupSelection((byte)data.getInt(key));
                    break;
                case "extraData" : {
                    setExtraData(data.getJSONObject(key));
                    break;
                }
                case "wineVersion" :
                    setWineVersion(data.getString(key));
                    break;
                case "box64Version":
                    setBox64Version(data.getString(key));
                    break;
                case "fexcoreVersion":
                    setFEXCoreVersion(data.getString(key));
                    break;
                case "fexcorePreset":
                    setFEXCorePreset(data.getString(key));
                    break;
                case "box64Preset" :
                    setBox64Preset(data.getString(key));
                    break;
                case "audioDriver" :
                    setAudioDriver(data.getString(key));
                    break;
                case "displayRenderer" :
                    setDisplayRenderer(data.getString(key));
                    break;
                case "desktopTheme" :
                    setDesktopTheme(data.getString(key));
                    break;
                case "midiSoundFont" :
                    setMidiSoundFont(data.getString(key));
                    break;
                case "lc_all" :
                    setLC_ALL(data.getString(key));
                    break;
                case "primaryController" :
                    setPrimaryController(data.getInt(key));
                    break;
                case "controllerMapping" :
                    controllerMapping = data.getString(key);
                    break;
                case "exclusiveXInput" :
                    setExclusiveXInput(data.getBoolean(key));
                    break;
            }
        }
    }

    /**
     * Removes the two environment settings that were costing frames.
     *
     * A default only reaches a container that has not been created yet, and the
     * containers that matter are the ones already on the device — they carry
     * their own copy of whatever `DEFAULT_ENV_VARS` said the day they were made,
     * and would keep `sysmem` forever. So the two are stripped on load.
     *
     * Deliberately narrow. It removes exactly the tokens it knows are wrong and
     * leaves everything else, including anything the user added by hand, because
     * a migration that rewrote the whole string would silently discard a setting
     * somebody worked out for one difficult game.
     */
    private static String withoutKnownSlowdowns(String envVars) {
        if (envVars == null || envVars.isEmpty()) return envVars;

        StringBuilder result = new StringBuilder();
        for (String token : envVars.trim().split("\\s+")) {
            if (token.isEmpty()) continue;

            // The whole variable goes; there is no useful value for it.
            if (token.startsWith("DXVK_DISABLE_TIMELINE_SEMAPHORES=")) continue;

            // Only the one flag goes, and only if `noconform` survives it —
            // `TU_DEBUG=` with nothing after it is not the same as unset.
            if (token.startsWith("TU_DEBUG=")) {
                String value = token.substring("TU_DEBUG=".length());
                StringBuilder flags = new StringBuilder();
                for (String flag : value.split(",")) {
                    if (flag.isEmpty() || flag.equals("sysmem")) continue;
                    if (flags.length() > 0) flags.append(',');
                    flags.append(flag);
                }
                if (flags.length() == 0) continue;
                token = "TU_DEBUG=" + flags;
            }

            if (result.length() > 0) result.append(' ');
            result.append(token);
        }
        return result.toString();
    }

    /**
     * Turns the BCn transcode cache on for a container that predates the default.
     *
     * <p>The wrapper emulates block-compressed textures the GPU cannot sample
     * natively by transcoding them, and with the cache off it transcodes the
     * same texture again every time it is uploaded. A game that streams
     * textures — which is every open-world game, and Valheim in particular —
     * therefore runs a compute pass over its terrain and vegetation atlases
     * continuously while the player moves. That is what the constant GPU
     * spiking is: not one heavy frame, but transcodes arriving forever.</p>
     *
     * <p>Migrated rather than merely defaulted, for the reason the note above
     * gives: a default reaches containers that do not exist yet, and the ones
     * that matter are already on the device carrying a copy of what the default
     * said the day they were made.</p>
     *
     * <p>Only the explicit off is changed. A container someone deliberately set
     * to something else keeps it, and one that never had the key gets it from
     * the default like any other missing property.</p>
     */
    private static String withBcnCache(String graphicsDriverConfig) {
        if (graphicsDriverConfig == null || graphicsDriverConfig.isEmpty()) {
            return graphicsDriverConfig;
        }
        KeyValueSet config = new KeyValueSet(graphicsDriverConfig);
        if (!"0".equals(config.get("bcnEmulationCache"))) return graphicsDriverConfig;
        config.put("bcnEmulationCache", "1");
        return config.toString();
    }

    public static void checkObsoleteOrMissingProperties(JSONObject data) {
        try {
            if (data.has("envVars")) {
                data.put("envVars", withoutKnownSlowdowns(data.getString("envVars")));
            }
            if (data.has("dxcomponents")) {
                data.put("wincomponents", data.getString("dxcomponents"));
                data.remove("dxcomponents");
            }
            if (data.has("dxwrapper")) {
                String dxwrapper = data.getString("dxwrapper");
                if (dxwrapper.equals("original-wined3d")) {
                    data.put("dxwrapper", DEFAULT_DXWRAPPER);
                }
                else if (dxwrapper.startsWith("d8vk-") || dxwrapper.startsWith("dxvk-")) {
                    data.put("dxwrapper", dxwrapper);
                }
            }
            if (data.has("dxwrapperConfig")) {
                KeyValueSet config = new KeyValueSet(data.getString("dxwrapperConfig"));
                if (config.get("version").equals("3.0.2-thor2")) {
                    config.put("version", "2.3.1");
                    config.put("mobileTuning", "0");
                    config.put("relaxedBarriers", "0");
                    data.put("dxwrapperConfig", config.toString());

                    if (data.has("graphicsDriverConfig")) {
                        KeyValueSet graphics = new KeyValueSet(data.getString("graphicsDriverConfig"));
                        if (graphics.get("vulkanVersion").equals("1.4")) {
                            graphics.put("vulkanVersion", "1.3");
                            data.put("graphicsDriverConfig", graphics.toString());
                        }
                    }
                }
            }
            if (data.has("graphicsDriverConfig")) {
                data.put("graphicsDriverConfig",
                        withBcnCache(data.getString("graphicsDriverConfig")));
            }
            if (data.has("graphicsDriver")) {
                String graphicsDriver = data.getString("graphicsDriver");
                if (graphicsDriver.equals("turnip-zink") || graphicsDriver.equals("turnip")) {
                    data.put("graphicsDriver", "wrapper");
                }
                else if (graphicsDriver.equals("llvmpipe")) {
                    data.put("graphicsDriver", "wrapper");
                }
            }
            KeyValueSet wincomponents1 = new KeyValueSet(DEFAULT_WINCOMPONENTS);
            KeyValueSet wincomponents2 = new KeyValueSet(data.getString("wincomponents"));
            String result = "";
            for (String[] wincomponent1 : wincomponents1) {
                String value = wincomponent1[1];
                for (String[] wincomponent2 : wincomponents2) {
                    if (wincomponent1[0].equals(wincomponent2[0])) {
                        value = wincomponent2[1];
                        break;
                    }
                }
                result += (!result.isEmpty() ? "," : "")+wincomponent1[0]+"="+value;
            }
            data.put("wincomponents", result);
        }
        catch (JSONException e) {
            Log.w("Container", "Could not store the Windows component list", e);
        }
    }

    public static String getFallbackCPUList() {
        String cpuList = "";
        int numProcessors = Runtime.getRuntime().availableProcessors();
        for (int i = 0; i < numProcessors; i++) cpuList += (!cpuList.isEmpty() ? "," : "")+i;
        return cpuList;
    }

    public static String getFallbackCPUListWoW64() {
        String cpuList = "";
        int numProcessors = Runtime.getRuntime().availableProcessors();
        for (int i = numProcessors / 2; i < numProcessors; i++) cpuList += (!cpuList.isEmpty() ? "," : "")+i;
        return cpuList;
    }

    // Check if a specific environment variable exists
    public boolean hasEnvVar(String keyValue) {
        if (envVars == null || envVars.isEmpty()) return false;
        String[] vars = envVars.split(",");
        for (String var : vars) {
            if (var.trim().equalsIgnoreCase(keyValue.trim())) {
                return true; // Found the variable
            }
        }
        return false;
    }

}
