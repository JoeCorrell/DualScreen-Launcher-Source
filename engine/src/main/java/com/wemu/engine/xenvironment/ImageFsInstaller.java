package com.wemu.engine.xenvironment;

import android.content.Context;
import android.util.Log;

import com.wemu.engine.R;
import com.wemu.engine.container.Container;
import com.wemu.engine.container.ContainerManager;
import com.wemu.engine.contents.AdrenotoolsManager;
import com.wemu.engine.contents.GuestPayloads;
import com.wemu.engine.contents.PayloadUnavailableException;
import com.wemu.engine.core.FileUtils;
import com.wemu.engine.core.OnExtractFileListener;
import com.wemu.engine.core.TarCompressorUtils;
import com.wemu.engine.core.WemuPaths;

import java.io.File;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Unpacks the container root filesystem, Wine and the bundled GPU drivers.
 *
 * This runs once on a fresh install and again whenever [LATEST_VERSION] moves.
 * It is the longest single operation in the app — several minutes and about a
 * gigabyte of extraction — so how it reports progress matters.
 *
 * The original reported it by holding a `MainActivity` and a `DownloadProgressDialog`
 * and calling `runOnUiThread` from inside the extraction loop. That tied the
 * installer to one screen, made it impossible to run without that screen on
 * top, and meant the work could not survive the activity being recreated. It
 * takes a [Listener] instead: `:app` renders the numbers in Compose, on
 * whichever panel makes sense, and decides for itself what thread to observe on.
 */
public abstract class ImageFsInstaller {

    private static final String TAG = "ImageFsInstaller";

    public static final byte LATEST_VERSION = 22;

    /**
     * Assumed compression ratio, used to turn bytes-written into a percentage.
     *
     * The archives are zstd level 22 and their uncompressed size is not recorded
     * anywhere the app can cheaply read, so the total is estimated from the
     * packed size. It is an estimate: the bar can reach 100% slightly early or
     * late, which is why the stage is shown beside it.
     */
    private static final float COMPRESSION_RATIO = 22f;

    /** The device's own Vulkan driver: a choice, not an archive. */
    private static final String SYSTEM_DRIVER = "System";

    /**
     * Archives fetched during the install so that no container ever waits on one.
     *
     * These are read when a container is *created*, not when the app is
     * installed, so nothing here forced them to be downloaded up front — and
     * that is exactly what went wrong. A container was built while GitHub was
     * answering 503, got no Vulkan wrapper, and died four seconds after opening
     * with a message about an importable driver not existing.
     *
     * Pulling them now moves the only network the container path needs into the
     * one place the user already expects to wait, and `GuestPayloads` keeps
     * every `.tzst` after unpacking, so creating a container stays an offline
     * operation forever after.
     *
     * The two DXVK entries are the container defaults. A container built with
     * the defaults is the common case and should not be the one that needs a
     * download.
     */
    private static final String[] PREFETCHED = {
            "container_pattern_common.tzst",
            "proton-9.0-arm64ec_container_pattern.tzst",
            "graphics_driver/wrapper.tzst",
            "graphics_driver/extra_libs.tzst",
            "dxwrapper/dxvk-2.3.1.tzst",
            "dxwrapper/vkd3d-2.8.tzst",
            // Not a version choice -- ContainerBootstrap extracts it alongside
            // whichever DXVK is selected, so every container needs it.
            "dxwrapper/d8vk-1.0.tzst",
    };

    /*
     * The list is the answer to "is everything downloaded by the time the
     * install finishes", and it has to stay that way.
     *
     * Anything read at container-creation time and not named above turns
     * creating a container into a network operation, which is how the graphics
     * wrapper produced a container that opened and died. The two patterns were
     * missed on the first pass for exactly that reason: they are unpacked from
     * ContainerManager and ContainerBootstrap rather than from here, so nothing
     * in this file mentioned them.
     *
     * If a new archive is unbundled, the question to ask is not "can it be
     * fetched" but "who fetches it, and are they somewhere the user is
     * expecting to wait".
     */

