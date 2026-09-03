package com.wemu.runtime.internal;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;

import androidx.preference.PreferenceManager;

import com.wemu.runtime.RuntimeSession;
import com.wemu.runtime.RuntimeTerminationListener;
import com.wemu.engine.XServerHost;
import com.wemu.engine.session.ContainerBootstrap;
import com.wemu.engine.session.BethesdaDisplayCompatibility;
import com.wemu.engine.session.Fallout3Compatibility;
import com.wemu.engine.session.SkyrimAudioCompatibility;
import com.wemu.engine.session.RendererOptions;
import com.wemu.engine.session.ContainerPerformanceOptions;
import com.wemu.engine.services.NotificationService;
import com.wemu.engine.config.DXWrapperConfig;
import com.wemu.engine.container.Container;
import com.wemu.engine.container.ContainerManager;
import com.wemu.engine.container.Shortcut;
import com.wemu.engine.contents.ContentsManager;
import com.wemu.engine.core.EnvVars;
import com.wemu.engine.core.FileUtils;
import com.wemu.engine.core.FrameGeneration;
import com.wemu.engine.core.ProcessHelper;
import com.wemu.engine.core.GraphicsDriverConfig;
import com.wemu.engine.core.KeyValueSet;
import com.wemu.engine.core.StringUtils;
import com.wemu.engine.core.StardewCoreClrCompatibility;
import com.wemu.engine.core.SteamLaunchIdentity;
import com.wemu.engine.core.WineInfo;
import com.wemu.engine.winhandler.WinHandler;
import com.wemu.engine.xconnector.UnixSocketConfig;
import com.wemu.engine.xenvironment.ImageFs;
import com.wemu.engine.xenvironment.XEnvironment;
import com.wemu.engine.xenvironment.components.ALSAServerComponent;
import com.wemu.engine.xserver.extensions.PresentExtension;
import com.wemu.engine.xenvironment.components.GuestProgramLauncherComponent;
import com.wemu.engine.xenvironment.components.PulseAudioComponent;
import com.wemu.engine.xenvironment.components.SysVSharedMemoryComponent;
import com.wemu.engine.xenvironment.components.XServerComponent;
import com.wemu.engine.xserver.ScreenInfo;
import com.wemu.engine.xserver.ScreenLayout;
import com.wemu.engine.xserver.XServer;

import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;

/**
 * Starts and stops one container.
 *
 * The whole of a running session, with no screen attached: the X server, the
 * unix sockets the guest talks to it over, the audio server, and the Wine
 * process tree. What draws the result — and on which panel — is the caller's
 * business, which is the point. The original's equivalent was 2,716 lines of
 * activity in which this logic was interleaved with a drawer, a HUD, six
 * settings panels and a task manager, so none of it could be started without
 * all of it.
 *
 * Not thread-safe, and not meant to be: [start] does real work — unpacking
 * archives, editing a registry — and must be called off the main thread.
 *
 * Reached through [RuntimeSession] rather than by name. The inherited types on
 * that contract are bridge types: this class is the first implementation of it,
 * not the shape the interface is meant to keep.
 */
public class WemuSession implements RuntimeSession {

    private static final String TAG = "WemuSession";

    private final Context context;
    private final XServerHost host;
    private final Container container;
    private final Shortcut shortcut;

    private final ImageFs imageFs;
    private final ContentsManager contentsManager;
    private final SharedPreferences preferences;
    private final EnvVars envVars = new EnvVars();

    private XServer xServer;
    private XEnvironment environment;
    private WinHandler winHandler;
    private GuestProgramLauncherComponent guestProgramLauncherComponent;
    private ALSAServerComponent alsaServerComponent;
    private volatile float requestedAudioGainDb;
    private volatile boolean skyrimAudioCompatibility;
    private volatile boolean shortcutLaunchDeferred;


    private String graphicsDriver;
    private HashMap<String, String> graphicsDriverConfig;
    private String audioDriver;
    private String dxwrapper;
    private KeyValueSet dxwrapperConfig;
    private WineInfo wineInfo;
    private boolean firstTimeBoot;
    private String locale = "";
    private String cpuTopology = "";

    private volatile boolean running = false;
    private volatile boolean stopRequested = false;
    private final Object lifecycleLock = new Object();
    private boolean environmentStopped = false;
    private boolean sessionProtectionStarted = false;

    private RuntimeTerminationListener terminationListener;

    public WemuSession(Context context, XServerHost host, Container container, Shortcut shortcut) {
        this.context = context.getApplicationContext();
        this.host = host;
        this.container = container;
        this.shortcut = shortcut;
        this.imageFs = ImageFs.find(this.context);
        this.contentsManager = new ContentsManager(this.context);
        this.preferences = PreferenceManager.getDefaultSharedPreferences(this.context);
    }

    @Override
    public void setTerminationListener(RuntimeTerminationListener listener) {
        this.terminationListener = listener;
    }

    @Override
    public XServer getXServer() {
        return xServer;
    }

    @Override
    public WinHandler getWinHandler() {
        return winHandler;
    }

    @Override
    public boolean isRunning() {
        return running;
    }

    @Override
    public Container getContainer() {
        return container;
    }

    @Override
    public Shortcut getShortcut() {
        return shortcut;
    }

    /** The resolved Wine build, for a host that wants to check it exists. */
    @Override
    public WineInfo getWineInfo() {
        return wineInfo;
    }

    /**
     * The container's stored renderer settings, put onto a live renderer.
     *
     * Here rather than on the caller because the shortcut/container precedence
     * and the parsed driver config are this class's, and a host that had to
     * reassemble them would be reimplementing [prepare].
     */
    @Override
    public void configureRenderer(com.wemu.engine.renderer.XServerRenderer renderer) {
        /*
         * Everything here is a property of the Vulkan compositor.
         *
         * Upscalers, sharpening, post-processing, filter and present modes are
         * all shader or swapchain settings, and the scanout renderer has
         * neither — it hands buffers to SurfaceFlinger and the display
         * controller decides. Rather than give `ASurfaceRenderer` a set of
         * setters that accept a value and discard it, the settings simply do
         * not reach it, and the container editor hides them when it is chosen.
         */
        RendererOptions.applyCommon(renderer);

        if (renderer instanceof com.wemu.engine.renderer.CompositorRenderer) {
            RendererOptions.apply(context,
                    (com.wemu.engine.renderer.CompositorRenderer) renderer,
                    container, shortcut, graphicsDriverConfig);
        } else if (renderer instanceof com.wemu.engine.renderer.ASurfaceRenderer) {
            RendererOptions.applyScanout(
                    (com.wemu.engine.renderer.ASurfaceRenderer) renderer, container, shortcut);
        }
    }

    @Override
    public void setAudioGainDb(float gainDb) {
        requestedAudioGainDb = gainDb;
        ALSAServerComponent server = alsaServerComponent;
        if (server != null) server.setGainDb(gainDb);
    }

    @Override
    public void setSkyrimAudioCompatibility(boolean enabled) {
        skyrimAudioCompatibility = enabled;
        if (enabled) {
            Log.i(TAG, "Skyrim audio compatibility enabled");
        }
    }

    @Override
    public void setShortcutLaunchDeferred(boolean deferred) {
        shortcutLaunchDeferred = deferred;
        if (deferred) {
            Log.i(TAG, "Shortcut launch deferred; retaining renderer/input configuration "
                    + "while isolating the bootstrap environment");
        }
    }

