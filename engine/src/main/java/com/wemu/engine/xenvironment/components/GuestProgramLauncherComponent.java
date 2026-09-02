package com.wemu.engine.xenvironment.components;

import android.app.Service;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.net.ConnectivityManager;
import android.os.Process;
import android.util.Log;

import androidx.preference.PreferenceManager;

import com.wemu.engine.box64.Box64Preset;
import com.wemu.engine.box64.Box64PresetManager;
import com.wemu.engine.container.Container;
import com.wemu.engine.container.Shortcut;
import com.wemu.engine.contents.ContentProfile;
import com.wemu.engine.contents.ContentsManager;
import com.wemu.engine.core.Callback;
import com.wemu.engine.core.EnvVars;
import com.wemu.engine.core.FileUtils;
import com.wemu.engine.core.GPUInformation;
import com.wemu.engine.core.KeyValueSet;
import com.wemu.engine.core.ProcessHelper;
import com.wemu.engine.core.StardewCoreClrCompatibility;
import com.wemu.engine.core.TarCompressorUtils;
import com.wemu.engine.core.WineInfo;
import com.wemu.engine.fexcore.FEXCoreManager;
import com.wemu.engine.fexcore.FEXCorePreset;
import com.wemu.engine.fexcore.FEXCorePresetManager;
import com.wemu.engine.xconnector.UnixSocketConfig;
import com.wemu.engine.xenvironment.EnvironmentComponent;
import com.wemu.engine.xenvironment.ImageFs;
import com.wemu.runtime.internal.OfficialSteamPrefixPreflight;
import com.wemu.runtime.internal.OfficialSteamPrerequisiteStateRepair;
import com.wemu.runtime.internal.OfficialSteamWebHelperPolicy;
import com.wemu.runtime.internal.SteamClientRuntimePolicy;

import java.io.File;
import java.io.IOException;
import java.net.InetAddress;
import java.net.URL;
import java.nio.file.Files;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Locale;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.function.IntSupplier;

public class GuestProgramLauncherComponent extends EnvironmentComponent {
    private static final long WINESERVER_STOP_TIMEOUT_MS = 2_000L;
    private static final long WINESERVER_FORCE_WAIT_MS = 250L;
    private static final long STEAM_PREFIX_WINEBOOT_TIMEOUT_SECONDS = 120L;
    private static final long STEAM_PREFIX_SERVER_WAIT_TIMEOUT_SECONDS = 15L;
    private static final long STEAM_PREFIX_CLEANUP_TIMEOUT_SECONDS = 10L;
    private static final long STEAM_SUPERVISOR_POLL_MS = 500L;
    private static final Object BOX64_EXTRACTION_LOCK = new Object();
    private static final ConcurrentHashMap<String, Object> STEAM_PREFIX_PREFLIGHT_LOCKS =
            new ConcurrentHashMap<>();
    private String guestExecutable;
    private String[] guestArgv;
    private File guestWorkingDir;
    private int pid = -1;
    private String[] bindingPaths;
    private EnvVars envVars;
    private WineInfo wineInfo;
    private String box64Preset = Box64Preset.COMPATIBILITY;
    private String fexcorePreset = FEXCorePreset.INTERMEDIATE;
    private Callback<Integer> terminationCallback;
    private final Object lock = new Object();
    private String[] wineserverStopCommand;
    private String[] shutdownEnvVars;
    private File shutdownWorkingDir;
    private String shutdownWinePrefix;
    private OfficialSteamBootstrapSupervisor steamBootstrapSupervisor;
    private Thread steamBootstrapThread;
    private OfficialSteamProcessHealthMonitor steamHealthMonitor;
    private boolean stopping;
    private final ContentsManager contentsManager;
    private final ContentProfile wineProfile;
    private SharedBox64RuntimeState.RuntimeRequest activeBox64RuntimeRequest;
    private Container container;
    private final Shortcut shortcut;

    public void setWineInfo(WineInfo wineInfo) {
        this.wineInfo = wineInfo;
    }
    public WineInfo getWineInfo() {
        return this.wineInfo;
    }

    public Container getContainer() { return this.container; }
    public void setContainer(Container container) { this.container = container; }

    private SharedBox64RuntimeState.RuntimeRequest resolveBox64RuntimeRequest() {
        String box64Version = container.getBox64Version();
        if (shortcut != null) {
            box64Version = shortcut.getExtra(
                    "box64Version", shortcut.container.getBox64Version());
        }
        return SharedBox64RuntimeState.runtimeRequest(
                container.getExtra(
                        SharedBox64RuntimeState.MANAGED_STEAM_CONTAINER_MARKER_KEY),
                container.getExtra(
                        SharedBox64RuntimeState.MANAGED_STEAM_SPEC_MARKER_KEY),
                container.getExtra(
                        SharedBox64RuntimeState.MANAGED_STEAM_CONTAINER_REVISION_KEY),
                container.getWineVersion(),
                box64Version,
                container.getExtra(SharedBox64RuntimeState.PINNED_RUNTIME_KEY));
    }

    /** Returns the same translator choice execGuestProgram will apply. */
    public String getEffectiveEmulator() {
        SharedBox64RuntimeState.RuntimeRequest request = activeBox64RuntimeRequest != null
                ? activeBox64RuntimeRequest : resolveBox64RuntimeRequest();
        String shortcutEmulator = shortcut == null
                ? null : shortcut.getExtra("emulator", "");
        return SharedBox64RuntimeState.resolveEmulator(
                request,
                container.getEmulator(),
                shortcutEmulator,
                wineInfo != null && wineInfo.isArm64EC());
    }

    /** Full authenticated identity for diagnostics; empty for an ordinary selection. */
    public String getPinnedBox64RuntimeDiagnostic() {
        SharedBox64RuntimeState.RuntimeRequest request = activeBox64RuntimeRequest != null
                ? activeBox64RuntimeRequest : resolveBox64RuntimeRequest();
        if (!request.forceBox64Translator) return "";
        return request.version
                + "/box64-sha256=" + request.requiredSha256
                + "/wowbox64-sha256=" + request.requiredWowBox64Sha256;
    }

    private void extractBox64Files() {
        synchronized (BOX64_EXTRACTION_LOCK) {
            extractSharedBox64Files();
        }
    }

    private void extractSharedBox64Files() {
        ImageFs imageFs = environment.getImageFs();
        Context context = environment.getContext();

        SharedBox64RuntimeState.RuntimeRequest box64Request = resolveBox64RuntimeRequest();
        String box64Version = box64Request.version;

        Log.d("GuestProgramLauncherComponent", "box64Version: " + box64Version);

        File rootDir = imageFs.getRootDir();
        File box64File = new File(rootDir, "/usr/bin/box64");
        File versionMarker = SharedBox64RuntimeState.markerFile(
                new File(context.getFilesDir(), "runtime/box64"));

        /*
         * This executable belongs to the shared image, not to a container.
         * The app-private marker records both its selected version and SHA-256,
         * so a different container or a guest-side edit cannot silently leave
         * the wrong Box64 in use. Replacement is staged and atomic below.
         */
        if (!SharedBox64RuntimeState.isCurrent(
                box64File,
                versionMarker,
                box64Request.markerId,
                box64Request.requiredSha256)) {
            try {
                // Never leave the old version claiming ownership while its
                // shared binary is being replaced.
                SharedBox64RuntimeState.invalidate(versionMarker);
            } catch (IOException e) {
                throw new IllegalStateException(
                        "Could not invalidate the shared Box64 version marker", e);
            }
            ContentProfile profile = box64Request.allowInstalledProfile
                    ? contentsManager.getProfileByEntryName("box64-" + box64Version)
                    : null;
            installSharedBox64Binary(
                    context,
                    profile,
                    box64Version,
                    box64File,
                    box64Request.requiredSha256);
            try {
                SharedBox64RuntimeState.publish(
                        versionMarker,
                        box64Request.markerId,
                        box64File,
                        box64Request.requiredSha256);
            } catch (IOException e) {
                throw new IllegalStateException(
                        "Could not publish the shared Box64 version marker", e);
            }
        }

        FileUtils.chmod(box64File, 0755);
        activeBox64RuntimeRequest = box64Request;
    }