    /** Which part of the install is running. */
    public enum Stage {
        DOWNLOADING_SYSTEM_FILES(R.string.downloading_system_files),
        DOWNLOADING_WINE_FILES(R.string.downloading_wine_files),
        DOWNLOADING_CONTAINER_FILES(R.string.downloading_container_files),
        SYSTEM_FILES(R.string.installing_system_files),
        WINE_FILES(R.string.installing_wine_files),
        GRAPHICS_DRIVERS(R.string.installing_wine_files);

        public final int labelResId;

        Stage(int labelResId) {
            this.labelResId = labelResId;
        }
    }

    /**
     * Told how the install is going.
     *
     * Called from the extraction thread, not the main one. A listener that
     * touches the UI has to marshal — which is the caller's decision to make,
     * and on a two-screen device it is not always the obvious one.
     */
    public interface Listener {
        void onStage(Stage stage);

        /** 0..100, and permitted to go backwards when a new stage begins. */
        void onProgress(int percent);
    }

    /** Kept for callers that only want to know when it is over. */
    public interface onInstallationFinish {
        void call();
    }

    private static void resetContainerImgVersions(Context context) {
        ContainerManager manager = new ContainerManager(context);
        for (Container container : manager.getContainers()) {
            container.putExtra("imgVersion", null);
            container.saveData();
        }
    }

    public static boolean installWineArchive(final Context context, String version, File archiveFile) {
        File rootDir = ImageFs.find(context).getRootDir();
        File outFile = new File(rootDir, "opt/" + version);
        FileUtils.delete(outFile);
        outFile.mkdirs();
        boolean success = TarCompressorUtils.extract(TarCompressorUtils.Type.ZSTD, archiveFile, outFile);
        if (!success) success = TarCompressorUtils.extract(TarCompressorUtils.Type.XZ, archiveFile, outFile);
        if (!success) FileUtils.delete(outFile);
        return success;
    }

    /**
     * Unpacks every bundled Wine build.
     *
     * @return false if any of them failed, which the caller must treat as a
     *   failed install. It used to return nothing at all: a Wine build that did
     *   not extract left `install` reporting success, the image marked valid, and
     *   the failure surfacing much later as a container that starts and exits —
     *   with nothing on screen or in the log connecting the two.
     */
    private static boolean installWineFromAssets(final Context context, final Listener listener)
            throws PayloadUnavailableException {
        String[] versions = context.getResources().getStringArray(R.array.wine_entries);
        File rootDir = ImageFs.find(context).getRootDir();
        boolean allExtracted = true;

        notifyStage(listener, Stage.WINE_FILES);

        for (String version : versions) {
            File outFile = new File(rootDir, "opt/" + version);
            outFile.mkdirs();

            if (!extractPayload(context, listener, version + ".tar.zst", outFile, Stage.WINE_FILES)) {
                Log.e(TAG, "Could not install " + version + ".tar.zst");
                allExtracted = false;
            }
        }
        return allExtracted;
    }

    /**
     * Downloads what a container will need, before it needs it.
     *
     * Fetched but deliberately not unpacked: where these go depends on the
     * container being built, and `GuestPayloads` keeps a `.tzst` on disk after
     * use, so the extraction at container-creation time finds them already here
     * and touches no network.
     *
     * A payload already in the APK is skipped, so re-bundling one costs nothing
     * but a check.
     *
     * @return false if one could not be had, which fails the install — the
     *   alternative is an install that reports success and a container that dies
     *   four seconds after opening, which is the failure this exists to prevent.
     */
    private static boolean prefetchContainerPayloads(Context context, Listener listener)
            throws PayloadUnavailableException {
        notifyStage(listener, Stage.DOWNLOADING_CONTAINER_FILES);

        for (int i = 0; i < PREFETCHED.length; i++) {
            String assetName = PREFETCHED[i];
            if (GuestPayloads.isBundled(context, assetName)) continue;

            final int index = i;
            File payload = GuestPayloads.fetch(context, assetName, percent ->
                    // One bar across the set, so four small downloads do not look
                    // like four separate stalls.
                    reportProgress(listener, index * 100 + percent, PREFETCHED.length * 100));
            if (payload == null) {
                throw new PayloadUnavailableException(assetName + ": " + GuestPayloads.lastFailure());
            }
        }
        return true;
    }