    /**
     * Which renderer this session should build, resolved shortcut-then-container.
     *
     * Read by the host before the view exists, because the choice decides how
     * every X drawable is allocated and that has to be settled first.
     */
    @Override
    public String getDisplayRenderer() {
        return resolve("displayRenderer", container.getDisplayRenderer());
    }

    /**
     * Reads the container's configuration and builds the X server.
     *
     * Split from [start] because the server has to exist before the surface that
     * draws it can be created, and the surface has to exist before Wine is told
     * what screen it has. Calling [start] without this first is a programming
     * error rather than a recoverable one.
     */
    @Override
    public XServer prepare() {
        /*
         * Point the image's home directory at this container. Before anything.
         *
         * Containers live at `imagefs/home/xuser-<id>`, and `imagefs/home/xuser`
         * is a *symlink* that `activateContainer` repoints at whichever one is
         * being launched. Almost everything downstream addresses the container
         * through that link rather than by its real path: `ImageFs.WINEPREFIX`
         * is `/home/xuser/.wine`, so it is the `WINEPREFIX` Wine is given, the
         * directory `ContainerBootstrap` unpacks DXVK and the Windows components
         * into, and the `system32` the guest launcher writes the translation DLL
         * to.
         *
         * Upstream did this from the activity, which is why it was missed when
         * the session logic was lifted off it. Without it the link is absent on
         * a fresh install and stale afterwards, so the first extraction quietly
         * creates `home/xuser` as a real directory and every subsequent write
         * lands in a container that does not exist. The container itself is
         * never touched — which is why it unpacks, reports success, and then
         * Wine exits instantly against a prefix that was never built, and why
         * the preflight finds no translation DLL in the container it checked.
         */
        ContainerManager manager = container.getManager();
        if (manager != null) {
            manager.activateContainer(container);
        } else {
            throw new IllegalStateException("Container " + container.id
                    + " has no manager; its prefix cannot be activated safely");
        }

        contentsManager.syncContents();

        String screenSize = container.getScreenSize();
        if (shortcut != null) {
            String shortcutScreenSize = shortcut.getExtra("screenSize");
            if (!shortcutScreenSize.isEmpty()) screenSize = shortcutScreenSize;
        }

        xServer = createXServer(resolveScreenInfo(screenSize));
        PresentExtension present = xServer.getExtension(PresentExtension.MAJOR_OPCODE);
        if (present != null) {
            // A dual-screen guest can submit two independent DXVK swapchains
            // before Android's next frame callback. The legacy per-window
            // coalescing path drops an older IdleNotify in that case, so DXVK
            // never gets that buffer back and can exhaust a swapchain during
            // startup. Preserve every release whenever the second output is
            // active; the explicit performance option keeps doing the same for
            // single-screen compatibility trials.
            present.setOrderedIdleNotifications(
                    isEnabled(container, ContainerPerformanceOptions.ORDERED_PRESENT)
                    || isEnabled(container, ContainerPerformanceOptions.SECOND_SCREEN));
        }

        graphicsDriver = resolve("graphicsDriver", container.getGraphicsDriver());
        audioDriver = resolve("audioDriver", container.getAudioDriver());
        if (skyrimAudioCompatibility && !SkyrimAudioCompatibility.AUDIO_DRIVER.equals(audioDriver)) {
            // Keep Skyrim on WEMU's in-process sink. This makes the repair
            // independent of a container's PulseAudio preset and gives stream
            // recovery one backend with behavior WEMU controls end to end.
            audioDriver = SkyrimAudioCompatibility.AUDIO_DRIVER;
            Log.i(TAG, "Skyrim audio compatibility selected the managed ALSA bridge");
        }
        dxwrapper = resolve("dxwrapper", container.getDXWrapper());
        dxwrapperConfig = DXWrapperConfig.parse(
                resolve("dxwrapperConfig", container.getDXWrapperConfig()));
        graphicsDriverConfig = GraphicsDriverConfig.parse(
                resolve("graphicsDriverConfig", container.getGraphicsDriverConfig()));
        if (shortcut != null) {
            String shortcutDriver = shortcut.getExtra("rendererDriverId");
            if (!shortcutDriver.isEmpty()) {
                /*
                 * A shortcut driver override has to select the guest ICD too.
                 * RendererOptions already used this value for Wemu's host-side
                 * compositor; leaving the parsed guest version unchanged made
                 * the UI claim one driver while DXVK loaded another.
                 */
                graphicsDriverConfig.put("version", shortcutDriver);
            }
        }
        wineInfo = WineInfo.fromIdentifier(context, contentsManager, container.getWineVersion());

        /*
         * Where Wine actually is, for everything downstream that needs its path.
         *
         * `ImageFs` defaults this to the bundled Proton build, so a container on
         * the default Wine version works without it and every other container
         * silently gets the wrong `PATH` and the wrong DLL source directories.
         * Which is the worst shape a bug can have: correct on the configuration
         * everybody tests with.
         */
        imageFs.setWinePath(wineInfo.path);

        locale = resolve("lc_all", container.getLC_ALL());
        cpuTopology = resolveCpuTopology();

        /*
         * Whether this container has ever been booted against an image.
         *
         * Read here rather than in [start], and read from the *container* rather
         * than from the image filesystem. Both matter:
         *
         *  - `imageFs.isValid()` answers "has the app unpacked its image", which
         *    is true for every session after the very first launch. Using it
         *    means a brand new container is never told it is new, so the wrapper
         *    Vulkan ICD, the layers and the extra libs are never extracted into
         *    it — and `VK_ICD_FILENAMES` then points at a file that does not
         *    exist. Wine gets no Vulkan device and the session dies before it
         *    draws anything.
         *  - It has to be captured before `setupWineSystemFiles` runs, because
         *    that is what writes `imgVersion` and would otherwise erase the
         *    answer on its way past.
         */
        firstTimeBoot = container.getExtra("imgVersion").isEmpty();

        winHandler = new WinHandler(host);

        // The server reaches the gamepad bridge through this, so a session
        // without it starts, draws, and quietly ignores every controller.
        xServer.setWinHandler(winHandler);

        // The container's XInput/DirectInput choice. Without this the bridge
        // keeps its own default and the container's Input settings do nothing.
        String shortcutInputType = shortcut != null ? shortcut.getExtra("inputType") : "";
        winHandler.setInputType(!shortcutInputType.isEmpty()
                ? Byte.parseByte(shortcutInputType)
                : (byte) container.getInputType());

        return xServer;
    }