    /** Stages a complete binary and atomically swaps the shared live path. */
    private void installSharedBox64Binary(
            Context context,
            ContentProfile profile,
            String box64Version,
            File liveBinary,
            String requiredSha256) {
        File extractionDirectory = null;
        try {
            final File sourceBinary;
            final File sourceRoot;
            if (profile != null) {
                sourceRoot = ContentsManager.getInstallDir(context, profile)
                        .getCanonicalFile();
                sourceBinary = findInstalledBox64Source(context, profile);
            } else {
                File stagingRoot = new File(context.getFilesDir(), "runtime/box64/staging");
                if (!stagingRoot.isDirectory() && !stagingRoot.mkdirs()
                        && !stagingRoot.isDirectory()) {
                    throw new IOException(
                            "Could not create the private Box64 staging directory");
                }
                extractionDirectory = Files.createTempDirectory(
                        stagingRoot.toPath(), "bundled-").toFile();
                boolean extracted = TarCompressorUtils.extract(
                        TarCompressorUtils.Type.ZSTD,
                        context,
                        "box64/box64-" + box64Version + ".tzst",
                        extractionDirectory);
                if (!extracted) {
                    throw new IOException(
                            "Could not extract bundled Box64 " + box64Version);
                }
                sourceBinary = new File(extractionDirectory, "usr/bin/box64");
                sourceRoot = extractionDirectory;
            }

            SharedBox64RuntimeState.installBinaryAtomically(
                    sourceBinary,
                    sourceRoot,
                    liveBinary,
                    environment.getImageFs().getRootDir(),
                    requiredSha256);
        } catch (IOException e) {
            throw new IllegalStateException(
                    "Could not stage Box64 " + box64Version, e);
        } finally {
            if (extractionDirectory != null && !FileUtils.delete(extractionDirectory)) {
                Log.w("GuestProgramLauncherComponent",
                        "Could not remove private Box64 staging directory: "
                                + extractionDirectory);
            }
        }
    }

    private static File findInstalledBox64Source(
            Context context,
            ContentProfile profile) throws IOException {
        if (profile.type != ContentProfile.ContentType.CONTENT_TYPE_BOX64
                || profile.fileList == null) {
            throw new IOException("The selected content is not a Box64 profile");
        }
        for (ContentProfile.ContentFile contentFile : profile.fileList) {
            String target = contentFile.target == null
                    ? "" : contentFile.target.replace('\\', '/');
            if (!target.equals("${bindir}/box64")) continue;
            if (contentFile.source == null || contentFile.source.isEmpty()) {
                throw new IOException("The Box64 content source is missing");
            }

            File installDirectory = ContentsManager.getInstallDir(context, profile)
                    .getCanonicalFile();
            File source = ContentsManager.getSourceFile(
                    context, profile, contentFile.source).getCanonicalFile();
            if (!source.toPath().startsWith(installDirectory.toPath())) {
                throw new IOException("The Box64 content source escapes its install directory");
            }
            return source;
        }
        throw new IOException("The selected Box64 profile has no ${bindir}/box64 file");
    }

    private void extractEmulatorsDlls() {;
        Context context = environment.getContext();
        File rootDir = environment.getImageFs().getRootDir();
        File system32dir = new File(rootDir + "/home/xuser/.wine/drive_c/windows/system32");
        boolean containerDataChanged = false;

        String wowbox64Version = container.getBox64Version();
        String fexcoreVersion = container.getFEXCoreVersion();

        if (shortcut != null) {
            wowbox64Version = shortcut.getExtra("box64Version", shortcut.container.getBox64Version());
            fexcoreVersion = shortcut.getExtra("fexcoreVersion", shortcut.container.getFEXCoreVersion());
        }

        SharedBox64RuntimeState.RuntimeRequest pinnedRequest = activeBox64RuntimeRequest;
        if (pinnedRequest != null && pinnedRequest.forceBox64Translator) {
            // extractSharedBox64Files validated any shortcut version override
            // before this ARM64EC companion DLL is considered.
            wowbox64Version = pinnedRequest.version;
        }

        Log.d("GuestProgramLauncherComponent", "box64Version in use: " + wowbox64Version);
        Log.d("GuestProgramLauncherComponent", "fexcoreVersion in use: " + fexcoreVersion);

        /*
         * As in `extractBox64Files`: the version is recorded on attempt rather
         * than on success, so the file it should have produced is the condition
         * that can be trusted. These two are the DLLs `HODLL` names, and Wine
         * exits immediately when it cannot load one.
         */
        File wowbox64Dll = new File(system32dir, "wowbox64.dll");
        File fexDll = new File(system32dir, "libwow64fex.dll");

        File wowbox64Marker = SharedBox64RuntimeState.markerFile(
                new File(context.getFilesDir(), "runtime/wowbox64"));
        boolean pinnedWowBox64 = pinnedRequest != null
                && pinnedRequest.requiredWowBox64Sha256 != null;
        boolean wowbox64Current = pinnedWowBox64
                ? SharedBox64RuntimeState.isCurrent(
                        wowbox64Dll,
                        wowbox64Marker,
                        pinnedRequest.markerId,
                        pinnedRequest.requiredWowBox64Sha256)
                : wowbox64Version.equals(container.getExtra("box64Version"))
                        && wowbox64Dll.isFile();
        if (!wowbox64Current) {
            if (pinnedWowBox64) {
                try {
                    SharedBox64RuntimeState.invalidate(wowbox64Marker);
                } catch (IOException e) {
                    throw new IllegalStateException(
                            "Could not invalidate the pinned WOWBox64 marker", e);
                }
                installPinnedWowBox64Binary(
                        context,
                        wowbox64Version,
                        wowbox64Dll,
                        pinnedRequest.requiredWowBox64Sha256);
            }
            ContentProfile profile = contentsManager.getProfileByEntryName("wowbox64-" + wowbox64Version);
            boolean provisioned = pinnedWowBox64 || (profile != null
                    ? contentsManager.applyContent(profile)
                    : TarCompressorUtils.extract(TarCompressorUtils.Type.ZSTD,
                            environment.getContext(),
                            "wowbox64/wowbox64-" + wowbox64Version + ".tzst",
                            system32dir));
            if (!provisioned || !wowbox64Dll.isFile()) {
                throw new IllegalStateException("wowbox64 " + wowbox64Version
                        + " did not produce " + wowbox64Dll);
            }
            if (pinnedWowBox64) {
                try {
                    SharedBox64RuntimeState.publish(
                            wowbox64Marker,
                            pinnedRequest.markerId,
                            wowbox64Dll,
                            pinnedRequest.requiredWowBox64Sha256);
                } catch (IOException e) {
                    throw new IllegalStateException(
                            "Could not verify the pinned WOWBox64 runtime", e);
                }
            }
            container.putExtra("box64Version", wowbox64Version);
            containerDataChanged = true;
        }

        if (!fexcoreVersion.equals(container.getExtra("fexcoreVersion")) || !fexDll.isFile()) {
            ContentProfile profile = contentsManager.getProfileByEntryName("fexcore-" + fexcoreVersion);
            boolean provisioned = profile != null
                    ? contentsManager.applyContent(profile)
                    : TarCompressorUtils.extract(TarCompressorUtils.Type.ZSTD,
                            environment.getContext(),
                            "fexcore/fexcore-" + fexcoreVersion + ".tzst",
                            system32dir);
            if (!provisioned || !fexDll.isFile()) {
                throw new IllegalStateException("fexcore " + fexcoreVersion
                        + " did not produce " + fexDll);
            }
            container.putExtra("fexcoreVersion", fexcoreVersion);
            containerDataChanged = true;
        }
        if (containerDataChanged && !container.saveData()) {
            throw new IllegalStateException("Could not persist translator provisioning state");
        }
    }

    /** Extracts only the authenticated WEMU DLL, then publishes it atomically. */
    private void installPinnedWowBox64Binary(
            Context context,
            String version,
            File liveBinary,
            String requiredSha256) {
        File extractionDirectory = null;
        try {
            File stagingRoot = new File(context.getFilesDir(), "runtime/wowbox64/staging");
            if (!stagingRoot.isDirectory() && !stagingRoot.mkdirs()
                    && !stagingRoot.isDirectory()) {
                throw new IOException("Could not create the private WOWBox64 staging directory");
            }
            extractionDirectory = Files.createTempDirectory(
                    stagingRoot.toPath(), "bundled-").toFile();
            boolean extracted = TarCompressorUtils.extract(
                    TarCompressorUtils.Type.ZSTD,
                    context,
                    "wowbox64/wowbox64-" + version + ".tzst",
                    extractionDirectory);
            if (!extracted) {
                throw new IOException("Could not extract bundled WOWBox64 " + version);
            }
            File sourceBinary = new File(extractionDirectory, "wowbox64.dll");
            SharedBox64RuntimeState.installBinaryAtomically(
                    sourceBinary,
                    extractionDirectory,
                    liveBinary,
                    environment.getImageFs().getRootDir(),
                    requiredSha256);
        } catch (IOException e) {
            throw new IllegalStateException("Could not stage WOWBox64 " + version, e);
        } finally {
            if (extractionDirectory != null && !FileUtils.delete(extractionDirectory)) {
                Log.w("GuestProgramLauncherComponent",
                        "Could not remove private WOWBox64 staging directory: "
                                + extractionDirectory);
            }
        }
    }

