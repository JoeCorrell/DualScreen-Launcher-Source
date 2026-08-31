package com.wemu.engine.core;

import android.content.Context;
import android.net.Uri;
import android.util.Log;

import org.apache.commons.compress.archivers.ArchiveOutputStream;
import org.apache.commons.compress.archivers.tar.TarArchiveEntry;
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream;
import org.apache.commons.compress.archivers.tar.TarArchiveOutputStream;
import org.apache.commons.compress.archivers.tar.TarConstants;
import org.apache.commons.compress.compressors.gzip.GzipCompressorInputStream;
import org.apache.commons.compress.compressors.gzip.GzipCompressorOutputStream;
import org.apache.commons.compress.compressors.xz.XZCompressorInputStream;
import org.apache.commons.compress.compressors.xz.XZCompressorOutputStream;
import org.apache.commons.compress.compressors.zstandard.ZstdCompressorInputStream;
import org.apache.commons.compress.compressors.zstandard.ZstdCompressorOutputStream;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.LinkOption;

public abstract class TarCompressorUtils {
    public enum Type {XZ, ZSTD, GZIP}

    // Interface to define the exclusion filter
    public interface ExclusionFilter {
        boolean shouldInclude(File file);
    }


    private static void addFile(ArchiveOutputStream tar, File file, String entryName) {
        try {
            tar.putArchiveEntry(tar.createArchiveEntry(file, entryName));
            try (BufferedInputStream inStream = new BufferedInputStream(new FileInputStream(file), StreamUtils.BUFFER_SIZE)) {
                StreamUtils.copy(inStream, tar);
            }
            tar.closeArchiveEntry();
        }
        catch (Exception e) {}
    }

    private static void addLinkFile(ArchiveOutputStream tar, File file, String entryName) {
        try {
            TarArchiveEntry entry = new TarArchiveEntry(entryName, TarConstants.LF_SYMLINK);
            entry.setLinkName(FileUtils.readSymlink(file));
            tar.putArchiveEntry(entry);
            tar.closeArchiveEntry();
        }
        catch (Exception e) {}
    }

    private static void addDirectory(ArchiveOutputStream tar, File folder, String basePath, ExclusionFilter filter) throws IOException {
        File[] files = folder.listFiles();
        if (files == null) return;
        for (File file : files) {
            if (filter != null && !filter.shouldInclude(file)) {
                continue; // Skip files that should be excluded
            }
            if (FileUtils.isSymlink(file)) {
                addLinkFile(tar, file, basePath + file.getName());
            } else if (file.isDirectory()) {
                String entryName = basePath + file.getName() + "/";
                tar.putArchiveEntry(tar.createArchiveEntry(folder, entryName));
                tar.closeArchiveEntry();
                addDirectory(tar, file, entryName, filter);
            } else {
                addFile(tar, file, basePath + file.getName());
            }
        }
    }
    public static void compress(Type type, File file, File destination, int level) {
        compress(type, new File[]{file}, destination, level, null);
    }

    public static void compress(Type type, File file, File destination, int level, ExclusionFilter filter) {
        compress(type, new File[]{file}, destination, level, filter);
    }