    /**
     * What Wine should believe the CPU looks like, or empty to leave it alone.
     *
     * `count:list` — the number of cores followed by the cores themselves. Only
     * produced when the container asked for it *and* named an affinity list;
     * telling Wine about a topology while letting it run on every core is a
     * description that does not match the machine it is running on.
     */
    private String resolveCpuTopology() {
        /*
         * A pinned guest is told about the cores it actually has.
         *
         * `affinityCores` is the mask the kernel is given, and it is the truth
         * about the machine the guest is running on. Wine reporting more
         * processors than that is the same lie the comment above rejects, in
         * the other direction -- and it is the more expensive one, because
         * something is listening: Unity sizes its job system from the processor
         * count. Told eight while pinned to five, it starts eight workers that
         * fight over five cores, gets them descheduled mid-job, and leaves the
         * main thread waiting on work that is not running. That arrives as
         * uneven frame times and a GPU that goes hungry between them.
         *
         * Reported whether or not the container asked to sync its topology,
         * because this is not the sync feature. Sync mirrors a chosen CPU list;
         * this refuses to overstate the machine.
         */
        String pinned = container.getExtra("affinityCores");
        if (pinned != null && !pinned.isEmpty()) {
            int cores = pinned.split(",").length;
            // The launcher refuses to pin fewer than two, so a list that short
            // will not be applied and must not be described either.
            if (cores >= 2) return cores + ":" + pinned;
        }

        String affinity = resolve("cpuList", container.getCPUList(true));

        boolean sync = shortcut != null
                ? shortcut.getExtra("syncCpuTopology", container.isSyncCpuTopology() ? "1" : "").equals("1")
                : container.isSyncCpuTopology();

        if (!sync || affinity == null || affinity.isEmpty()) return "";
        return affinity.split(",").length + ":" + affinity;
    }

    /**
     * A setting from the shortcut if it overrides one, otherwise the container's.
     *
     * A shortcut is a saved way of launching one executable inside a container,
     * and it may pin any of these — a game that needs an older DXVK should not
     * force every other game in the container onto it.
     */
    private String resolve(String key, String containerValue) {
        if (shortcut == null) return containerValue;
        String override = shortcut.getExtra(key);
        return override.isEmpty() ? containerValue : override;
    }

    /**
     * Unpacks what the container needs and starts Wine.
     *
     * Blocking, and slow the first time a container runs after its configuration
     * changes — it may extract several hundred megabytes. Call it off the main
     * thread.
     */
    @Override
    public void start() {
        if (stopRequested) return;
        if (xServer == null) {
            throw new IllegalStateException("prepare() must be called before start()");
        }

        /*
         * Native Wine children are charged to this Android process. If the
         * activity is covered, Android otherwise classifies the process as
         * cached and its phantom-process CPU monitor can kill the entire guest
         * after a sustained install or game workload. Request the foreground
         * service before doing the bootstrap work, giving its main-thread
         * callback time to reach startForeground before Wine is exec'd.
         */
        synchronized (lifecycleLock) {
            if (stopRequested) return;
            startSessionProtectionLocked();
        }

        try {
            startGuestEnvironment();
        } catch (RuntimeException | Error failure) {
            // A failed bootstrap has no Activity-owned guest to retire later.
            // Keep the service lifetime paired with this runtime itself.
            stop();
            throw failure;
        }
    }

    private void startGuestEnvironment() {
        if (stopRequested) return;

        ContainerBootstrap bootstrap = new ContainerBootstrap(
                context, container, imageFs, contentsManager, xServer, envVars, shortcut,
                dxwrapper, dxwrapperConfig, graphicsDriver, graphicsDriverConfig,
                audioDriver, wineInfo, firstTimeBoot, skyrimAudioCompatibility);

        /*
         * The order is upstream's, and it is not arbitrary.
         *
         * `setupWineSystemFiles` runs `applyGeneralPatches` on a container whose
         * image version has moved, which unpacks the container pattern over the
         * root directory and clears the container's record of which graphics
         * driver was extracted. Doing it after the driver extraction throws that
         * record away and re-extracts on every launch; doing it before is what
         * makes the record mean something.
         */
        bootstrap.setupWineSystemFiles();
        if (stopRequested) return;
        bootstrap.extractGraphicsDriverFiles();
        if (stopRequested) return;
        bootstrap.changeWineAudioDriver();
        if (stopRequested) return;
        bootstrap.changeWineDPI();
        if (stopRequested) return;

        /*
         * Publish the running state before the launcher is started. Its exit
         * callback may run while setupEnvironment is still returning; setting
         * this afterwards would overwrite that callback's `false` and leave a
         * dead guest reported as alive.
         */
        synchronized (lifecycleLock) {
            /*
             * stop() can arrive while the bootstrap above is doing filesystem
             * work. It records that request immediately; once we reach the
             * only section that can publish sockets or Wine processes, either
             * this block starts the complete environment or stop() owns it.
             * There is no gap in which teardown can see an empty environment
             * and return just before start() launches the guest.
             */
            if (stopRequested) return;
            running = true;
            try {
                setupEnvironment(bootstrap.getStartupSelection());
            } catch (RuntimeException | Error failure) {
                running = false;
                stopEnvironmentLocked();
                throw failure;
            }
        }
    }