    public GuestProgramLauncherComponent(ContentsManager contentsManager, ContentProfile wineProfile, Shortcut shortcut) {
        this.contentsManager = contentsManager;
        this.wineProfile = wineProfile;
        this.shortcut = shortcut;
    }

    @Override
    public void start() {
        synchronized (lock) {
            stopping = false;
            if (wineInfo.isArm64EC()) {
                if (!SharedBox64RuntimeState.isEmpty(
                        container.getExtra(SharedBox64RuntimeState.PINNED_RUNTIME_KEY))) {
                    // The ARM64EC path executes WOWBox64, but the requested
                    // WEMU Box64 build is still staged and authenticated too.
                    extractBox64Files();
                }
                extractEmulatorsDlls();
            } else {
                extractBox64Files();
            }
            pid = execGuestProgram();
            if (pid <= 0) {
                String target = guestArgv != null
                        ? java.util.Arrays.toString(guestArgv)
                        : guestExecutable;
                throw new IllegalStateException(
                        "Could not start the guest process (target=" + target + ")");
            }
            applyCpuAffinity();
            if (activeBox64RuntimeRequest != null
                    && activeBox64RuntimeRequest.managedSteamRuntime
                    && shutdownWinePrefix != null
                    && !shutdownWinePrefix.isEmpty()) {
                steamHealthMonitor = new OfficialSteamProcessHealthMonitor(shutdownWinePrefix);
                steamHealthMonitor.start();
            }
        }
    }

    /*
     * There was a `raiseGuestPriority()` here that put the guest tree at
     * THREAD_PRIORITY_DISPLAY. It is gone, and the reason is that it was never
     * measured. It arrived after the last build known to run games, it was
     * present in the build that stopped running them, and nothing about it was
     * ever confirmed to help — so it is exactly the kind of change that should
     * not survive an unexplained crash on the strength of sounding sensible.
     *
     * It is not obviously harmless either. Nice values are inherited, so this
     * raised every thread Wine went on to create, dozens of them, above the
     * app's own threads — including the one drawing the game to the screen.
     * Winning that race is not a win. Restore it only alongside a measurement
     * showing what it is worth.
     */

    /**
     * Pins the guest tree to the cores the container asked for.
     *
     * Read from an extra this app writes, not from the container's `cpuList`.
     * They hold the same kind of value and it is tempting to use the one that
     * already exists — but `cpuList` predates this by a long way and until
     * recently did nothing except tell Wine how many processors to report. A
     * container carrying a stale list from upstream the original would have had it
     * enforced for the first time on update, silently, and a game that stopped
     * running would have had no visible cause. That has already happened once
     * on this project with a different setting and is not worth repeating.
     *
     * So: only a value chosen in this app pins anything. Absent means the
     * kernel decides, which is what every container did before this existed.
     */
    private void applyCpuAffinity() {
        if (pid <= 0 || container == null) return;

        String list = container.getExtra("affinityCores");
        if (list == null || list.isEmpty()) return;

        int mask = 0;
        for (String part : list.split(",")) {
            try {
                int core = Integer.parseInt(part.trim());
                if (core >= 0 && core < 32) mask |= (1 << core);
            } catch (NumberFormatException ignored) {
                // A malformed list is a setting to ignore, not a launch to fail.
            }
        }

        /*
         * Never pin the whole guest to one core.
         *
         * "Performance cores" resolved to a single CPU on an 8 Gen 2 until
         * recently, and the list it resolves to is written into the container
         * at the moment it is chosen — so fixing the arithmetic did nothing for
         * a container that had already been saved with the bad answer in it.
         * That list outlives the bug that produced it, and one core running
         * box64, Wine, DXVK and a game is not slow, it is a hang.
         *
         * Refusing the mask here repairs those containers on the next launch
         * rather than waiting for the setting to be visited again, and costs
         * nothing otherwise: a deliberate one-core pin is not a thing anyone
         * wants. Two is the floor, not a recommendation.
         */
        if (Integer.bitCount(mask) < 2) {
            Log.w("GuestLauncher", "Ignoring core list \"" + list + "\": too few cores to run a guest on");
            return;
        }

        ProcessHelper.setProcessAffinity(pid, mask);
    }

    /*
     * There was a `checkDependencies()` here and it is gone.
     *
     * It ran `ldd` on `libXau.so` through `Runtime.exec`, read both of its
     * streams to the end and waited for it — synchronously, inside the lock
     * `start()` holds, on the launch path of every container — and then passed
     * the result to `Log.d` under the tag "CurlDeps" and returned it to a caller
     * that discarded it. Nothing branched on the answer. It was a process spawn
     * and a pair of blocking stream reads inserted at the exact moment a
     * container is starting and the CPU has the most to do.
     *
     * If the guest's libraries need auditing again, do it once behind the
     * developer settings rather than on every launch for everyone.
     */


    @Override
    public void stop() {
        int launcherPid;
        String[] stopCommand;
        String[] stopEnvironment;
        File stopWorkingDir;
        String winePrefix;
        OfficialSteamBootstrapSupervisor bootstrapSupervisor;
        Thread bootstrapThread;
        OfficialSteamProcessHealthMonitor healthMonitor;
        synchronized (lock) {
            stopping = true;
            launcherPid = pid;
            pid = -1;
            stopCommand = wineserverStopCommand;
            stopEnvironment = shutdownEnvVars;
            stopWorkingDir = shutdownWorkingDir;
            winePrefix = shutdownWinePrefix;
            wineserverStopCommand = null;
            shutdownEnvVars = null;
            shutdownWorkingDir = null;
            shutdownWinePrefix = null;
            bootstrapSupervisor = steamBootstrapSupervisor;
            bootstrapThread = steamBootstrapThread;
            steamBootstrapSupervisor = null;
            steamBootstrapThread = null;
            healthMonitor = steamHealthMonitor;
            steamHealthMonitor = null;
        }
        if (bootstrapSupervisor != null) bootstrapSupervisor.cancel();
        if (bootstrapThread != null) bootstrapThread.interrupt();
        if (healthMonitor != null) healthMonitor.stop();
        if (launcherPid != -1 && winePrefix != null && !winePrefix.isEmpty()) {
            byte[] expectedPrefix = ("WINEPREFIX=" + winePrefix)
                    .getBytes(StandardCharsets.UTF_8);
            File launcherProc = new File("/proc", Integer.toString(launcherPid));
            if (hasEnvironmentEntry(launcherProc, expectedPrefix)) {
                Process.killProcess(launcherPid);
            }
        }

        /*
         * Wine detaches Explorer, winhandler and the game from the Java process
         * which started them. Killing that one PID therefore leaves the real
         * guest behind (several leaked winhandlers retained hundreds of MB
         * each on a repeated launch test). Ask this Wine build's own wineserver
         * to stop this exact prefix instead. It is both narrower and more
         * complete than ProcessHelper's global /proc name sweep.
         */
        stopWinePrefix(stopCommand, stopEnvironment, stopWorkingDir, winePrefix);
    }