    /**
     * Unpacks one of the large payloads, from the APK or from the network.
     *
     * A payload inside `assets` is streamed straight out of it, which is what
     * every install did before any of this existed. One that is not is fetched
     * to disk first and unpacked from there, then dropped — see
     * {@link GuestPayloads}.
     *
     * The progress the listener sees runs twice for a downloaded payload: once
     * to 100 while it arrives and again while it unpacks. That is honest about
     * what is happening and is better than a single bar that appears to stall
     * for two minutes at whatever fraction the download occupies.
     */
    private static boolean extractPayload(Context context, Listener listener,
                                          String assetName, File destination, Stage owningStage)
            throws PayloadUnavailableException {
        final long contentLength =
                (long) (FileUtils.getSize(context, assetName) * (100.0f / COMPRESSION_RATIO));
        AtomicLong totalSizeRef = new AtomicLong();
        OnExtractFileListener onExtract = (file, size) -> {
            if (size > 0) reportProgress(listener, totalSizeRef.addAndGet(size), contentLength);
            return file;
        };

        if (GuestPayloads.isBundled(context, assetName)) {
            return TarCompressorUtils.extract(
                    TarCompressorUtils.Type.ZSTD, context, assetName, destination, onExtract);
        }

        if (!GuestPayloads.isKnown(assetName)) {
            Log.e(TAG, assetName + " is neither bundled nor downloadable");
            return false;
        }

        /*
         * Named for what is coming down, not just "downloading".
         *
         * There are two payloads and each passes through here, so a single
         * shared label showed "Downloading the Windows runtime…", then a
         * progress bar that finished, then the identical line again — which
         * reads as the install having looped rather than having moved on to
         * Wine.
         */
        notifyStage(listener, owningStage == Stage.WINE_FILES
                ? Stage.DOWNLOADING_WINE_FILES
                : Stage.DOWNLOADING_SYSTEM_FILES);
        File payload = GuestPayloads.fetch(context, assetName,
                percent -> reportProgress(listener, percent, 100));
        notifyStage(listener, owningStage);
        if (payload == null) {
            /*
             * Thrown rather than returned false, so the reason survives.
             *
             * `install` reports a boolean, and the boot screen's message for a
             * false is "check free storage and try again" — which is the wrong
             * thing to tell someone whose wifi is off, and once these payloads
             * are downloaded rather than bundled that is the likeliest way a
             * first install fails.
             */
            throw new PayloadUnavailableException(assetName + ": " + GuestPayloads.lastFailure());
        }

        // The size of the archive on disk, since there is no asset to measure.
        final long downloadedLength = (long) (payload.length() * (100.0f / COMPRESSION_RATIO));
        AtomicLong extracted = new AtomicLong();
        boolean ok = TarCompressorUtils.extract(
                TarCompressorUtils.Type.ZSTD, payload, destination,
                (file, size) -> {
                    if (size > 0) reportProgress(listener, extracted.addAndGet(size), downloadedLength);
                    return file;
                });

        if (ok) GuestPayloads.release(payload);
        return ok;
    }