    /**
     * Builds the guest environment and starts every component of it.
     *
     * Ported from `XServerDisplayActivity.setupXEnvironment`. The order is
     * load-bearing: shared memory before the X server, because the server hands
     * out buffers from it; both before the launcher, because Wine connects to
     * their sockets as it starts.
     */
    private void setupEnvironment(String startupSelection) {
        // The container's locale. Wine reads it for the code page a program is
        // given, which is what decides whether a Japanese title renders its text
        // or a screen of boxes.
        envVars.put("LC_ALL", locale);
        /*
         * Pin this session to the container directory, not the mutable
         * /home/xuser symlink used while activating containers. Wine copies
         * this value into every descendant, which lets teardown identify one
         * prefix precisely and prevents a later container activation from
         * redirecting `wineserver -k` at the wrong session.
         */
        String winePrefix = imageFs.wineprefix;
        try {
            winePrefix = new File(winePrefix).getCanonicalPath();
        } catch (java.io.IOException error) {
            Log.w(TAG, "Could not resolve Wine prefix " + winePrefix, error);
        }
        envVars.put("WINEPREFIX", winePrefix);

        boolean enableWineDebug = preferences.getBoolean("enable_wine_debug", false);
        String wineDebugChannels = preferences.getString("wine_debug_channels", "");

        // Guest output reaches logcat only while this is on. Off it still
        // reaches the console's Log page, which is where a user reads it; on it
        // also reaches `adb`, which is where anyone debugging the guest reads it.
        ProcessHelper.setPrintDebug(enableWineDebug);
        envVars.put("WINEDEBUG", enableWineDebug && !wineDebugChannels.isEmpty()
                ? "+" + wineDebugChannels.replace(",", ",+")
                : "-all");

        String rootPath = imageFs.getRootDir().getPath();
        FileUtils.clear(imageFs.getTmpDir());

        /*
         * A deferred shortcut describes the game which the host will submit
         * through WinHandler after Wine is ready.  It is still the source of
         * renderer, input, screen and translator settings during prepare(),
         * but it is not the process being started here.  Passing it into the
         * guest launcher leaked target-only CoreCLR/FEX variables into
         * explorer, winhandler and wfm.exe.  Stardew then reached coreclr.dll
         * but never created a window.  Keep this bootstrap identical to the
         * known-good desktop launch; WinHandler's hidden VBS applies Stardew's
         * policy to the actual child later.
         */
        Shortcut bootstrapShortcut = shortcutLaunchDeferred ? null : shortcut;
        guestProgramLauncherComponent = new GuestProgramLauncherComponent(
                contentsManager,
                contentsManager.getProfileByEntryName(container.getWineVersion()),
                bootstrapShortcut);

        guestProgramLauncherComponent.setContainer(container);
        guestProgramLauncherComponent.setWineInfo(wineInfo);

        /*
         * Whether Wine gets a desktop of its own, or whether Wemu is the only
         * one.
         *
         * `explorer /desktop=shell,WxH` puts every guest window inside a virtual
         * desktop window. Wemu has never drawn that window — the compositor
         * marks the shell unviewable and composes the game directly — so what it
         * contributes to the picture is nothing, and what it costs is a layer of
         * Wine's own window management between the game and this X server, plus
         * the loss of Wine's fullscreen handling: inside a virtual desktop the
         * desktop *is* a window, so a game asking for a mode Wine would
         * otherwise scale just gets a differently-sized window instead. That is
         * why `WINE_FULLSCREEN_FSR` cannot work today and why the "fit game to
         * screen" setting had to be built in the compositor to compensate.
         *
         * Off by default, and it must stay off by default. A container that runs
         * today runs on the arrangement it was created under, and this changes
         * how every window in it is created and mapped; a default is not a
         * setting, and the moment a working container is opted in without asking
         * it becomes one. The switch is in the editor's Display tab.
         */
        boolean wemuOwnsDesktop = "1".equals(resolve("wemuOwnsDesktop", container.getExtra("wemuOwnsDesktop")));
        if (wemuOwnsDesktop) {
            /*
             * `root` is Wine Explorer's explicit name for the real X root.
             * Unlike `/desktop=shell,WxH`, it does not set Wine's
             * DF_WINE_CREATE_DESKTOP flag, create a containing X window, or
             * disable the fullscreen hack. Explorer still stays resident and
             * starts winhandler, though, so the EXEC/process/front-window bridge
             * and its `/dir` launch semantics remain exactly as in the legacy
             * stack.
             *
             * Use the explicit magic name rather than bare `/desktop`. The bare
             * form consults HKCU\\Software\\Wine\\Explorer\\Desktop and could
             * silently become a virtual desktop again if a prefix has that set.
             */
            String guestCommand = "wine explorer /desktop=root " + wineStartCommand();
            guestProgramLauncherComponent.setGuestExecutable(guestCommand);
            Log.d(TAG, "Wemu owns the desktop; Wine Explorer is anchored to the X root: "
                    + guestCommand);
        } else {
            // This Wine patch suppresses Wine's automatic explorer.
            // It is only a duplicate when we start explorer explicitly here;
            // rootless mode needs Wine to create its own shell infrastructure.
            envVars.put("WINE_NO_DUPLICATE_EXPLORER", "1");
            /*
             * Sized to the primary monitor rather than to the root window.
             *
             * A virtual desktop is one X window, so whatever is inside it sees
             * exactly one screen no matter what RandR says — the second panel
             * is unreachable from in here by construction. Sizing it to a root
             * that spans both panels would therefore not gain a screen; it
             * would only make a desktop twice as tall as the one it is drawn
             * on. A container that wants the second screen wants the root path
             * above.
             */
            ScreenLayout.Monitor primary = xServer.getPrimaryMonitor();
            String desktopSize = primary.width + "x" + primary.height;
            String guestCommand = "wine explorer /desktop=shell," + desktopSize
                    + " " + wineStartCommand();
            guestProgramLauncherComponent.setGuestExecutable(guestCommand);
            Log.d(TAG, "Wine runs inside its own virtual desktop at " + desktopSize
                    + ": " + guestCommand);
        }

        envVars.putAll(container.getEnvVars());
        if (bootstrapShortcut != null) {
            envVars.putAll(bootstrapShortcut.getExtra("envVars"));
        }
        // WINEPREFIX is session ownership, not a free-form launch override.
        // Reassert the canonical container path after both user-controlled
        // environment merges so teardown can never target another prefix.
        envVars.put("WINEPREFIX", winePrefix);
        boolean officialSteamLaunch = bootstrapShortcut != null
                && "1".equals(bootstrapShortcut.getExtra("steamClientLaunch"));
        if (officialSteamLaunch) {
            /*
             * `-applaunch` tells Valve's client which game to start. The
             * SteamAppId/SteamGameId variables are normally exported *by* that
             * client to the game it creates; putting them on Steam.exe itself
             * can make the client bootstrap look like the game process. Keep
             * the descriptor's app id for app-side identity and diagnostics,
             * but let the authenticated client own its child's environment.
             */
            envVars.remove("SteamAppId");
            envVars.remove("SteamGameId");
        } else {
            if (bootstrapShortcut != null) applySteamLaunchIdentity();
        }
        String steamShimMode = bootstrapShortcut == null
                ? null
                : bootstrapShortcut.getExtra("steamShimMode");
        if (SteamClientRuntimePolicy.shouldDisableProtonLsteamclient(
                officialSteamLaunch, steamShimMode)) {
            // Proton's native-Steam bridge must not replace Valve's Windows
            // steamclient DLLs in a managed full-client session. Steam's game
            // children inherit this and remain connected to the genuine
            // client. The legacy ColdClient loader also requires this switch.
            envVars.put("PROTON_DISABLE_LSTEAMCLIENT", "1");
        }
        if (!cpuTopology.isEmpty()) envVars.put("WINE_CPU_TOPOLOGY", cpuTopology);
        if (!envVars.has("WINEESYNC")) envVars.put("WINEESYNC", "1");
        applyPerformanceOverrides(envVars, container);
        if (bootstrapShortcut != null
                && StardewCoreClrCompatibility.applyTo(envVars, bootstrapShortcut.path)) {
            Log.i(TAG, "Stardew CoreCLR compatibility applied to " + bootstrapShortcut.path);
        }
        if (bootstrapShortcut != null
                && Fallout3Compatibility.applyDxvkEnvironment(
                        envVars, bootstrapShortcut.path)) {
            Log.i(TAG, "Fallout 3 Vulkan pipeline compatibility applied to "
                    + bootstrapShortcut.path);
        }
        if (bootstrapShortcut != null
                && Fallout3Compatibility.isLaunchChainExecutable(bootstrapShortcut.path)) {
            boolean settingsReady = Fallout3Compatibility.applyRuntimeSettings(
                    new File(winePrefix));
            Log.i(TAG, "Fallout 3 performance profile "
                    + (settingsReady ? "verified" : "deferred; INIs not created yet"));
        }
        if (bootstrapShortcut != null
                && BethesdaDisplayCompatibility.isSupportedExecutable(bootstrapShortcut.path)) {
            int profiles = BethesdaDisplayCompatibility.applyExistingProfiles(
                    new File(winePrefix));
            Log.i(TAG, "Bethesda dual-screen display compatibility verified "
                    + profiles + " existing profile(s) for " + bootstrapShortcut.path);
        }
        logEffectivePerformanceConfiguration();

        /*
         * Steam's Chromium helpers currently hit a FEX multiblock translation
         * bug and repeatedly crash before the client can draw. Keep the
         * workaround scoped to the official client: managed Steam descriptors
         * are explicit, while a bare desktop receives it only if Steam exists
         * in this exact prefix so opening Steam there works too. Direct game
         * shortcuts retain the container's selected FEX performance preset.
         *
         * This is deliberately applied after container/shortcut environment
         * and performance merges. GuestProgramLauncherComponent merges its FEX
         * preset first and this session environment last, so this value wins.
         */
        if (SteamClientRuntimePolicy.shouldDisableFexMultiblock(
                officialSteamLaunch, bootstrapShortcut == null, new File(winePrefix))) {
            envVars.put("FEX_MULTIBLOCK", "0");
            Log.i(TAG, "Disabled FEX multiblock for the official Steam client session");
        }

        /*
         * Frame generation is an implicit Vulkan layer. Its files can exist in
         * the shared image without affecting a game, but ENABLE_VAR causes the
         * loader to insert it into this session's Vulkan device. Gate both
         * preparation and that variable on the container's actual setting.
         */
        boolean frameGenerationDiagnostic = FrameGeneration.isDiagnostic(context);
        boolean frameGenerationRequested =
                FrameGeneration.isRequested(container, frameGenerationDiagnostic);
        if (frameGenerationRequested && FrameGeneration.prepare(context, container)) {
            FrameGeneration.applyEnvVars(envVars);
            // The loader says nothing about a layer it declined to load unless
            // asked, so a container already collecting logs collects those too
            // — and a diagnostic run always does, since the whole reason it is
            // running is to read what the loader saw.
            if (enableWineDebug || frameGenerationDiagnostic) {
                FrameGeneration.applyDebugEnvVars(envVars, frameGenerationDiagnostic);
            }
            Log.d(TAG, "Frame generation enabled");
        } else {
            // Container and shortcut environment is merged above. Remove a
            // stale/manual copy as well as declining to add a new one.
            FrameGeneration.clearEnvVars(envVars);
            if (frameGenerationRequested) {
                Log.w(TAG, "Frame generation was requested but its layer could not be prepared");
            } else {
                Log.d(TAG, "Frame generation disabled; guest Vulkan layer not requested");
            }
        }

        ArrayList<String> bindingPaths = new ArrayList<>();
        for (String[] drive : container.drivesIterator()) bindingPaths.add(drive[1]);
        guestProgramLauncherComponent.setBindingPaths(bindingPaths.toArray(new String[0]));

        guestProgramLauncherComponent.setBox64Preset(resolve("box64Preset", container.getBox64Preset()));
        guestProgramLauncherComponent.setFEXCorePreset(resolve("fexcorePreset", container.getFEXCorePreset()));

        environment = new XEnvironment(context, imageFs);
        environment.addComponent(new SysVSharedMemoryComponent(
                xServer, UnixSocketConfig.createSocket(rootPath, UnixSocketConfig.SYSVSHM_SERVER_PATH)));

        XServerComponent xServerComponent = new XServerComponent(
                xServer, UnixSocketConfig.createSocket(rootPath, UnixSocketConfig.XSERVER_PATH));
        /*
         * Says whether a guest ever arrived, and it is here because that is the
         * one question the desktopless mode keeps failing on.
         *
         * Three attempts have now ended with the session closing, and telling
         * them apart needs to distinguish "Wine never reached X" from "its X
         * clients arrived and then left". Automatic Explorer and Wine services
         * are clients too, so this diagnoses the display stack, not the target
         * executable by itself.
         *
         * Log only, and only in this mode. Nothing branches on it: the launcher
         * owns the session's lifetime again, exactly as under the desktop, and
         * making this load-bearing is what the last two attempts got wrong.
         */
        if (wemuOwnsDesktop) {
            xServerComponent.setClientPresenceListener(count ->
                    Log.d(TAG, "X clients: " + count));
        }
        environment.addComponent(xServerComponent);

        if ("alsa".equals(audioDriver)) {
            envVars.put("ANDROID_ALSA_SERVER", rootPath + UnixSocketConfig.ALSA_SERVER_PATH);
            envVars.put("ANDROID_ASERVER_USE_SHM", "true");
            alsaServerComponent = new ALSAServerComponent(
                    UnixSocketConfig.createSocket(rootPath, UnixSocketConfig.ALSA_SERVER_PATH),
                    isEnabled(container, ContainerPerformanceOptions.BURST_ALIGNED_AUDIO));
            // The request may arrive before prepare/start. Applying it here
            // ensures the very first XAudio stream starts at the intended level
            // instead of jumping from unity after Skyrim's first window appears.
            alsaServerComponent.setGainDb(requestedAudioGainDb);
            environment.addComponent(alsaServerComponent);
        } else if ("pulseaudio".equals(audioDriver)) {
            envVars.put("PULSE_SERVER", rootPath + UnixSocketConfig.PULSE_SERVER_PATH);
            environment.addComponent(new PulseAudioComponent(
                    UnixSocketConfig.createSocket(rootPath, UnixSocketConfig.PULSE_SERVER_PATH)));
        }

        guestProgramLauncherComponent.setEnvVars(envVars);
        Log.i(TAG, "Final launch environment: " + envVars);
        guestProgramLauncherComponent.setTerminationCallback(status -> {
            running = false;
            Log.i(TAG, "Guest process tree ended with status " + status
                    + " (shortcut=" + (shortcut != null ? shortcut.path : "desktop") + ")");
            RuntimeTerminationListener listener = terminationListener;
            if (listener != null) listener.onSessionEnded(status);
        });
        environment.addComponent(guestProgramLauncherComponent);

        // Wine expects this to exist before it enumerates input devices; it does
        // not create it itself, and a missing directory is a silent failure that
        // shows up much later as a game that sees no gamepad.
        File devInputDir = new File(imageFs.getRootDir(), "dev/input");
        if (!devInputDir.exists() && !devInputDir.mkdirs()) {
            Log.w(TAG, "Could not create " + devInputDir + "; input devices may not be visible");
        }

        /*
         * Where gamepad state is written, and without it there is no gamepad.
         *
         * `WinHandler` writes evdev events into `event0`..`event3` under this
         * directory, which is what the guest reads as its controllers. The path
         * is the only thing that turns the writers on: `assignSlot` checks
         * `fakeInputBasePath != null` before constructing a `FakeInputWriter`,
         * so with it unset every controller is assigned a slot whose writer is
         * null and every `sendGamepadState` silently discards the state.
         *
         * Upstream set this from the activity, so it went missing when the
         * session was lifted off one — the same way `activateContainer` did. The
         * symptom is a game that runs perfectly and ignores the controller.
         */
        winHandler.setFakeInputPath(devInputDir.getPath());

        // INIT is a one-shot UDP packet, so the listener must be ready first.
        if (!winHandler.start()) {
            throw new IllegalStateException(
                    "The Windows guest bridge could not start. Close any other WEMU session and try again.");
        }
        try {
            environment.startEnvironmentComponents();
        } catch (RuntimeException | Error failure) {
            winHandler.stop();
            throw failure;
        }
    }