    private void stopWinePrefix(String[] command, String[] environment,
                                File workingDir, String winePrefix) {
        if (environment == null || winePrefix == null || winePrefix.isEmpty()) return;
        java.lang.Process shutdown = null;
        try {
            if (command == null || command.length == 0 || !new File(command[0]).isFile()) {
                throw new IOException("Wine shutdown command is unavailable");
            }
            ProcessBuilder builder = new ProcessBuilder(command);
            if (workingDir != null && workingDir.isDirectory()) builder.directory(workingDir);
            for (String item : environment) {
                int equals = item.indexOf('=');
                if (equals > 0) builder.environment().put(
                        item.substring(0, equals), item.substring(equals + 1));
            }
            File nullFile = new File("/dev/null");
            builder.redirectOutput(nullFile);
            builder.redirectError(nullFile);
            shutdown = builder.start();
            if (!shutdown.waitFor(WINESERVER_STOP_TIMEOUT_MS, TimeUnit.MILLISECONDS)) {
                Log.w("GuestLauncher", "Timed out stopping Wine prefix " + winePrefix);
                shutdown.destroyForcibly();
                shutdown.waitFor(WINESERVER_FORCE_WAIT_MS, TimeUnit.MILLISECONDS);
            } else if (shutdown.exitValue() != 0) {
                Log.w("GuestLauncher", "wineserver could not stop prefix " + winePrefix
                        + " (status " + shutdown.exitValue() + ")");
            }
        } catch (Exception error) {
            Log.w("GuestLauncher", "Could not stop detached Wine processes", error);
            if (shutdown != null) shutdown.destroyForcibly();
        } finally {
            /*
             * A failed Wine service can outlive its server. That is the exact
             * regression this shutdown repairs: winedevice/plugplay/winhandler
             * processes became children of init, then spun for minutes after
             * the activity was gone. Once Wine has had a graceful chance to
             * stop, reap only processes whose immutable launch environment
             * names this exact prefix. This cannot touch another container or
             * an Android process merely because its name happens to contain
             * "wine".
             */
            reapDetachedPrefixProcesses(winePrefix);
        }
    }

    private static void reapDetachedPrefixProcesses(String winePrefix) {
        File[] entries = new File("/proc").listFiles(file ->
                file.isDirectory() && file.getName().matches("[0-9]+"));
        if (entries == null) return;

        byte[] expected = ("WINEPREFIX=" + winePrefix).getBytes(StandardCharsets.UTF_8);
        int reaped = 0;
        for (File entry : entries) {
            int candidatePid;
            try {
                candidatePid = Integer.parseInt(entry.getName());
            } catch (NumberFormatException ignored) {
                continue;
            }
            if (candidatePid == Process.myPid() || !hasEnvironmentEntry(entry, expected)) continue;
            Process.killProcess(candidatePid);
            reaped++;
        }
        if (reaped > 0) {
            Log.w("GuestLauncher", "Reaped " + reaped
                    + " detached process(es) from Wine prefix " + winePrefix);
        }
    }

    private static boolean hasEnvironmentEntry(File procEntry, byte[] expected) {
        try {
            byte[] environment = Files.readAllBytes(new File(procEntry, "environ").toPath());
            return containsEnvironmentEntry(environment, expected);
        } catch (IOException | SecurityException ignored) {
            // The process exited, or it did not belong to this app.
        }
        return false;
    }

    private static boolean containsEnvironmentEntry(byte[] environment, byte[] expected) {
        for (int offset = 0; offset + expected.length <= environment.length; offset++) {
            if (offset > 0 && environment[offset - 1] != 0) continue;
            int index = 0;
            while (index < expected.length && environment[offset + index] == expected[index]) index++;
            if (index == expected.length &&
                    (offset + index == environment.length || environment[offset + index] == 0)) {
                return true;
            }
        }
        return false;
    }

    private static String mergePreloadValue(String baseValue, String overrideValue) {
        if (overrideValue == null || overrideValue.isEmpty()) {
            return baseValue == null ? "" : baseValue;
        }
        if (baseValue == null || baseValue.isEmpty()) {
            return overrideValue;
        }
        if (overrideValue.equals(baseValue)) {
            return baseValue;
        }
        return baseValue + ":" + overrideValue;
    }
    private static String appendFirstExistingPreload(String ldPreload, File[] candidates) {
        for (File candidate : candidates) {
            if (candidate.exists()) {
                return mergePreloadValue(ldPreload, candidate.getAbsolutePath());
            }
        }
        return ldPreload;
   }

    public Callback<Integer> getTerminationCallback() {
        return terminationCallback;
    }

    public void setTerminationCallback(Callback<Integer> terminationCallback) {
        this.terminationCallback = terminationCallback;
    }

    public String getGuestExecutable() {
        return guestExecutable;
    }

    public void setGuestExecutable(String guestExecutable) {
        this.guestExecutable = guestExecutable;
    }

    /**
     * The guest command as an argument list, skipping the string form entirely.
     *
     * `splitCommand` keeps the quote characters inside the token it produces —
     * `"game.exe"` arrives at the guest with the quotes still attached. Every
     * program launched through here has historically been `winhandler.exe`,
     * which re-parses its own command line and strips them, so nothing noticed.
     * Hand the same string to any other Windows program and the quotes are part
     * of the name.
     *
     * That cost an evening. `wine start /wait "wfm.exe"` returned 0 after 1.6
     * seconds having run nothing, because `start` reads a quoted first argument
     * as a window title and was left with no program at all.
     *
     * So a caller that is not talking to `winhandler.exe` sets this instead and
     * says exactly what each argument is. Takes precedence over
     * {@link #setGuestExecutable(String)}; the emulator prefix is still added
     * here, because that is this component's business rather than the caller's.
     */
    public void setGuestArgv(String[] guestArgv) {
        this.guestArgv = guestArgv;
    }

    /**
     * The directory the guest process starts in.
     *
     * Null keeps the image root, which is what every launch used when
     * `winhandler.exe /dir` was setting the working directory inside the guest
     * instead. Set it and Wine maps it onto a drive, so a bare program name
     * resolves beside its own data — which is what a game needs and what
     * looks like a corrupt install when it is missing.
     */
    public void setGuestWorkingDir(File guestWorkingDir) {
        this.guestWorkingDir = guestWorkingDir;
    }

    public String[] getBindingPaths() {
        return bindingPaths;
    }

    public void setBindingPaths(String[] bindingPaths) {
        this.bindingPaths = bindingPaths;
    }

    public EnvVars getEnvVars() {
        return envVars;
    }

    public void setEnvVars(EnvVars envVars) {
        this.envVars = envVars;
    }

    public String getBox64Preset() {
        return box64Preset;
    }

    public void setBox64Preset(String box64Preset) {
        this.box64Preset = box64Preset;
    }

    public void setFEXCorePreset (String fexcorePreset) { this.fexcorePreset = fexcorePreset; }

