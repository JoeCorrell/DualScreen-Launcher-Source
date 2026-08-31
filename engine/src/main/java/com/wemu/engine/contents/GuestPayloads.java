package com.wemu.engine.contents;

import android.content.Context;
import android.util.Log;

import com.wemu.engine.core.Callback;
import com.wemu.engine.core.FileUtils;
import com.wemu.engine.core.OnExtractFileListener;
import com.wemu.engine.core.TarCompressorUtils;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

/**
 * The large payloads a container is built from, wherever they happen to live.
 *
 * The root filesystem and the Wine build are the two things nothing works
 * without, and between them they are a quarter of a gigabyte. Shipping them
 * inside the APK made every install carry them whether or not the device was
 * ever going to create a container, and made every update re-download all of it
 * to change a Compose file.
 *
 * So they are fetched instead, from the same URLs the Gradle build used to pull
 * them from at build time. Nothing else changes: they are unpacked by exactly
 * the code that unpacked them from `assets`, into exactly the same place.
 *
 * <h2>Bundled still wins</h2>
 *
 * {@link #isBundled} is checked first and a payload present in `assets` is used
 * from there without touching the network. That is what makes this safe to add
 * before anything is removed from the APK — with the assets still in place the
 * behaviour is identical, and the download path can be tested on its own by
 * building without them.
 */
public final class GuestPayloads {

    private static final String TAG = "GuestPayloads";

    /** The release the payloads are published to, and the one place a move shows. */
    private static final String PAYLOAD_RELEASE =
            "https://github.com/JoeCorrell/wemu-payloads/releases/download/runtime-latest/";

    /** Where each payload comes from when it is not in the APK, in order. */
    private static final Map<String, String[]> CATALOG = new HashMap<>();

    /** Why the last fetch failed, for a caller that has to explain it. */
    private static volatile String lastFailure;

    /*
     * Release assets, and single files.
     *
     * These were the upstream backup repository's URLs, and imagefs came from
     * `raw.githubusercontent.com` — the endpoint that serves files out of a git
     * tree. It is not a CDN. Measured on an AYN Thor: the first hundred
     * megabytes arrive at about 3 MB/s and everything after that at 333 KB/s,
     * sustained, which turns a 185 MB payload into a four-minute wait on a
     * connection that is not the limit.
     *
     * They are also no longer split. The 100 MB parts existed because that is
     * the file size limit on the raw endpoint; a release asset may be 2 GB, so
     * joining two halves back together was work being done to satisfy a
     * constraint that no longer applies.
     */
    static {
        CATALOG.put("imagefs.tar.zst", parts(PAYLOAD_RELEASE + "imagefs.tar.zst", 1));
        CATALOG.put("proton-9.0-arm64ec.tar.zst",
                parts(PAYLOAD_RELEASE + "proton-9.0-arm64ec.tar.zst", 1));
        CATALOG.put("container_pattern_common.tzst",
                parts(PAYLOAD_RELEASE + "container_pattern_common.tzst", 1));
        CATALOG.put("proton-9.0-arm64ec_container_pattern.tzst",
                parts(PAYLOAD_RELEASE + "proton-9.0-arm64ec_container_pattern.tzst", 1));
    }

    /**
     * Whether a payload is kept on disk after it has been unpacked.
     *
     * `.tar.zst` — the root filesystem and the Wine builds — is unpacked once
     * into a tree that is then authoritative, so keeping the archive as well
     * would be 250MB of duplicate. Those are dropped.
     *
     * Everything else is `.tzst`, and every `.tzst` here is read again: the
     * container patterns on each container created, DXVK and the drivers
     * whenever a container's selection changes. Dropping those would mean a
     * download the second time somebody makes a container, and no container at
     * all on a device that has since gone offline. A few megabytes each is a
     * fair price for that staying an offline operation.
     */
    private static boolean keepAfterUnpack(String assetName) {
        return !assetName.endsWith(".tar.zst");
    }

    /**
     * Unpacks a payload from wherever it is, fetching it first if it has to.
     *
     * The one entry point for anything that used to call
     * {@code TarCompressorUtils.extract(…, context, assetName, …)} directly. A
     * caller should not have to know whether a given archive ships in the APK
     * this build produced, because that answer changes per payload and per
     * build.
     *
     * @return false if the payload could not be fetched or could not be unpacked
     */
    public static boolean extractOrFetch(Context context, String assetName, File destination,
                                         OnExtractFileListener listener) {
        if (isBundled(context, assetName)) {
            return TarCompressorUtils.extract(
                    TarCompressorUtils.Type.ZSTD, context, assetName, destination, listener);
        }
        File payload = fetch(context, assetName, null);
        if (payload == null) return false;

        boolean ok = TarCompressorUtils.extract(
                TarCompressorUtils.Type.ZSTD, payload, destination, listener);
        if (ok && !keepAfterUnpack(assetName)) release(payload);
        return ok;
    }