    /**
     * Gives a Steam game the identity stored on its shortcut.
     *
     * The guest launcher has historically supplied {@code SteamGameId=0} as a
     * Proton default. That value is harmless for ordinary Windows programs but
     * it is actively wrong for a Steam shortcut: both Valve's API bootstrap and
     * compatible Steamworks implementations use these variables when a title
     * is started outside the desktop Steam client. The shortcut already carries
     * the authoritative app id, so do not make every game rediscover it from a
     * sidecar file in whichever directory happens to be current.
     *
     * Applied after the free-form container and shortcut environment so stale
     * hand-written values cannot make one shortcut identify as another game.
     */
    private void applySteamLaunchIdentity() {
        if (shortcut == null) return;

        String rawAppId = shortcut.getExtra("steamAppId");
        String value = SteamLaunchIdentity.apply(envVars, rawAppId);
        if (value != null) {
            Log.d(TAG, "Steam launch identity: " + value);
        } else if (!rawAppId.trim().isEmpty()) {
            Log.w(TAG, "Ignoring invalid Steam app id on " + shortcut.name + ": " + rawAppId);
        }
    }



    /*
     * The only setting here that reduces what the guest draws, and the reason
     * there is no longer a render scale beside it.
     *
     * Nothing this app does after the guest has finished a frame can make the
     * guest produce it faster. The upscalers in the compositor run on a finished
     * image: they decide how it is reconstructed on its way to the panel and
     * cost a little to do it. Asked whether turning one on gains frames, the
     * answer is no, and no amount of work on them will change that.
     *
     * Render scale looked like the half that did. It lowered the desktop the
     * guest was given, so a game that takes its resolution from the desktop
     * drew fewer pixels and really was faster. A game that remembers its own
     * resolution — most of them, after a first run — ignored it entirely, and
     * there was no way to tell which you had short of trying. Measured here: a
     * container at 1280x720 with the scale at 75% handed the guest a 960x540
     * desktop, and `video.scr` said `Resolution(1286,722)`, and the game drew
     * every one of them. The hack below then scaled 1286x722 down to 960x540 so
     * the compositor could scale it back up to 1080p — two resamplings, a
     * softer picture, no frames.
     *
     * And it was never a second, independent figure. It multiplied the screen
     * size and handed the result to the X server, so it was the screen size,
     * spelled as a percentage of itself. A container that wants a 960x540 guest
     * can say so directly in one control instead of two that multiply.
     *
     * `WINE_FULLSCREEN_FSR` is the case that works, and it works by being on
     * the other side of the boundary. The resolution is lowered *in the game*,
     * where the game will obey it, and Wine scales the smaller frame up to the
     * desktop with FSR on the way out. The pixels are never drawn, so the
     * saving is real, and the upscale happens before anything here sees the
     * frame.
     *
     * <h2>It also carries the fullscreen hack, which is a fix in its own right</h2>
     *
     * A game does not render at the desktop resolution because it was told to.
     * It picks a mode from a list and asks for it, and what it asks for are the
     * standard ones — 1600x900, 1920x1080. Ask for one larger than the desktop
     * with the hack disabled and Wine cannot scale it: the game gets an
     * oversized swapchain and the compositor shows the top-left corner of it.
     * That is "the game opens in the corner and only deleting the container
     * fixes it" — deleting the container worked because it deleted the game's
     * saved resolution along with it.
     *
     * Render scale used to be what turned the hack back on, because shrinking
     * the desktop is the easiest way to provoke the mismatch. It is not the only
     * way, so the switch owns it now: a container whose game opens cropped is
     * fixed by turning this on, whatever its screen size.
     *
     * The launcher sets `WINE_DISABLE_FULLSCREEN_HACK` to "1" directly and the
     * container's environment is merged over the top afterwards, so writing it
     * here wins.
     */
    private void applyGuestUpscalingEnv(EnvVars envVars, Container container) {
        if (!isEnabled(container, "guestFsr")) return;

        int strength = guestFsrStrength(container);
        envVars.put("WINE_FULLSCREEN_FSR", "1");
        envVars.put("WINE_FULLSCREEN_FSR_STRENGTH", String.valueOf(strength));
        envVars.put("WINE_DISABLE_FULLSCREEN_HACK", "0");
        Log.d(TAG, "Wine will scale a game running below the desktop up to it, with FSR at "
                + "strength " + strength + "; the fullscreen hack it needs is left enabled");
    }