    private int execGuestProgram() {
        Context context = environment.getContext();
        ImageFs imageFs = environment.getImageFs();
        File rootDir = imageFs.getRootDir();

        if (wineInfo != null && wineInfo.isArm64EC()) {
            int fexConfigs = StardewCoreClrCompatibility.installFexAppConfigs(
                    rootDir, container.getRootDir());
            ProcessHelper.emitDiagnostic(
                    "Stardew FEX application policy ready before Wine startup: "
                            + fexConfigs + " file(s)");
        }

        SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(context);
        boolean enableBox64Logs = preferences.getBoolean("enable_box64_logs", false);
        boolean openWithAndroidBrowser = preferences.getBoolean("open_with_android_browser", false);
        boolean shareAndroidClipboard = preferences.getBoolean("share_android_clipboard", false);

        if (openWithAndroidBrowser)
            this.envVars.put("WINE_OPEN_WITH_ANDROID_BROWSER", "1");
        if (shareAndroidClipboard) {
            this.envVars.put("WINE_FROM_ANDROID_CLIPBOARD", "1");
            this.envVars.put("WINE_TO_ANDROID_CLIPBOARD", "1");
        }

        EnvVars execEnvVars = new EnvVars();

        addBox64EnvVars(execEnvVars, enableBox64Logs);
        execEnvVars.putAll(FEXCorePresetManager.getEnvVars(context, fexcorePreset));

        String renderer = GPUInformation.getRenderer(null, null);

        if (renderer.contains("Mali")) 
            execEnvVars.put("BOX64_MMAP32", "0");

        if (execEnvVars.get("BOX64_MMAP32").equals("1") && !wineInfo.isArm64EC()) {
            Log.d("GuestProgramLauncherComponent", "Disabling map memory placed");
            execEnvVars.put("WRAPPER_DISABLE_PLACED", "1");
        }

        execEnvVars.put("HOME", imageFs.home_path);
        execEnvVars.put("USER", ImageFs.USER);
        execEnvVars.put("TMPDIR", rootDir.getPath() + "/usr/tmp");
        execEnvVars.put("XDG_DATA_DIRS", rootDir.getPath() + "/usr/share");
        execEnvVars.put("LD_LIBRARY_PATH", rootDir.getPath() + "/usr/lib" + ":" + "/system/lib64");
        execEnvVars.put("XDG_CONFIG_DIRS", rootDir.getPath() + "/usr/etc/xdg");
        execEnvVars.put("GST_PLUGIN_PATH", rootDir.getPath() + "/usr/lib/gstreamer-1.0");
        execEnvVars.put("FONTCONFIG_PATH", rootDir.getPath() + "/usr/etc/fonts");
        execEnvVars.put("VK_LAYER_PATH", rootDir.getPath() + "/usr/share/vulkan/implicit_layer.d" + ":" + rootDir.getPath() + "/usr/share/vulkan/explicit_layer.d");
        execEnvVars.put("WRAPPER_LAYER_PATH", rootDir.getPath() + "/usr/lib");
        execEnvVars.put("WRAPPER_CACHE_PATH", rootDir.getPath() + "/usr/var/cache");
        execEnvVars.put("PREFIX", rootDir.getPath() + "/usr");
        execEnvVars.put("DISPLAY", ":0");
        execEnvVars.put("WINE_DISABLE_FULLSCREEN_HACK", "1");
        execEnvVars.put("GST_PLUGIN_FEATURE_RANK", "ximagesink:3000");
        execEnvVars.put("ALSA_CONFIG_PATH", rootDir.getPath() + "/usr/share/alsa/alsa.conf" + ":" + rootDir.getPath() + "/usr/etc/alsa/conf.d/android_aserver.conf");
        execEnvVars.put("ALSA_PLUGIN_DIR", rootDir.getPath() + "/usr/lib/alsa-lib");
        execEnvVars.put("OPENSSL_CONF", rootDir.getPath() + "/usr/etc/tls/openssl.cnf");
        execEnvVars.put("SSL_CERT_FILE", rootDir.getPath() + "/usr/etc/tls/cert.pem");
        execEnvVars.put("SSL_CERT_DIR", rootDir.getPath() + "/usr/etc/tls/certs");
        execEnvVars.put("WINE_X11FORCEGLX", "1");
        execEnvVars.put("WINE_GST_NO_GL", "1");
        /*
         * SteamGameId=0 is a compatibility default for ordinary Wine
         * programs.  Do not put it on Valve's official Steam.exe: Steam owns
         * the environment of the game it starts via -applaunch, and treating
         * the client itself as app 0 interferes with that bootstrap.
         */
        if (shortcut == null || !"1".equals(shortcut.getExtra("steamClientLaunch"))) {
            execEnvVars.put("SteamGameId", "0");
        }
        execEnvVars.put("PROTON_AUDIO_CONVERT", "0");
        execEnvVars.put("PROTON_VIDEO_CONVERT", "0");
        execEnvVars.put("PROTON_DEMUX", "0");

        String winePath = imageFs.getWinePath() + "/bin";

        Log.d("GuestProgramLauncherComponent", "WinePath is " + winePath);

        execEnvVars.put("PATH", winePath + ":" +
                rootDir.getPath() + "/usr/bin");

        execEnvVars.put("ANDROID_SYSVSHM_SERVER", rootDir.getPath() + UnixSocketConfig.SYSVSHM_SERVER_PATH);

        String primaryDNS = "8.8.4.4";
        ConnectivityManager connectivityManager = (ConnectivityManager) context.getSystemService(Service.CONNECTIVITY_SERVICE);
        if (connectivityManager.getActiveNetwork() != null) {
            ArrayList<InetAddress> dnsServers = new ArrayList<>(connectivityManager.getLinkProperties(connectivityManager.getActiveNetwork()).getDnsServers());
            primaryDNS = dnsServers.get(0).toString().substring(1);
        }
        execEnvVars.put("ANDROID_RESOLV_DNS", primaryDNS);
        execEnvVars.put("WINE_NEW_NDIS", "1");

        String ld_preload = "";

        if ((new File(imageFs.getLibDir(), "libandroid-sysvshm.so")).exists()){
            ld_preload = imageFs.getLibDir() + "/libandroid-sysvshm.so";
        }

        File fakeinputDest = new File(imageFs.getLibDir(), "libfakeinput.so");
        String nativeLibDir = environment.getContext().getApplicationInfo().nativeLibraryDir;
        File fakeinputSrc = new File(nativeLibDir, "libfakeinput.so");

        Log.d("GuestLauncher", "nativeLibDir: " + nativeLibDir);
        Log.d("GuestLauncher", "fakeinputSrc exists: " + fakeinputSrc.exists());
        Log.d("GuestLauncher", "fakeinputDest: " + fakeinputDest.getAbsolutePath());

        try {
            if (fakeinputSrc.exists()) {
                FileUtils.copy(fakeinputSrc, fakeinputDest);
                Log.d("GuestLauncher", "Copied libfakeinput.so to imagefs");
            } else {
                Log.e("GuestLauncher", "libfakeinput.so NOT FOUND in APK: " + fakeinputSrc.getAbsolutePath());
            }
        } catch (Exception e) {
            Log.e("GuestLauncher", "Failed to copy libfakeinput.so: " + e.getMessage());
            e.printStackTrace();
        }

        Log.d("GuestLauncher", "fakeinputDest exists after copy: " + fakeinputDest.exists());
        if (fakeinputDest.exists()) {
            if (!ld_preload.isEmpty()) ld_preload += ":";
            ld_preload += fakeinputDest.getAbsolutePath();
        }

        File[] jpegCandidates = new File[] {
            new File("/system/lib64/libjpeg.so"),
            new File("/system_ext/lib64/libjpeg.so"),
        };
        
        ld_preload = appendFirstExistingPreload(ld_preload, jpegCandidates);

        File[] cryptoCandidates = new File[] {
            new File("/system/lib64/libcrypto.so"),
            new File("/system_ext/lib64/libcrypto.so"),
            new File(imageFs.getLibDir(), "libcrypto.so.3"),
        };
        
        ld_preload = appendFirstExistingPreload(ld_preload, cryptoCandidates);

        File devInputDir = new File(imageFs.getRootDir(), "dev/input");
        devInputDir.mkdirs();
        File event0 = new File(devInputDir, "event0");
        if (!event0.exists()) {
                try { event0.createNewFile(); } catch (Exception e) {}
        }

        execEnvVars.put("FAKE_EVDEV_DIR", devInputDir.getAbsolutePath());
        execEnvVars.put("FAKE_EVDEV_VIBRATION", "1");

        Log.d("GuestLauncher", "Final LD_PRELOAD: " + ld_preload);
        execEnvVars.put("LD_PRELOAD", ld_preload);

        if (this.envVars.has("MANGOHUD")) {
            this.envVars.remove("MANGOHUD");
        }

        if (this.envVars.has("MANGOHUD_CONFIG")) {
            this.envVars.remove("MANGOHUD_CONFIG");
        }
        
        if (this.envVars != null) {
            execEnvVars.putAll(this.envVars);
        }

        if (shortcut != null
                && StardewCoreClrCompatibility.applyTo(execEnvVars, shortcut.path)) {
            ProcessHelper.emitDiagnostic(
                    "Stardew CoreCLR compatibility active: W^X=0 "
                            + "fex-tso=1 multiblock=0 smc=mtrack");
        }

        /*
         * Apply this after every preset and caller-supplied environment merge:
         * an official x86_64 Steam descriptor must be allowed to read its one
         * trusted compatibility profile. Ordinary games continue to receive
         * BOX64_NORCFILES=1, and ARM64EC Steam follows the separate FEX policy.
         */
        boolean officialSteamLaunch = shortcut != null
                && "1".equals(shortcut.getExtra("steamClientLaunch"));
        boolean softwareSteamCef = shortcut != null
                && "1".equals(shortcut.getExtra("steamClientSoftwareCef"));
        boolean managedOfficialSteamRuntime = activeBox64RuntimeRequest != null
                && activeBox64RuntimeRequest.managedSteamRuntime;
        if (managedOfficialSteamRuntime) {
            // The exact managed tuple was authenticated before the shared
            // translator was staged. Apply the genuine Windows-client policy
            // even when Steam is started from a bare desktop, its Run key, or
            // an in-prefix shortcut rather than Wemu's hidden descriptor.
            execEnvVars.put("PROTON_DISABLE_LSTEAMCLIENT", "1");
        }
        if (SteamClientRuntimePolicy.shouldUseOfficialSteamBox64Profile(
                officialSteamLaunch || managedOfficialSteamRuntime,
                wineInfo.isArm64EC())) {
            File runtimeDirectory = new File(context.getFilesDir(), "runtime/box64");
            final File steamProfile;
            try {
                steamProfile = SteamClientRuntimePolicy.ensureOfficialSteamBox64Profile(
                        runtimeDirectory,
                        softwareSteamCef || managedOfficialSteamRuntime);
            } catch (IOException e) {
                throw new IllegalStateException(
                        "Could not prepare the official Steam Box64 profile", e);
            }
            execEnvVars.remove("BOX64_NORCFILES");
            execEnvVars.put("BOX64_RCFILE", steamProfile.getAbsolutePath());
            ProcessHelper.emitDiagnostic(
                    "Managed Steam helper policy staged: strongmem=1 bigblock=3 "
                            + "purge=1 avx=0");
        }

        /*
         * The pinned Proton 10/11 archives carry a newer Wine prefix schema
         * than the prefix pack Wemu originally activated for them. Complete
         * that selected runtime's own update before any descriptor -- including
         * a bare desktop which can start Steam through its Run key -- is allowed
         * to run. Proton 9 and ordinary containers never enter this path.
         */
        ensureManagedSteamPrefixCurrent(execEnvVars, winePath, rootDir);

        String emulator = getEffectiveEmulator();

        String command = "";
        String overriddenCommand = execEnvVars.get("GUEST_PROGRAM_LAUNCHER_COMMAND");
        if (!overriddenCommand.isEmpty()) {
            String[] parts = overriddenCommand.split(";");
            for (String part : parts)
                command += part + " ";
            command = command.trim();
        }
        else {
            if (wineInfo.isArm64EC()) {
                command = winePath + "/" + guestExecutable;
                if (emulator.toLowerCase().equals("fexcore"))
                    execEnvVars.put("HODLL", "libwow64fex.dll");
                else
                    execEnvVars.put("HODLL", "wowbox64.dll");
            } else
                command = imageFs.getBinDir() + "/box64 " + guestExecutable;
        }

        File box64File = new File(rootDir, "/usr/bin/box64");
        if (box64File.exists()) {
            FileUtils.chmod(box64File, 0755);
        }

        if (guestArgv != null) {
            String[] argv = new String[guestArgv.length + 1];
            if (wineInfo.isArm64EC()) {
                argv[0] = winePath + "/" + guestArgv[0];
                execEnvVars.put("HODLL",
                        emulator.toLowerCase().equals("fexcore") ? "libwow64fex.dll" : "wowbox64.dll");
            } else {
                // box64 resolves the first argument on the PATH set above, which
                // is how the string form reaches `wine` too.
                argv[0] = imageFs.getBinDir() + "/box64";
                argv[1] = guestArgv[0];
            }
            int at = wineInfo.isArm64EC() ? 1 : 2;
            for (int i = 1; i < guestArgv.length; i++) argv[at++] = guestArgv[i];
            String[] finalArgv = wineInfo.isArm64EC()
                    ? java.util.Arrays.copyOf(argv, guestArgv.length)
                    : java.util.Arrays.copyOf(argv, guestArgv.length + 1);

            File workingDir = guestWorkingDir != null && guestWorkingDir.isDirectory()
                    ? guestWorkingDir : rootDir;
            rememberShutdownContext(winePath, execEnvVars, rootDir);
            Log.d("GuestLauncher", "argv=" + java.util.Arrays.toString(finalArgv)
                    + " cwd=" + workingDir);

            return execWithManagedSteamPrefixPolicies(
                    execEnvVars,
                    managedOfficialSteamRuntime,
                    () -> ProcessHelper.exec(
                            finalArgv,
                            execEnvVars.toStringArray(),
                            workingDir,
                            this::onLauncherProcessExited));
        }

        rememberShutdownContext(winePath, execEnvVars, rootDir);
        String finalCommand = command;
        return execWithManagedSteamPrefixPolicies(
                execEnvVars,
                managedOfficialSteamRuntime,
                () -> ProcessHelper.exec(
                        finalCommand,
                        execEnvVars.toStringArray(),
                        rootDir,
                        this::onLauncherProcessExited));
    }