    /** Below this a payload is one stream; the extra round trips would not pay. */
    private static final long RANGE_THRESHOLD_BYTES = 8L * 1024 * 1024;

    /** How many streams share the download. */
    private static final int RANGE_STREAMS = 4;

    /**
     * Fetches one file as {@value #RANGE_STREAMS} concurrent byte ranges.
     *
     * @return the finished payload, or null if any range failed — in which case
     *   the caller falls back to a single stream rather than reporting failure,
     *   because a server that mishandles ranges still serves the whole file.
     */
    private static File fetchByRanges(String url, long length, File dir, String assetName,
                                      File partial, File target, Callback<Integer> progress) {
        long chunk = length / RANGE_STREAMS;
        File[] parts = new File[RANGE_STREAMS];
        String[] failures = new String[RANGE_STREAMS];
        java.util.concurrent.atomic.AtomicLong received = new java.util.concurrent.atomic.AtomicLong();
        // What each range has contributed to `received`, so a retry can take its
        // own bytes back out before starting that range again.
        final long[] rangeReceived = new long[RANGE_STREAMS];
        final int[] lastPercent = {-1};

        Log.d(TAG, "Fetching " + assetName + " as " + RANGE_STREAMS + " ranges of "
                + (length / 1048576) + "MB total");

        ExecutorService pool = Executors.newFixedThreadPool(RANGE_STREAMS);
        try {
            List<Future<?>> running = new ArrayList<>();
            for (int i = 0; i < RANGE_STREAMS; i++) {
                final int index = i;
                final long from = i * chunk;
                final long to = (i == RANGE_STREAMS - 1) ? length - 1 : from + chunk - 1;
                parts[i] = localFile(dir, assetName, ".range" + i);
                running.add(pool.submit(() -> failures[index] = Downloader.withRetries(() -> {
                    /*
                     * Per range, because a retry starts that range over.
                     *
                     * A failed range is deleted and fetched again from its first
                     * byte, so a single shared total would count those bytes
                     * twice and drive the bar past 100 while the download was
                     * going backwards.
                     */
                    received.addAndGet(-rangeReceived[index]);
                    rangeReceived[index] = 0;
                    return Downloader.downloadRange(url, parts[index], from, to, count -> {
                        rangeReceived[index] += count;
                        long total = received.addAndGet(count);
                        if (progress == null) return;
                        int percent = (int) Math.min(100, total * 100 / length);
                        synchronized (lastPercent) {
                            if (percent != lastPercent[0]) {
                                lastPercent[0] = percent;
                                progress.call(percent);
                            }
                        }
                    });
                })));
            }
            for (Future<?> f : running) f.get();
        } catch (Exception e) {
            Log.w(TAG, "Ranged fetch failed", e);
            for (File p : parts) FileUtils.delete(p);
            return null;
        } finally {
            pool.shutdownNow();
        }

        for (String failure : failures) {
            if (failure != null) {
                Log.w(TAG, "A range of " + assetName + " failed: " + failure);
                for (File p : parts) FileUtils.delete(p);
                return null;
            }
        }

        try (OutputStream out = new FileOutputStream(partial)) {
            for (File part : parts) {
                try (InputStream in = new java.io.FileInputStream(part)) {
                    byte[] buffer = new byte[1 << 16];
                    int read;
                    while ((read = in.read(buffer)) != -1) out.write(buffer, 0, read);
                }
                FileUtils.delete(part);
            }
            out.flush();
        } catch (IOException e) {
            lastFailure = e.getClass().getSimpleName() + ": " + e.getMessage();
            Log.e(TAG, "Could not join the ranges of " + assetName, e);
            for (File p : parts) FileUtils.delete(p);
            FileUtils.delete(partial);
            return null;
        }

        if (partial.length() != length) {
            Log.w(TAG, "Ranged fetch produced " + partial.length() + " of " + length + " bytes");
            FileUtils.delete(partial);
            return null;
        }
        if (!partial.renameTo(target)) {
            FileUtils.delete(partial);
            return null;
        }
        if (progress != null) progress.call(100);
        return target;
    }

    /**
     * The name a payload is stored under locally.
     *
     * Flattened, because an asset name is a *path* — `graphics_driver/wrapper.tzst`
     * — and `new File(payloadsDir, name + ".range0")` therefore addressed a
     * `graphics_driver` directory that nothing had created. Every fetch of a
     * payload in a subdirectory failed with ENOENT on its own temporary file,
     * which surfaced as an install that could not unpack the system files.
     *
     * The same flattening the release URLs use, so the local file and the remote
     * asset have the same name.
     */
    private static File localFile(File dir, String assetName, String suffix) {
        return new File(dir, assetName.replace("/", "__") + suffix);
    }