    /**
     * Wine's own FSR strength, on Proton's scale rather than this app's.
     *
     * The console stores sharpness as 0..100 with 100 sharpest.
     * `WINE_FULLSCREEN_FSR_STRENGTH` is 0..5 with *0* sharpest, so the mapping
     * is an inversion, not a rescale. Getting that backwards is invisible —
     * the picture is merely softer than asked for — which is the kind of bug
     * that survives for a long time.
     */
    private int guestFsrStrength(Container container) {
        float sharpness = RendererOptions.sharpnessOf(container);   // 0..1
        int strength = Math.round((1f - sharpness) * 5f);
        return Math.max(0, Math.min(5, strength));
    }

    /**
     * Applies explicit compatibility overrides carried by this container.
     *
     * Current Wemu defaults already allow Turnip tiling and DXVK timeline
     * semaphores. The two switches below matter only when an imported or
     * hand-edited environment contains the corresponding disable variable;
     * leaving a restriction absent is deliberately not presented as a new
     * optimization.
     */
    private void applyPerformanceOverrides(EnvVars envVars, Container container) {
        applyGuestUpscalingEnv(envVars, container);

        /*
         * Two guest monitors require normal rootless X windows. Proton's
         * fullscreen hack lets a game keep its fullscreen state and native
         * resolution while Wine represents that surface as a borderless
         * window, so an auxiliary swapchain can occupy the lower monitor.
         * Without it DXHR creates an exclusive primary surface; changing that
         * state later from a Present hook tears down WSI mid-frame and leaves
         * the top panel white. This is not upscaling and does not alter the
         * rendered resolution.
         */
        if (isEnabled(container, ContainerPerformanceOptions.SECOND_SCREEN)) {
            envVars.put("WINE_DISABLE_FULLSCREEN_HACK", "0");
            Log.d(TAG, "Wine fullscreen compatibility left enabled for the second output");
        }

        if (isEnabled(container, "speedupTiledRendering")) {
            String tuDebug = envVars.get("TU_DEBUG");
            if (tuDebug != null && tuDebug.contains("sysmem")) {
                String kept = tuDebug
                        .replace(",sysmem", "")
                        .replace("sysmem,", "")
                        .replace("sysmem", "");
                if (kept.isEmpty()) envVars.remove("TU_DEBUG");
                else envVars.put("TU_DEBUG", kept);
                Log.d(TAG, "Tiled rendering on; TU_DEBUG is now '" + kept + "'");
            }
        }

        if (isEnabled(container, "speedupTimelineSemaphores")) {
            envVars.remove("DXVK_DISABLE_TIMELINE_SEMAPHORES");
            Log.d(TAG, "Timeline semaphores on");
        }

        /*
         * The state cache, which is the one change here that is not a trade.
         *
         * DXVK compiles pipelines the first time it meets them and the pauses
         * that causes are the most-felt performance problem in a Direct3D game.
         * It will keep them between runs given somewhere to write, and nothing
         * was giving it one.
         *
         * A *guest* path. This is read by DXVK, which is a Windows DLL using
         * Windows file APIs — handing it the host's `/data/data/...` directory
         * names a location that does not exist from inside the container, which
         * is how the first version of this quietly did nothing.
         */
        /*
         * Wine's shortcut builder, switched off.
         *
         * `winemenubuilder.exe` exists to turn Windows shortcuts into Linux
         * desktop entries. There is no desktop here to put them on, and Wine
         * starts it every time an executable is launched and every time one
         * installs anything — so it is pure cost, paid at exactly the moment a
         * game is starting and the CPU is busiest.
         *
         * Appended rather than assigned: a container may already override
         * something, and silently dropping that would be a worse bug than the
         * one this fixes.
         */
        String overrides = envVars.get("WINEDLLOVERRIDES");
        String builderOff = "winemenubuilder.exe=d";
        if (overrides == null || overrides.isEmpty()) {
            envVars.put("WINEDLLOVERRIDES", builderOff);
        } else if (!overrides.contains("winemenubuilder")) {
            envVars.put("WINEDLLOVERRIDES", overrides + ";" + builderOff);
        }

        File cacheDir = new File(container.getRootDir(), ".wine/drive_c/dxvk-cache");

        /*
         * Thrown away when the revision moves, and this is why it has one.
         *
         * A state cache is per executable and outlives everything else here. It
         * records pipeline state a game built under whatever settings were in
         * force at the time — so a cache written while a container was running
         * with the wrong rendering flags keeps handing those pipelines back
         * afterwards. Correcting the setting does not correct the file, and the
         * symptom is a game that ran, stopped running, and will not start again
         * whatever you change: the thing that broke it is on disk and is
         * reloaded every launch.
         *
         * Bump the revision on any change that could alter what DXVK compiles.
         */
        if (!String.valueOf(DXVK_CACHE_REVISION).equals(container.getExtra("dxvkCacheRevision"))) {
            if (cacheDir.isDirectory()) {
                FileUtils.delete(cacheDir);
                Log.d(TAG, "Discarded the DXVK state cache for container " + container.id);
            }
            container.putExtra("dxvkCacheRevision", String.valueOf(DXVK_CACHE_REVISION));
            container.saveData();
        }

        if (cacheDir.isDirectory() || cacheDir.mkdirs()) {
            envVars.put("DXVK_STATE_CACHE_PATH", "C:\\dxvk-cache");
            envVars.put("DXVK_GPLASYNC_CACHE_PATH", "C:\\dxvk-cache");
        }
    }

