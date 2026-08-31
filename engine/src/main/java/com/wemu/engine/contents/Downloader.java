package com.wemu.engine.contents;

import android.util.Log;

import com.wemu.engine.core.Callback;
import com.wemu.engine.core.StreamUtils;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLConnection;
import java.nio.charset.StandardCharsets;

/**
 * Fetches a file or a string over HTTP, and nothing else.
 *
 * Blocking on the calling thread by design. `HttpUtils` already does this behind
 * its own executor and, in one overload, an `Activity` and a progress dialog —
 * which makes it unusable from anywhere that is not a screen. Downloading is not
 * a screen's job; deciding what to show while it happens is, and the caller is
 * the only one that can answer that.
 */
public class Downloader {

    private Downloader() {}

    public static boolean downloadFile(String address, File file) {
        return downloadTo(address, file, null) == null;
    }

    public static boolean downloadFile(String address, File file, Callback<Integer> progressCallback) {
        return downloadTo(address, file, progressCallback) == null;
    }

    /** How many times a transient refusal is worth waiting out. */
    private static final int RETRY_ATTEMPTS = 4;

    /**
     * Whether a failure is worth trying again.
     *
     * A 503 or a 429 is the server saying "not now", and GitHub says it readily
     * to anyone pulling several hundred megabytes in a short window. Giving up
     * on the first one turned a pause into a container with no Vulkan driver
     * that died four seconds after opening — a failure that looked nothing like
     * a download problem from where the user was sitting.
     *
     * A 404 is not retried: the asset is not there and waiting will not put it
     * there.
     */
    private static boolean worthRetrying(String failure) {
        if (failure == null) return false;
        return failure.contains("503") || failure.contains("429") || failure.contains("500")
                || failure.contains("502") || failure.contains("504")
                || failure.contains("SocketTimeout") || failure.contains("SocketException")
                || failure.contains("UnknownHost")
                // A connection that died mid-handshake rather than a server
                // that refused. Left out of the first version of this list, so
                // the one failure that actually reached a user -- "unexpected
                // end of stream" -- was the one it did not cover.
                || failure.contains("unexpected end of stream")
                || failure.contains("Connection reset")
                || failure.contains("Broken pipe")
                || failure.contains("stream was reset");
    }

    /**
     * Runs a download, waiting out a server that is refusing for now.
     *
     * Backs off between attempts — a second, then three, then nine — because
     * retrying a rate limit immediately is what produced it.
     */
    public static String withRetries(Attempt attempt) {
        String failure = null;
        long waitMs = 1000;
        for (int i = 0; i < RETRY_ATTEMPTS; i++) {
            failure = attempt.run();
            if (failure == null) return null;
            if (!worthRetrying(failure) || i == RETRY_ATTEMPTS - 1) return failure;

            Log.w("Downloader", "Attempt " + (i + 1) + " failed (" + failure
                    + "); waiting " + waitMs + "ms");
            try {
                Thread.sleep(waitMs);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                return failure;
            }
            waitMs *= 3;
        }
        return failure;
    }

    /** One go at a download, reporting null on success. */
    public interface Attempt {
        String run();
    }

    /**
     * The size of what is at this address, or -1 if it will not say.
     *
     * Also the test for whether it can be fetched in pieces: a server that
     * answers a HEAD without `Accept-Ranges: bytes` gets one connection and no
     * argument.
     */
    public static long rangeableLength(String address) {
        try {
            HttpURLConnection connection = (HttpURLConnection) new URL(address).openConnection();
            connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
            connection.setReadTimeout(READ_TIMEOUT_MS);
            connection.setRequestMethod("HEAD");
            /*
             * This connection must not be reused, and saying so is the whole
             * point of the header.
             *
             * `HttpURLConnection` is okhttp underneath. A HEAD answers with the
             * `Content-Length` of a body it does not send, and the pooled
             * connection is then handed to the next request expecting those
             * bytes to be there. The next request here is a 185 MB GET, which
             * failed in about a tenth of a second with "unexpected end of stream
             * on com.android.okhttp.Address" — a setup that looked instantly
             * broken while `curl` on the same device fetched the same URL
             * without complaint.
             */
            connection.setRequestProperty("Connection", "close");
            connection.connect();
            long length = connection.getContentLengthLong();
            String ranges = connection.getHeaderField("Accept-Ranges");
            connection.disconnect();
            return "bytes".equalsIgnoreCase(ranges) ? length : -1;
        } catch (Exception e) {
            return -1;
        }
    }

    /**
     * Downloads one byte range to a file.
     *
     * The point is throughput rather than resumption: a single TCP stream to a
     * distant server is often held well below the link's capacity by round-trip
     * time and loss, and several streams sharing the same link are not. It is
     * also what makes a per-connection shaper irrelevant.
     *
     * @param from first byte wanted, inclusive
     * @param to last byte wanted, inclusive
     * @return null on success, or a message describing the failure
     */
    public static String downloadRange(String address, File file, long from, long to,
                                       Callback<Long> bytesCallback) {
        try {
            HttpURLConnection connection = (HttpURLConnection) new URL(address).openConnection();
            connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
            connection.setReadTimeout(READ_TIMEOUT_MS);
            connection.setRequestProperty("Range", "bytes=" + from + "-" + to);
            connection.connect();

            int status = connection.getResponseCode();
            // 206 is the answer to a range request. A 200 means the server
            // ignored the header and is sending the whole file down every
            // connection, which would be worse than not asking.
            if (status != HttpURLConnection.HTTP_PARTIAL) {
                connection.disconnect();
                return status == HttpURLConnection.HTTP_OK
                        ? "server ignored the range request"
                        : describeStatus(status);
            }

            try (InputStream input = connection.getInputStream();
                 OutputStream output = new FileOutputStream(file)) {
                byte[] data = new byte[StreamUtils.BUFFER_SIZE];
                int count;
                while ((count = input.read(data)) != -1) {
                    output.write(data, 0, count);
                    if (bytesCallback != null) bytesCallback.call((long) count);
                }
                output.flush();
            }
            connection.disconnect();
            return null;
        } catch (Exception e) {
            if (file.exists() && !file.delete()) file.deleteOnExit();
            return e.getClass().getSimpleName() + ": " + e.getMessage();
        }
    }