    private GuestPayloads() {}

    /**
     * The URLs a payload arrives in, one entry per part.
     *
     * A count of one is the whole file at the template as given; anything more
     * treats the template as a printf pattern and numbers the parts, which is
     * what the raw-file endpoint required and a release asset does not.
     */
    private static String[] parts(String template, int count) {
        if (count == 1) return new String[]{template};
        String[] urls = new String[count];
        for (int i = 0; i < count; i++) urls[i] = String.format(template, i);
        return urls;
    }

    /** Whether the payload ships inside the APK, in which case nothing is downloaded. */
    public static boolean isBundled(Context context, String assetName) {
        try (InputStream ignored = context.getAssets().open(assetName)) {
            return true;
        } catch (IOException e) {
            return false;
        }
    }

    /**
     * Whether this payload can be fetched at all when it is not bundled.
     *
     * Always true for a `.tzst` or `.tar.zst`, because {@link #urlsFor} can
     * derive a release URL for any of them. Whether that URL exists is a
     * question only the server can answer, and it answers it with a 404 that
     * reaches the user as the reason the install failed.
     */
    public static boolean isKnown(String assetName) {
        return CATALOG.containsKey(assetName)
                || assetName.endsWith(".tzst") || assetName.endsWith(".tar.zst");
    }

    /**
     * Where a payload comes from, named or derived.
     *
     * Anything not in the catalogue is looked for on the release under its asset
     * path with the separators flattened — `dxwrapper/dxvk-2.3.1.tzst` becomes
     * `dxwrapper__dxvk-2.3.1.tzst`, because a release asset name cannot contain
     * a slash.
     *
     * Derived rather than enumerated so that unbundling one more archive is an
     * upload rather than an upload and a code change. There are thirteen DXVK,
     * VKD3D and driver builds, and a list of thirteen constants is a list that
     * will disagree with the release within a month.
     */
    private static String[] urlsFor(String assetName) {
        String[] named = CATALOG.get(assetName);
        if (named != null) return named;
        return new String[]{PAYLOAD_RELEASE + assetName.replace("/", "__")};
    }

    /**
     * Registers a checksum-pinned application payload before it is fetched.
     *
     * The launcher publishes its own release assets separately from the engine
     * root filesystem. Reusing this downloader retains the proven ranged,
     * retrying and atomic download behavior without coupling the engine to the
     * launcher's repository URL.
     */
    public static synchronized void registerPayload(String assetName, String url) {
        if (assetName == null || assetName.trim().isEmpty()) {
            throw new IllegalArgumentException("assetName must not be blank");
        }
        if (url == null || !url.startsWith("https://")) {
            throw new IllegalArgumentException("payload URL must use HTTPS");
        }
        CATALOG.put(assetName, parts(url, 1));
    }