    /**
     * Applies every direct registry policy while the exact prefix is inactive,
     * then keeps the same prefix lock through the process spawn. The launch is
     * inside the transaction so a second component cannot pass the inactivity
     * check before this child exists in {@code /proc}.
     */
    private int execWithManagedSteamPrefixPolicies(
            EnvVars execEnvVars,
            boolean managedOfficialSteamRuntime,
            IntSupplier launch) {
        if (!OfficialSteamWebHelperPolicy.shouldApply(
                managedOfficialSteamRuntime, wineInfo.isArm64EC())) {
            return launch.getAsInt();
        }

        String prefixValue = execEnvVars.get("WINEPREFIX");
        final File canonicalWinePrefix;
        try {
            File containerRoot = container == null ? null : container.getRootDir();
            canonicalWinePrefix = requireExactManagedWinePrefix(prefixValue, containerRoot);
        } catch (IOException invalidPrefix) {
            throw new IllegalStateException(
                    "The managed Steam prefix policy path is invalid; Steam was not started",
                    invalidPrefix);
        }

        Object prefixLock = STEAM_PREFIX_PREFLIGHT_LOCKS.computeIfAbsent(
                canonicalWinePrefix.getPath(), ignored -> new Object());
        synchronized (prefixLock) {
            try {
                if (hasRunningPrefixProcess(prefixValue)) {
                    throw new IOException(
                            "the Wine prefix was already active before its policies were applied");
                }
                OfficialSteamPrerequisiteStateRepair.Result prerequisiteRepair =
                        OfficialSteamPrerequisiteStateRepair.repair(
                                container.getRootDir(),
                                container.getExtra(
                                        OfficialSteamPrefixPreflight.MANAGED_RUNTIME_MARKER_KEY),
                                container.getExtra(
                                        OfficialSteamPrefixPreflight.SPEC_MARKER_KEY));
                if (prerequisiteRepair.changed()) {
                    Log.i("GuestLauncher", "Removed " + prerequisiteRepair.valuesRemoved
                            + " false Steam prerequisite completion value(s); "
                            + "Valve's signed installers will run");
                }
                OfficialSteamWebHelperPolicy.ensureWineGStreamerDisabled(canonicalWinePrefix);
                ProcessHelper.emitDiagnostic(
                        "Managed Steam helper media policy staged: "
                                + "winegstreamer=disabled (per-app)");
                return launch.getAsInt();
            } catch (IOException failure) {
                throw new IllegalStateException(
                        "The managed Steam prefix policies could not be applied; Steam was not started",
                        failure);
            }
        }
    }

    /** Resolves only the canonical {@code .wine} owned by this exact container. */
    static File requireExactManagedWinePrefix(String prefixValue, File containerRoot)
            throws IOException {
        if (prefixValue == null || prefixValue.isEmpty()) {
            throw new IOException("WINEPREFIX is unavailable");
        }
        if (containerRoot == null) {
            throw new IOException("the managed Steam container root is unavailable");
        }

        File winePrefix = new File(prefixValue);
        File canonicalWinePrefix = winePrefix.getCanonicalFile();
        if (!winePrefix.getAbsoluteFile().toPath().normalize().equals(
                canonicalWinePrefix.toPath().toAbsolutePath().normalize())) {
            throw new IOException("WINEPREFIX is not canonical: " + winePrefix);
        }

        File suppliedContainerPrefix = new File(containerRoot, ".wine");
        File canonicalContainerPrefix = suppliedContainerPrefix.getCanonicalFile();
        if (!suppliedContainerPrefix.getAbsoluteFile().toPath().normalize().equals(
                canonicalContainerPrefix.toPath().toAbsolutePath().normalize())) {
            throw new IOException("the managed Steam container prefix is not canonical");
        }
        if (!canonicalWinePrefix.equals(canonicalContainerPrefix)) {
            throw new IOException("WINEPREFIX does not identify the managed Steam container prefix");
        }
        return canonicalWinePrefix;
    }