    /**
     * Records resolved performance state after every environment override.
     * This makes a no-op switch distinguishable from an ineffective
     * optimization in an adb capture without logging the complete environment.
     */
    private void logEffectivePerformanceConfiguration() {
        String tuDebug = envVars.get("TU_DEBUG");
        boolean tiled = tuDebug == null || !tuDebug.contains("sysmem");
        boolean timelines = !envVars.has("DXVK_DISABLE_TIMELINE_SEMAPHORES");
        boolean mobileDxvk = "1".equals(dxwrapperConfig.get("mobileTuning"));
        boolean relaxedBarriers = "1".equals(dxwrapperConfig.get("relaxedBarriers"));
        String affinity = container.getExtra("affinityCores");
        if (affinity == null || affinity.isEmpty()) affinity = "automatic";
        // Resolve through the guest launcher's policy so diagnostics cannot
        // drift from HODLL selection. Ordinary ARM64EC sessions remain on FEX;
        // the authenticated Transformers preset is the explicit Box64 exception.
        String effectiveEmulator = guestProgramLauncherComponent.getEffectiveEmulator();
        boolean fex = !"box64".equalsIgnoreCase(effectiveEmulator);
        String translator = fex ? "FEXCore" : "Box64";
        String pinnedBox64 = guestProgramLauncherComponent.getPinnedBox64RuntimeDiagnostic();

        Log.i(TAG, "Effective performance: renderer=" + getDisplayRenderer()
                + ", directScanout="
                + isEnabled(container, ContainerPerformanceOptions.DIRECT_SCANOUT)
                + ", guestPresent=" + graphicsDriverConfig.get("presentMode")
                + ", skipPresentWait="
                + "1".equals(graphicsDriverConfig.get("disablePresentWait"))
                + ", turnipAutotune="
                + com.wemu.engine.core.TurnipPerformanceOptions.resolveAutotune(
                        graphicsDriverConfig.get(
                                com.wemu.engine.core.TurnipPerformanceOptions.CONFIG_AUTOTUNE))
                + ", turnipTrace="
                + com.wemu.engine.core.TurnipPerformanceOptions.gpuTraceEnabled(
                        graphicsDriverConfig.get(
                                com.wemu.engine.core.TurnipPerformanceOptions.CONFIG_GPU_TRACE))
                + ", tiled=" + tiled
                + ", timelines=" + timelines
                + ", mobileDxvk=" + mobileDxvk
                + ", relaxedBarriers=" + relaxedBarriers
                + ", affinity=" + affinity
                + ", translator=" + translator + "/"
                + resolve(fex ? "fexcorePreset" : "box64Preset",
                        fex ? container.getFEXCorePreset() : container.getBox64Preset())
                + (pinnedBox64.isEmpty() ? "" : ", pinnedBox64=" + pinnedBox64));
    }

    /**
     * Bumped whenever something could change what DXVK compiles.
     *
     * Two, because one was the revision under which caches were written while
     * tiled rendering and timeline semaphores were wrongly on for every
     * container that had been saved in that window.
     */
    private static final int DXVK_CACHE_REVISION = 2;

    /** A container extra explicitly turned on. Absent means the stock behaviour. */
    private static boolean isEnabled(Container container, String key) {
        return "1".equals(container.getExtra(key));
    }

    /**
     * Builds the X server, and decides how many screens the guest is told it has.
     *
     * <h2>Why the root window grows</h2>
     *
     * X has one root window per screen, and everything a client can put on
     * screen lives inside it. Two monitors in X are two rectangles of one root,
     * not two roots — so telling the guest about the Thor's bottom panel means
     * the root has to be tall enough to contain it. That is why this returns a
     * different {@link ScreenInfo} rather than only a different layout: a
     * two-monitor layout inside a root the size of one panel would describe a
     * screen positioned outside the only surface a window can exist on, and
     * anything moved there would be clipped away.
     *
     * <h2>Why the second panel is measured rather than assumed</h2>
     *
     * The bottom screen is not the same shape as the top one and is not the
     * same on every unit. Reading it from the display Android is already
     * reporting keeps the layout honest on hardware this was not written on,
     * and means a device with no second panel gets the single-screen layout
     * even if the container asks for two — which is the right answer for a
     * container carried over to a phone.
     */
    /**
     * The root window size, or the best available answer when it is unusable.
     *
     * A screen size is text held in a container -- and, for a shortcut, in an
     * override typed per game -- so it can be malformed or outside the range the
     * X protocol can carry. Neither is worth refusing to start a session over,
     * and neither should be accepted silently: a truncated dimension produces a
     * guest rendering to a screen that does not exist, with nothing anywhere
     * saying why.
     *
     * The shortcut override is tried first, then the container's own value, then
     * a documented default. Every fallback says what it rejected.
     */
    private ScreenInfo resolveScreenInfo(String requested) {
        try {
            return new ScreenInfo(requested);
        } catch (IllegalArgumentException rejected) {
            Log.w(TAG, "Screen size \"" + requested + "\" is unusable ("
                    + rejected.getMessage() + "); falling back to the container's own");
        }
        String containerSize = container.getScreenSize();
        if (!containerSize.equals(requested)) {
            try {
                return new ScreenInfo(containerSize);
            } catch (IllegalArgumentException rejected) {
                Log.w(TAG, "The container's screen size \"" + containerSize
                        + "\" is unusable too (" + rejected.getMessage() + ")");
            }
        }
        Log.w(TAG, "Falling back to " + FALLBACK_SCREEN_WIDTH + "x" + FALLBACK_SCREEN_HEIGHT);
        return new ScreenInfo(FALLBACK_SCREEN_WIDTH, FALLBACK_SCREEN_HEIGHT);
    }

    /** What a session runs at when its configured size cannot be used at all. */
    private static final int FALLBACK_SCREEN_WIDTH = 1280;
    private static final int FALLBACK_SCREEN_HEIGHT = 720;