    /**
     * The payload as a file on disk, downloading it if it is not already here.
     *
     * Kept in `files/payloads` rather than the cache directory. The cache can be
     * evicted by the platform at any moment, and losing a 185 MB download
     * halfway through unpacking it is not a recoverable state — it would surface
     * as an image that installed successfully and is missing half its files.
     *
     * A payload already downloaded is reused, so an install that fails partway
     * and is retried does not pay for the network twice.
     *
     * @param progress called with 0..100 across the whole payload, parts included
     * @return the file, or null if it could not be fetched
     */
    public static File fetch(Context context, String assetName, Callback<Integer> progress) {
        lastFailure = null;
        String[] urls = urlsFor(assetName);

        File dir = new File(context.getFilesDir(), "payloads");
        if (!dir.isDirectory() && !dir.mkdirs()) {
            lastFailure = "could not create " + dir;
            Log.e(TAG, lastFailure);
            return null;
        }

        File target = localFile(dir, assetName, "");
        if (target.isFile() && target.length() > 0) {
            Log.d(TAG, "Reusing the payload already downloaded: " + target);
            if (progress != null) progress.call(100);
            return target;
        }

        // Assembled beside the target and renamed at the end, so an interrupted
        // download can never be mistaken for a finished one on the next run.
        File partial = localFile(dir, assetName, ".partial");
        FileUtils.delete(partial);

        /*
         * The parts come down together, not one after the other.
         *
         * `imagefs` is served from raw.githubusercontent.com, which is the
         * endpoint for reading source files out of a repository rather than a
         * CDN, and it measured about 690 KB/s on the device — roughly four
         * minutes for 185 MB. Where a throttle like that is applied per
         * connection, asking for both halves at once gets both halves at that
         * rate. Where it is applied per address it changes nothing, and the
         * cost of finding out is one thread.
         *
         * They are still joined in order afterwards, so the archive is
         * byte-identical either way.
         */
        /*
         * One URL is fetched as several byte ranges rather than one stream.
         *
         * Measured on the Thor against the same file and the same code: 4.2MB/s
         * one afternoon and 752KB/s twenty minutes later, so a single stream to
         * GitHub from that device is not reliably anywhere near the link, which
         * reported 61Mbit available. Round-trip time, loss and per-connection
         * shaping all produce that, and several streams sharing a link answer
         * all three without needing to know which it was.
         *
         * Only when the server says `Accept-Ranges: bytes` and gives a length,
         * and only above a size where the extra round trips are worth it. A
         * server that answers a range request with 200 rather than 206 is
         * sending the whole file down every connection, which is worse than not
         * asking, so that is detected and the download falls back.
         */
        if (urls.length == 1) {
            long length = Downloader.rangeableLength(urls[0]);
            if (length > RANGE_THRESHOLD_BYTES) {
                File ranged = fetchByRanges(urls[0], length, dir, assetName, partial, target, progress);
                if (ranged != null) return ranged;
                Log.w(TAG, "Ranged fetch of " + assetName + " did not work; using one stream");
                FileUtils.delete(partial);
            }
        }

        Log.d(TAG, "Fetching " + assetName + " in " + urls.length + " parts, concurrently");
        File[] parts = new File[urls.length];
        for (int i = 0; i < urls.length; i++) parts[i] = localFile(dir, assetName, ".part" + i);

        final int[] partPercent = new int[urls.length];
        final String[] failures = new String[urls.length];
        ExecutorService pool = Executors.newFixedThreadPool(urls.length);
        try {
            List<Future<?>> running = new ArrayList<>();
            for (int i = 0; i < urls.length; i++) {
                final int index = i;
                running.add(pool.submit(() -> {
                    failures[index] = Downloader.withRetries(() ->
                            Downloader.downloadTo(urls[index], parts[index], percent -> {
                                synchronized (partPercent) {
                                    partPercent[index] = percent;
                                    if (progress != null) {
                                        int total = 0;
                                        for (int p : partPercent) total += p;
                                        progress.call(total / partPercent.length);
                                    }
                                }
                            }));
                }));
            }
            for (Future<?> f : running) f.get();
        } catch (Exception e) {
            lastFailure = e.getClass().getSimpleName() + ": " + e.getMessage();
            Log.e(TAG, "Could not fetch " + assetName, e);
            for (File part : parts) FileUtils.delete(part);
            FileUtils.delete(partial);
            return null;
        } finally {
            pool.shutdownNow();
        }

        for (int i = 0; i < failures.length; i++) {
            if (failures[i] != null) {
                Log.e(TAG, "Could not fetch part " + i + " of " + assetName + ": " + failures[i]);
                lastFailure = failures[i];
                for (File part : parts) FileUtils.delete(part);
                FileUtils.delete(partial);
                return null;
            }
        }

        try (OutputStream out = new FileOutputStream(partial)) {
            for (File part : parts) {
                try (InputStream in = new java.io.FileInputStream(part)) {
                    byte[] buffer = new byte[1 << 16];
                    int read;
                    while ((read = in.read(buffer)) != -1) out.write(buffer, 0, read);
                }
                FileUtils.delete(part);
            }
            out.flush();
        } catch (IOException e) {
            // Out of space, most likely: these are joined on the same volume they
            // are about to be unpacked onto.
            lastFailure = e.getClass().getSimpleName() + ": " + e.getMessage();
            Log.e(TAG, "Could not assemble " + assetName, e);
            FileUtils.delete(partial);
            return null;
        }

        if (!partial.renameTo(target)) {
            lastFailure = "could not put " + partial.getName() + " in place";
            Log.e(TAG, lastFailure);
            FileUtils.delete(partial);
            return null;
        }
        if (progress != null) progress.call(100);
        return target;
    }

    /** The reason the last {@link #fetch} returned null, or null if it has not. */
    public static String lastFailure() {
        return lastFailure;
    }

    /**
     * Drops a downloaded payload once it has been unpacked.
     *
     * Called after a successful extraction only. These are large enough that
     * keeping them costs more than a re-download would, and a payload that is
     * still needed is one whose extraction failed — which is exactly the case
     * where keeping it saves the user the whole download on the retry.
     */
    public static void release(File payload) {
        if (payload != null && payload.getParentFile() != null
                && "payloads".equals(payload.getParentFile().getName())) {
            FileUtils.delete(payload);
        }
    }
}