    private void ensureManagedSteamPrefixCurrent(
            EnvVars execEnvVars,
            String wineBinDirectory,
            File workingDirectory) {
        String specMarker = container.getExtra(OfficialSteamPrefixPreflight.SPEC_MARKER_KEY);
        SharedBox64RuntimeState.RuntimeRequest box64Request = activeBox64RuntimeRequest;
        final String requiredRevision;
        try {
            requiredRevision = OfficialSteamPrefixPreflight.requiredRevision(
                    container.getExtra(OfficialSteamPrefixPreflight.MANAGED_RUNTIME_MARKER_KEY),
                    specMarker,
                    container.getExtra(
                            SharedBox64RuntimeState.MANAGED_STEAM_CONTAINER_REVISION_KEY),
                    container.getWineVersion(),
                    box64Request == null ? null : box64Request.version,
                    box64Request == null ? null : box64Request.markerId,
                    box64Request == null ? null : box64Request.requiredSha256);
        } catch (IllegalArgumentException invalidRuntime) {
            throw new IllegalStateException(
                    "The managed " + specMarker
                            + " Steam prefix has an unsupported runtime pairing; Steam was not started",
                    invalidRuntime);
        }
        if (requiredRevision == null) return;

        String prefixValue = execEnvVars.get("WINEPREFIX");
        File winePrefix = new File(prefixValue);
        final String prefixLockKey;
        final File canonicalWinePrefix;
        try {
            canonicalWinePrefix = winePrefix.getCanonicalFile();
            if (!winePrefix.getAbsoluteFile().toPath().normalize().equals(
                    canonicalWinePrefix.toPath().toAbsolutePath().normalize())) {
                throw new IOException("WINEPREFIX is not canonical: " + winePrefix);
            }
            prefixLockKey = canonicalWinePrefix.getPath();
        } catch (IOException invalidPrefix) {
            throw new IllegalStateException(
                    "The managed " + specMarker
                            + " Steam prefix path is invalid; Steam was not started",
                    invalidPrefix);
        }

        Object prefixLock = STEAM_PREFIX_PREFLIGHT_LOCKS.computeIfAbsent(
                prefixLockKey, ignored -> new Object());
        synchronized (prefixLock) {
            // Box64 lives in the shared imagefs. Hold its publication lock from
            // validation through both helpers so another container cannot swap
            // the executable halfway through this prefix transaction.
            synchronized (BOX64_EXTRACTION_LOCK) {
                ensureManagedSteamPrefixCurrentLocked(
                        execEnvVars,
                        wineBinDirectory,
                        workingDirectory,
                        canonicalWinePrefix,
                        prefixValue,
                        specMarker,
                        requiredRevision,
                        box64Request);
            }
        }
    }

    private void ensureManagedSteamPrefixCurrentLocked(
            EnvVars execEnvVars,
            String wineBinDirectory,
            File workingDirectory,
            File winePrefix,
            String prefixValue,
            String specMarker,
            String requiredRevision,
            SharedBox64RuntimeState.RuntimeRequest box64Request) {
        File box64 = new File(environment.getImageFs().getBinDir(), "box64");
        File box64Marker = SharedBox64RuntimeState.markerFile(
                new File(environment.getContext().getFilesDir(), "runtime/box64"));
        if (box64Request == null
                || box64Request.allowInstalledProfile
                || box64Request.requiredSha256 == null
                || !SharedBox64RuntimeState.isCurrent(
                        box64,
                        box64Marker,
                        box64Request.markerId,
                        box64Request.requiredSha256)) {
            throw new IllegalStateException(
                    "The managed " + specMarker
                            + " Box64 runtime is not the validated pinned pairing; Steam was not started");
        }

        try {
            if (OfficialSteamPrefixPreflight.isCurrent(
                    winePrefix,
                    container.getExtra(OfficialSteamPrefixPreflight.CONTAINER_REVISION_KEY),
                    requiredRevision)) {
                Log.d("GuestLauncher",
                        "Managed Steam prefix preflight is current for " + specMarker);
                return;
            }
        } catch (IOException invalidMarker) {
            throw new IllegalStateException(
                    "The managed " + specMarker
                            + " Steam prefix preflight marker is invalid; Steam was not started",
                    invalidMarker);
        }

        if (wineInfo.isArm64EC()) {
            throw new IllegalStateException(
                    "Managed " + specMarker
                            + " requires its selected x86_64 Wine runtime; Steam was not started");
        }

        /*
         * This check is deliberately outside the failed-preflight cleanup
         * scope below. A process that was already using the prefix belongs to
         * another live session; refusing the update must never turn into
         * wineserver -k against that session.
         */
        try {
            if (hasRunningPrefixProcess(prefixValue)) {
                throw new IOException(
                        "the Wine prefix was already active before its required update");
            }
        } catch (IOException unsafeActiveState) {
            throw new IllegalStateException(
                    "Managed " + specMarker
                            + " Steam prefix could not be proven inactive before repair; "
                            + "Steam was not started",
                    unsafeActiveState);
        }

        File wineLoader = new File(wineBinDirectory, "wine");
        File wineserver = new File(wineBinDirectory, "wineserver");
        EnvVars preflightEnvVars = new EnvVars();
        preflightEnvVars.putAll(execEnvVars);
        preflightEnvVars.put(
                "WINEDLLOVERRIDES",
                OfficialSteamPrefixPreflight.preflightDllOverrides(
                        execEnvVars.get("WINEDLLOVERRIDES")));
        String[] environment = preflightEnvVars.toStringArray();
        String step = "wine wineboot.exe -u -r";
        try {
            requirePreflightExecutable(box64, "Box64");
            requirePreflightExecutable(wineLoader, "Wine loader");
            requirePreflightExecutable(wineserver, "wineserver");

            Log.i("GuestLauncher",
                    "Updating managed Steam prefix with " + specMarker
                            + " before guest launch");
            int winebootStatus = ProcessHelper.execAndWait(
                    OfficialSteamPrefixPreflight.winebootCommand(box64, wineLoader),
                    environment,
                    workingDirectory,
                    STEAM_PREFIX_WINEBOOT_TIMEOUT_SECONDS,
                    TimeUnit.SECONDS);
            if (winebootStatus != 0) {
                throw new IOException(
                        "wine wineboot.exe -u -r exited with status " + winebootStatus);
            }

            step = "wineserver -w";
            int wineserverStatus = ProcessHelper.execAndWait(
                    new String[]{box64.getAbsolutePath(), wineserver.getAbsolutePath(), "-w"},
                    environment,
                    workingDirectory,
                    STEAM_PREFIX_SERVER_WAIT_TIMEOUT_SECONDS,
                    TimeUnit.SECONDS);
            if (wineserverStatus != 0) {
                throw new IOException(
                        "wineserver -w exited with status " + wineserverStatus);
            }
            if (hasRunningPrefixProcess(prefixValue)) {
                throw new IOException(
                        "the Wine prefix remained active after wineserver -w");
            }

            step = "preflight marker publication";
            OfficialSteamPrefixPreflight.publishPrefixRevision(
                    winePrefix, requiredRevision);
            container.putExtra(
                    OfficialSteamPrefixPreflight.CONTAINER_REVISION_KEY,
                    requiredRevision);
            if (!container.saveData()) {
                throw new IOException("the completed prefix preflight state could not be saved");
            }
            Log.i("GuestLauncher",
                    "Managed Steam prefix preflight completed for " + specMarker);
        } catch (IOException | InterruptedException | TimeoutException failure) {
            cleanupFailedSteamPrefixPreflight(
                    box64, wineserver, environment, workingDirectory, prefixValue);
            if (failure instanceof InterruptedException) Thread.currentThread().interrupt();
            throw new IllegalStateException(
                    "Managed " + specMarker + " Steam prefix repair failed during " + step
                            + "; Steam was not started",
                    failure);
        }
    }

    private static void requirePreflightExecutable(File file, String label) throws IOException {
        if (!file.isFile()) {
            throw new IOException(label + " is missing from the selected runtime: " + file);
        }
    }

    private static boolean hasRunningPrefixProcess(String winePrefix) throws IOException {
        return hasRunningPrefixProcess(new File("/proc"), winePrefix);
    }

    /** Package-visible seam for exact-prefix/fail-closed process-scan tests. */
    static boolean hasRunningPrefixProcess(File procRoot, String winePrefix) throws IOException {
        File[] entries = procRoot.listFiles(file ->
                file.isDirectory() && file.getName().matches("[0-9]+"));
        if (entries == null) {
            throw new IOException("Could not verify that the Wine prefix became inactive");
        }
        byte[] expected = ("WINEPREFIX=" + winePrefix).getBytes(StandardCharsets.UTF_8);
        boolean inspectedAnyEnvironment = false;
        for (File entry : entries) {
            try {
                byte[] environment = Files.readAllBytes(new File(entry, "environ").toPath());
                inspectedAnyEnvironment = true;
                if (containsEnvironmentEntry(environment, expected)) return true;
            } catch (IOException | SecurityException ignored) {
                // The process exited, or belongs to another Android UID. At
                // least this app's own environment must remain inspectable for
                // the result below to be a trustworthy negative.
            }
        }
        if (!inspectedAnyEnvironment) {
            throw new IOException("Could not inspect any process while checking the Wine prefix");
        }
        return false;
    }