    /**
     * Unpacks the bundled GPU drivers.
     *
     * Only the ones that are genuinely assets are required. The same resource
     * array names the driver *choices* a container can be given, and the first
     * of those is "System" — the device's own driver, which has nothing to
     * unpack. Requiring every entry to extract would fail the install on the one
     * entry that was never a file.
     *
     * @return false if a driver that does ship in the APK failed to extract.
     */
    private static boolean installDriversFromAssets(final Context context, final Listener listener) {
        notifyStage(listener, Stage.GRAPHICS_DRIVERS);

        AdrenotoolsManager adrenotoolsManager = new AdrenotoolsManager(context);
        String[] drivers = context.getResources()
                .getStringArray(R.array.wrapper_graphics_driver_version_entries);
        boolean allExtracted = true;

        for (String driver : drivers) {
            /*
             * "System" is the device's own driver and has no archive anywhere.
             *
             * This used to be filtered by `isFromResources`, which answered the
             * question by opening the asset — so the entry that was never a file
             * fell out for free. That test now consults the resource array,
             * which "System" is in, so it has to be named here instead. Getting
             * this wrong fails the whole install on a 404 for
             * `adrenotools-System.tzst`.
             */
            if (SYSTEM_DRIVER.equals(driver)) continue;
            if (!adrenotoolsManager.isFromResources(driver)) continue;

            final long contentLength = (long) (FileUtils.getSize(context, adrenotoolsManager.getAssetPath(driver))
                    * (100.0f / COMPRESSION_RATIO));
            AtomicLong totalSizeRef = new AtomicLong();

            boolean extracted = adrenotoolsManager.extractDriverFromResources(driver, (file, size) -> {
                if (size > 0) reportProgress(listener, totalSizeRef.addAndGet(size), contentLength);
                return file;
            });

            if (!extracted) {
                Log.e(TAG, "Could not extract the bundled driver " + driver);
                allExtracted = false;
            }
        }
        return allExtracted;
    }

    /**
     * Runs the whole install on the calling thread.
     *
     * Blocking, deliberately. The original spawned its own single-thread
     * executor and there was no way to wait for it, cancel it, or discover it
     * had failed other than by watching a dialog. `:app` runs this inside a
     * coroutine it owns, so cancellation and lifecycle are handled where they
     * are understood.
     *
     * @return true when the image filesystem is installed and usable
     */
    public static boolean install(final Context context, final Listener listener)
            throws PayloadUnavailableException {
        ImageFs imageFs = ImageFs.find(context);
        File rootDir = imageFs.getRootDir();

        WemuPaths.resetEmulatorsVersion(context);
        notifyStage(listener, Stage.SYSTEM_FILES);

        clearRootDir(rootDir);

        if (!extractPayload(context, listener, "imagefs.tar.zst", rootDir, Stage.SYSTEM_FILES)) {
            Log.e(TAG, "Could not install imagefs.tar.zst");
            return false;
        }

        if (!installWineFromAssets(context, listener)) return false;
        if (!installDriversFromAssets(context, listener)) return false;
        if (!prefetchContainerPayloads(context, listener)) return false;

        /*
         * The version file is written last, and only on success.
         *
         * It is what `isInstallNeeded` reads, so writing it after a partial
         * install would mark a broken image as current and there would be no
         * path back short of clearing the app's data. Every failure above
         * therefore returns before reaching this line, leaving the image invalid
         * and the install ready to be retried.
         */
        imageFs.createImgVersionFile(LATEST_VERSION);
        FileUtils.symlink(
                "libSDL2-2.0.so",
                new File(imageFs.getLibDir(), "libSDL2-2.0.so.0").getAbsolutePath());
        resetContainerImgVersions(context);
        return true;
    }

    /** Whether [install] needs to run: no image, or one older than this build. */
    public static boolean isInstallNeeded(final Context context) {
        ImageFs imageFs = ImageFs.find(context);
        return !imageFs.isValid() || imageFs.getVersion() < LATEST_VERSION;
    }

    private static void notifyStage(Listener listener, Stage stage) {
        if (listener != null) listener.onStage(stage);
    }

    private static void reportProgress(Listener listener, long written, long total) {
        if (listener == null || total <= 0) return;
        // Clamped, because the total is an estimate and an archive that
        // compresses better than assumed would otherwise report over 100.
        int percent = (int) ((written / (float) total) * 100);
        listener.onProgress(Math.max(0, Math.min(100, percent)));
    }

    private static void clearRootDir(File rootDir) {
        if (rootDir.isDirectory()) {
            File[] files = rootDir.listFiles();
            if (files != null) {
                for (File file : files) {
                    // `home` holds the containers themselves — the user's
                    // installed games and their drive_c. Reinstalling the image
                    // must not take those with it.
                    if (file.isDirectory() && file.getName().equals("home")) continue;
                    FileUtils.delete(file);
                }
            }
        } else {
            rootDir.mkdirs();
        }
    }
}