    private XServer createXServer(ScreenInfo screenInfo) {
        if (!isEnabled(container, ContainerPerformanceOptions.SECOND_SCREEN)) {
            return new XServer(screenInfo);
        }

        android.view.Display secondScreen = findSecondScreen();
        if (secondScreen == null) {
            Log.w(TAG, "Container asked for the second screen, but this device reports none");
            return new XServer(screenInfo);
        }

        android.graphics.Point size = new android.graphics.Point();
        secondScreen.getRealSize(size);
        if (size.x <= 0 || size.y <= 0) return new XServer(screenInfo);

        /*
         * The root spans the taller of the two widths, so neither panel's
         * rectangle runs off the side of it. The bottom monitor keeps its own
         * width and is left-aligned; a guest reads that width from RandR, so a
         * narrower second panel is described as narrower rather than stretched.
         */
        int rootWidth = Math.max(screenInfo.width, size.x);
        int rootHeight = screenInfo.height + size.y;
        ScreenInfo spanning = new ScreenInfo(rootWidth, rootHeight);
        ScreenLayout layout = ScreenLayout.stacked(
                screenInfo.width, screenInfo.height, size.x, size.y);

        Log.i(TAG, "Second screen enabled: root " + spanning + ", monitors " + layout);
        /*
         * Told to the renderers here rather than when the panel's surface
         * arrives, because the two happen in either order: the console's
         * presentation may already be up from a previous session, and a
         * renderer submitting a frame before the rectangle is known would send
         * a second-monitor window through the scene instead.
         */
        com.wemu.engine.renderer.SecondScreenOutput.setMonitor(layout.getMonitors().get(1));
        return new XServer(spanning, layout);
    }

    /**
     * The device's own second panel, if it has one.
     *
     * Asks for presentation displays and takes the first, which on the Thor is
     * the bottom screen. A display the app itself created for recording is
     * private and never appears in this category, so nothing the app is doing
     * to itself can be mistaken for a panel.
     */
    private android.view.Display findSecondScreen() {
        android.hardware.display.DisplayManager displays =
                (android.hardware.display.DisplayManager)
                        context.getSystemService(Context.DISPLAY_SERVICE);
        if (displays == null) return null;
        android.view.Display[] found = displays.getDisplays(
                android.hardware.display.DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
        if (found == null || found.length == 0) return null;
        if (found.length == 1) return found[0];

        /*
         * More than one presentation display means something is plugged in.
         *
         * Taking the first was right while the Thor's own panel was the only
         * candidate, and wrong the moment a dock or an HDMI cable adds another:
         * the root window is sized from whichever display this returns, so
         * picking the external one builds the whole session around a screen the
         * player is not holding.
         *
         * The built-in panel is the one that is not removable, and Android
         * knows which that is -- Display.getType() answers TYPE_INTERNAL. It is
         * not public SDK, so it is read reflectively and the whole thing falls
         * back to the previous behaviour if it cannot be. A hidden API worth
         * asking, never worth depending on.
         */
        for (android.view.Display display : found) {
            if (display != null && isInternalDisplay(display)) return display;
        }
        Log.w(TAG, found.length + " presentation displays and none could be identified as"
                + " built in; using the first, which may be an attached monitor");
        return found[0];
    }

    /** Display.TYPE_INTERNAL, which is @hide and therefore asked for carefully. */
    private static final int DISPLAY_TYPE_INTERNAL = 1;

    private static boolean isInternalDisplay(android.view.Display display) {
        try {
            java.lang.reflect.Method getType =
                    android.view.Display.class.getMethod("getType");
            Object type = getType.invoke(display);
            return (type instanceof Integer) && (Integer) type == DISPLAY_TYPE_INTERNAL;
        } catch (ReflectiveOperationException | RuntimeException unavailable) {
            return false;
        }
    }

    /**
     * The command Wine is started with.
     *
     * `winhandler.exe` is the original's own shim rather than the game: it is what
     * the app talks to over UDP for gamepad state and process listings, and it
     * launches the real target itself.
     */
    private String wineStartCommand() {
        String args;

        if (shortcut == null || shortcutLaunchDeferred) {
            args = "\"wfm.exe\"";
        } else {
            String execArgs = shortcut.getExtra("execArgs");
            execArgs = execArgs.isEmpty() ? "" : " " + execArgs;

            if (shortcut.path.endsWith(".lnk")) {
                args = "\"" + shortcut.path + "\"" + execArgs;
            } else {
                String fullPath = shortcut.path.replace("\"", "");
                String exeDir;
                String filename;

                int lastSlash = fullPath.lastIndexOf("\\");
                if (lastSlash != -1) {
                    exeDir = fullPath.substring(0, lastSlash);
                    filename = fullPath.substring(lastSlash + 1);
                } else {
                    exeDir = FileUtils.getDirname(fullPath);
                    filename = FileUtils.getName(fullPath);
                }

                // Arguments written after the extension in the stored path — a
                // habit from shortcuts typed by hand — are split back out here,
                // or Wine is handed them as part of the filename.
                int dotIndex = filename.lastIndexOf(".");
                int spaceIndex = dotIndex != -1 ? filename.indexOf(" ", dotIndex) : -1;
                if (spaceIndex != -1) {
                    execArgs = filename.substring(spaceIndex + 1) + execArgs;
                    filename = filename.substring(0, spaceIndex);
                }

                args = "/dir " + StringUtils.escapeDOSPath(exeDir) + " \"" + filename + "\"" + execArgs;
            }
        }

        return "winhandler.exe " + args;
    }

    /**
     * Ends the session and everything it started.
     *
     * Safe to call more than once and safe to call on a session that never
     * started — which matters, because the paths that need it most are the ones
     * where `start` threw partway through.
     */
    @Override
    public void stop() {
        stopRequested = true;
        /*
         * The bottom panel goes back to the console before the guest is torn
         * down, not after. Its layer holds whatever the guest last drew, and a
         * frozen HUD sitting over a console that has resumed is what the panel
         * shows for as long as this is left until later.
         */
        com.wemu.engine.renderer.SecondScreenOutput.clear();
        com.wemu.engine.renderer.SecondScreenOutput.setMonitor(null);
        synchronized (lifecycleLock) {
            running = false;
            stopEnvironmentLocked();
            stopSessionProtectionLocked();
        }
    }

    /** Called only while {@link #lifecycleLock} is held. */
    private void startSessionProtectionLocked() {
        if (sessionProtectionStarted) return;
        NotificationService.startForSession(context);
        sessionProtectionStarted = true;
    }

    /** Called only while {@link #lifecycleLock} is held, after guest teardown. */
    private void stopSessionProtectionLocked() {
        if (!sessionProtectionStarted) return;
        sessionProtectionStarted = false;
        NotificationService.stopForSession(context);
    }

    /** Called only while {@link #lifecycleLock} is held. */
    private void stopEnvironmentLocked() {
        if (environmentStopped) return;
        environmentStopped = true;
        if (winHandler != null) {
            try {
                winHandler.stop();
            } catch (Exception e) {
                Log.w(TAG, "WinHandler did not stop cleanly", e);
            }
        }
        if (environment != null) {
            try {
                environment.stopEnvironmentComponents();
            } catch (Exception e) {
                Log.w(TAG, "Environment did not stop cleanly", e);
            }
        }
    }
}