    /** Best-effort, exact-prefix cleanup after a failed or timed-out barrier. */
    private static void cleanupFailedSteamPrefixPreflight(
            File box64,
            File wineserver,
            String[] environment,
            File workingDirectory,
            String winePrefix) {
        try {
            if (!box64.isFile() || !wineserver.isFile()) return;
            int status = ProcessHelper.execAndWait(
                    new String[]{
                            box64.getAbsolutePath(), wineserver.getAbsolutePath(), "-k", "-w"
                    },
                    environment,
                    workingDirectory,
                    STEAM_PREFIX_CLEANUP_TIMEOUT_SECONDS,
                    TimeUnit.SECONDS);
            if (status != 0) {
                Log.w("GuestLauncher",
                        "Could not clean failed Steam prefix preflight " + winePrefix
                                + " (status " + status + ")");
            }
        } catch (Exception cleanupFailure) {
            Log.w("GuestLauncher",
                    "Could not clean failed Steam prefix preflight " + winePrefix,
                    cleanupFailure);
            if (cleanupFailure instanceof InterruptedException) {
                Thread.currentThread().interrupt();
            }
        } finally {
            // The bounded waiter can only kill its Box64 parent. Reap any Wine
            // children it left behind, but only when their immutable process
            // environment names this exact canonical prefix.
            reapDetachedPrefixProcesses(winePrefix);
        }
    }

    /**
     * Steam replaces its initial process while bootstrapping. Ordinary games
     * still use that process as their lifetime; an official Steam descriptor
     * waits for non-resident work in this exact Wine prefix instead. That work
     * includes Steam, prerequisite installers, and the game Steam launches.
     */
    private void onLauncherProcessExited(int status) {
        String winePrefix;
        OfficialSteamBootstrapSupervisor supervisor;
        Callback<Integer> directCallback = null;
        synchronized (lock) {
            pid = -1;
            if (stopping) return;
            if (shortcut == null || !"1".equals(shortcut.getExtra("steamClientLaunch"))) {
                directCallback = terminationCallback;
            } else {
                winePrefix = shutdownWinePrefix;
                if (winePrefix == null || winePrefix.isEmpty()) {
                    directCallback = terminationCallback;
                } else {
                    if (steamBootstrapSupervisor != null) return;
                    supervisor = new OfficialSteamBootstrapSupervisor();
                    supervisor.onLauncherExit(status, monotonicMs());
                    steamBootstrapSupervisor = supervisor;
                    Thread thread = new Thread(
                            () -> superviseOfficialSteam(winePrefix, supervisor),
                            "OfficialSteam-Supervisor");
                    thread.setDaemon(true);
                    steamBootstrapThread = thread;
                    thread.start();
                    Log.i("GuestLauncher",
                            "Steam bootstrap exited; supervising its prefix hand-off");
                    return;
                }
            }
        }
        if (directCallback != null) directCallback.call(status);
    }

    private void superviseOfficialSteam(
            String winePrefix,
            OfficialSteamBootstrapSupervisor supervisor) {
        try {
            while (!Thread.currentThread().isInterrupted()) {
                OfficialSteamBootstrapSupervisor.Decision decision =
                        supervisor.onPrefixSnapshot(
                                monotonicMs(), scanOfficialSteamProcesses(winePrefix));
                if (decision.shouldFinish) {
                    Callback<Integer> callback;
                    synchronized (lock) {
                        if (stopping || steamBootstrapSupervisor != supervisor) return;
                        steamBootstrapSupervisor = null;
                        steamBootstrapThread = null;
                        callback = terminationCallback;
                    }
                    Log.i("GuestLauncher",
                            "Official Steam session activity left the Wine prefix");
                    if (callback != null) callback.call(decision.exitStatus);
                    return;
                }
                Thread.sleep(STEAM_SUPERVISOR_POLL_MS);
            }
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
    }

    private static OfficialSteamBootstrapSupervisor.PrefixSnapshot
            scanOfficialSteamProcesses(String winePrefix) {
        File[] entries = new File("/proc").listFiles(file ->
                file.isDirectory() && file.getName().matches("[0-9]+"));
        if (entries == null) {
            return OfficialSteamBootstrapSupervisor.PrefixSnapshot.UNAVAILABLE;
        }

        byte[] expectedPrefix = ("WINEPREFIX=" + winePrefix).getBytes(StandardCharsets.UTF_8);
        boolean readAnyEnvironment = false;
        for (File entry : entries) {
            byte[] environment;
            try {
                environment = Files.readAllBytes(new File(entry, "environ").toPath());
                readAnyEnvironment = true;
            } catch (IOException | SecurityException ignored) {
                continue;
            }
            if (!containsEnvironmentEntry(environment, expectedPrefix)) continue;

            final String commandLine;
            try {
                commandLine = new String(
                        Files.readAllBytes(new File(entry, "cmdline").toPath()),
                        StandardCharsets.UTF_8).toLowerCase(Locale.ROOT);
            } catch (IOException | SecurityException ignored) {
                // A matching process existed but could not be classified. It
                // may be Steam, so this scan cannot honestly claim emptiness.
                return OfficialSteamBootstrapSupervisor.PrefixSnapshot.UNAVAILABLE;
            }
            OfficialSteamBootstrapSupervisor.ProcessKind processKind =
                    OfficialSteamBootstrapSupervisor.classifyPrefixProcess(commandLine);
            if (processKind == OfficialSteamBootstrapSupervisor.ProcessKind.UNAVAILABLE) {
                return OfficialSteamBootstrapSupervisor.PrefixSnapshot.UNAVAILABLE;
            }
            if (processKind == OfficialSteamBootstrapSupervisor.ProcessKind.SESSION_ACTIVITY) {
                return OfficialSteamBootstrapSupervisor.PrefixSnapshot.SESSION_ACTIVITY;
            }
        }
        return readAnyEnvironment
                ? OfficialSteamBootstrapSupervisor.PrefixSnapshot.INACTIVE
                : OfficialSteamBootstrapSupervisor.PrefixSnapshot.UNAVAILABLE;
    }

    static boolean isOfficialSteamProcess(String commandLine) {
        return OfficialSteamBootstrapSupervisor.classifyPrefixProcess(commandLine)
                == OfficialSteamBootstrapSupervisor.ProcessKind.SESSION_ACTIVITY;
    }

    private static long monotonicMs() {
        return System.nanoTime() / 1_000_000L;
    }

    private void rememberShutdownContext(String wineBinDir, EnvVars execEnvVars, File rootDir) {
        String server = wineBinDir + "/wineserver";
        wineserverStopCommand = wineInfo != null && wineInfo.isArm64EC()
                ? new String[]{server, "-k", "-w"}
                : new String[]{environment.getImageFs().getBinDir() + "/box64",
                        server, "-k", "-w"};
        shutdownEnvVars = execEnvVars.toStringArray();
        shutdownWorkingDir = rootDir;
        shutdownWinePrefix = execEnvVars.get("WINEPREFIX");
    }

    private void addBox64EnvVars(EnvVars envVars, boolean enableLogs) {
        envVars.put("BOX64_NOBANNER", ProcessHelper.isPrintDebug() && enableLogs ? "0" : "1");
        envVars.put("BOX64_DYNAREC", "1");

        /*
         * AVX on by default, before the preset gets a say.
         *
         * Box64 leaves AVX off unless asked, and this fork never asked — so
         * every AVX path in a modern game fell back to whatever the recompiler
         * could manage without it. Set here rather than in the presets so it is
         * the baseline: a preset that wants it off, or wants AVX2, still says so
         * below and wins.
         */
        envVars.put("BOX64_AVX", "1");

        if (enableLogs) {
            envVars.put("BOX64_LOG", "1");
            envVars.put("BOX64_DYNAREC_MISSING", "1");
        }

        envVars.putAll(Box64PresetManager.getEnvVars("box64", environment.getContext(), box64Preset));
        envVars.put("BOX64_X11GLX", "1");
        envVars.put("BOX64_NORCFILES", "1");
    }

    public void suspendProcess() {
        synchronized (lock) {
            if (pid != -1) ProcessHelper.suspendProcess(pid);
        }
    }

    public void resumeProcess() {
        synchronized (lock) {
            if (pid != -1) ProcessHelper.resumeProcess(pid);
        }
    }
}