    /**
     * Downloads to a file and says what went wrong if anything did.
     *
     * A boolean was not enough. The realistic failures here are a GitHub rate
     * limit, an asset that has moved, and no network — three completely
     * different problems that all arrived at the user as "Download failed", and
     * only one of which is worth retrying. See {@link #withRetries}.
     *
     * @param progressCallback called with 0..100, or never when the server sends
     *   no content length — which GitHub's release assets sometimes do not, so a
     *   caller must cope with a download that reports nothing until it finishes.
     * @return null on success, or a message describing the failure.
     */
    public static String downloadTo(String address, File file, Callback<Integer> progressCallback) {
        return downloadTo(address, file, Long.MAX_VALUE, progressCallback);
    }

    /**
     * Downloads with a hard response-body ceiling.
     *
     * This is for checksum-pinned assets whose exact maximum is known. It
     * rejects a contradictory Content-Length before opening the destination and
     * still counts bytes while streaming because servers may omit or lie about
     * that header.
     */
    public static String downloadTo(String address, File file, long maxBytes,
                                    Callback<Integer> progressCallback) {
        if (maxBytes < 0) throw new IllegalArgumentException("maxBytes must be non-negative");
        try {
            URLConnection connection = new URL(address).openConnection();
            connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
            connection.setReadTimeout(READ_TIMEOUT_MS);
            connection.connect();

            if (connection instanceof HttpURLConnection) {
                int status = ((HttpURLConnection) connection).getResponseCode();
                if (status != HttpURLConnection.HTTP_OK) {
                    return describeStatus(status);
                }
            }

            long contentLength = connection.getContentLengthLong();
            if (contentLength > maxBytes) {
                if (file.exists() && !file.delete()) file.deleteOnExit();
                return "Download is larger than the allowed size.";
            }
            long downloadedSize = 0;
            int lastProgress = -1;

            try (InputStream input = connection.getInputStream();
                 OutputStream output = new FileOutputStream(file)) {
                byte[] data = new byte[StreamUtils.BUFFER_SIZE];
                int count;
                while ((count = input.read(data)) != -1) {
                    if (downloadedSize > maxBytes - count) {
                        throw new IOException("Download exceeded the allowed size");
                    }
                    output.write(data, 0, count);
                    downloadedSize += count;
                    if (progressCallback != null && contentLength > 0) {
                        int progress = (int) Math.min(100, downloadedSize * 100 / contentLength);
                        // Only on change: a 200 MB driver otherwise calls back
                        // twenty-five thousand times to move a bar a hundred steps.
                        if (progress != lastProgress) {
                            lastProgress = progress;
                            progressCallback.call(progress);
                        }
                    }
                }
                output.flush();
            }
            if (progressCallback != null) progressCallback.call(100);
            return null;
        } catch (Exception e) {
            // A failed download must leave nothing behind that looks like a
            // finished one — the installer downstream only checks that the file
            // exists.
            if (file.exists() && !file.delete()) file.deleteOnExit();
            return e.getClass().getSimpleName()
                    + (e.getMessage() != null ? ": " + e.getMessage() : "");
        }
    }

    /**
     * An HTTP status as something worth showing.
     *
     * 403 is called out by name because it is overwhelmingly the one that
     * happens: GitHub allows sixty unauthenticated API calls an hour, and three
     * driver repositories checked on every refresh get through that faster than
     * anyone expects.
     */
    private static String describeStatus(int status) {
        switch (status) {
            case HttpURLConnection.HTTP_FORBIDDEN:
                return "Refused (403) — GitHub rate limit reached. Try again in an hour.";
            case HttpURLConnection.HTTP_NOT_FOUND:
                return "Not found (404) — the file has moved or been removed.";
            case HttpURLConnection.HTTP_UNAVAILABLE:
                return "Server unavailable (503).";
            default:
                return "Server returned " + status + ".";
        }
    }

    /** The body of a URL as text, or null if it could not be fetched. */
    public static String downloadString(String address) {
        try {
            URLConnection connection = new URL(address).openConnection();
            connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
            connection.setReadTimeout(READ_TIMEOUT_MS);
            connection.connect();

            try (BufferedReader reader = new BufferedReader(
                    new InputStreamReader(connection.getInputStream(), StandardCharsets.UTF_8))) {
                StringBuilder builder = new StringBuilder();
                String line;
                while ((line = reader.readLine()) != null) builder.append(line).append('\n');
                return builder.toString();
            }
        } catch (Exception e) {
            return null;
        }
    }

    private static final int CONNECT_TIMEOUT_MS = 15_000;

    /**
     * Generous, because this is also the idle timeout on a large body.
     *
     * A driver package on a slow connection can go a long time between reads
     * without the download having failed.
     */
    private static final int READ_TIMEOUT_MS = 60_000;
}