    public static void compress(Type type, File[] files, File destination, int level, ExclusionFilter filter) {
        try (OutputStream outStream = getCompressorOutputStream(type, destination, level);
             TarArchiveOutputStream tar = new TarArchiveOutputStream(outStream)) {
            tar.setLongFileMode(TarArchiveOutputStream.LONGFILE_GNU);
            for (File file : files) {
                if (filter != null && !filter.shouldInclude(file)) {
                    continue; // Skip files that should be excluded
                }
                if (FileUtils.isSymlink(file)) {
                    addLinkFile(tar, file, file.getName());
                } else if (file.isDirectory()) {
                    String basePath = file.getName() + "/";
                    tar.putArchiveEntry(tar.createArchiveEntry(file, basePath));
                    tar.closeArchiveEntry();
                    addDirectory(tar, file, basePath, filter);
                } else {
                    addFile(tar, file, file.getName());
                }
            }
            tar.finish();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }


    public static boolean extract(Type type, Context context, String assetFile, File destination) {
        return extract(type, context, assetFile, destination, null);
    }

    public static boolean extract(Type type, Context context, String assetFile, File destination, OnExtractFileListener onExtractFileListener) {
        try {
            return extract(type, context.getAssets().open(assetFile), destination,
                    onExtractFileListener, true);
        }
        catch (IOException e) {
            return false;
        }
    }

    public static boolean extract(Type type, Context context, Uri source, File destination) {
        return extract(type, context, source, destination, null);
    }

    public static boolean extract(Type type, Context context, Uri source, File destination, OnExtractFileListener onExtractFileListener) {
        if (source == null) return false;
        try {
            if (source.toString().startsWith("/")) {
                return extract(type, new FileInputStream(source.toString()), destination,
                        onExtractFileListener, false);
            } else {
                return extract(type, context.getContentResolver().openInputStream(source),
                        destination, onExtractFileListener, false);
            }
        }
        catch (FileNotFoundException e) {
            return false;
        }
    }

    public static boolean extract(Type type, File source, File destination) {
        return extract(type, source, destination, null);
    }

    public static boolean extract(Type type, File source, File destination, OnExtractFileListener onExtractFileListener) {
        if (source == null || !source.isFile()) return false;
        try {
            return extract(type,
                    new BufferedInputStream(new FileInputStream(source), StreamUtils.BUFFER_SIZE),
                    destination, onExtractFileListener, true);
        }
        catch (FileNotFoundException e) {
            return false;
        }
    }

    private static boolean extract(
            Type type,
            InputStream source,
            File destination,
            OnExtractFileListener onExtractFileListener,
            boolean allowAbsoluteGuestLinks) {
        if (source == null) return false;
        try (InputStream inStream = getCompressorInputStream(type, source);
             TarArchiveInputStream tar = new TarArchiveInputStream(inStream)) {
            TarArchiveEntry entry;
            while ((entry = (TarArchiveEntry)tar.getNextEntry()) != null) {
                if (!tar.canReadEntryData(entry)
                        || !extractEntry(tar, entry, destination,
                                entry.getName(), onExtractFileListener,
                                allowAbsoluteGuestLinks)) return false;
            }
            return true;
        }
        catch (IOException | RuntimeException e) {
            Log.e("TarCompressorUtils", "Could not safely extract archive", e);
            return false;
        }
    }

    /**
     * Identifies the compressor from its file signature instead of trusting an
     * extension supplied by a document provider.  Content packages exist in
     * .txz, .tzst and .tgz form, and probing each decoder both wastes time and
     * leaves misleading decompression failures in the log for valid archives.
     */
    public static Type detectType(Context context, Uri source) {
        if (source == null) return null;
        try (InputStream input = source.toString().startsWith("/")
                ? new FileInputStream(source.toString())
                : context.getContentResolver().openInputStream(source)) {
            return detectType(input);
        }
        catch (IOException | RuntimeException e) {
            return null;
        }
    }

    static Type detectType(InputStream source) throws IOException {
        if (source == null) return null;
        byte[] magic = new byte[6];
        int count = 0;
        while (count < magic.length) {
            int read = source.read(magic, count, magic.length - count);
            if (read < 0) break;
            if (read == 0) continue;
            count += read;
        }
        if (count >= 2 && (magic[0] & 0xff) == 0x1f && (magic[1] & 0xff) == 0x8b) {
            return Type.GZIP;
        }
        if (count >= 4 && (magic[0] & 0xff) == 0x28 && (magic[1] & 0xff) == 0xb5
                && (magic[2] & 0xff) == 0x2f && (magic[3] & 0xff) == 0xfd) {
            return Type.ZSTD;
        }
        if (count >= 6 && (magic[0] & 0xff) == 0xfd && (magic[1] & 0xff) == 0x37
                && (magic[2] & 0xff) == 0x7a && (magic[3] & 0xff) == 0x58
                && (magic[4] & 0xff) == 0x5a && (magic[5] & 0xff) == 0x00) {
            return Type.XZ;
        }
        return null;
    }

    /** Writes one already-decoded tar entry under {@code destination}. */
    private static boolean extractEntry(
            TarArchiveInputStream tar,
            TarArchiveEntry entry,
            File destination,
            String outputName,
            OnExtractFileListener onExtractFileListener,
            boolean allowAbsoluteGuestLinks) throws IOException {
        // Commons Compress exposes sparse files as ordinary files too.  They
        // are deliberately unsupported here: their logical size can be far
        // larger than the bytes in the archive and this generic extractor has
        // no package-specific sparse-file manifest to pin that expansion to.
        if (entry.isSparse() || entry.isLink()
                || (!entry.isDirectory() && !entry.isSymbolicLink() && !entry.isFile())) {
            Log.e("TarCompressorUtils", "Unsupported tar entry: " + entry.getName());
            return false;
        }

        File file = ArchivePathPolicy.resolveEntry(destination, outputName);
        if (onExtractFileListener != null) {
            file = onExtractFileListener.onExtractFile(file, entry.getSize());
            if (file == null) return true;
        }
        file = ArchivePathPolicy.requireContainedOutput(destination, file);

        if (entry.isDirectory()) {
            ArchivePathPolicy.createSafeDirectory(destination, file);
            FileUtils.chmod(file, 0771);
            return true;
        }

        ArchivePathPolicy.createSafeParents(destination, file);
        if (entry.isSymbolicLink()) {
            ArchivePathPolicy.validateSymbolicLink(destination, file, entry.getLinkName(),
                    allowAbsoluteGuestLinks);
            if (Files.exists(file.toPath(), LinkOption.NOFOLLOW_LINKS)) {
                if (Files.isDirectory(file.toPath(), LinkOption.NOFOLLOW_LINKS)
                        && !FileUtils.isEmpty(file)) {
                    throw new IOException("Archive link collides with a non-empty directory: "
                            + file);
                }
                Files.delete(file.toPath());
            }
            Files.createSymbolicLink(file.toPath(),
                    java.nio.file.Paths.get(entry.getLinkName()));
            // chmod follows links on Android.  Never let an archive use it to
            // change the mode of a link target outside the extraction tree.
            return true;
        }

        ArchivePathPolicy.rejectExistingLink(file);
        if (Files.isDirectory(file.toPath(), LinkOption.NOFOLLOW_LINKS)) {
            throw new IOException("Archive file collides with a directory: " + file);
        }
        try (BufferedOutputStream outStream = new BufferedOutputStream(
                new FileOutputStream(file), StreamUtils.BUFFER_SIZE)) {
            if (!StreamUtils.copy(tar, outStream)) return false;
        }
        FileUtils.chmod(file, 0771);
        return true;
    }

    private static InputStream getCompressorInputStream(Type type, InputStream source) throws IOException {
        if (type == Type.XZ) {
            return new XZCompressorInputStream(source);
        }
        else if (type == Type.ZSTD) {
            return new ZstdCompressorInputStream(source);
        }
        else if (type == Type.GZIP) {
            return new GzipCompressorInputStream(source);
        }
        return null;
    }

    private static OutputStream getCompressorOutputStream(Type type, File destination, int level) throws IOException {
        if (type == Type.XZ) {
            return new XZCompressorOutputStream(new BufferedOutputStream(new FileOutputStream(destination), StreamUtils.BUFFER_SIZE), level);
        }
        else if (type == Type.ZSTD) {
            return new ZstdCompressorOutputStream(new BufferedOutputStream(new FileOutputStream(destination), StreamUtils.BUFFER_SIZE), level);
        }
        else if (type == Type.GZIP) {
            return new GzipCompressorOutputStream(new BufferedOutputStream(
                    new FileOutputStream(destination), StreamUtils.BUFFER_SIZE));
        }
        return null;
    }

    public static void archive(File[] files, File destination, ExclusionFilter filter) {
        try (OutputStream outStream = new BufferedOutputStream(new FileOutputStream(destination), StreamUtils.BUFFER_SIZE);
             TarArchiveOutputStream tar = new TarArchiveOutputStream(outStream)) {
            tar.setLongFileMode(TarArchiveOutputStream.LONGFILE_GNU);
            for (File file : files) {
                if (filter != null && !filter.shouldInclude(file)) {
                    continue; // Skip files that should be excluded
                }
                if (FileUtils.isSymlink(file)) {
                    addLinkFile(tar, file, file.getName());
                } else if (file.isDirectory()) {
                    String basePath = file.getName() + "/";
                    tar.putArchiveEntry(tar.createArchiveEntry(file, basePath));
                    tar.closeArchiveEntry();
                    addDirectory(tar, file, basePath, filter);
                } else {
                    addFile(tar, file, file.getName());
                }
            }
            tar.finish();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    public static boolean extractTar(File source, File destination, OnExtractFileListener onExtractFileListener) {
        if (source == null || !source.isFile()) return false;
        try (InputStream inStream = new BufferedInputStream(new FileInputStream(source), StreamUtils.BUFFER_SIZE);
             TarArchiveInputStream tar = new TarArchiveInputStream(inStream)) {
            TarArchiveEntry entry;
            String topLevelDirectory = null;
            while ((entry = (TarArchiveEntry) tar.getNextEntry()) != null) {
                if (!tar.canReadEntryData(entry)) return false;

                // Get the top-level directory name
                String entryName = entry.getName();
                if (topLevelDirectory == null) {
                    if (entry.isDirectory()) {
                        topLevelDirectory = entryName;
                        continue; // Skip creating the top-level directory
                    }
                }

                // Skip the entire tmp directory
                if (entryName.contains("/tmp/")) {
                    Log.d("RestoreOp", "Skipping tmp directory: " + entryName);
                    continue;
                }

                // Adjust the extraction path to remove the top-level directory
                String adjustedName = topLevelDirectory != null
                        && entryName.startsWith(topLevelDirectory)
                        ? entryName.substring(topLevelDirectory.length())
                        : entryName;
                if (!extractEntry(tar, entry, destination, adjustedName,
                        onExtractFileListener, false)) return false;
            }
            return true;
        } catch (IOException | RuntimeException e) {
            Log.e("RestoreOp", "Failed to extract tar file", e);
            return false;
        }
    }


}







